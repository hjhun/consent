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

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>

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
// PID and count update together, including concurrent first creation after fork.
std::atomic<uint64_t> client_budget{0};

bool ReserveClient() {
  const uint64_t process = static_cast<uint64_t>(getpid()) << 32;
  uint64_t observed = client_budget.load();
  for (;;) {
    unsigned count = (observed & 0xffffffff00000000ULL) == process ?
        static_cast<unsigned>(observed) : 0;
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
    if (lstat(parent.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != 0 ||
        (info.st_mode & 0002))
      return false;
    if (info.st_mode & 0020) {
      // Tizen /run is shared with system services. This exception grants no
      // trust to an arbitrary socket: connected kernel identity below is
      // mandatory, including its original bound pathname and SMACK label.
      struct group group = {};
      struct group* resolved = nullptr;
      char buffer[4096];
      if (parent != "/run" || endpoint != "/run/.consentd.sock" ||
          getgrnam_r("system_share", &group, buffer, sizeof(buffer), &resolved) ||
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
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || size != sizeof(peer))
    return false;
  struct sockaddr_un address = {};
  socklen_t address_size = sizeof(address);
  if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&address), &address_size))
    return false;
  char label[256] = {};
  size = sizeof(label);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &size) ||
      !size || size >= sizeof(label))
    return false;
  return MatchActivatedPeer(peer.pid, peer.uid, address, address_size,
      label, size, CONSENT_SOCKET_PATH, CONSENT_SERVER_SMACK_LABEL);
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

  explicit State(GMainContext* context)
      : context_(context ? g_main_context_ref(context) :
          g_main_context_ref_thread_default()), owner_(g_thread_self()), pid_(getpid()) {
    g_mutex_init(&mutex_);
    g_cond_init(&condition_);
  }
  ~State() {
    // Source destroy-notify may run in a fork child without a live public
    // handle. Close only our inherited fd copies; never clear inherited GLib
    // contexts, conditions or mutexes there.
    if (pid_ != getpid()) {
      if (socket_ >= 0)
        close(socket_);
      if (wake_ >= 0)
        close(wake_);
      return;
    }
    if (socket_ >= 0)
      close(socket_);
    if (wake_ >= 0)
      close(wake_);
    g_main_context_unref(context_);
    g_cond_clear(&condition_);
    g_mutex_clear(&mutex_);
    if (counted_)
      ReleaseClient(pid_);
  }

  void Wake() {
    uint64_t one = 1;
    if (wake_ >= 0) {
      ssize_t ignored = write(wake_, &one, sizeof(one));
      (void)ignored;
    }
  }

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
      auto it = state->sources_.find(operation->local_id);
      if (it != state->sources_.end()) {
        source = it->second;
        state->sources_.erase(it);
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
        callback(operation->status, operation->status ? nullptr : &result, user_data);
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
    auto source = sources_.find(operation->local_id);
    if (source != sources_.end())
      g_source_attach(source->second, context_);
  }

  int Admit(Message message, unsigned timeout, bool asynchronous,
      consent_result_cb callback, void* data, std::shared_ptr<Operation>* output) {
    auto operation = std::make_shared<Operation>();
    operation->message = std::move(message);
    operation->asynchronous = asynchronous;
    operation->callback = callback;
    operation->data = data;
    operation->admitted = g_get_monotonic_time();
    operation->deadline = operation->admitted + static_cast<gint64>(timeout) * 1000;
    // Allocate all callback bookkeeping before publishing acceptance. A failed
    // C ABI call must leave neither I/O nor a callback using caller user_data.
    auto release_source = [](GSource* source) {
      if (source) {
        g_source_destroy(source);
        g_source_unref(source);
      }
    };
    std::unique_ptr<GSource, decltype(release_source)> source(nullptr, release_source);
    if (asynchronous) {
      auto delivery = std::unique_ptr<Delivery>(new Delivery{shared_from_this(), operation});
      source.reset(g_idle_source_new());
      g_source_set_callback(source.get(), Dispatch, delivery.release(), [](gpointer data) {
        delete static_cast<Delivery*>(data);
      });
    }
    Message cached;
    {
      Lock lock(mutex_);
      if (closed_ || disconnected_)
        return CONSENT_ERROR_DISCONNECTED;
      if (operations_.size() >= kMaxPending)
        return CONSENT_ERROR_BUSY;
      if (Get(operation->message, "method") == "request" && synced_) {
        auto it = cache_.find(CacheKey(operation->message));
        if (it != cache_.end() && it->second.expires > g_get_monotonic_time()) {
          cached = it->second.result;
          it->second.last_used = g_get_monotonic_time();
          cached["source"] = "CACHE";
          cached.erase("request_id");
          cached.erase("id");
        }
      }
      operation->local_id = next_local_++;
      operations_.emplace(operation->local_id, operation);
      try {
        if (source)
          sources_.emplace(operation->local_id, source.get());
      } catch (...) {
        operations_.erase(operation->local_id);
        throw;
      }
      source.release();
    }
    *output = operation;
    if (!cached.empty())
      Complete(operation, 0, std::move(cached));
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
    if (output_bytes_ + frame.size() > kMaxOutputBytes) {
      Complete(operation, CONSENT_ERROR_BUSY);
      return;
    }
    wire_[id] = operation;
    output_bytes_ += frame.size();
    output_.push_back({std::move(frame), 0, operation});
    operation->inflight = true;
  }

  void Invalidate(const Message& message) {
    Lock lock(mutex_);
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
    auto it = wire_.find(Get(message, "id"));
    if (it == wire_.end())
      return true;  // Locally timed-out/detached operations can reply late.
    auto operation = it->second;
    wire_.erase(it);
    operation->inflight = false;
    int64_t status = CONSENT_ERROR_PROTOCOL;
    if (!ParseNumber(Get(message, "status"), &status) || status > 0 || status < INT_MIN)
      return false;
    {
      Lock lock(mutex_);
      operation->accepted = true;
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
        Get(message, "decision") == "ALLOWED" && Get(message, "cacheable") == "1" &&
        (Get(operation->message, "session").empty() ||
         (Get(message, "session") == Get(operation->message, "session") &&
          !Get(operation->message, "generation").empty() &&
          Get(message, "generation") == Get(operation->message, "generation")))) {
      gint64 ttl = std::min<int64_t>(1000, Number(message, "cache_ttl_ms", 0));
      // The server's remaining lifetime was measured before this reply arrived.
      // Admission precedes that measurement, so it is a conservative anchor
      // even after queueing, fragmented reads or a long user approval wait.
      gint64 expires = operation->admitted + std::max<gint64>(0, ttl) * 1000;
      if (ttl > 0 && expires > g_get_monotonic_time()) {
        Lock lock(mutex_);
        if (synced_) {
          if (cache_.size() >= 64) {
            auto oldest = std::min_element(cache_.begin(), cache_.end(),
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
        if (error == CONSENT_ERROR_DISCONNECTED && operation->sent && !operation->accepted)
          status = CONSENT_ERROR_OUTCOME_UNKNOWN;
      }
      Complete(operation, status);
    }
  }

  bool Read() {
    size_t received = 0;
    uint8_t bytes[8192];
    while (true) {
      ssize_t count = recv(socket_, bytes, sizeof(bytes), 0);
      if (count == 0)
        return false;
      if (count < 0) {
        if (errno == EINTR)
          continue;
        return errno == EAGAIN || errno == EWOULDBLOCK;
      }
      received += static_cast<size_t>(count);
      input_.insert(input_.end(), bytes, bytes + count);
      while (input_.size() >= 4) {
        uint32_t size = FrameSize(input_.data());
        if (!size || size > kMaxFrameSize) {
          read_error_ = CONSENT_ERROR_PROTOCOL;
          return false;
        }
        if (input_.size() < size + 4)
          break;
        Message message;
        if (!Decode(input_.data() + 4, size, &message) || !Receive(std::move(message))) {
          read_error_ = CONSENT_ERROR_PROTOCOL;
          return false;
        }
        input_.erase(input_.begin(), input_.begin() + size + 4);
      }
      if (input_.empty())
        partial_since_ = 0;
      else if (!partial_since_)
        partial_since_ = g_get_monotonic_time();
      // Yield to local deadlines/shutdown even if the peer keeps streaming.
      if (received >= kMaxFrameSize)
        return true;
    }
  }

  bool Write() {
    while (!output_.empty()) {
      auto& current = output_.front();
      {
        Lock lock(mutex_);
        if (!current.offset && (current.operation->done || current.operation->detached)) {
          output_bytes_ -= current.frame.size();
          output_.pop_front();
          continue;
        }
        // Mark before attempting a nonblocking write: a concurrent local
        // timeout must conservatively report an uncertain remote outcome.
        current.operation->sent = true;
      }
      ssize_t count = send(socket_, current.frame.data() + current.offset,
          current.frame.size() - current.offset, MSG_NOSIGNAL);
      if (count < 0) {
        if (errno == EINTR)
          continue;
        return errno == EAGAIN || errno == EWOULDBLOCK;
      }
      if (count == 0)
        return false;
      current.offset += count;
      output_bytes_ -= count;
      if (current.offset == current.frame.size())
        output_.pop_front();
    }
    return true;
  }

  void Run() noexcept {
    try {
      for (;;) {
        std::vector<std::shared_ptr<Operation>> pending;
        {
          Lock lock(mutex_);
          if (closed_)
            break;
          for (const auto& entry : operations_) {
            if (!entry.second->done && !entry.second->detached)
              pending.push_back(entry.second);
          }
        }
        gint64 now = g_get_monotonic_time();
        for (const auto& operation : pending) {
          if (operation->deadline <= now)
            Complete(operation, operation->sent && !operation->accepted ?
                CONSENT_ERROR_OUTCOME_UNKNOWN : CONSENT_ERROR_TIMEOUT);
          else if (!operation->inflight && operation->next_poll <= now)
            Queue(operation);
        }
        for (auto it = wire_.begin(); it != wire_.end();) {
          bool done;
          {
            Lock lock(mutex_);
            done = it->second->done || it->second->detached;
          }
          if (done)
            it = wire_.erase(it);
          else
            ++it;
        }
        if (partial_since_ && now - partial_since_ > 5000000) {
          Fail(CONSENT_ERROR_PROTOCOL);
          break;
        }
        struct pollfd fds[2] = {{socket_, static_cast<short>(POLLIN |
            (output_.empty() ? 0 : POLLOUT)), 0}, {wake_, POLLIN, 0}};
        int result = poll(fds, 2, 50);
        if (result < 0 && errno == EINTR)
          continue;
        if (result < 0) {
          Fail(CONSENT_ERROR_DISCONNECTED);
          break;
        }
        if (fds[1].revents & POLLIN) {
          uint64_t value;
          ssize_t ignored = read(wake_, &value, sizeof(value));
          (void)ignored;
        }
        if ((fds[0].revents & POLLIN) && !Read()) {
          Fail(read_error_);
          break;
        }
        if ((fds[0].revents & POLLOUT) && !Write()) {
          Fail(CONSENT_ERROR_DISCONNECTED);
          break;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
          Fail(CONSENT_ERROR_DISCONNECTED);
          break;
        }
      }
    } catch (...) {
      try { Fail(CONSENT_ERROR_OUT_OF_MEMORY); } catch (...) {}
    }
    shutdown(socket_, SHUT_RDWR);  // Accepted client socket, never listener.
  }

  GMainContext* context_;
  GThread* owner_;
  pid_t pid_;
  GMutex mutex_;
  GCond condition_;
  GThread* thread_ = nullptr;
  int socket_ = -1;
  int wake_ = -1;
  bool counted_ = false;
  bool closed_ = false;
  bool disconnected_ = false;
  bool synced_ = false;
  uint64_t next_local_ = 1;
  uint64_t next_wire_ = 1;
  std::string epoch_;
  int64_t revision_ = -1;
  std::map<uint64_t, std::shared_ptr<Operation>> operations_;
  std::map<uint64_t, GSource*> sources_;
  std::map<std::string, CacheEntry> cache_;
  // Below is exclusively owned by the I/O thread.
  std::map<std::string, std::shared_ptr<Operation>> wire_;
  std::deque<Output> output_;
  std::vector<uint8_t> input_;
  size_t output_bytes_ = 0;
  gint64 partial_since_ = 0;
  int read_error_ = CONSENT_ERROR_DISCONNECTED;
};

Client::Client(GMainContext* context) : state_(std::make_shared<State>(context)) {}
Client::~Client() { Close(); }
bool Client::IsOwner() const { return state_->owner_ == g_thread_self(); }
bool Client::IsCurrentProcess() const { return state_->pid_ == getpid(); }

int Client::Connect() {
  EndpointSnapshot endpoint;
  if (!TrustedEndpoint(&endpoint))
    return CONSENT_ERROR_PERMISSION_DENIED;
  if (!ReserveClient())
    return CONSENT_ERROR_BUSY;
  state_->counted_ = true;
  state_->socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (state_->socket_ < 0)
    return CONSENT_ERROR_DISCONNECTED;
  struct sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  if (sizeof(CONSENT_SOCKET_PATH) > sizeof(address.sun_path))
    return CONSENT_ERROR_INVALID_PARAMETER;
  memcpy(address.sun_path, CONSENT_SOCKET_PATH, sizeof(CONSENT_SOCKET_PATH));
  int ret = connect(state_->socket_, reinterpret_cast<struct sockaddr*>(&address),
      sizeof(address));
  if (ret && errno == EINPROGRESS) {
    struct pollfd descriptor = {state_->socket_, POLLOUT, 0};
    if (poll(&descriptor, 1, 2000) <= 0)
      return CONSENT_ERROR_TIMEOUT;
    int error = 0;
    socklen_t size = sizeof(error);
    if (getsockopt(state_->socket_, SOL_SOCKET, SO_ERROR, &error, &size) || error)
      return CONSENT_ERROR_DISCONNECTED;
  } else if (ret) {
    return CONSENT_ERROR_DISCONNECTED;
  }
  if (!TrustedConnection(state_->socket_, endpoint))
    return CONSENT_ERROR_PERMISSION_DENIED;
  state_->wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
  if (state_->wake_ < 0)
    return CONSENT_ERROR_DISCONNECTED;
  GError* error = nullptr;
  state_->thread_ = g_thread_try_new("consent-io", [](gpointer data) -> gpointer {
    static_cast<State*>(data)->Run();
    return nullptr;
  }, state_.get(), &error);
  if (!state_->thread_) {
    g_clear_error(&error);
    return CONSENT_ERROR_OUT_OF_MEMORY;
  }
  Message result;
  return Call({{"method", "hello"}}, 2000, &result);
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
    sources.swap(state_->sources_);
    g_cond_broadcast(&state_->condition_);
  }
  for (const auto& source : sources) {
    g_source_destroy(source.second);
    g_source_unref(source.second);
  }
  state_->Wake();
  if (state_->thread_) {
    g_thread_join(state_->thread_);
    state_->thread_ = nullptr;
  }
  return 0;
}

int Client::Call(Message message, unsigned timeout_ms, Message* result) {
  if (!IsCurrentProcess() || !result || !timeout_ms || timeout_ms > kMaximumWaitMs)
    return CONSENT_ERROR_INVALID_PARAMETER;
  if (Get(message, "method") != "hello" && g_main_context_is_owner(state_->context_))
    return CONSENT_ERROR_WOULD_DEADLOCK;
  std::shared_ptr<State::Operation> operation;
  int status = state_->Admit(std::move(message), timeout_ms, false, nullptr,
      nullptr, &operation);
  if (status)
    return status;
  Lock lock(state_->mutex_);
  while (!operation->done) {
    if (!g_cond_wait_until(&state_->condition_, &state_->mutex_, operation->deadline)) {
      if (!operation->done) {
        operation->done = true;
        operation->status = operation->sent && !operation->accepted ?
            CONSENT_ERROR_OUTCOME_UNKNOWN : CONSENT_ERROR_TIMEOUT;
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
    auto posted = state_->sources_.find(operation_id);
    if (posted != state_->sources_.end()) {
      source = posted->second;
      state_->sources_.erase(posted);
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
