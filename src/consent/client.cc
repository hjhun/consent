/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All rights reserved.
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
#include "client.hh"

#include "endpoint.hh"
#include "io_context.hh"
#include "common/resource.hh"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <deque>
#include <utility>

#ifndef CONSENT_SOCKET_PATH
#define CONSENT_SOCKET_PATH "/run/.consentd.sock"
#endif
#ifndef CONSENT_SERVER_SMACK_LABEL
#define CONSENT_SERVER_SMACK_LABEL "System::Privileged"
#endif

namespace consent {
namespace {
constexpr size_t kMaxPending = 64;
constexpr size_t kMaxOutputBytes = 262144;
constexpr unsigned kMaximumWaitMs = 300000;
// PID and count update together, including concurrent first creation after
// fork.
std::atomic<uint64_t> client_budget{0};

bool ReserveClient() {
  const uint64_t process = static_cast<uint64_t>(getpid()) << 32;
  uint64_t observed = client_budget.load();
  for (;;) {
    unsigned count = (observed & 0xffffffff00000000ULL) == process
                         ? static_cast<unsigned>(observed)
                         : 0;
    if (count >= 16)
      return false;
    if (client_budget.compare_exchange_weak(observed, process | (count + 1)))
      return true;
  }
}

void ReleaseClient(pid_t pid) {
  if (pid != getpid())
    return;
  const uint64_t process = static_cast<uint64_t>(pid) << 32;
  uint64_t observed = client_budget.load();
  while ((observed & 0xffffffff00000000ULL) == process &&
         static_cast<unsigned>(observed) > 0) {
    if (client_budget.compare_exchange_weak(observed, observed - 1))
      return;
  }
}

class Lock final {
 public:
  explicit Lock(GMutex& mutex) : mutex_(mutex) { g_mutex_lock(&mutex_); }
  ~Lock() { g_mutex_unlock(&mutex_); }

 private:
  GMutex& mutex_;
};

struct EndpointSnapshot {
  dev_t device = 0;
  ino_t inode = 0;
};

bool TrustedEndpoint(EndpointSnapshot* snapshot) {
#ifndef CONSENT_TESTING
  const std::string endpoint = CONSENT_SOCKET_PATH;
  for (size_t end = 0; end < endpoint.size(); ++end) {
    if (end && endpoint[end] != '/')
      continue;
    std::string parent = end ? endpoint.substr(0, end) : "/";
    struct stat info = {};
    if (lstat(parent.c_str(), &info) || !S_ISDIR(info.st_mode) ||
        info.st_uid != 0 || (info.st_mode & 0002))
      return false;
    if (info.st_mode & 0020) {
      // Tizen /run is shared with system services. This exception grants no
      // trust to an arbitrary socket: connected kernel identity below is
      // mandatory, including its original bound pathname and SMACK label.
      struct group group = {};
      struct group* resolved = nullptr;
      char buffer[4096];
      if (parent != "/run" || endpoint != "/run/.consentd.sock" ||
          getgrnam_r("system_share", &group, buffer, sizeof(buffer),
                     &resolved) ||
          !resolved || info.st_gid != resolved->gr_gid)
        return false;
    }
  }
  struct stat info = {};
  if (lstat(endpoint.c_str(), &info) || !S_ISSOCK(info.st_mode) ||
      info.st_uid != 0 || (info.st_mode & 0002))
    return false;
  snapshot->device = info.st_dev;
  snapshot->inode = info.st_ino;
#else
  (void)snapshot;
#endif
  return true;
}

bool TrustedConnection(int fd, const EndpointSnapshot& snapshot) {
#ifndef CONSENT_TESTING
  struct stat info = {};
  if (lstat(CONSENT_SOCKET_PATH, &info) || !S_ISSOCK(info.st_mode) ||
      info.st_uid != 0 || (info.st_mode & 0002) ||
      info.st_dev != snapshot.device || info.st_ino != snapshot.inode)
    return false;
  struct ucred peer = {};
  socklen_t size = sizeof(peer);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) ||
      size != sizeof(peer))
    return false;
  struct sockaddr_un address = {};
  socklen_t address_size = sizeof(address);
  if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&address),
                  &address_size))
    return false;
  char label[256] = {};
  size = sizeof(label);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &size) || !size ||
      size >= sizeof(label))
    return false;
  return MatchActivatedPeer(peer.pid, peer.uid, address, address_size, label,
                            size, CONSENT_SOCKET_PATH,
                            CONSENT_SERVER_SMACK_LABEL);
