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
#include "common/message.hh"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef CONSENT_SOCKET_PATH
#error This manual fixture requires an explicit isolated socket at compile time
#endif

namespace {
std::string baseline_epoch;

void Require(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

class Connection final {
 public:
  Connection() {
    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    Require(fd_ >= 0, "socket");
    struct sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    static_assert(sizeof(CONSENT_SOCKET_PATH) <= sizeof(address.sun_path), "socket path");
    memcpy(address.sun_path, CONSENT_SOCKET_PATH, sizeof(CONSENT_SOCKET_PATH));
    if (connect(fd_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address))) {
      close(fd_);
      throw std::runtime_error("connect isolated consentd");
    }
  }
  ~Connection() { close(fd_); }
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  bool Send(const uint8_t* bytes, size_t size, unsigned timeout = 3000,
      size_t* transmitted = nullptr) {
    if (transmitted)
      *transmitted = 0;
    gint64 deadline = g_get_monotonic_time() + timeout * 1000LL;
    while (size) {
      ssize_t count = send(fd_, bytes, size, MSG_NOSIGNAL);
      if (count > 0) {
        if (transmitted)
          *transmitted += static_cast<size_t>(count);
        size -= count;
        bytes += count;
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!Wait(POLLOUT, deadline))
          return false;
      } else {
        return false;
      }
    }
    return true;
  }
  bool Send(const std::vector<uint8_t>& frame, unsigned timeout = 3000,
      size_t* transmitted = nullptr) {
    return Send(frame.data(), frame.size(), timeout, transmitted);
  }
  bool Reply(uint64_t id, unsigned timeout = 3000) {
    gint64 deadline = g_get_monotonic_time() + timeout * 1000LL;
    for (;;) {
      uint8_t header[4];
      if (!Read(header, sizeof(header), deadline))
        return false;
      uint32_t size = consent::FrameSize(header);
      Require(size > 0 && size <= consent::kMaxFrameSize, "invalid reply size");
      std::vector<uint8_t> body(size);
      if (!Read(body.data(), body.size(), deadline))
        return false;
      consent::Message response;
      Require(consent::Decode(body.data(), body.size(), &response), "invalid native Parcel reply");
      if (consent::Get(response, "method") == "event")
        continue;
      Require(consent::Get(response, "method") == "reply", "not a reply");
      Require(consent::Number(response, "id") == static_cast<int64_t>(id), "reply correlation");
      Require(consent::Number(response, "status", -1) == 0, "hello rejected");
      Require(!consent::Get(response, "epoch").empty(), "hello epoch missing");
      if (baseline_epoch.empty())
        baseline_epoch = consent::Get(response, "epoch");
      Require(consent::Get(response, "epoch") == baseline_epoch,
          "daemon generation changed during wire fixture");
      return true;
    }
  }
  bool Hello(uint64_t id = 1) {
    return Send(Frame(id)) && Reply(id);
  }
  bool Closed(unsigned timeout = 3000) {
    gint64 deadline = g_get_monotonic_time() + timeout * 1000LL;
    uint8_t bytes[8192];
    size_t drained = 0;
    while (g_get_monotonic_time() < deadline) {
      ssize_t count = recv(fd_, bytes, sizeof(bytes), 0);
      if (!count || (count < 0 && (errno == ECONNRESET || errno == ENOTCONN)))
        return true;
      if (count > 0) {
        drained += count;
        Require(drained < 4 * 1024 * 1024, "unbounded output while waiting for close");
      } else if (errno == EINTR) {
        continue;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        return true;
      } else if (!Wait(POLLIN, deadline)) {
        return false;
      }
    }
    return false;
  }
  void FinishWrite() { Require(shutdown(fd_, SHUT_WR) == 0, "shutdown writer"); }
  static std::vector<uint8_t> Frame(uint64_t id) {
    return consent::Encode({{"v", "1"}, {"id", std::to_string(id)}, {"method", "hello"}});
  }

 private:
  bool Wait(short events, gint64 deadline) {
    for (;;) {
      gint64 remaining = deadline - g_get_monotonic_time();
      if (remaining <= 0)
        return false;
      struct pollfd descriptor{fd_, events, 0};
      int result = poll(&descriptor, 1, static_cast<int>((remaining + 999) / 1000));
      if (result > 0)
        return true;
      if (!result || errno != EINTR)
        return false;
    }
  }
  bool Read(uint8_t* data, size_t size, gint64 deadline) {
    while (size) {
      ssize_t count = recv(fd_, data, size, 0);
      if (count > 0) {
        size -= count;
        data += count;
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        if (!Wait(POLLIN, deadline))
          return false;
      } else {
        return false;
      }
    }
    return true;
  }
  int fd_ = -1;
};

void Healthy() {
  // Let the daemon account for the previous connection's EOF before checking.
  g_usleep(20000);
  Connection connection;
  Require(connection.Hello(), "daemon unavailable after malformed input");
}

void Rejected(const char* name, const std::vector<uint8_t>& frame,
    bool finish = false) {
  Connection connection;
  Require(connection.Hello(), "initial authenticated hello");
  Require(connection.Send(frame), "write malformed fixture");
  if (finish)
    connection.FinishWrite();
  Require(connection.Closed(), name);
  Healthy();
  printf("PASS wire malformed %s closes connection; daemon remains responsive\n", name);
}

