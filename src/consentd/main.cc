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

#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>

#include <exception>

#ifndef CONSENT_SOCKET_PATH
#define CONSENT_SOCKET_PATH "/run/.consentd.sock"
#endif

int main() {
  // Activation is the only listener path, including the separately built test
  // daemon. Do not unlink/bind/shutdown a listener owned by systemd.
  const int count = sd_listen_fds(1);
  if (count != 1) {
    for (int i = 0; i < count; ++i)
      close(SD_LISTEN_FDS_START + i);
    g_warning("event=activation-invalid count=%d", count);
    return 1;
  }
  int fd = SD_LISTEN_FDS_START;
  if (sd_is_socket_unix(fd, SOCK_STREAM, 1, CONSENT_SOCKET_PATH, 0) <= 0) {
    close(fd);
    g_warning("event=activation-invalid endpoint=%s", CONSENT_SOCKET_PATH);
    return 1;
  }
  try {
    consentd::Server server;
    return server.Run(fd);
  } catch (const std::exception& error) {
    g_warning("event=startup-failed reason=%s", error.what());
  } catch (...) {
    g_warning("event=startup-failed reason=unknown");
  }
  return 1;
}
