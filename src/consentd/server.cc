/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "server.hh"
#include "server_connection.hh"
#include "common/logging.hh"
#include "common/dispatch.hh"
#include "common/resource.hh"
#include "consent.h"

#include <glib-unix.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <exception>
#include <utility>
#include <vector>
#include <sstream>

#include "repository.hh"
#include "bootstrap.hh"
#include "key_file.hh"
#include "profile_config.hh"
#include "common/offline_registration.hh"

#ifndef CONSENT_STATE_DIR
#define CONSENT_STATE_DIR "/opt/var/lib/consentd"
#endif
#ifndef CONSENT_AUTHORITY_DIR
#define CONSENT_AUTHORITY_DIR "/opt/var/lib/consent-authority"
#endif
#ifndef CONSENT_ROLE_CONFIG
#define CONSENT_ROLE_CONFIG "/etc/consent/roles.conf"
#endif
#ifndef CONSENT_SERVICE_UNIT
#define CONSENT_SERVICE_UNIT "consentd.service"
#endif

namespace consentd {
namespace {
constexpr unsigned kMaxConnections = 64;
constexpr unsigned kMaxJobs = 128;
constexpr size_t kMaxOutput = 262144;
constexpr size_t kMaxFrame = 65536;

void Destroy(GSource*& source) {
  if (!source)
    return;
  g_source_destroy(source);
  g_source_unref(source);
  source = nullptr;
}

GSource* WakeSource(GMainContext* context, GSourceFunc callback,
                    gpointer data) {
  static GSourceFuncs functions = {
      nullptr,
      nullptr,
      [](GSource*, GSourceFunc function, gpointer argument) -> gboolean {
        return function(argument);
      },
      nullptr,
      nullptr,
      nullptr};
  auto* source = g_source_new(&functions, sizeof(GSource));
  g_source_set_ready_time(source, -1);
  g_source_set_callback(source, callback, data, nullptr);
  g_source_attach(source, context);
  return source;
}

consent::Message Error(const consent::Message& request, int status) {
  return {{"v", "1"},
          {"id", consent::Get(request, "id")},
          {"status", std::to_string(status)},
          {"method", "reply"}};
}
}  // namespace

Server::Connection::~Connection() {
  Destroy(read_source);
  Destroy(write_source);
  Destroy(deadline);
  if (stream)
    g_object_unref(stream);
}

struct Server::ParseJob {
  Server* server;
  std::shared_ptr<Connection> connection;
  std::vector<uint8_t> payload;
};

Server::Server() {
  // C++ allocations precede GLib resource acquisition. Stop never allocates.
  profiles_ = std::make_shared<ProfileState>();
  shutdown_job_ =
      std::make_unique<std::function<void()>>([this] { ShutdownDatabase(); });
  main_context_ = g_main_context_default();
  main_loop_ = g_main_loop_new(main_context_, FALSE);
  io_context_ = g_main_context_new();
  io_loop_ = g_main_loop_new(io_context_, FALSE);
  db_queue_ = g_async_queue_new();
}

Server::~Server() {
  stopping_ = true;
  if (profile_authority_)
    profile_authority_->Stop();
  Destroy(tick_);
  Destroy(sigterm_);
  Destroy(sigint_);
  if (listener_)
    g_object_unref(listener_);
  if (parser_pool_)
    g_thread_pool_free(parser_pool_, FALSE, TRUE);
  if (io_thread_) {
    // Queued quit remains effective even before the thread enters Run.
    g_source_set_ready_time(io_quit_source_, 0);
    g_main_context_wakeup(io_context_);
    g_thread_join(io_thread_);
  }
  if (db_thread_) {
    // An empty callable is the queue sentinel. No producers remain here.
    g_async_queue_push(db_queue_, &db_stop_);
    g_thread_join(db_thread_);
  }
  // No producers remain. Cancel instead of executing late startup/completion
  // callbacks against a partially dismantled Server on another owner thread.
  dispatcher_.Cancel();
  Destroy(io_quit_source_);
  Destroy(emergency_source_);
  Destroy(stop_io_source_);
  Destroy(finished_source_);
  clients_.clear();
  g_async_queue_unref(db_queue_);
  g_main_loop_unref(io_loop_);
  g_main_context_unref(io_context_);
  g_main_loop_unref(main_loop_);
  repository_.reset();
  if (lifecycle_lock_ >= 0)
    close(lifecycle_lock_);
}

void Server::DispatchFailed() noexcept {
  if (emergency_source_) {
    g_source_set_ready_time(emergency_source_, 0);
    g_main_context_wakeup(main_context_);
  }
}

bool Server::Post(GMainContext* context, std::function<void()> work) noexcept {
  try {
    if (dispatcher_.Post(context, [this, work = std::move(work)] {
          try {
            work();
          } catch (...) {
            LOG(ERROR) << "event=dispatch-failed reason=exception";
            DispatchFailed();
          }
        }))
      return true;
  } catch (...) {
  }
  DispatchFailed();
  return false;
}

bool Server::Submit(std::function<void()> work) {
  auto job = std::make_unique<std::function<void()>>(std::move(work));
  consent::Admission admission(db_jobs_, kMaxJobs);
  if (!admission.Accepted())
    return false;
  g_async_queue_push(db_queue_, job.release());
  admission.Commit();
  return true;
}

void Server::StartThreads() {
  io_thread_ = g_thread_new(
      "consent-io",
      [](gpointer data) -> gpointer {
        auto* self = static_cast<Server*>(data);
        g_main_context_push_thread_default(self->io_context_);
        g_main_loop_run(self->io_loop_);
        g_main_context_pop_thread_default(self->io_context_);
        return nullptr;
      },
      this);
  db_thread_ = g_thread_new(
      "consent-db",
      [](gpointer data) -> gpointer {
        auto* self = static_cast<Server*>(data);
        for (;;) {
          auto* raw = static_cast<std::function<void()>*>(
              g_async_queue_pop(self->db_queue_));
          if (raw == &self->db_stop_)
            break;
          std::unique_ptr<std::function<void()>> job(raw);
          try {
            (*job)();
          } catch (const std::exception& error) {
            LOG(WARNING) << "event=db-job-failed reason=" << error.what();
            self->DispatchFailed();
          } catch (...) {
            LOG(WARNING) << "event=db-job-failed reason=unknown";
            self->DispatchFailed();
          }
          --self->db_jobs_;
        }
        self->repository_.reset();
        return nullptr;
      },
      this);
}

void Server::InitializeWakeSources() {
  io_quit_source_ = WakeSource(
      io_context_,
      [](gpointer data) noexcept -> gboolean {
        auto* self = static_cast<Server*>(data);
        while (!self->clients_.empty()) {
          auto connection = self->clients_.begin()->second;
          self->Close(connection, "owner-teardown");
        }
        g_main_loop_quit(self->io_loop_);
        return G_SOURCE_REMOVE;
      },
      this);
  emergency_source_ = WakeSource(
      main_context_,
      [](gpointer data) noexcept -> gboolean {
        auto* self = static_cast<Server*>(data);
        g_source_set_ready_time(self->emergency_source_, -1);
        self->exit_status_ = 1;
        self->Stop();
        return G_SOURCE_CONTINUE;
      },
      this);
  stop_io_source_ = WakeSource(
      io_context_,
      [](gpointer data) noexcept -> gboolean {
        auto* self = static_cast<Server*>(data);
        g_source_set_ready_time(self->stop_io_source_, -1);
        self->StopIo();
        return G_SOURCE_CONTINUE;
      },
      this);
  finished_source_ = WakeSource(
      main_context_,
      [](gpointer data) noexcept -> gboolean {
        auto* self = static_cast<Server*>(data);
        g_source_set_ready_time(self->finished_source_, -1);
        LOG(INFO) << "event=shutdown stage=database-drained";
        g_main_loop_quit(self->main_loop_);
        return G_SOURCE_CONTINUE;
      },
      this);
}

bool Server::LoadProfiles() {
  const std::string path = std::string(CONSENT_AUTHORITY_DIR) +
                           "/profiles.conf";
  struct stat identity = {};
  if (lstat(path.c_str(), &identity) < 0) {
    if (errno == ENOENT) {
      LOG(INFO) << "event=profile-authority state=legacy-static";
      return true;
    }
    return false;
  }
  consent::Descriptor fd(OpenProtected(path));
  if (fd.Get() < 0)
    return false;
  ProfileConfig config;
  if (!ReadProfileConfig(fd.Get(), &config))
    return false;
#ifndef CONSENT_TEST_BUILD
  if (config.fixture)
    return false;
#endif
  if (!profiles_->Configure(std::move(config.bindings)))
    return false;
  profile_fixture_ = config.fixture;
  profile_uid_ = config.session_uid;
  return true;
}

void Server::StartProfiles() {
  if (!profiles_->Enabled())
    return;
  ProfileAuthority::Barrier barrier =
      [this](bool retire, const std::string& removed,
             ProfileAuthority::Done done) {
        if (stopping_ || !Submit([this, retire, removed, done] {
          bool success = false;
          consent::Message snapshot;
          try {
            repository_->FenceProfiles(retire, removed);
            snapshot = repository_->Snapshot();
            success = true;
          } catch (...) {
            profiles_->Fence();
          }
          Post(io_context_, [this, snapshot] { Publish(snapshot); });
          Post(main_context_, [done, success] { done(success); });
        })) {
          profiles_->Fence();
          done(false);
        }
      };
#ifdef CONSENT_TEST_BUILD
  if (profile_fixture_) {
    // Test-only fixed private fixture bus. No product bus-address setting.
    profile_authority_ = std::make_unique<ProfileAuthority>(
        profiles_, profile_uid_, std::move(barrier),
        "unix:path=" + std::string(CONSENT_AUTHORITY_DIR) +
            "/profile-bus.sock");
  } else
#endif
  {
    profile_authority_ = std::make_unique<ProfileAuthority>(
        profiles_, profile_uid_, std::move(barrier));
  }
  profile_authority_->Start();
}

int Server::Run(int listener_fd) {
  consent::Descriptor listener_owner(listener_fd);
  InitializeWakeSources();
  std::string error;
  if (!identity_.Load(CONSENT_ROLE_CONFIG, &error)) {
    LOG(WARNING) << "event=identity-config-failed reason=" << error.c_str();
    return 1;
  }
  if (!LoadProfiles())
    return 1;
  lifecycle_lock_ =
      OpenProtected(std::string(CONSENT_AUTHORITY_DIR) + "/lifecycle.lock");
  if (lifecycle_lock_ < 0 || flock(lifecycle_lock_, LOCK_SH | LOCK_NB) < 0) {
    LOG(WARNING) << "event=storage-migration-unavailable";
    return 1;
  }
  // Role/executable/authority paths remain root protected. Only this state
  // leaf belongs to the service account, beneath root-owned ancestors.
  std::string state_path = CONSENT_STATE_DIR;
  auto slash = state_path.rfind('/');
  std::string leaf = state_path.substr(slash + 1);
  int parent = slash != std::string::npos && slash > 0 && !leaf.empty() &&
                       leaf != "." && leaf != ".."
                   ? OpenProtected(state_path.substr(0, slash), true)
                   : -1;
  int state = parent >= 0
                  ? openat(parent, leaf.c_str(),
                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
                  : -1;
  if (parent >= 0)
    close(parent);
  struct stat state_info = {};
  bool valid_state = state >= 0 && fstat(state, &state_info) == 0 &&
                     state_info.st_uid == geteuid() &&
                     (state_info.st_mode & 0777) == 0700;
  consent::Descriptor state_owner(state);
  if (!valid_state) {
    LOG(WARNING) << "event=state-directory-unprotected";
    return 1;
  }
  if (!CheckBootstrap(CONSENT_AUTHORITY_DIR, state_owner.Get(),
                      CONSENT_SERVICE_UNIT, &error)) {
    LOG(WARNING) << "event=bootstrap-rejected reason=" << error.c_str();
    return 1;
  }
  state_owner.Reset();
  GError* gio_error = nullptr;
  GSocket* socket = g_socket_new_from_fd(listener_fd, &gio_error);
  if (!socket) {
    g_clear_error(&gio_error);
    return 1;
  }
  listener_owner.Release();
  g_socket_set_blocking(socket, FALSE);
  listener_ = g_socket_service_new();
  g_socket_service_stop(listener_);
  gboolean added = g_socket_listener_add_socket(G_SOCKET_LISTENER(listener_),
                                                socket, nullptr, &gio_error);
  g_object_unref(socket);
  if (!added) {
    g_clear_error(&gio_error);
    return 1;
  }
  g_signal_connect(
      listener_, "incoming",
      G_CALLBACK(+[](GSocketService*, GSocketConnection* stream, GObject*,
                     gpointer data) -> gboolean {
        auto* self = static_cast<Server*>(data);
        if (self->stopping_)
          return FALSE;
        consent::Admission admission(self->connections_, kMaxConnections);
        if (!admission.Accepted())
          return FALSE;
        try {
          std::shared_ptr<GSocketConnection> owner(
              G_SOCKET_CONNECTION(g_object_ref(stream)),
              [](GSocketConnection* value) { g_object_unref(value); });
          if (!self->Post(self->io_context_, [self, owner] {
                try {
                  self->AddConnection(owner.get());
                } catch (...) {
                  --self->connections_;
                  self->DispatchFailed();
                }
              }))
            return FALSE;
        } catch (...) {
          return FALSE;
        }
        admission.Commit();
        return TRUE;
      }),
      this);

  parser_pool_ = g_thread_pool_new(
      [](gpointer data, gpointer) {
        std::unique_ptr<ParseJob> job(static_cast<ParseJob*>(data));
        consent::Message request;
        bool valid = false;
        try {
          valid = consent::Decode(job->payload.data(), job->payload.size(),
                                  &request);
        } catch (...) {
          valid = false;
        }
        auto* self = job->server;
        auto connection = job->connection;
        --self->parse_jobs_;
        try {
          self->Post(self->io_context_,
                     [self, connection, valid,
                      request = std::move(request)]() mutable {
                       connection->parsing = false;
                       if (connection->closed)
                         return;
                       if (!valid) {
                         self->Close(connection, "invalid-frame");
                         return;
                       }
                       self->Execute(connection, std::move(request));
                       self->Receive(connection);
                     });
        } catch (...) {
          self->DispatchFailed();
        }
      },
      nullptr, 2, FALSE, &gio_error);
  if (!parser_pool_) {
    g_clear_error(&gio_error);
    return 1;
  }
  StartThreads();

  auto attach_signal = [this](int number) {
    GSource* source = g_unix_signal_source_new(number);
    g_source_set_callback(
        source,
        [](gpointer data) -> gboolean {
          static_cast<Server*>(data)->Stop();
          return G_SOURCE_CONTINUE;
        },
        this, nullptr);
    g_source_attach(source, main_context_);
    return source;
  };
  sigterm_ = attach_signal(SIGTERM);
  sigint_ = attach_signal(SIGINT);
  Submit([this] {
    std::string error;
    bool ready = false;
    consent::Message snapshot;
    try {
      repository_ = std::make_unique<Repository>(
          std::string(CONSENT_STATE_DIR) + "/consent.db", CONSENT_STATE_DIR);
      repository_->SetProfileState(profiles_);
      repository_->SetInstallationValidator(ValidateInstallation);
      repository_->SetOfflineInstallationValidator(ValidateOfflineInstallation);
      repository_->SetPackageGenerationValidator(ValidatePackageGeneration);
      std::vector<consent::Message> registrations;
      ready = consent::offline::LoadRegistrations(CONSENT_AUTHORITY_DIR,
                                                  &registrations, &error) == 0;
      // Keep strict authority classification through Open/Replay and the final
      // Snapshot, including a source replacement after the initial preflight.
      std::unique_ptr<Repository::OfflineReconciliation> reconciliation;
      if (ready && !registrations.empty())
        reconciliation =
            std::make_unique<Repository::OfflineReconciliation>(*repository_);
      if (ready && !registrations.empty()) {
        int status = CheckOfflineAuthority();
        if (status != 0 && status != -ESTALE) {
          error =
              "offline-authority-preflight status=" + std::to_string(status);
          ready = false;
        }
      }
      if (ready)
        ready = repository_->Open(&error);
      if (ready) {
        for (size_t index = 0; index < registrations.size(); ++index) {
          auto result =
              repository_->ImportOfflineRegistration(registrations[index]);
          int status = static_cast<int>(
              consent::Number(result, "status", CONSENT_ERROR_STORAGE));
          if (status == CONSENT_ERROR_STALE) {
            // Missing/rotated installation authority cannot activate a record.
            // Keep it immutable and continue with independent packages.
            LOG(INFO) << "event=offline-registration state=deferred index="
                      << index << " status=" << status;
            continue;
          }
          if (status != 0) {
            error = "offline registration reconciliation failed: " +
                    std::to_string(status);
            ready = false;
            break;
          }
          LOG(INFO) << "event=offline-registration state=reconciled index="
                    << index;
        }
      }
      if (ready)
        snapshot = repository_->Snapshot();
    } catch (const std::exception& failure) {
      ready = false;
      error = failure.what();
    } catch (...) {
      ready = false;
      error = "storage initialization failed";
    }
    Post(main_context_, [this, ready, error, snapshot] {
      if (!ready) {
        LOG(WARNING) << "event=database-open-failed reason=" << error.c_str();
        exit_status_ = 1;
        Stop();
        return;
      }
      if (stopping_)
        return;
      Post(io_context_, [this, snapshot] { Publish(snapshot); });
      StartProfiles();
      tick_ = g_timeout_source_new(250);
      g_source_set_callback(
          tick_,
          [](gpointer data) -> gboolean {
            auto* self = static_cast<Server*>(data);
            if (self->stopping_)
              return G_SOURCE_REMOVE;
            try {
              self->Submit([self] {
                try {
                  self->repository_->Tick();
                  auto snapshot = self->repository_->Snapshot();
                  self->Post(self->io_context_,
                             [self, snapshot] { self->Publish(snapshot); });
                } catch (...) {
                  self->Post(self->io_context_, [self] {
                    auto clients = self->clients_;
                    for (const auto& entry : clients)
                      self->Close(entry.second, "storage-unavailable");
                    self->published_.clear();
                  });
                }
              });
            } catch (...) {
              self->DispatchFailed();
              return G_SOURCE_REMOVE;
            }
            return G_SOURCE_CONTINUE;
          },
          this, nullptr);
      g_source_attach(tick_, main_context_);
      g_socket_service_start(listener_);
      if (sd_notify(0, "READY=1") < 0) {
        exit_status_ = 1;
        Stop();
      } else {
        LOG(INFO) << "event=ready epoch="
                  << consent::Get(snapshot, "epoch").c_str()
                  << " connections=" << kMaxConnections
                  << " db_jobs=" << kMaxJobs << " parser_threads=2";
      }
    });
  });
  g_main_loop_run(main_loop_);
  return exit_status_;
}

void Server::AddConnection(GSocketConnection* stream) {
  auto connection = std::make_shared<Connection>();
  connection->server = this;
  connection->id = ++next_client_;
  connection->stream = G_SOCKET_CONNECTION(g_object_ref(stream));
  connection->socket = g_socket_connection_get_socket(stream);
  g_socket_set_blocking(connection->socket, FALSE);
  clients_.emplace(connection->id, connection);
  try {
    std::string error;
    bool trusted =
        !stopping_ &&
        identity_.Authenticate(g_socket_get_fd(connection->socket),
                               &connection->peer, &connection->process, &error);
    LOG(INFO) << "event=client-connected instance="
              << static_cast<unsigned long long>(connection->id)
              << " pid=" << static_cast<long>(connection->peer.pid)
              << " uid=" << static_cast<unsigned long>(connection->peer.uid)
              << " gid=" << static_cast<unsigned long>(connection->peer.gid)
              << " role="
              << (trusted ? connection->peer.identity.c_str() : "rejected");
    if (!trusted) {
      Close(connection, error.c_str());
      return;
    }
    unsigned same_uid = 0;
    for (const auto& entry : clients_) {
      if (entry.second->peer.uid == connection->peer.uid)
        ++same_uid;
    }
    const bool control = connection->peer.roles.count("installer") ||
                         connection->peer.roles.count("ui") ||
                         connection->peer.roles.count("admin");
    if (same_uid > (control ? 32u : 24u)) {
      Close(connection, "uid-connection-limit");
      return;
    }
    connection->last_input = g_get_monotonic_time();
    ArmRead(connection);
    connection->deadline = g_timeout_source_new_seconds(1);
    g_source_set_callback(
        connection->deadline,
        [](gpointer data) -> gboolean {
          auto connection = *static_cast<std::shared_ptr<Connection>*>(data);
          const auto now = g_get_monotonic_time();
          if ((!connection->hello || !connection->input.empty()) &&
              now - connection->last_input > 5000000) {
            connection->server->Close(connection, "frame-timeout");
            return G_SOURCE_REMOVE;
          }
          if (!connection->output.empty() &&
              now - connection->write_started > 5000000) {
            connection->server->Close(connection, "write-timeout");
            return G_SOURCE_REMOVE;
          }
          return G_SOURCE_CONTINUE;
        },
        new std::shared_ptr<Connection>(connection),
        [](gpointer data) {
          delete static_cast<std::shared_ptr<Connection>*>(data);
        });
    g_source_attach(connection->deadline, io_context_);
  } catch (...) {
    Close(connection, "admission-exception");
    DispatchFailed();
  }
}

void Server::ArmRead(const std::shared_ptr<Connection>& connection) {
  if (connection->closed || connection->read_source || connection->parsing)
    return;
  connection->read_source = g_socket_create_source(
      connection->socket,
      static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), nullptr);
  g_source_set_callback(
      connection->read_source,
      G_SOURCE_FUNC(+[](GSocket*, GIOCondition, gpointer data) -> gboolean {
        auto connection = *static_cast<std::shared_ptr<Connection>*>(data);
        try {
          connection->server->Receive(connection);
        } catch (...) {
          connection->server->Close(connection, "receive-exception");
        }
        return connection->closed ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
      }),
      new std::shared_ptr<Connection>(connection), [](gpointer data) {
        delete static_cast<std::shared_ptr<Connection>*>(data);
      });
  g_source_attach(connection->read_source, io_context_);
}

void Server::Receive(const std::shared_ptr<Connection>& connection) {
  if (connection->closed || connection->parsing || stopping_)
    return;
  uint8_t buffer[8192];
  for (;;) {
    if (connection->input.size() >= 4) {
      uint32_t size = consent::FrameSize(connection->input.data());
      if (!size || size > kMaxFrame) {
        Close(connection, "frame-size");
        return;
      }
      if (connection->input.size() >= size + 4) {
        std::vector<uint8_t> payload(connection->input.begin() + 4,
                                     connection->input.begin() + 4 + size);
        connection->input.erase(connection->input.begin(),
                                connection->input.begin() + 4 + size);
        Parse(connection, std::move(payload));
        return;
      }
    }
    GError* error = nullptr;
    gssize length =
        g_socket_receive(connection->socket, reinterpret_cast<gchar*>(buffer),
                         sizeof(buffer), nullptr, &error);
    if (length < 0 &&
        g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
      g_clear_error(&error);
      ArmRead(connection);
      return;
    }
    g_clear_error(&error);
    if (length <= 0) {
      Close(connection, "read-eof-or-error");
      return;
    }
    if (connection->input.empty())
      connection->last_input = g_get_monotonic_time();
    connection->input.insert(connection->input.end(), buffer, buffer + length);
    if (connection->input.size() > kMaxFrame + sizeof(buffer) + 4) {
      Close(connection, "input-limit");
      return;
    }
  }
}

void Server::Parse(const std::shared_ptr<Connection>& connection,
                   std::vector<uint8_t> payload) {
  auto job = std::make_unique<ParseJob>(
      ParseJob{this, connection, std::move(payload)});
  consent::Admission admission(parse_jobs_, kMaxJobs);
  if (!admission.Accepted()) {
    Close(connection, "parser-overload");
    return;
  }
  connection->parsing = true;
  Destroy(connection->read_source);
  GError* error = nullptr;
  if (!g_thread_pool_push(parser_pool_, job.get(), &error)) {
    g_clear_error(&error);
    Close(connection, "parser-unavailable");
    return;
  }
  job.release();
  admission.Commit();
}

void Server::Execute(const std::shared_ptr<Connection>& connection,
                     consent::Message request) {
  auto method = consent::Get(request, "method");
  int64_t id = 0;
  if (consent::Get(request, "v") != "1" || method.empty() ||
      !consent::ParseNumber(consent::Get(request, "id"), &id) || id <= 0 ||
      (!connection->hello && method != "hello")) {
    Close(connection, "protocol-handshake");
    return;
  }
  connection->last_input = g_get_monotonic_time();
  for (const auto& field : request) {
    if (field.first.empty() || field.first[0] == '_') {
      Queue(connection, Error(request, CONSENT_ERROR_INVALID_PARAMETER));
      return;
    }
  }
  if (!identity_.IsAlive(connection->peer, connection->process) ||
      !IdentityPolicy::Allows(connection->peer, method)) {
    Queue(connection, Error(request, CONSENT_ERROR_PERMISSION_DENIED));
    return;
  }
  if (connection->inflight >= 32 || stopping_) {
    Queue(connection, Error(request, CONSENT_ERROR_BUSY));
    return;
  }
  if (method == "hello") {
    connection->hello = true;
    auto reply = published_;
    reply["v"] = "1";
    reply["method"] = "reply";
    reply["id"] = consent::Get(request, "id");
    reply["status"] = "0";
    reply["approval_version"] = "1";
    reply["approval_supported_versions"] = "1,2";
    reply["approval_period_choice"] = "1";
    Queue(connection, reply);
    return;
  }
  ++connection->inflight;
  auto peer = connection->peer;
  auto request_id = consent::Get(request, "id");
  if (!Submit([this, connection, peer, request = std::move(request)]() mutable {
        consent::Message reply;
        consent::Message snapshot;
        try {
          const auto method = consent::Get(request, "method");
          if (method == "prompt")
            request["method"] = "get_prompt";
          else if (method == "session_state")
            request["method"] = "session_get_state";
          else if (method == "data_derived")
            request["method"] = "data_register_derived";
          else if (method == "cleanup")
            request["method"] = "cleanup_get_state";
          if (method == "register" || method == "update") {
            std::string install;
            if (!GetInstallationIdentity(consent::Get(request, "package"),
                                         consent::Get(request, "app"),
                                         &install) ||
                install != consent::Get(request, "expected_generation")) {
              reply = Error(request, CONSENT_ERROR_PERMISSION_DENIED);
            } else {
              request["_install_identity"] = install;
            }
          }
          if (reply.empty())
            reply = repository_->Execute(peer, request);
          snapshot = repository_->Snapshot();
          // Snapshot may detect replacement and recover. A decision from the
          // old database must never be relabelled with the recovered
          // generation.
          if (consent::Get(reply, "status") == "0" &&
              consent::Get(reply, "epoch") != consent::Get(snapshot, "epoch"))
            reply = Error(request, CONSENT_ERROR_STORAGE);
        } catch (...) {
          reply = Error(request, CONSENT_ERROR_STORAGE);
          snapshot.clear();
        }
        reply["v"] = "1";
        reply["id"] = consent::Get(request, "id");
        reply["method"] = "reply";
        Post(io_context_, [this, connection, snapshot, reply] {
          if (snapshot.empty()) {
            // A disconnected client treats every local cached decision as
            // unsynced.
            auto clients = clients_;
            for (const auto& entry : clients) {
              if (entry.second != connection)
                Close(entry.second, "storage-unavailable");
            }
          }
          Publish(snapshot);
          const auto generation = consent::Get(reply, "profile_generation");
          if (!generation.empty() &&
              generation != std::to_string(profiles_->Generation())) {
            auto stale = reply;
            stale.clear();
            stale["v"] = "1";
            stale["id"] = consent::Get(reply, "id");
            stale["method"] = "reply";
            stale["status"] = std::to_string(CONSENT_ERROR_STALE);
            if (connection->inflight)
              --connection->inflight;
            if (!connection->closed)
              Queue(connection, stale);
            return;
          }
          if (connection->inflight)
            --connection->inflight;
          if (!connection->closed &&
              identity_.IsAlive(connection->peer, connection->process))
            Queue(connection, reply);
          else if (!connection->closed)
            Close(connection, "identity-lost-before-reply");
        });
      })) {
    --connection->inflight;
    Queue(connection, {{"v", "1"},
                       {"id", request_id},
                       {"method", "reply"},
                       {"status", "-16"}});
  }
}

void Server::Queue(const std::shared_ptr<Connection>& connection,
                   const consent::Message& message) {
  if (connection->closed)
    return;
  auto bytes = consent::Encode(message);
  if (bytes.empty()) {
    Close(connection, "invalid-outgoing-message");
    return;
  }
  if (connection->output_bytes + bytes.size() > kMaxOutput) {
    Close(connection, "output-limit");
    return;
  }
  connection->output_bytes += bytes.size();
  if (connection->output.empty())
    connection->write_started = g_get_monotonic_time();
  connection->output.push_back({std::move(bytes),
                                consent::Get(message, "profile_generation")});
  Write(connection);
  if (connection->closed || connection->output.empty() ||
      connection->write_source)
    return;
  connection->write_source = g_socket_create_source(
      connection->socket,
      static_cast<GIOCondition>(G_IO_OUT | G_IO_HUP | G_IO_ERR), nullptr);
  g_source_set_callback(
      connection->write_source,
      G_SOURCE_FUNC(+[](GSocket*, GIOCondition, gpointer data) -> gboolean {
        auto connection = *static_cast<std::shared_ptr<Connection>*>(data);
        connection->server->Write(connection);
        return connection->closed || connection->output.empty()
                   ? G_SOURCE_REMOVE
                   : G_SOURCE_CONTINUE;
      }),
      new std::shared_ptr<Connection>(connection), [](gpointer data) {
        delete static_cast<std::shared_ptr<Connection>*>(data);
      });
  g_source_attach(connection->write_source, io_context_);
}

void Server::Write(const std::shared_ptr<Connection>& connection) {
  while (!connection->closed && !connection->output.empty()) {
    auto& frame = connection->output.front();
    if (!frame.profile_generation.empty() &&
        frame.profile_generation != std::to_string(profiles_->Generation())) {
      // Closing also handles partially written frames without splicing a new
      // envelope into the old response. Fully sent frames are nonretractable.
      Close(connection, "profile-generation-changed-before-send");
      return;
    }
    auto& bytes = frame.bytes;
    GError* error = nullptr;
    gssize n = g_socket_send(
        connection->socket,
        reinterpret_cast<const gchar*>(bytes.data() + connection->offset),
        bytes.size() - connection->offset, nullptr, &error);
    if (n < 0 && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
      g_clear_error(&error);
      return;
    }
    g_clear_error(&error);
    if (n <= 0) {
      Close(connection, "write-error");
      return;
    }
    connection->offset += static_cast<size_t>(n);
    connection->output_bytes -= static_cast<size_t>(n);
    if (connection->offset == bytes.size()) {
      connection->output.pop_front();
      connection->offset = 0;
    }
  }
  Destroy(connection->write_source);
}

void Server::Close(const std::shared_ptr<Connection>& connection,
                   const char* reason) noexcept {
  if (connection->closed)
    return;
  const auto pending_input_bytes = connection->input.size();
  connection->closed = true;
  Destroy(connection->read_source);
  Destroy(connection->write_source);
  Destroy(connection->deadline);
  g_io_stream_close(G_IO_STREAM(connection->stream), nullptr, nullptr);
  connection->output.clear();
  connection->input.clear();
  clients_.erase(connection->id);
  --connections_;
  LOG(INFO) << "event=client-disconnected instance="
            << static_cast<unsigned long long>(connection->id)
            << " pid=" << static_cast<long>(connection->peer.pid)
            << " uid=" << static_cast<unsigned long>(connection->peer.uid)
            << " gid=" << static_cast<unsigned long>(connection->peer.gid)
            << " reason=" << reason
            << " pending_input_bytes=" << pending_input_bytes;
}

void Server::Publish(const consent::Message& snapshot) {
  if (consent::Get(snapshot, "epoch") == consent::Get(published_, "epoch") &&
      consent::Get(snapshot, "revision") ==
          consent::Get(published_, "revision"))
    return;
  published_ = snapshot;
  auto event = snapshot;
  event.erase("status");
  event["v"] = "1";
  event["id"] = "0";
  event["method"] = "event";
  event["event"] = "invalidate";
  auto clients = clients_;  // Queue may close slow consumers and erase entries.
  for (const auto& entry : clients) {
    if (entry.second->hello)
      Queue(entry.second, event);
  }
}

void Server::Stop() noexcept {
  if (stopping_.exchange(true))
    return;
  sd_notify(0, "STOPPING=1");
  if (listener_)
    g_socket_service_stop(listener_);
  Destroy(tick_);
  if (profile_authority_)
    profile_authority_->Stop();
  LOG(INFO) << "event=shutdown stage=stop-admission";
  g_source_set_ready_time(stop_io_source_, 0);
  g_main_context_wakeup(io_context_);
}

void Server::StopIo() noexcept {
  while (!clients_.empty()) {
    auto connection = clients_.begin()->second;
    Close(connection, "daemon-shutdown");
  }
  // One preallocated final job may exceed normal admission by one. All normal
  // producers are stopped; FIFO preserves accepted DB work before shutdown.
  ++db_jobs_;
  g_async_queue_push(db_queue_, shutdown_job_.release());
}

void Server::ShutdownDatabase() noexcept {
  try {
    if (repository_)
      repository_->Shutdown();
  } catch (...) {
    LOG(ERROR) << "event=shutdown reason=storage-failed cleanup=unconfirmed";
  }
  g_source_set_ready_time(finished_source_, 0);
  g_main_context_wakeup(main_context_);
}

}  // namespace consentd