void Framing() {
  Connection connection;
  auto frame = Connection::Frame(1);
  // Separate writes and delays ensure both incomplete-header and body states.
  for (size_t offset = 0; offset < frame.size();) {
    size_t length = offset < 4 ? 1 : std::min<size_t>(3, frame.size() - offset);
    Require(connection.Send(frame.data() + offset, length), "fragment write");
    offset += length;
    g_usleep(10000);
  }
  Require(connection.Reply(1), "fragmented hello reply");
  std::vector<uint8_t> coalesced;
  for (uint64_t id = 2; id <= 17; ++id) {
    frame = Connection::Frame(id);
    coalesced.insert(coalesced.end(), frame.begin(), frame.end());
  }
  Require(connection.Send(coalesced), "coalesced write");
  for (uint64_t id = 2; id <= 17; ++id)
    Require(connection.Reply(id), "coalesced reply");
  puts("PASS wire authenticated fragmented header/body and 16 coalesced native Parcel frames");
}

void Malformed() {
  Rejected("zero-size", {0, 0, 0, 0});
  Rejected("oversize-65537", {0, 1, 0, 1});
  auto frame = Connection::Frame(2);
  // A hello with no field data claims the maximum array count. The generated
  // reader must reject remaining/min-wire before constructing Field[256].
  frame[frame.size() - 2] = 1;
  Rejected("count-256-with-no-elements", frame);
  frame = Connection::Frame(2);
  frame[20] = 0x7f;  // First byte of native method string's u32 length.
  Rejected("unbounded-string-length", frame);
  frame = Connection::Frame(2);
  frame.push_back(0);
  ++frame[3];
  Rejected("trailing-byte", frame);
  frame = Connection::Frame(2);
  frame.resize(frame.size() / 2);
  Rejected("middle-body-eof", frame, true);
  Rejected("partial-header-eof", {0, 0}, true);
  Connection slow;
  Require(slow.Hello(), "slow hello");
  Require(slow.Send(std::vector<uint8_t>{0}), "slow header");
  Require(slow.Closed(8000), "partial frame completion deadline");
  Healthy();
  puts("PASS wire incomplete frame deadline and EOF cleanup");
}

void ConnectionLimit() {
  std::vector<std::unique_ptr<Connection>> held;
  unsigned rejected = 0;
  for (unsigned i = 0; i < 28; ++i) {
    auto connection = std::make_unique<Connection>();
    if (connection->Hello())
      held.push_back(std::move(connection));
    else {
      Require(connection->Closed(), "over-limit client not closed");
      ++rejected;
    }
  }
  Require(!held.empty() && held.size() <= 24 && rejected > 0, "checker UID connection bound");
  for (const auto& connection : held)
    Require(connection->Hello(2), "admitted connection lost at UID bound");
  printf("PASS wire UID bound: accepted=%zu rejected=%u (checker limit 24)\n",
      held.size(), rejected);
  held.clear();
  Healthy();
}

void SlowReader() {
  Connection connection;
  Require(connection.Hello(), "slow-reader initial hello");
  Connection monitor;
  Require(monitor.Hello(), "pressure monitor initial hello");
  // No reads while requests are processed. The server's output cap or bounded
  // write deadline must retire this one consumer without starving new clients.
  size_t transmitted = 0;
  bool sent = true;
  gint64 deadline = g_get_monotonic_time() + 4000000;
  for (uint64_t id = 2; id < 8194; ++id) {
    auto frame = Connection::Frame(id);
    gint64 remaining = deadline - g_get_monotonic_time();
    if (remaining <= 0) {
      sent = false;
      break;
    }
    size_t count = 0;
    sent = connection.Send(frame, static_cast<unsigned>((remaining + 999) / 1000), &count);
    transmitted += count;
    if (!sent)
      break;
  }
  // Correlation is a fixed-width native u64, so every hello has equal size.
  // A partial final frame could trigger the *input* deadline, making bounded
  // close insufficient evidence of output pressure. Reject that ambiguity.
  const size_t frame_size = Connection::Frame(1).size();
  Require(transmitted >= 256 * frame_size && transmitted % frame_size == 0,
      "pressure write did not end on a complete frame boundary");
  for (uint64_t id = 2; id <= 14; ++id) {
    Require(monitor.Hello(id), "live monitor starved or generation changed under pressure");
    g_usleep(500000);
  }
  Require(connection.Closed(), "slow consumer output bound/deadline");
  Healthy();
  printf("PASS wire slow-consumer bounded close: all-input-sent=%d bytes=%zu frames=%zu; "
      "live monitor remained responsive (verify output-limit/write-timeout in daemon log)\n",
      sent, transmitted, transmitted / frame_size);
}
}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  try {
    Framing();
    Malformed();
    ConnectionLimit();
    SlowReader();
    Healthy();
    puts("PASS wire scenario: isolated authenticated daemon survives malformed IPC and pressure");
    return 0;
  } catch (const std::exception& error) {
    fprintf(stderr, "FAIL wire scenario: %s (errno=%d)\n", error.what(), errno);
    return 1;
  }
}
