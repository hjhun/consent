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
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// Linked ONLY into consentd-shutdown-test. No production flag, environment
// variable or authentication exception enables this gate in either daemon.
constexpr char kRoot[] = "/tmp/consent-test";
constexpr char kReady[] = "/tmp/consent-test/shutdown-db-ready";
constexpr char kRelease[] = "/tmp/consent-test/shutdown-db-release";
bool revoke_started = false;
bool gate_used = false;

int64_t MonotonicMs() {
  timespec now = {};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    return -1;
  return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

bool Protected(const struct stat& file) {
  return file.st_uid == geteuid() && (file.st_mode & 0077) == 0;
}

bool Gate() {
  struct stat root = {};
  if (lstat(kRoot, &root) != 0 || !S_ISDIR(root.st_mode) || !Protected(root))
    return false;
  int release = open(kRelease, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
  struct stat pipe = {};
  if (release < 0 || fstat(release, &pipe) != 0 || !S_ISFIFO(pipe.st_mode) ||
      !Protected(pipe) || pipe.st_nlink != 1) {
    if (release >= 0)
      close(release);
    return false;
  }
  int ready = open(kReady, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (ready < 0) {
    close(release);
    return false;
  }
  char marker[96];
  int size = snprintf(marker, sizeof(marker), "pid=%ld state=before-commit\n", static_cast<long>(getpid()));
  int written = 0;
  while (written < size) {
    ssize_t count = write(ready, marker + written, size - written);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      break;
    written += count;
  }
  bool persisted = written == size && fsync(ready) == 0;
  close(ready);
  int64_t start = MonotonicMs();
  bool released = false;
  while (persisted && start >= 0) {
    int64_t now = MonotonicMs();
    int64_t remaining = 5000 - (now - start);
    if (now < 0 || remaining <= 0)
      break;
    pollfd wait{release, POLLIN, 0};
    int count = poll(&wait, 1, static_cast<int>(remaining));
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0 || !(wait.revents & POLLIN))
      break;
    char command = 0;
    ssize_t read_count;
    do {
      read_count = read(release, &command, 1);
    } while (read_count < 0 && errno == EINTR);
    released = read_count == 1 && command == 'C';
    break;
  }
  close(release);
  return released;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int sqlite3_step(sqlite3_stmt* statement) {
  using Step = int (*)(sqlite3_stmt*);
  static Step real_step = reinterpret_cast<Step>(dlsym(RTLD_NEXT, "sqlite3_step"));
  if (!real_step)
    _exit(99);
  int result = real_step(statement);
  const char* sql = sqlite3_sql(statement);
  if (!gate_used && result == SQLITE_DONE && sqlite3_changes(sqlite3_db_handle(statement)) > 0 && sql &&
      std::strstr(sql, "UPDATE grants SET revoked=1 WHERE definition=? AND subject=?") == sql)
    revoke_started = true;
  return result;
}

extern "C" __attribute__((visibility("default"))) int sqlite3_exec(sqlite3* database, const char* sql,
    int (*callback)(void*, int, char**, char**), void* data, char** error) {
  using Exec = int (*)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
  static Exec real_exec = reinterpret_cast<Exec>(dlsym(RTLD_NEXT, "sqlite3_exec"));
  if (!real_exec)
    _exit(99);
  if (revoke_started && !gate_used && std::strcmp(sql, "COMMIT") == 0) {
    gate_used = true;
    if (sqlite3_get_autocommit(database) != 0 || !Gate()) {
      if (error)
        *error = sqlite3_mprintf("shutdown fixture gate failed or timed out");
      return SQLITE_IOERR;
    }
  }
  return real_exec(database, sql, callback, data, error);
}
