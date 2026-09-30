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

#include "tool-process.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "tool-json.h"

extern char** environ;
static volatile sig_atomic_t interrupted;

static void on_signal(int signal_number) {
  (void)signal_number;
  interrupted = 1;
}

void tool_install_signal_handlers(void) {
  struct sigaction action = {0};
  action.sa_handler = on_signal;
  sigemptyset(&action.sa_mask);
  smoke_expect(
      !sigaction(SIGTERM, &action, NULL) && !sigaction(SIGINT, &action, NULL),
      "tool signal handlers");
}

int tool_interrupted(void) { return interrupted; }

int tool_open(const char* path, int executable) {
  smoke_expect(path[0] == '/', "absolute fixture path");
  char** parts = g_strsplit(path + 1, "/", -1);
  int parent = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  smoke_expect(parent >= 0, "root directory");
  unsigned count = g_strv_length(parts);
  for (unsigned i = 0; i + 1 < count; ++i) {
    smoke_expect(*parts[i] && strcmp(parts[i], ".") && strcmp(parts[i], ".."),
                 "canonical fixture path");
    int next = openat(parent, parts[i],
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    smoke_expect(
        next >= 0 && !fstat(next, &st) && st.st_uid == 0 &&
            (!(st.st_mode & 0022) ||
             (i == 0 && !strcmp(parts[i], "tmp") && (st.st_mode & S_ISVTX))),
        "protected root-owned ancestor");
    close(parent);
    parent = next;
  }
  int fd = openat(parent, parts[count - 1], O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  struct stat st;
  smoke_expect(fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) &&
                   st.st_uid == 0 && st.st_nlink == 1 && !(st.st_mode & 0022) &&
                   (!executable || (st.st_mode & 0111)),
               "root-owned regular fixture inode");
  close(parent);
  g_strfreev(parts);
  return fd;
}

static int drain(int* fd, GString* output) {
  char data[1024];
  for (;;) {
    ssize_t size = read(*fd, data, sizeof(data));
    if (size > 0) {
      if (output->len + size > TOOL_LIMIT) return 0;
      g_string_append_len(output, data, size);
      continue;
    }
    if (!size) {
      close(*fd);
      *fd = -1;
      return 1;
    }
    if (errno == EINTR) continue;
    return errno == EAGAIN || errno == EWOULDBLOCK;
  }
}

int tool_execute(int executable_fd, const char* request, const char* id) {
  if (interrupted) return 0;
  int out[2] = {-1, -1}, err[2] = {-1, -1};
  int pinned = fcntl(executable_fd, F_DUPFD_CLOEXEC, 10);
  smoke_expect(pinned >= 10 && !pipe2(out, O_CLOEXEC) && !pipe2(err, O_CLOEXEC),
               "provider pipes");
  posix_spawn_file_actions_t actions;
  smoke_expect(!posix_spawn_file_actions_init(&actions), "spawn actions");
  smoke_expect(!posix_spawn_file_actions_adddup2(&actions, pinned, 3) &&
                   !posix_spawn_file_actions_adddup2(&actions, out[1], 1) &&
                   !posix_spawn_file_actions_adddup2(&actions, err[1], 2),
               "provider descriptor actions");
  char* argv[] = {"consent-smoke-tool", "--json", (char*)request, NULL};
  pid_t pid = -1;
  int status =
      posix_spawn(&pid, "/proc/self/fd/3", &actions, NULL, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pinned);
  close(out[1]);
  close(err[1]);
  if (status) {
    close(out[0]);
    close(err[0]);
    printf("PROVIDER spawn_failed=%d\n", status);
    return 0;
  }
  smoke_expect(fcntl(out[0], F_SETFL, O_NONBLOCK) == 0 &&
                   fcntl(err[0], F_SETFL, O_NONBLOCK) == 0,
               "provider nonblocking pipes");
  GString* stdout_data = g_string_new(NULL);
  GString* stderr_data = g_string_new(NULL);
  gint64 deadline = g_get_monotonic_time() + 1500000;
  int healthy = 1;
  int reaped = 0;
  int child_status = 0;
  while (healthy && (!reaped || out[0] >= 0 || err[0] >= 0)) {
    if (interrupted || g_get_monotonic_time() >= deadline) {
      healthy = 0;
      break;
    }
    struct pollfd descriptors[] = {{out[0], POLLIN, 0}, {err[0], POLLIN, 0}};
    int ready = poll(descriptors, 2, 25);
    if (ready < 0 && errno != EINTR) healthy = 0;
    if (out[0] >= 0 && !drain(&out[0], stdout_data)) healthy = 0;
    if (err[0] >= 0 && !drain(&err[0], stderr_data)) healthy = 0;
    if (!reaped) {
      pid_t waited = waitpid(pid, &child_status, WNOHANG);
      if (waited == pid) reaped = 1;
      if (waited < 0 && errno != EINTR) healthy = 0;
    }
  }
  if (!reaped) {
    kill(pid, SIGKILL);
    pid_t waited;
    do {
      waited = waitpid(pid, &child_status, 0);
    } while (waited < 0 && errno == EINTR);
    smoke_expect(waited == pid, "provider final wait");
  }
  /* Drain available bytes after kill; never wait for an inherited grandchild.
   */
  if (out[0] >= 0) {
    drain(&out[0], stdout_data);
    if (out[0] >= 0) close(out[0]);
  }
  if (err[0] >= 0) {
    drain(&err[0], stderr_data);
    if (err[0] >= 0) close(err[0]);
  }
  printf("PROVIDER pid=%ld healthy=%d wait_status=%d stdout=%zu stderr=%zu\n",
         (long)pid, healthy, child_status, stdout_data->len, stderr_data->len);
  /* Fixture payload only; streams are captured independently. */
  printf("PROVIDER_STDERR %s\n", stderr_data->str);
  printf("PROVIDER_STDOUT %s\n", stdout_data->str);
  int result = 0;
  if (healthy && !memchr(stdout_data->str, 0, stdout_data->len) &&
      !memchr(stderr_data->str, 0, stderr_data->len)) {
    char* out_text = g_strstrip(stdout_data->str);
    char* err_text = g_strstrip(stderr_data->str);
    int out_result = *out_text ? tool_validate_response(out_text, id) : 0;
    int err_result = *err_text ? tool_validate_response(err_text, id) : 0;
    if (out_result && !*err_text) result = out_result;
    if (err_result && !*out_text) result = err_result;
    if (out_result && err_result) {
      JsonParser* out_parser = tool_parse(out_text);
      JsonParser* err_parser = tool_parse(err_text);
      if (json_node_equal(json_parser_get_root(out_parser),
                          json_parser_get_root(err_parser)))
        result = out_result;
      g_object_unref(out_parser);
      g_object_unref(err_parser);
    }
  }
  /* A valid native response and process exit/signal are distinct observations.
   */
  g_string_free(stdout_data, TRUE);
  g_string_free(stderr_data, TRUE);
  return result;
}
