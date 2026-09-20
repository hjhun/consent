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

#include <glib-unix.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <exception>
#include <utility>
#include <vector>

#include "repository.hh"

#ifndef CONSENT_STATE_DIR
#define CONSENT_STATE_DIR "/opt/var/lib/consentd"
#endif
#ifndef CONSENT_ROLE_CONFIG
#define CONSENT_ROLE_CONFIG "/etc/consent/roles.conf"
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

consent::Message Error(const consent::Message& request, int status) {
  return {{"v", "1"}, {"id", consent::Get(request, "id")},
          {"status", std::to_string(status)}, {"method", "reply"}};
}
}  // namespace

struct Server::Connection {
  Server* server = nullptr;
  uint64_t id = 0;
  GSocketConnection* stream = nullptr;
  GSocket* socket = nullptr;  // Borrowed from stream, only I/O owner accesses it.
  GSource* read_source = nullptr;
  GSource* write_source = nullptr;
  GSource* deadline = nullptr;
  Peer peer;
  ProcessIdentity process;
  bool closed = false;
  bool parsing = false;
  bool hello = false;
  unsigned inflight = 0;
  gint64 last_input = 0;
  std::vector<uint8_t> input;
  std::deque<std::vector<uint8_t>> output;
  size_t output_bytes = 0;
  size_t offset = 0;
  gint64 write_started = 0;
  ~Connection() {
    Destroy(read_source);
    Destroy(write_source);
    Destroy(deadline);
    if (stream)
      g_object_unref(stream);
  }
};

struct Server::ParseJob {
  Server* server;
  std::shared_ptr<Connection> connection;
  std::vector<uint8_t> payload;
};

Server::Server() {
  main_context_ = g_main_context_default();
  main_loop_ = g_main_loop_new(main_context_, FALSE);
  io_context_ = g_main_context_new();
  io_loop_ = g_main_loop_new(io_context_, FALSE);
  db_queue_ = g_async_queue_new();
}

Server::~Server() {
  Destroy(tick_);
  Destroy(sigterm_);
  Destroy(sigint_);
  if (listener_)
    g_object_unref(listener_);
  if (parser_pool_)
    g_thread_pool_free(parser_pool_, FALSE, TRUE);
  if (io_thread_) {
    Post(io_context_, [this] { g_main_loop_quit(io_loop_); });
    g_thread_join(io_thread_);
  }
  if (db_thread_) {
    // An empty callable is the queue sentinel. No producers remain here.
    g_async_queue_push(db_queue_, new std::function<void()>());
    g_thread_join(db_thread_);
  }
  while (g_main_context_pending(io_context_))
    g_main_context_iteration(io_context_, FALSE);
  clients_.clear();
  g_async_queue_unref(db_queue_);
  g_main_loop_unref(io_loop_);
  g_main_context_unref(io_context_);
  g_main_loop_unref(main_loop_);
}

void Server::Post(GMainContext* context, std::function<void()> work) {
  GSource* source = g_idle_source_new();
  g_source_set_callback(source, [](gpointer data) -> gboolean {
    try {
      (*static_cast<std::function<void()>*>(data))();
    } catch (const std::exception& error) {
      g_warning("event=dispatch-failed reason=%s", error.what());
    } catch (...) {
      g_warning("event=dispatch-failed reason=unknown");
    }
    return G_SOURCE_REMOVE;
  }, new std::function<void()>(std::move(work)), [](gpointer data) {
    delete static_cast<std::function<void()>*>(data);
  });
  g_source_attach(source, context);
  g_source_unref(source);
}

bool Server::Submit(std::function<void()> work) {
  if (db_jobs_.fetch_add(1) >= kMaxJobs) {
    --db_jobs_;
    return false;
  }
  g_async_queue_push(db_queue_, new std::function<void()>(std::move(work)));
  return true;
}