#else
  (void)fd;
  (void)snapshot;
#endif
  return true;
}

std::string CacheKey(const Message& message) {
  std::string key;
  for (const auto& field : message) {
    if (field.first == "id" || field.first == "client_request_id" ||
        field.first == "operation_id" || field.first == "deadline_ms")
      continue;
    key += std::to_string(field.first.size()) + ":" + field.first +
           std::to_string(field.second.size()) + ":" + field.second;
  }
  return key;
}
}  // namespace

struct Client::State : public std::enable_shared_from_this<Client::State> {
  struct Operation {
    uint64_t local_id = 0;
    Message message;
    Message result;
    consent_result_cb callback = nullptr;
    void* data = nullptr;
    int status = 0;
    bool asynchronous = false;
    bool done = false;
    bool detached = false;
    bool sent = false;
    bool accepted = false;
    bool inflight = false;
    gint64 admitted = 0;
    gint64 deadline = 0;
    gint64 next_poll = 0;
    std::string remote_id;
  };
  struct Output {
    std::vector<uint8_t> frame;
    size_t offset = 0;
    std::shared_ptr<Operation> operation;
  };
  struct Delivery {
    std::shared_ptr<State> state;
    std::shared_ptr<Operation> operation;
  };
  struct CacheEntry {
    Message result;
    gint64 expires = 0;
    gint64 last_used = 0;
  };

  class CallbackDelivery final {
    friend struct State;
    friend class Client;
    friend class ClientTestPeer;

   public:
    explicit CallbackDelivery(GMainContext* context)
        : pid_(getpid()),
          context_(context ? g_main_context_ref(context)
                           : g_main_context_ref_thread_default()) {}
    ~CallbackDelivery() {
      if (pid_ != getpid()) {
        // Raw source slots have no implicit GLib deleter in a fork child.
        context_ = nullptr;
        return;
      }
      Cancel();
      g_main_context_unref(context_);
    }
    void Cancel() noexcept {
      if (pid_ != getpid())
        return;
      std::map<uint64_t, GSource*> sources;
      sources.swap(sources_);
      for (const auto& entry : sources) {
        g_source_destroy(entry.second);
        g_source_unref(entry.second);
      }
    }

   private:
    pid_t pid_;
    GMainContext* context_;
    std::map<uint64_t, GSource*> sources_;
  };

  class SocketTransport final {
    friend struct State;
    friend class Client;
    friend class ClientTestPeer;

