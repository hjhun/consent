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
#include <systemd/sd-daemon.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
constexpr char kEndpoint[] = "/opt/var/lib/consent-poc-runtime/consent.sock";
constexpr int kWaitMs = 20000;

class Descriptor final {
 public:
  explicit Descriptor(int value) : value_(value) {}
  ~Descriptor() { if (value_ >= 0) close(value_); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int Get() const { return value_; }
 private:
  int value_;
};

int Error(const char* stage, int status) {
  std::fprintf(stderr, "event=poc-peer-probe-error stage=%s status=%d\n", stage, status);
  return 1;
}

std::string Escape(const char* bytes, size_t size) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  for (size_t index = 0; index < size; ++index) {
    unsigned char byte = bytes[index];
    if (byte == '"' || byte == '\\') {
      result += '\\';
      result += static_cast<char>(byte);
    } else if (byte < 0x20 || byte >= 0x7f) {
      // Kernel bytes are diagnostic evidence, not assumed UTF-8 text.
      result += "\\u00";
      result += hex[byte >> 4];
      result += hex[byte & 15];
    } else {
      result += static_cast<char>(byte);
    }
  }
  return result;
}
}  // namespace

int main() {
  // sd_listen_fds validates LISTEN_PID/LISTEN_FDS and marks inherited FDs
  // CLOEXEC. No independent socket creation, bind or activation fallback exists.
  int count = sd_listen_fds(1);
  if (count != 1)
    return Error("activation-count", count);
  Descriptor listener(SD_LISTEN_FDS_START);
  int valid = sd_is_socket_unix(listener.Get(), SOCK_STREAM, 1, kEndpoint, 0);
  if (valid <= 0)
    return Error("activation-endpoint", valid);
  int flags = fcntl(listener.Get(), F_GETFL);
  if (flags < 0 || fcntl(listener.Get(), F_SETFL, flags | O_NONBLOCK) < 0)
    return Error("listener-nonblocking", -errno);
  std::printf("{\"event\":\"poc-peer-probe-ready\",\"fd\":3,\"endpoint\":\"%s\",\"wait_ms\":%d}\n",
      kEndpoint, kWaitMs);
  if (std::fflush(stdout) != 0)
    return Error("ready-output", -errno);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kWaitMs);
  int accepted = -1;
  while (accepted < 0) {
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0)
      return Error("accept-timeout", -ETIMEDOUT);
    struct pollfd event = {listener.Get(), POLLIN, 0};
    int status = poll(&event, 1, static_cast<int>(remaining));
    if (status < 0 && errno == EINTR)
      continue;
    if (status < 0)
      return Error("listener-poll", -errno);
    if (status == 0)
      return Error("accept-timeout", -ETIMEDOUT);
    if (event.revents & (POLLERR | POLLHUP | POLLNVAL))
      return Error("listener-state", -EIO);
    if (!(event.revents & POLLIN))
      continue;
    accepted = accept4(listener.Get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (accepted < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR &&
        errno != ECONNABORTED)
      return Error("accept", -errno);
  }
  Descriptor connection(accepted);
  struct ucred credentials = {};
  socklen_t credential_size = sizeof(credentials);
  if (getsockopt(connection.Get(), SOL_SOCKET, SO_PEERCRED, &credentials, &credential_size) < 0)
    return Error("peer-credentials", -errno);
  if (credential_size != sizeof(credentials) || credentials.pid <= 0)
    return Error("peer-credentials-size", -EINVAL);

  char label[1024] = {};
  socklen_t label_size = sizeof(label);
  if (getsockopt(connection.Get(), SOL_SOCKET, SO_PEERSEC, label, &label_size) < 0)
    return Error("peer-security", -errno);
  if (!label_size || label_size > sizeof(label))
    return Error("peer-security-size", -EINVAL);
  if (label[label_size - 1] == '\0')
    --label_size;
  if (!label_size || std::memchr(label, '\0', label_size))
    return Error("peer-security-label", -EINVAL);
  const auto escaped = Escape(label, label_size);
  std::printf("{\"event\":\"poc-peer-probe\",\"pid\":%ld,\"uid\":%lu,\"gid\":%lu,"
      "\"label\":\"%s\",\"protocol_response\":false}\n",
      static_cast<long>(credentials.pid), static_cast<unsigned long>(credentials.uid),
      static_cast<unsigned long>(credentials.gid), escaped.c_str());
  if (std::fflush(stdout) != 0)
    return Error("peer-output", -errno);
  // Deliberately close without reading an application frame or returning a
  // consent response. An actual UI client must fail against this probe.
  return 0;
}
