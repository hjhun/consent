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
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef CONSENT_STATE_DIR
#define CONSENT_STATE_DIR "/opt/var/lib/consentd"
#endif

namespace {
bool Identifier(const std::string& text) {
  if (text.empty() || text.size() > 255)
    return false;
  for (unsigned char value : text) {
    if (!g_ascii_isalnum(value) && value != '.' && value != '_' && value != '-')
      return false;
  }
  return true;
}

std::string Get(GKeyFile* file, const std::string& group, const char* key) {
  gchar* raw = g_key_file_get_string(file, group.c_str(), key, nullptr);
  std::string result = raw ? raw : "";
  g_free(raw);
  return result;
}

void Set(GKeyFile* file, const std::string& group, const char* key,
         const std::string& value) {
  g_key_file_set_string(file, group.c_str(), key, value.c_str());
}

int OpenDirectory() {
  std::string path = CONSENT_STATE_DIR;
  int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  size_t offset = 1;
  while (fd >= 0 && offset < path.size()) {
    auto end = path.find('/', offset);
    auto component = path.substr(offset, end == std::string::npos ? end : end - offset);
    int next = openat(fd, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    close(fd);
    if (next < 0)
      return -1;
    struct stat st = {};
    bool protected_dir = fstat(next, &st) == 0 && st.st_uid == 0 && !(st.st_mode & 0022);
#ifdef CONSENT_TEST_BUILD
    if (offset == 1 && component == "tmp" && st.st_uid == 0 && (st.st_mode & S_ISVTX))
      protected_dir = true;
#endif
    if (!protected_dir) {
      close(next);
      return -1;
    }
    fd = next;
    if (end == std::string::npos)
      break;
    offset = end + 1;
  }
  return fd;
}

bool Persist(int directory, GKeyFile* file) {
  gsize size = 0;
  gchar* data = g_key_file_to_data(file, &size, nullptr);
  if (!data || size > 1048576) {
    g_free(data);
    return false;
  }
  gchar* uuid = g_uuid_string_random();
  std::string temporary = std::string(".installations-") + uuid;
  g_free(uuid);
  int fd = openat(directory, temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY |
      O_NOFOLLOW | O_CLOEXEC, 0600);
  bool ok = fd >= 0;
  size_t written = 0;
  while (ok && written < size) {
    ssize_t count = write(fd, data + written, size - written);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      ok = false;
      break;
    }
    written += static_cast<size_t>(count);
  }
  g_free(data);
  if (ok)
    ok = fsync(fd) == 0;
  if (fd >= 0 && close(fd) < 0)
    ok = false;
  if (ok)
    ok = renameat(directory, temporary.c_str(), directory, "installations.conf") == 0;
  if (ok)
    ok = fsync(directory) == 0;
  if (!ok)
    unlinkat(directory, temporary.c_str(), 0);
  return ok;
}

bool Apply(GKeyFile* file, const std::string& command,
           const std::string& package, const std::string& app,
           const std::string& operation, const std::string& expected,
           std::string* result) {
  std::string key = "package " + package;
  std::string operation_key = "operation " + operation;
  std::string fingerprint = command + "|" + package + "|" + app + "|" + expected;
  auto previous = Get(file, operation_key, "fingerprint");
  if (!previous.empty()) {
    if (previous != fingerprint)
      return false;
    *result = Get(file, operation_key, "generation");
    return true;
  }
  gsize group_count = 0;
  gchar** groups = g_key_file_get_groups(file, &group_count);
  if (group_count >= 4096) {
    g_strfreev(groups);
    return false;
  }
  auto current = Get(file, key, "generation");
  auto state = Get(file, key, "state");
  bool ok = current == expected || (current.empty() && expected == "absent");
  if (command == "begin" && ok) {
    gchar* uuid = g_uuid_string_random();
    *result = uuid;
    g_free(uuid);
    Set(file, key, "generation", *result);
    Set(file, key, "state", "pending");
    for (gsize i = 0; i < group_count; ++i) {
      if (Get(file, groups[i], "package") == package)
        Set(file, groups[i], "state", "removed");
    }
  } else if (command == "attach" && ok && state == "pending") {
    auto owner = Get(file, app, "package");
    if (!owner.empty() && owner != package) {
      ok = false;
    } else {
      Set(file, app, "package", package);
      Set(file, app, "generation", current);
      Set(file, app, "state", "pending");
      *result = current;
    }
  } else if (command == "commit" && ok && state == "pending") {
    size_t apps = 0;
    for (gsize i = 0; i < group_count; ++i) {
      if (Get(file, groups[i], "package") == package &&
          Get(file, groups[i], "generation") == current &&
          Get(file, groups[i], "state") == "pending") {
        Set(file, groups[i], "state", "active");
        ++apps;
      }
    }
    ok = apps != 0;
    if (ok) {
      Set(file, key, "state", "active");
      *result = current;
    }
  } else if (command == "remove" && ok && !current.empty()) {
    Set(file, key, "state", "removed");
    for (gsize i = 0; i < group_count; ++i) {
      if (Get(file, groups[i], "package") == package)
        Set(file, groups[i], "state", "removed");
    }
    *result = current;
  } else {
    ok = false;
  }
  g_strfreev(groups);
  if (ok) {
    Set(file, "authority", "schema", "1");
    Set(file, operation_key, "fingerprint", fingerprint);
    Set(file, operation_key, "generation", *result);
  }
  return ok;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 5 || argc > 6 || geteuid() != 0) {
    fprintf(stderr, "Usage (root): %s begin|commit|remove PACKAGE OPERATION EXPECTED_GENERATION\n"
        "              %s attach PACKAGE APP OPERATION GENERATION\n", argv[0], argv[0]);
    return 2;
  }
  std::string command = argv[1], package = argv[2];
  bool attach = command == "attach";
  if ((attach && argc != 6) || (!attach && argc != 5))
    return 2;
  std::string app = attach ? argv[3] : "";
  std::string operation = argv[attach ? 4 : 3];
  std::string expected = argv[attach ? 5 : 4];
  if (!Identifier(package) || (attach && !Identifier(app)) ||
      !Identifier(operation) || !Identifier(expected))
    return 2;
  int directory = OpenDirectory();
  if (directory < 0) {
    fprintf(stderr, "Protected state directory unavailable\n");
    return 1;
  }
  int lock = openat(directory, "installations.lock", O_CREAT | O_RDWR |
      O_CLOEXEC | O_NOFOLLOW, 0600);
  struct stat st = {};
  if (lock < 0 || fstat(lock, &st) < 0 || st.st_uid != 0 ||
      (st.st_mode & 0077) || st.st_nlink != 1 || flock(lock, LOCK_EX | LOCK_NB) < 0) {
    if (lock >= 0)
      close(lock);
    close(directory);
    return 1;
  }
  GKeyFile* file = g_key_file_new();
  int fd = openat(directory, "installations.conf", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  bool ok = true;
  if (fd >= 0) {
    ok = fstat(fd, &st) == 0 && st.st_uid == 0 && !(st.st_mode & 0077) &&
        st.st_nlink == 1 && S_ISREG(st.st_mode) && st.st_size <= 1048576;
    std::string content;
    char bytes[4096];
    ssize_t count;
    while (ok && (count = read(fd, bytes, sizeof(bytes))) != 0) {
      if (count < 0) {
        if (errno == EINTR)
          continue;
        ok = false;
        break;
      }
      content.append(bytes, static_cast<size_t>(count));
      ok = content.size() <= 1048576;
    }
    close(fd);
    ok = ok && g_key_file_load_from_data(file, content.data(), content.size(),
        G_KEY_FILE_NONE, nullptr) && Get(file, "authority", "schema") == "1";
  } else {
    ok = errno == ENOENT;
  }
  std::string result;
  ok = ok && Apply(file, command, package, app, operation, expected, &result);
  ok = ok && Persist(directory, file);
  g_key_file_unref(file);
  close(lock);
  close(directory);
  if (!ok) {
    fprintf(stderr, "Authority update failed or outcome uncertain; keep package fenced and retry the SAME operation\n");
    return 1;
  }
  printf("%s\n", result.c_str());
  return 0;
}