   public:
    SocketTransport() : pid_(getpid()) {}
    ~SocketTransport() {
      if (pid_ != getpid()) {
        // Never unref an inherited GSocket/source. Close our fd copy once;
        // clearing the slot prevents a later deleter from closing a reused fd.
        Abandon();
        return;
      }
      Close();
      if (socket_)
        g_object_unref(socket_);
    }
    bool Adopt(int fd) {
      consent::Descriptor guard(fd);
      GError* error = nullptr;
      socket_ = g_socket_new_from_fd(fd, &error);
      g_clear_error(&error);
      if (!socket_)
        return false;
      fd_ = guard.Release();
      g_socket_set_blocking(socket_, FALSE);
      return true;
    }
    int Connect(IoContext& io) {
      GSocketAddress* address = g_unix_socket_address_new(CONSENT_SOCKET_PATH);
      GError* error = nullptr;
      bool connected = g_socket_connect(socket_, address, nullptr, &error);
      g_object_unref(address);
      bool pending = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PENDING) ||
                     g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
      g_clear_error(&error);
      if (!connected && pending) {
        if (!io.Wait(socket_, 2000))
          return CONSENT_ERROR_TIMEOUT;
        connected = g_socket_check_connect_result(socket_, &error);
        g_clear_error(&error);
      }
      return connected ? 0 : CONSENT_ERROR_DISCONNECTED;
    }
    void ArmRead(GMainContext* context, GSourceFunc callback, void* data) {
      read_ = g_socket_create_source(
          socket_, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
          nullptr);
      g_source_set_callback(read_, callback, data, nullptr);
      g_source_attach(read_, context);
    }
    void ArmWrite(GMainContext* context, GSourceFunc callback, void* data) {
      if (output_.empty()) {
        Destroy(write_);
      } else if (!write_) {
        write_ = g_socket_create_source(
            socket_, static_cast<GIOCondition>(G_IO_OUT | G_IO_ERR | G_IO_HUP),
            nullptr);
        g_source_set_callback(write_, callback, data, nullptr);
        g_source_attach(write_, context);
      }
    }
    gssize Read(void* buffer, size_t size) {
      GError* error = nullptr;
      auto count = g_socket_receive(socket_, static_cast<char*>(buffer), size,
                                    nullptr, &error);
      would_block_ = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
      g_clear_error(&error);
      return count;
    }
    gssize Write(const void* buffer, size_t size) {
      GError* error = nullptr;
      auto count = g_socket_send(socket_, static_cast<const char*>(buffer),
                                 size, nullptr, &error);
      would_block_ = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
      g_clear_error(&error);
      return count;
    }
    void Close() noexcept {
      if (pid_ != getpid())
        return;
      Destroy(read_);
      Destroy(write_);
      if (socket_)
        g_socket_close(socket_, nullptr);
      fd_ = -1;
      wire_.clear();
      output_.clear();
      input_.clear();
      output_bytes_ = 0;
    }

