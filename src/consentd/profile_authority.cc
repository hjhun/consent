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
#include "profile_authority.hh"

#include <cstring>
#include <utility>

#include "common/logging.hh"

namespace consentd {
namespace {

constexpr char kManager[] = "org.tizen.sessiond";
constexpr char kReady[] = "org.tizen.sessiond.fully_ready";
constexpr char kPath[] = "/org/tizen/sessiond";
constexpr char kInterface[] = "org.tizen.sessiond.subsession.Manager";

}  // namespace

class ProfileAuthority::Impl final
    : public std::enable_shared_from_this<ProfileAuthority::Impl> {
 public:
  Impl(std::shared_ptr<ProfileState> state, int uid, Barrier barrier,
       GDBusConnection* connection, unsigned timeout)
      : state_(std::move(state)), uid_(uid), barrier_(std::move(barrier)),
        timeout_(timeout), injected_(connection != nullptr) {
    if (connection)
      connection_ = G_DBUS_CONNECTION(g_object_ref(connection));
  }
  ~Impl() {
    if (connection_)
      g_object_unref(connection_);
  }

  std::string test_address_;

  void Start() {
    if (stopping_ || connecting_ || watches_[0] || watches_[1])
      return;
    if (connection_) {
      Watch();
      return;
    }
    GError* error = nullptr;
    gchar* address = test_address_.empty()
        ? g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SYSTEM, nullptr, &error)
        : g_strdup(test_address_.c_str());
    if (!address) {
      g_clear_error(&error);
      Lose();
      return;
    }
    connecting_ = true;
    connect_cancel_ = g_cancellable_new();
    auto* pin = new Connect{shared_from_this(), incarnation_};
    g_dbus_connection_new_for_address(
        address, static_cast<GDBusConnectionFlags>(
                     G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                     G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, connect_cancel_,
        [](GObject*, GAsyncResult* result, gpointer data) {
          std::unique_ptr<Connect> pin(static_cast<Connect*>(data));
          auto self = pin->self;
          GError* error = nullptr;
          auto* connection = g_dbus_connection_new_for_address_finish(
              result, &error);
          g_clear_error(&error);
          if (pin->incarnation != self->incarnation_) {
            if (connection) {
              g_dbus_connection_close(connection, nullptr, nullptr, nullptr);
              g_object_unref(connection);
            }
            return;
          }
          self->connecting_ = false;
          g_clear_object(&self->connect_cancel_);
          self->ClearTimer();
          if (self->stopping_) {
            if (connection)
              g_object_unref(connection);
          } else if (connection) {
            self->connection_ = connection;
            self->Watch();
          } else {
            self->Lose();
          }
        }, pin);
    g_free(address);
    ArmTimer([self = shared_from_this()] {
      if (self->connect_cancel_)
        g_cancellable_cancel(self->connect_cancel_);
    }, timeout_);
  }

  void Stop() noexcept {
    if (stopping_)
      return;
    stopping_ = true;
    ++incarnation_;
    state_->Fence();
    ClearTimer();
    if (connect_cancel_) {
      g_cancellable_cancel(connect_cancel_);
      g_clear_object(&connect_cancel_);
    }
    CancelCalls();
    Unwatch();
    if (connection_ && !injected_)
      g_dbus_connection_close(connection_, nullptr, nullptr, nullptr);
  }

 private:
  struct Connect {
    std::shared_ptr<Impl> self;
    uint64_t incarnation;
  };

  struct Call {
    std::shared_ptr<Impl> self;
    uint64_t generation;
    std::string owner;
    std::function<void(GVariant*)> done;
  };

  void ClearTimer() noexcept {
    if (timer_) {
      g_source_destroy(timer_);
      g_source_unref(timer_);
      timer_ = nullptr;
    }
  }

  void ArmTimer(std::function<void()> work, unsigned timeout) {
    ClearTimer();
    auto self = shared_from_this();
    auto guarded = [self, work = std::move(work)] {
      try {
        work();
      } catch (...) {
        self->Stop();
      }
    };
    timer_ = g_timeout_source_new(timeout);
    g_source_set_callback(timer_, [](gpointer data) -> gboolean {
      try {
        auto work = *static_cast<std::function<void()>*>(data);
        work();
      } catch (...) {
        LOG(WARNING) << "event=profile-authority callback=timer-failed";
      }
      return G_SOURCE_REMOVE;
    }, new std::function<void()>(std::move(guarded)), [](gpointer data) {
      delete static_cast<std::function<void()>*>(data);
    });
    g_source_attach(timer_, g_main_context_get_thread_default());
  }

  void CancelCalls() noexcept {
    if (cancel_) {
      g_cancellable_cancel(cancel_);
      g_clear_object(&cancel_);
    }
  }

  void Unwatch() noexcept {
    for (auto& id : watches_) {
      if (id)
        g_bus_unwatch_name(std::exchange(id, 0));
    }
    if (signal_) {
      g_dbus_connection_signal_unsubscribe(connection_, signal_);
      signal_ = 0;
    }
    if (closed_) {
      g_signal_handler_disconnect(connection_, closed_);
      closed_ = 0;
    }
  }

  void Watch() {
    if (stopping_ || watches_[0] || watches_[1])
      return;
    g_dbus_connection_set_exit_on_close(connection_, FALSE);
    closed_ = g_signal_connect(connection_, "closed", G_CALLBACK(+[](
        GDBusConnection*, gboolean, GError*, gpointer data) {
      auto* self = static_cast<Impl*>(data);
      try {
        self->Lose();
      } catch (...) {
        self->Stop();
      }
    }), this);
    for (unsigned i = 0; i < 2; ++i) {
      watches_[i] = g_bus_watch_name_on_connection(
          connection_, i ? kReady : kManager, G_BUS_NAME_WATCHER_FLAGS_NONE,
          [](GDBusConnection*, const gchar* name, const gchar* owner,
             gpointer data) {
            auto* self = static_cast<Impl*>(data);
            if (self->stopping_)
              return;
            auto& slot = !strcmp(name, kReady) ? self->ready_owner_
                                               : self->owner_;
            if (!slot.empty() && slot != owner) {
              self->Lose();
              return;
            }
            slot = owner;
            try {
              self->Initialize();
            } catch (...) {
              self->Stop();
            }
          },
          [](GDBusConnection*, const gchar*, gpointer data) {
            auto* self = static_cast<Impl*>(data);
            if (!self->stopping_ &&
                (!self->owner_.empty() || !self->ready_owner_.empty()))
              try {
                self->Lose();
              } catch (...) {
                self->Stop();
              }
          }, this, nullptr);
    }
  }

  void Lose() {
    if (stopping_)
      return;
    ClearTimer();
    ++incarnation_;
    connecting_ = false;
    if (connect_cancel_) {
      g_cancellable_cancel(connect_cancel_);
      g_clear_object(&connect_cancel_);
    }
    state_->Fence();
    initialized_ = false;
    synchronized_ = false;
    pending_ = 0;
    barrier_done_ = false;
    CancelCalls();
    Unwatch();
    owner_.clear();
    ready_owner_.clear();
    if (connection_ && !injected_) {
      g_dbus_connection_close(connection_, nullptr, nullptr, nullptr);
      g_clear_object(&connection_);
    }
    auto self = shared_from_this();
    const auto generation = state_->Generation();
    const auto incarnation = incarnation_;
    barrier_(true, "", [self, generation, incarnation](bool success) {
      if (!self->stopping_ && success &&
          self->incarnation_ == incarnation &&
          self->state_->Generation() == generation)
        self->ArmTimer([self] { self->Start(); }, 1000);
    });
    LOG(WARNING) << "event=profile-authority state=unsynced";
  }

  void Method(const char* name, GVariant* params, const GVariantType* type,
              std::function<void(GVariant*)> done) {
    auto* call = new Call{shared_from_this(), state_->Generation(), owner_,
                          std::move(done)};
    g_dbus_connection_call(connection_, owner_.c_str(), kPath, kInterface,
        name, params, type, G_DBUS_CALL_FLAGS_NONE, timeout_, cancel_,
        [](GObject* object, GAsyncResult* result, gpointer data) {
          std::unique_ptr<Call> call(static_cast<Call*>(data));
          GError* error = nullptr;
          GVariant* reply = g_dbus_connection_call_finish(
              G_DBUS_CONNECTION(object), result, &error);
          auto self = call->self;
          bool current = !self->stopping_ &&
              call->generation == self->state_->Generation() &&
              call->owner == self->owner_ &&
              self->owner_ == self->ready_owner_;
          if (current) {
            if (reply)
              try {
                call->done(reply);
              } catch (...) {
                self->Stop();
              }
            else {
              try {
                self->Lose();
              } catch (...) {
                self->Stop();
              }
            }
          }
          if (reply)
            g_variant_unref(reply);
          g_clear_error(&error);
        }, call);
  }

  bool Known(const std::string& user) const {
    for (const auto& binding : state_->Bindings()) {
      if (binding.user == user)
        return true;
    }
    return false;
  }

  void Initialize() {
    if (initialized_ || owner_.empty() || ready_owner_.empty())
      return;
    if (owner_ != ready_owner_) {
      Lose();
      return;
    }
    initialized_ = true;
    state_->Fence();
    cancel_ = g_cancellable_new();
    signal_ = g_dbus_connection_signal_subscribe(
        connection_, owner_.c_str(), kInterface, nullptr, kPath, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        [](GDBusConnection*, const gchar* sender, const gchar* path,
           const gchar* interface, const gchar* name, GVariant* params,
           gpointer data) {
          auto* self = static_cast<Impl*>(data);
          if (!self->stopping_ && self->owner_ == sender &&
              !strcmp(path, kPath) && !strcmp(interface, kInterface))
            try {
              self->Signal(name, params);
            } catch (...) {
              self->Stop();
            }
        }, this, nullptr);
    auto self = shared_from_this();
    Method("SwitchUserWait", g_variant_new("(i)", uid_),
           G_VARIANT_TYPE_UNIT, [self](GVariant*) {
      self->Method("RemoveUserWait", g_variant_new("(i)", self->uid_),
                   G_VARIANT_TYPE_UNIT, [self](GVariant*) {
        self->Read(true);
      });
    });
  }

  void Read(bool initial, bool removal = false) {
    auto self = shared_from_this();
    Method("GetCurrentUser", g_variant_new("(i)", uid_),
           G_VARIANT_TYPE("(s)"), [self, initial, removal](GVariant* reply) {
      const gchar* user = nullptr;
      g_variant_get(reply, "(&s)", &user);
      if (strlen(user) >= 20 || !self->Known(user) ||
          (!initial && !removal &&
           (self->next_ != user || !self->barrier_done_)) ||
          (removal && self->active_ != user)) {
        self->Lose();
        return;
      }
      std::string current(user);
      uint64_t generation = self->state_->Generation();
      self->barrier_(initial, "", [self, generation, current](bool success) {
        if (!self->stopping_ && success &&
            self->state_->Activate(generation, current)) {
          self->active_ = current;
          self->synchronized_ = true;
          self->pending_ = 0;
          self->ClearTimer();
          LOG(INFO) << "event=profile-authority state=ready generation="
                    << generation;
        }
      });
    });
  }

  void Signal(const gchar* name, GVariant* params) {
    if (!strcmp(name, "RemoveUserStarted")) {
      if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(is)"))) {
        Lose();
        return;
      }
      gint uid = 0;
      const gchar* user = nullptr;
      g_variant_get(params, "(i&s)", &uid, &user);
      if (uid != uid_)
        return;
      if (!synchronized_) {
        Lose();
        return;
      }
      if (!Known(user) || strlen(user) >= 20) {
        Lose();
        return;
      }
      state_->Fence();
      const uint64_t generation = state_->Generation();
      auto self = shared_from_this();
      std::string removed(user);
      barrier_(false, removed, [self, removed, generation](bool success) {
        if (self->stopping_ || !success ||
            generation != self->state_->Generation())
          return;
        self->Method("RemoveUserDone", g_variant_new("(is)", self->uid_,
                     removed.c_str()), G_VARIANT_TYPE_UNIT,
                     [self](GVariant*) { self->Read(false, true); });
      });
      return;
    }
    bool started = !strcmp(name, "SwitchUserStarted");
    if (!started && strcmp(name, "SwitchUserCompleted"))
      return;
    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(ixss)"))) {
      Lose();
      return;
    }
    gint uid = 0;
    gint64 id = 0;
    const gchar* previous = nullptr;
    const gchar* next = nullptr;
    g_variant_get(params, "(ix&s&s)", &uid, &id, &previous, &next);
    if (uid != uid_)
      return;
    if (!synchronized_) {
      Lose();
      return;
    }
    if (id <= 0 || strlen(previous) >= 20 || strlen(next) >= 20 ||
        !Known(previous) || !Known(next)) {
      Lose();
      return;
    }
    if (!started) {
      if (pending_ != id || previous_ != previous || next_ != next ||
          !barrier_done_) {
        Lose();
        return;
      }
      completed_ = true;
      if (acknowledged_)
        Read(false);
      return;
    }
    uint64_t generation = state_->Fence();
    if (pending_ || active_ != previous) {
      Lose();
      return;
    }
    pending_ = id;
    previous_ = previous;
    next_ = next;
    barrier_done_ = false;
    acknowledged_ = false;
    completed_ = false;
    auto self = shared_from_this();
    ArmTimer([self] { self->Lose(); }, 12000);
    barrier_(false, "", [self, generation, id](bool success) {
      if (self->stopping_ || generation != self->state_->Generation())
        return;
      if (!success) {
        self->Lose();
        return;
      }
      self->barrier_done_ = true;
      self->Method("SwitchUserDone", g_variant_new("(ix)", self->uid_, id),
                   G_VARIANT_TYPE_UNIT, [self](GVariant*) {
        self->acknowledged_ = true;
        if (self->completed_)
          self->Read(false);
      });
    });
  }

  std::shared_ptr<ProfileState> state_;
  int uid_;
  Barrier barrier_;
  unsigned timeout_;
  bool injected_;
  bool stopping_ = false;
  bool connecting_ = false;
  uint64_t incarnation_ = 1;
  bool initialized_ = false;
  bool synchronized_ = false;
  bool barrier_done_ = false;
  bool acknowledged_ = false;
  bool completed_ = false;
  GDBusConnection* connection_ = nullptr;
  GCancellable* cancel_ = nullptr;
  GCancellable* connect_cancel_ = nullptr;
  guint watches_[2] = {};
  guint signal_ = 0;
  gulong closed_ = 0;
  GSource* timer_ = nullptr;
  std::string owner_;
  std::string ready_owner_;
  std::string active_;
  gint64 pending_ = 0;
  std::string previous_;
  std::string next_;
};

ProfileAuthority::ProfileAuthority(std::shared_ptr<ProfileState> state,
                                   int session_uid, Barrier barrier)
    : impl_(std::make_shared<Impl>(std::move(state), session_uid,
                                  std::move(barrier), nullptr, 3000)) {}

ProfileAuthority::ProfileAuthority(std::shared_ptr<ProfileState> state,
                                   int session_uid, Barrier barrier,
                                   GDBusConnection* test_connection,
                                   unsigned timeout_ms)
    : impl_(std::make_shared<Impl>(std::move(state), session_uid,
                                  std::move(barrier), test_connection,
                                  timeout_ms)) {}

ProfileAuthority::ProfileAuthority(std::shared_ptr<ProfileState> state,
                                   int session_uid, Barrier barrier,
                                   std::string test_address)
    : ProfileAuthority(std::move(state), session_uid, std::move(barrier)) {
  impl_->test_address_ = std::move(test_address);
}

ProfileAuthority::~ProfileAuthority() {
  Stop();
}

void ProfileAuthority::Start() {
  impl_->Start();
}

void ProfileAuthority::Stop() noexcept {
  impl_->Stop();
}

}  // namespace consentd