int Server::Run(int listener_fd) {
  std::string error;
  if (!identity_.Load(CONSENT_ROLE_CONFIG, &error)) {
    close(listener_fd);
    g_warning("event=identity-config-failed reason=%s", error.c_str());
    return 1;
  }
  int state = OpenProtected(CONSENT_STATE_DIR, true);
  if (state < 0) {
    close(listener_fd);
    g_warning("event=state-directory-unprotected");
    return 1;
  }
  close(state);
  GError* gio_error = nullptr;
  GSocket* socket = g_socket_new_from_fd(listener_fd, &gio_error);
  if (!socket) {
    close(listener_fd);
    g_clear_error(&gio_error);
    return 1;
  }
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
  g_signal_connect(listener_, "incoming", G_CALLBACK(+[](
      GSocketService*, GSocketConnection* stream, GObject*, gpointer data)
      -> gboolean {
    auto* self = static_cast<Server*>(data);
    if (self->stopping_ || self->connections_.fetch_add(1) >= kMaxConnections) {
      if (!self->stopping_)
        --self->connections_;
      return FALSE;
    }
    g_object_ref(stream);
    Post(self->io_context_, [self, stream] { self->AddConnection(stream); });
    return TRUE;
  }), this);

  parser_pool_ = g_thread_pool_new([](gpointer data, gpointer) {
    std::unique_ptr<ParseJob> job(static_cast<ParseJob*>(data));
    consent::Message request;
    bool valid = false;
    try {
      valid = consent::Decode(job->payload.data(), job->payload.size(), &request);
    } catch (...) {
      valid = false;
    }
    auto* self = job->server;
    auto connection = job->connection;
    --self->parse_jobs_;
    Post(self->io_context_, [self, connection, valid, request = std::move(request)]() mutable {
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
  }, nullptr, 2, FALSE, &gio_error);
  if (!parser_pool_) {
    g_clear_error(&gio_error);
    return 1;
  }
  io_thread_ = g_thread_new("consent-io", [](gpointer data) -> gpointer {
    auto* self = static_cast<Server*>(data);
    g_main_context_push_thread_default(self->io_context_);
    g_main_loop_run(self->io_loop_);
    g_main_context_pop_thread_default(self->io_context_);
    return nullptr;
  }, this);
  db_thread_ = g_thread_new("consent-db", [](gpointer data) -> gpointer {
    auto* self = static_cast<Server*>(data);
    for (;;) {
      std::unique_ptr<std::function<void()>> job(
          static_cast<std::function<void()>*>(g_async_queue_pop(self->db_queue_)));
      if (!*job)
        break;
      try {
        (*job)();
      } catch (const std::exception& error) {
        g_warning("event=db-job-failed reason=%s", error.what());
      } catch (...) {
        g_warning("event=db-job-failed reason=unknown");
      }
      --self->db_jobs_;
    }
    self->repository_.reset();
    return nullptr;
  }, this);

  auto attach_signal = [this](int number) {
    GSource* source = g_unix_signal_source_new(number);
    g_source_set_callback(source, [](gpointer data) -> gboolean {
      static_cast<Server*>(data)->Stop();
      return G_SOURCE_CONTINUE;
    }, this, nullptr);
    g_source_attach(source, main_context_);
    return source;
  };
  sigterm_ = attach_signal(SIGTERM);
  sigint_ = attach_signal(SIGINT);
  Submit([this] {
    std::string error;
    repository_ = std::make_unique<Repository>(
        std::string(CONSENT_STATE_DIR) + "/consent.db", CONSENT_STATE_DIR);
    repository_->SetInstallationValidator(ValidateInstallation);
    repository_->SetPackageGenerationValidator(ValidatePackageGeneration);
    bool ready = repository_->Open(&error);
    auto snapshot = ready ? repository_->Snapshot() : consent::Message{};
    Post(main_context_, [this, ready, error, snapshot] {
      if (!ready) {
        g_warning("event=database-open-failed reason=%s", error.c_str());
        exit_status_ = 1;
        Stop();
        return;
      }
      if (stopping_)
        return;
      Post(io_context_, [this, snapshot] { Publish(snapshot); });
      tick_ = g_timeout_source_new(250);
      g_source_set_callback(tick_, [](gpointer data) -> gboolean {
        auto* self = static_cast<Server*>(data);
        if (self->stopping_)
          return G_SOURCE_REMOVE;
        self->Submit([self] {
          try {
            self->repository_->Tick();
            auto snapshot = self->repository_->Snapshot();
            Post(self->io_context_, [self, snapshot] { self->Publish(snapshot); });
          } catch (...) {
            Post(self->io_context_, [self] {
              auto clients = self->clients_;
              for (const auto& entry : clients)
                self->Close(entry.second, "storage-unavailable");
              self->published_.clear();
            });
          }
        });
        return G_SOURCE_CONTINUE;
      }, this, nullptr);
      g_source_attach(tick_, main_context_);
      g_socket_service_start(listener_);
      if (sd_notify(0, "READY=1") < 0) {
        exit_status_ = 1;
        Stop();
      } else {
        g_message("event=ready epoch=%s connections=%u db_jobs=%u parser_threads=2",
            consent::Get(snapshot, "epoch").c_str(), kMaxConnections, kMaxJobs);
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
  connection->stream = stream;
  connection->socket = g_socket_connection_get_socket(stream);
  g_socket_set_blocking(connection->socket, FALSE);
  clients_[connection->id] = connection;
  std::string error;
  bool trusted = !stopping_ && identity_.Authenticate(
      g_socket_get_fd(connection->socket), &connection->peer,
      &connection->process, &error);
  g_message("event=client-connected instance=%llu pid=%ld uid=%lu gid=%lu role=%s",
      static_cast<unsigned long long>(connection->id),
      static_cast<long>(connection->peer.pid),
      static_cast<unsigned long>(connection->peer.uid),
      static_cast<unsigned long>(connection->peer.gid),
      trusted ? connection->peer.identity.c_str() : "rejected");
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
      connection->peer.roles.count("ui") || connection->peer.roles.count("admin");
  if (same_uid > (control ? 32u : 24u)) {
    Close(connection, "uid-connection-limit");
    return;
  }
  connection->last_input = g_get_monotonic_time();
  ArmRead(connection);
  connection->deadline = g_timeout_source_new_seconds(1);
  g_source_set_callback(connection->deadline, [](gpointer data) -> gboolean {
    auto connection = *static_cast<std::shared_ptr<Connection>*>(data);
    const auto now = g_get_monotonic_time();
    if ((!connection->hello || !connection->input.empty()) &&
        now - connection->last_input > 5000000) {
      connection->server->Close(connection, "frame-timeout");
      return G_SOURCE_REMOVE;
    }
    if (!connection->output.empty() && now - connection->write_started > 5000000) {
      connection->server->Close(connection, "write-timeout");
      return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
  }, new std::shared_ptr<Connection>(connection), [](gpointer data) {
    delete static_cast<std::shared_ptr<Connection>*>(data);
  });
  g_source_attach(connection->deadline, io_context_);
}

void Server::ArmRead(const std::shared_ptr<Connection>& connection) {
  if (connection->closed || connection->read_source || connection->parsing)
    return;
  connection->read_source = g_socket_create_source(connection->socket,
      static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), nullptr);
  g_source_set_callback(connection->read_source,
      G_SOURCE_FUNC(+[](GSocket*, GIOCondition, gpointer data) -> gboolean {
        auto connection = *static_cast<std::shared_ptr<Connection>*>(data);
        try {
          connection->server->Receive(connection);
        } catch (...) {
          connection->server->Close(connection, "receive-exception");
        }
        return connection->closed ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
      }), new std::shared_ptr<Connection>(connection), [](gpointer data) {
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
    gssize length = g_socket_receive(connection->socket,
        reinterpret_cast<gchar*>(buffer), sizeof(buffer), nullptr, &error);
    if (length < 0 && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
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
  if (parse_jobs_.fetch_add(1) >= kMaxJobs) {
    --parse_jobs_;
    Close(connection, "parser-overload");
    return;
  }
  connection->parsing = true;
  Destroy(connection->read_source);
  auto* job = new ParseJob{this, connection, std::move(payload)};
  GError* error = nullptr;
  if (!g_thread_pool_push(parser_pool_, job, &error)) {
    --parse_jobs_;
    delete job;
    g_clear_error(&error);
    Close(connection, "parser-unavailable");
  }
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
      Queue(connection, Error(request, -22));
      return;
    }
  }
  if (!identity_.IsAlive(connection->peer, connection->process) ||
      !IdentityPolicy::Allows(connection->peer, method)) {
    Queue(connection, Error(request, -13));
    return;
  }
  if (connection->inflight >= 32 || stopping_) {
    Queue(connection, Error(request, -16));
    return;
  }
  if (method == "hello") {
    connection->hello = true;
    auto reply = published_;
    reply["v"] = "1";
    reply["method"] = "reply";
    reply["id"] = consent::Get(request, "id");
    reply["status"] = "0";
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
                consent::Get(request, "app"), &install) ||
            install != consent::Get(request, "expected_generation")) {
          reply = Error(request, -13);
        } else {
          request["_install_identity"] = install;
        }
      }
      if (reply.empty())
        reply = repository_->Execute(peer, request);
      snapshot = repository_->Snapshot();
      // Snapshot may detect replacement and recover. A decision from the old
      // database must never be relabelled with the recovered generation.
      if (consent::Get(reply, "status") == "0" &&
          consent::Get(reply, "epoch") != consent::Get(snapshot, "epoch"))
        reply = Error(request, -2006);
    } catch (...) {
      reply = Error(request, -2006);
      snapshot.clear();
    }
    reply["v"] = "1";
    reply["id"] = consent::Get(request, "id");
    reply["method"] = "reply";
    Post(io_context_, [this, connection, snapshot, reply] {
      if (snapshot.empty()) {
        // A disconnected client treats every local cached decision as unsynced.
        auto clients = clients_;
        for (const auto& entry : clients) {
          if (entry.second != connection)
            Close(entry.second, "storage-unavailable");
        }
      }
      Publish(snapshot);
      if (connection->inflight)
        --connection->inflight;
      if (!connection->closed && identity_.IsAlive(connection->peer, connection->process))
        Queue(connection, reply);
      else if (!connection->closed)
        Close(connection, "identity-lost-before-reply");
    });
  })) {
    --connection->inflight;
    Queue(connection, {{"v", "1"}, {"id", request_id}, {"method", "reply"},
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
  connection->output.push_back(std::move(bytes));
  Write(connection);
  if (connection->closed || connection->output.empty() || connection->write_source)
    return;
  connection->write_source = g_socket_create_source(connection->socket,
      static_cast<GIOCondition>(G_IO_OUT | G_IO_HUP | G_IO_ERR), nullptr);
  g_source_set_callback(connection->write_source,
      G_SOURCE_FUNC(+[](GSocket*, GIOCondition, gpointer data) -> gboolean {
        auto connection = *static_cast<std::shared_ptr<Connection>*>(data);
        connection->server->Write(connection);
        return connection->closed || connection->output.empty() ?
            G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
      }), new std::shared_ptr<Connection>(connection), [](gpointer data) {
        delete static_cast<std::shared_ptr<Connection>*>(data);
      });
  g_source_attach(connection->write_source, io_context_);
}

void Server::Write(const std::shared_ptr<Connection>& connection) {
  while (!connection->closed && !connection->output.empty()) {
    auto& bytes = connection->output.front();
    GError* error = nullptr;
    gssize n = g_socket_send(connection->socket,
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

void Server::Close(const std::shared_ptr<Connection>& connection, const char* reason) {
  if (connection->closed)
    return;
  connection->closed = true;
  Destroy(connection->read_source);
  Destroy(connection->write_source);
  Destroy(connection->deadline);
  g_io_stream_close(G_IO_STREAM(connection->stream), nullptr, nullptr);
  connection->output.clear();
  connection->input.clear();
  clients_.erase(connection->id);
  --connections_;
  g_message("event=client-disconnected instance=%llu pid=%ld uid=%lu gid=%lu reason=%s",
      static_cast<unsigned long long>(connection->id),
      static_cast<long>(connection->peer.pid),
      static_cast<unsigned long>(connection->peer.uid),
      static_cast<unsigned long>(connection->peer.gid), reason);
}

void Server::Publish(const consent::Message& snapshot) {
  if (consent::Get(snapshot, "epoch") == consent::Get(published_, "epoch") &&
      consent::Get(snapshot, "revision") == consent::Get(published_, "revision"))
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

void Server::Stop() {
  if (stopping_.exchange(true))
    return;
  sd_notify(0, "STOPPING=1");
  g_socket_service_stop(listener_);
  Destroy(tick_);
  g_message("event=shutdown stage=stop-admission");
  Post(io_context_, [this] {
    auto clients = clients_;
    for (const auto& entry : clients)
      Close(entry.second, "daemon-shutdown");
    // Run after all accepted jobs. The main and I/O loops remain alive until
    // the DB queue and parser completions have drained.
    ++db_jobs_;
    g_async_queue_push(db_queue_, new std::function<void()>([this] {
      try {
        if (repository_)
          repository_->Shutdown();
      } catch (...) {
        g_warning("event=shutdown reason=storage-failed cleanup=unconfirmed");
      }
      Post(main_context_, [this] {
        g_message("event=shutdown stage=database-drained");
        g_main_loop_quit(main_loop_);
      });
    }));
  });
}

}  // namespace consentd