   private:
    void Abandon() noexcept {
      if (fd_ >= 0)
        close(fd_);
      fd_ = -1;
      socket_ = nullptr;
      read_ = nullptr;
      write_ = nullptr;
    }
    static void Destroy(GSource*& source) noexcept {
      if (!source)
        return;
      g_source_destroy(source);
      g_source_unref(source);
      source = nullptr;
    }
    pid_t pid_;
    GSocket* socket_ = nullptr;
    int fd_ = -1;
    GSource* read_ = nullptr;
    GSource* write_ = nullptr;
    bool would_block_ = false;
    std::map<std::string, std::shared_ptr<Operation>> wire_;
    std::deque<Output> output_;
    std::vector<uint8_t> input_;
    size_t output_bytes_ = 0;
    gint64 partial_since_ = 0;
  };

  explicit State(GMainContext* context)
      : owner_(g_thread_self()),
        pid_(getpid()),
        delivery_(context),
        io_([this] { Pump(); }, [this] { transport_.Close(); }) {
    g_mutex_init(&mutex_);
    g_cond_init(&condition_);
  }
  ~State() {
    // Each member owner checks its own PID; returning here alone would not
    // suppress automatic C++ member destructors in a fork child.
    if (pid_ != getpid())
      return;
    io_.Stop();
    io_.Join();
    delivery_.Cancel();
    g_cond_clear(&condition_);
    g_mutex_clear(&mutex_);
    if (counted_)
      ReleaseClient(pid_);
  }

  void Wake() noexcept { io_.Wake(); }

  static gboolean Dispatch(gpointer data) noexcept {
    auto* delivery = static_cast<Delivery*>(data);
    auto state = delivery->state;
    // A child can iterate an inherited GLib context. Never touch inherited
    // mutexes or run callbacks registered in its parent.
    if (state->pid_ != getpid())
      return G_SOURCE_REMOVE;
    auto operation = delivery->operation;
    consent_result_cb callback = nullptr;
    void* user_data = nullptr;
    GSource* source = nullptr;
    {
      Lock lock(state->mutex_);
      auto it = state->delivery_.sources_.find(operation->local_id);
      if (it != state->delivery_.sources_.end()) {
        source = it->second;
        state->delivery_.sources_.erase(it);
      }
      state->operations_.erase(operation->local_id);
      if (!state->closed_ && !operation->detached) {
        callback = operation->callback;
        user_data = operation->data;
      }
      operation->callback = nullptr;
    }
    if (source)
      g_source_unref(source);
    if (callback) {
      try {
        consent_result result;
        result.values.swap(operation->result);
        callback(operation->status, operation->status ? nullptr : &result,
                 user_data);
      } catch (...) {
        // A caller callback must not throw across this C callback boundary.
      }
    }
    return G_SOURCE_REMOVE;
  }

  void Complete(const std::shared_ptr<Operation>& operation, int status,
                Message result = {}) {
    Lock lock(mutex_);
    if (operation->done || operation->detached)
      return;
    operation->status = status;
    operation->result = std::move(result);
    operation->done = true;
    g_cond_broadcast(&condition_);
    if (!operation->asynchronous) {
      operations_.erase(operation->local_id);
      return;
    }
    if (closed_) {
      operations_.erase(operation->local_id);
      return;
    }
    auto source = delivery_.sources_.find(operation->local_id);
    if (source != delivery_.sources_.end())
      g_source_attach(source->second, delivery_.context_);
  }

  int Admit(Message message, unsigned timeout, bool asynchronous,
            consent_result_cb callback, void* data,
            std::shared_ptr<Operation>* output) {
    auto operation = std::make_shared<Operation>();
    operation->message = std::move(message);
    operation->asynchronous = asynchronous;
    operation->callback = callback;
    operation->data = data;
    operation->admitted = g_get_monotonic_time();
    operation->deadline =
        operation->admitted + static_cast<gint64>(timeout) * 1000;
    // Allocate all callback bookkeeping before publishing acceptance. A failed
    // C ABI call must leave neither I/O nor a callback using caller user_data.
    auto release_source = [](GSource* source) {
      if (source) {
        g_source_destroy(source);
        g_source_unref(source);
      }
    };
    std::unique_ptr<GSource, decltype(release_source)> source(nullptr,
                                                              release_source);
    if (asynchronous) {
      auto delivery = std::unique_ptr<Delivery>(
          new Delivery{shared_from_this(), operation});
      source.reset(g_idle_source_new());
      g_source_set_callback(
          source.get(), Dispatch, delivery.release(),
          [](gpointer data) { delete static_cast<Delivery*>(data); });
    }
    Message cached;
    bool cached_hit = false;
    {
      Lock lock(mutex_);
      if (closed_ || disconnected_)
        return CONSENT_ERROR_DISCONNECTED;
      if (operation->message.count("approval_version")) {
        if (Get(operation->message, "approval_version") != "1")
          return CONSENT_ERROR_INVALID_PARAMETER;
        if (!approval_supported_)
          return CONSENT_ERROR_INVALID_OPERATION;
      }
      if (operations_.size() >= kMaxPending)
        return CONSENT_ERROR_BUSY;
      if (Get(operation->message, "method") == "request" && synced_ &&
          !profile_authority_ &&
          !operation->message.count("approval_version")) {
        auto it = cache_.find(CacheKey(operation->message));
        if (it != cache_.end() && it->second.expires > g_get_monotonic_time()) {
          cached = it->second.result;
          it->second.last_used = g_get_monotonic_time();
          cached["source"] = "CACHE";
          cached.erase("request_id");
          cached.erase("id");
        }
      }
      // Resolve a cache hit before publishing it to the I/O snapshot. A wake
      // already in progress must never turn this local result into a request.
      if (!cached.empty()) {
        cached_hit = true;
        operation->result = std::move(cached);
        operation->done = true;
      }
      operation->local_id = next_local_++;
      operations_.emplace(operation->local_id, operation);
      try {
        if (source)
          delivery_.sources_.emplace(operation->local_id, source.get());
      } catch (...) {
        operations_.erase(operation->local_id);
        throw;
      }
      auto* admitted_source = source.release();
      if (operation->done) {
        if (admitted_source)
          g_source_attach(admitted_source, delivery_.context_);
        else
          operations_.erase(operation->local_id);
      }
    }
    *output = operation;
#ifdef CONSENT_CLIENT_OWNER_TEST
    // Unit executable only; independent of endpoint/authentication fixtures.
    if (admission_observer_)
      admission_observer_(admission_observer_data_);
#endif
    if (!cached_hit)
      Wake();
    return 0;
  }

  void Queue(const std::shared_ptr<Operation>& operation) {
    {
      Lock lock(mutex_);
      if (operation->done || operation->detached)
        return;
    }
    Message message = operation->message;
    if (!operation->remote_id.empty()) {
      message.clear();
      message["method"] = "result";
      message["request_id"] = operation->remote_id;
    }
    std::string id = std::to_string(next_wire_++);
    message["v"] = "1";
    message["id"] = id;
    auto frame = Encode(message);
    if (frame.empty()) {
      Complete(operation, CONSENT_ERROR_INVALID_PARAMETER);
      return;
    }
    if (transport_.output_bytes_ + frame.size() > kMaxOutputBytes) {
      Complete(operation, CONSENT_ERROR_BUSY);
      return;
    }
    transport_.wire_[id] = operation;
    transport_.output_bytes_ += frame.size();
    transport_.output_.push_back({std::move(frame), 0, operation});
    operation->inflight = true;
  }

  void Invalidate(const Message& message) {
    Lock lock(mutex_);
    if (Get(message, "profile_authority") == "1")
      profile_authority_ = true;
    cache_.clear();
    epoch_ = Get(message, "epoch");
    revision_ = Number(message, "revision", -1);
    synced_ = !epoch_.empty() && revision_ >= 0;
  }

  bool Receive(Message message) {
    if (Get(message, "v") != "1")
      return false;
    if (Get(message, "method") == "event") {
      Invalidate(message);
      return true;
    }
    auto it = transport_.wire_.find(Get(message, "id"));
    if (it == transport_.wire_.end())
      return true;  // Locally timed-out/detached operations can reply late.
    auto operation = it->second;
    transport_.wire_.erase(it);
    operation->inflight = false;
    int64_t status = CONSENT_ERROR_PROTOCOL;
    if (!ParseNumber(Get(message, "status"), &status) || status > 0 ||
        status < INT_MIN)
      return false;
    {
      Lock lock(mutex_);
      operation->accepted = true;
      if (Get(message, "profile_authority") == "1") {
        profile_authority_ = true;
        cache_.clear();
      }
      std::string epoch = Get(message, "epoch");
      int64_t revision = Number(message, "revision", -1);
      if (epoch.empty() || revision < 0 ||
          (!epoch_.empty() && epoch != epoch_) ||
          (epoch == epoch_ && revision < revision_)) {
        // A late completion cannot roll synchronization back past an event.
        cache_.clear();
        synced_ = false;
      } else {
        if (revision != revision_)
          cache_.clear();
        epoch_ = epoch;
        revision_ = revision;
        synced_ = true;
      }
    }
    if (!status && Get(message, "decision") == "PENDING" &&
        Get(operation->message, "method") == "request") {
      operation->remote_id = Get(message, "request_id");
      if (operation->remote_id.empty())
        return false;
      operation->next_poll = g_get_monotonic_time() + 100000;
      return true;
    }
    if (!status && Get(operation->message, "method") == "request" &&
        !operation->message.count("approval_version") &&
        Get(message, "decision") == "ALLOWED" &&
        !profile_authority_ && Get(message, "cacheable") == "1" &&
        (Get(operation->message, "session").empty() ||
         (Get(message, "session") == Get(operation->message, "session") &&
          !Get(operation->message, "generation").empty() &&
          Get(message, "generation") ==
              Get(operation->message, "generation")))) {
      gint64 ttl = std::min<int64_t>(1000, Number(message, "cache_ttl_ms", 0));
      // The server's remaining lifetime was measured before this reply arrived.
      // Admission precedes that measurement, so it is a conservative anchor
      // even after queueing, fragmented reads or a long user approval wait.
      gint64 expires = operation->admitted + std::max<gint64>(0, ttl) * 1000;
      if (ttl > 0 && expires > g_get_monotonic_time()) {
        Lock lock(mutex_);
        if (synced_) {
          if (cache_.size() >= 64) {
            auto oldest = std::min_element(
                cache_.begin(), cache_.end(),
                [](const auto& left, const auto& right) {
                  return left.second.last_used < right.second.last_used;
                });
            cache_.erase(oldest);
          }
          gint64 now = g_get_monotonic_time();
          if (expires > now)
            cache_[CacheKey(operation->message)] = {message, expires, now};
        }
      }
    }
    if (!message.count("source"))
      message["source"] = "DAEMON";
    Complete(operation, static_cast<int>(status), std::move(message));
    return true;
  }

  void Fail(int error) {
    {
      Lock lock(mutex_);
      disconnected_ = true;
      synced_ = false;
      cache_.clear();
    }
    // No allocation here: this is also the out-of-memory completion path.
    for (;;) {
      std::shared_ptr<Operation> operation;
      int status = error;
      {
        Lock lock(mutex_);
        for (const auto& entry : operations_) {
          if (!entry.second->done && !entry.second->detached) {
            operation = entry.second;
            break;
          }
        }
        if (!operation)
          break;
        if (error == CONSENT_ERROR_DISCONNECTED && operation->sent &&
            !operation->accepted)
          status = CONSENT_ERROR_OUTCOME_UNKNOWN;
      }
      Complete(operation, status);
    }
  }

  bool Read() {
    size_t received = 0;
    uint8_t bytes[8192];
    while (true) {
      gssize count = transport_.Read(bytes, sizeof(bytes));
      if (count == 0)
        return false;
      if (count < 0) {
        return transport_.would_block_;
      }
      received += static_cast<size_t>(count);
      transport_.input_.insert(transport_.input_.end(), bytes, bytes + count);
      while (transport_.input_.size() >= 4) {
        uint32_t size = FrameSize(transport_.input_.data());
        if (!size || size > kMaxFrameSize) {
          read_error_ = CONSENT_ERROR_PROTOCOL;
          return false;
        }
        if (transport_.input_.size() < size + 4)
          break;
        Message message;
        if (!Decode(transport_.input_.data() + 4, size, &message) ||
            !Receive(std::move(message))) {
          read_error_ = CONSENT_ERROR_PROTOCOL;
          return false;
        }
        transport_.input_.erase(transport_.input_.begin(),
                                transport_.input_.begin() + size + 4);
      }
      if (transport_.input_.empty())
        transport_.partial_since_ = 0;
      else if (!transport_.partial_since_)
        transport_.partial_since_ = g_get_monotonic_time();
      // Yield to local deadlines/shutdown even if the peer keeps streaming.
      if (received >= kMaxFrameSize)
        return true;
    }
  }

  bool Write() {
    while (!transport_.output_.empty()) {
      auto& current = transport_.output_.front();
      {
        Lock lock(mutex_);
        if (!current.offset &&
            (current.operation->done || current.operation->detached)) {
          transport_.output_bytes_ -= current.frame.size();
          transport_.output_.pop_front();
          continue;
        }
        // Mark before attempting a nonblocking write: a concurrent local
        // timeout must conservatively report an uncertain remote outcome.
        current.operation->sent = true;
      }
      gssize count = transport_.Write(current.frame.data() + current.offset,
                                      current.frame.size() - current.offset);
      if (count < 0) {
        return transport_.would_block_;
      }
      if (count == 0)
        return false;
      current.offset += count;
      transport_.output_bytes_ -= count;
      if (current.offset == current.frame.size())
        transport_.output_.pop_front();
    }
    return true;
  }

  static gboolean Ready(GSocket*, GIOCondition condition,
                        gpointer data) noexcept {
    auto* self = static_cast<State*>(data);
    if (self->pid_ != getpid())
      return G_SOURCE_REMOVE;
    try {
      if ((condition & G_IO_IN) && !self->Read()) {
        self->Fail(self->read_error_);
        self->io_.Stop();
        return G_SOURCE_REMOVE;
      }
      if ((condition & G_IO_OUT) && !self->Write()) {
        self->Fail(CONSENT_ERROR_DISCONNECTED);
        self->io_.Stop();
        return G_SOURCE_REMOVE;
      }
      if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
        self->Fail(CONSENT_ERROR_DISCONNECTED);
        self->io_.Stop();
        return G_SOURCE_REMOVE;
      }
      self->Pump();
    } catch (...) {
      try {
        self->Fail(CONSENT_ERROR_OUT_OF_MEMORY);
      } catch (...) {
      }
      self->io_.Stop();
      return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
  }

  void Pump() noexcept {
    try {
      std::vector<std::shared_ptr<Operation>> pending;
      {
        Lock lock(mutex_);
        if (closed_ || disconnected_) {
          io_.Stop();
          return;
        }
        for (const auto& entry : operations_) {
          if (!entry.second->done && !entry.second->detached)
            pending.push_back(entry.second);
        }
      }
      gint64 now = g_get_monotonic_time();
      gint64 next = -1;
      for (const auto& operation : pending) {
        if (operation->deadline <= now) {
          Complete(operation, operation->sent && !operation->accepted
                                  ? CONSENT_ERROR_OUTCOME_UNKNOWN
                                  : CONSENT_ERROR_TIMEOUT);
          continue;
        }
        if (!operation->inflight && operation->next_poll <= now)
          Queue(operation);
        auto deadline = operation->deadline;
        if (!operation->inflight)
          deadline = std::min(deadline, operation->next_poll);
        next = next < 0 ? deadline : std::min(next, deadline);
      }
      for (auto it = transport_.wire_.begin(); it != transport_.wire_.end();) {
        bool done;
        {
          Lock lock(mutex_);
          done = it->second->done || it->second->detached;
        }
        if (done)
          it = transport_.wire_.erase(it);
        else
          ++it;
      }
      if (transport_.partial_since_) {
        auto deadline = transport_.partial_since_ + 5000000;
        if (now >= deadline) {
          Fail(CONSENT_ERROR_PROTOCOL);
          io_.Stop();
          return;
        }
        next = next < 0 ? deadline : std::min(next, deadline);
      }
      // Usually the local socket is writable immediately. Avoid allocating a
      // readiness source and another dispatch unless nonblocking I/O stalls.
      if (!Write()) {
        Fail(CONSENT_ERROR_DISCONNECTED);
        io_.Stop();
        return;
      }
      transport_.ArmWrite(io_.Get(), G_SOURCE_FUNC(Ready), this);
      // A single scheduler owns operation, result-poll and framing deadlines.
      // Idle connections leave this source disarmed, without periodic ticks.
      io_.Deadline(next);
    } catch (...) {
      try {
        Fail(CONSENT_ERROR_OUT_OF_MEMORY);
      } catch (...) {
      }
      io_.Stop();
    }
  }

  GThread* owner_;
  pid_t pid_;
  GMutex mutex_;
  GCond condition_;
  bool counted_ = false;
  bool closed_ = false;
  bool disconnected_ = false;
  bool synced_ = false;
  std::atomic<bool> profile_authority_{false};
  bool approval_supported_ = false;
  uint64_t next_local_ = 1;
  uint64_t next_wire_ = 1;
  std::string epoch_;
  int64_t revision_ = -1;
  std::map<uint64_t, std::shared_ptr<Operation>> operations_;
  std::map<std::string, CacheEntry> cache_;
