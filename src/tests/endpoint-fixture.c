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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>

#ifndef CONSENT_FIXTURE_SOCKET_PATH
#define CONSENT_FIXTURE_SOCKET_PATH "/run/.consentd.sock"
#endif

/* Development-emulator fixture. Never removes an existing socket. */
int main(int argc, char** argv) {
  const char* path = CONSENT_FIXTURE_SOCKET_PATH;
  struct sockaddr_un address = { .sun_family = AF_UNIX };
  int fd;
  if (argc != 2) return 2;
  if (!strcmp(argv[1], "--activated")) {
    fd = 3;
  } else {
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    strcpy(address.sun_path, path);
    socklen_t size = offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;
    if (!strcmp(argv[1], "--probe")) {
      struct ucred peer;
      char label[256] = {0};
      socklen_t length = sizeof(peer);
      if (connect(fd, (struct sockaddr*)&address, size) ||
          getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length)) return 1;
      printf("peer pid=%d uid=%u gid=%u length=%u\n", peer.pid, peer.uid, peer.gid, length);
      length = sizeof(label);
      if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &length)) return 1;
      printf("security length=%u label=%.*s\n", length, length, label);
      length = sizeof(address);
      if (getpeername(fd, (struct sockaddr*)&address, &length)) return 1;
      printf("address family=%u length=%u path=%s\n", address.sun_family, length, address.sun_path);
      close(fd);
      return 0;
    }
    if (strcmp(argv[1], "--direct") || bind(fd, (struct sockaddr*)&address, size) ||
        chmod(path, 0660) || listen(fd, 4)) return 1;
  }
  struct pollfd ready = { .fd = fd, .events = POLLIN };
  if (poll(&ready, 1, 10000) != 1) return 1;
  int client = accept(fd, NULL, NULL);
  if (client < 0) return 1;
  ready.fd = client;
  if (poll(&ready, 1, 5000) != 1) return 1;
  char byte;
  ssize_t count = read(client, &byte, 1);
  close(client);
  close(fd);
  if (!strcmp(argv[1], "--direct")) unlink(path);
  if (count != 0) {
    fprintf(stderr, "FAIL unauthenticated server received hello bytes=%zd\n", count);
    return 1;
  }
  puts("PASS untrusted endpoint rejected before hello");
  return 0;
}
