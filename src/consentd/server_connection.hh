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
#ifndef CONSENTD_SERVER_CONNECTION_HH_
#define CONSENTD_SERVER_CONNECTION_HH_

#include "server.hh"

#include <deque>
#include <vector>

namespace consentd {

struct Server::Connection {
  Server* server = nullptr;
  uint64_t id = 0;
  GSocketConnection* stream = nullptr;
  GSocket* socket =
      nullptr;  // Borrowed from stream, only I/O owner accesses it.
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
  struct Frame {
    std::vector<uint8_t> bytes;
    std::string profile_generation;
  };
  std::deque<Frame> output;
  size_t output_bytes = 0;
  size_t offset = 0;
  gint64 write_started = 0;
  ~Connection();
};

}  // namespace consentd
#endif  // CONSENTD_SERVER_CONNECTION_HH_