#ifdef CONSENT_CLIENT_OWNER_TEST
  void (*admission_observer_)(void*) noexcept = nullptr;
  void* admission_observer_data_ = nullptr;
#endif
  CallbackDelivery delivery_;
  SocketTransport transport_;
  IoContext io_;
  int read_error_ = CONSENT_ERROR_DISCONNECTED;
};

Client::Client(GMainContext* context)
    : state_(std::make_shared<State>(context)) {}
Client::~Client() {
  Close();
}
bool Client::IsOwner() const {
  return state_->owner_ == g_thread_self();
}
bool Client::IsCurrentProcess() const {
  return state_->pid_ == getpid();
}

int Client::Connect() {
  EndpointSnapshot endpoint;
  if (!TrustedEndpoint(&endpoint))
    return CONSENT_ERROR_PERMISSION_DENIED;
  if (!ReserveClient())
    return CONSENT_ERROR_BUSY;
  state_->counted_ = true;
  int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0 || !state_->transport_.Adopt(fd))
    return CONSENT_ERROR_DISCONNECTED;
  int status = state_->transport_.Connect(state_->io_);
  if (status)
    return status;
  if (!TrustedConnection(state_->transport_.fd_, endpoint))
    return CONSENT_ERROR_PERMISSION_DENIED;
  state_->transport_.ArmRead(state_->io_.Get(), G_SOURCE_FUNC(State::Ready),
                             state_.get());
  if (!state_->io_.Start())
    return CONSENT_ERROR_OUT_OF_MEMORY;
  Message result;
  status = Call({{"method", "hello"}}, 2000, &result);
  if (status == 0) {
    Lock lock(state_->mutex_);
    state_->approval_supported_ = Get(result, "approval_version") == "1";
  }
  return status;
}

int Client::Close() {
  if (!IsCurrentProcess())
    return CONSENT_ERROR_INVALID_PARAMETER;
  std::map<uint64_t, GSource*> sources;
  {
    Lock lock(state_->mutex_);
    if (state_->closed_)
      return 0;
    state_->closed_ = true;
    state_->cache_.clear();
    for (const auto& entry : state_->operations_) {
      entry.second->done = true;
      entry.second->detached = true;
      entry.second->status = CONSENT_ERROR_DISCONNECTED;
      entry.second->callback = nullptr;
    }
    state_->operations_.clear();
    sources.swap(state_->delivery_.sources_);
    g_cond_broadcast(&state_->condition_);
  }
  state_->io_.Stop();
  state_->io_.Join();
  for (const auto& source : sources) {
    g_source_destroy(source.second);
    g_source_unref(source.second);
  }
  return 0;
}

int Client::Call(Message message, unsigned timeout_ms, Message* result) {
  if (!IsCurrentProcess() || !result || !timeout_ms ||
      timeout_ms > kMaximumWaitMs)
    return CONSENT_ERROR_INVALID_PARAMETER;
  if (Get(message, "method") != "hello" &&
      g_main_context_is_owner(state_->delivery_.context_))
    return CONSENT_ERROR_WOULD_DEADLOCK;
  std::shared_ptr<State::Operation> operation;
  int status = state_->Admit(std::move(message), timeout_ms, false, nullptr,
                             nullptr, &operation);
  if (status)
    return status;
  Lock lock(state_->mutex_);
  while (!operation->done) {
    if (!g_cond_wait_until(&state_->condition_, &state_->mutex_,
                           operation->deadline)) {
      if (!operation->done) {
        operation->done = true;
        operation->status = operation->sent && !operation->accepted
                                ? CONSENT_ERROR_OUTCOME_UNKNOWN
                                : CONSENT_ERROR_TIMEOUT;
        state_->operations_.erase(operation->local_id);
      }
      break;
    }
  }
  if (!operation->status)
    *result = operation->result;
  return operation->status;
}

int Client::Submit(Message message, consent_result_cb callback, void* data,
                   uint64_t* operation_id) {
  if (!IsCurrentProcess() || !IsOwner() || !callback || !operation_id)
    return CONSENT_ERROR_INVALID_PARAMETER;
  std::shared_ptr<State::Operation> operation;
  int status = state_->Admit(std::move(message), kMaximumWaitMs, true, callback,
                             data, &operation);
  if (!status)
    *operation_id = operation->local_id;
  return status;
}

int Client::Detach(uint64_t operation_id) {
  if (!IsCurrentProcess() || !IsOwner())
    return CONSENT_ERROR_INVALID_PARAMETER;
  GSource* source = nullptr;
  {
    Lock lock(state_->mutex_);
    auto it = state_->operations_.find(operation_id);
    if (it == state_->operations_.end() || !it->second->asynchronous)
      return CONSENT_ERROR_NOT_FOUND;
    it->second->detached = true;
    it->second->callback = nullptr;
    state_->operations_.erase(it);
    auto posted = state_->delivery_.sources_.find(operation_id);
    if (posted != state_->delivery_.sources_.end()) {
      source = posted->second;
      state_->delivery_.sources_.erase(posted);
    }
  }
  if (source) {
    g_source_destroy(source);
    g_source_unref(source);
  }
  state_->Wake();
  return 0;
}
}  // namespace consent
