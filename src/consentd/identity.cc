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
#include "identity.hh"

#include "common/resource.hh"
#include "key_file.hh"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <glib.h>
#ifndef CONSENT_TEST_BUILD
#include <pkgmgr-info.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

#ifndef CONSENT_INSTALLATIONS
#define CONSENT_INSTALLATIONS "/opt/var/lib/consent-authority/installations.conf"
#endif

namespace {

std::string ReadFd(int fd, size_t limit) {
  std::string result;
  char buffer[4096];
  for (;;) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 || result.size() + static_cast<size_t>(n) > limit)
      return {};
    if (n == 0)
      return result;
    result.append(buffer, static_cast<size_t>(n));
  }
}

std::string ReadProc(pid_t pid, const char* name) {
  std::string path = "/proc/" + std::to_string(pid) + "/" + name;
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return {};
  consent::Descriptor descriptor(fd);
  return ReadFd(fd, 65536);
}

unsigned long long StartTime(pid_t pid) {
  auto stat = ReadProc(pid, "stat");
  auto end = stat.rfind(')');
  if (end == std::string::npos)
    return 0;
  std::istringstream fields(stat.substr(end + 2));
  std::string value;
  for (int field = 3; field <= 22; ++field) {
    if (!(fields >> value))
      return 0;
  }
  char* tail = nullptr;
  errno = 0;
  auto ticks = strtoull(value.c_str(), &tail, 10);
  return errno == 0 && tail && *tail == '\0' ? ticks : 0;
}

bool CheckUid(pid_t pid, uid_t expected) {
  std::istringstream lines(ReadProc(pid, "status"));
  std::string line;
  while (std::getline(lines, line)) {
    if (line.compare(0, 4, "Uid:") != 0)
      continue;
    std::istringstream values(line.substr(4));
    unsigned long real = 0, effective = 0, saved = 0, fs = 0;
    return (values >> real >> effective >> saved >> fs) &&
        real == expected && effective == expected && saved == expected &&
        fs == expected;
  }
  return false;
}

bool ReadKeyFile(const std::string& path, GKeyFile* file, size_t limit = 65536) {
  int fd = consentd::OpenProtected(path);
  if (fd < 0)
    return false;
  consent::Descriptor descriptor(fd);
  auto content = ReadFd(fd, limit);
  return !content.empty() && g_key_file_load_from_data(file, content.data(),
      content.size(), G_KEY_FILE_NONE, nullptr);
}

int ReadOfflineAuthority(GKeyFile* file) {
  const std::string path = CONSENT_INSTALLATIONS;
  const auto separator = path.rfind('/');
  if (separator == std::string::npos || separator == 0)
    return -EINVAL;
  // Only an absent leaf is a normal missing authority. A missing/unprotected
  // ancestor is a setup error. Do not inherit OpenProtected's bool errno.
  int directory = consentd::OpenProtected(path.substr(0, separator), true);
  if (directory < 0)
    return -EACCES;
  consent::Descriptor directory_owner(directory);
  int fd = openat(directory, path.substr(separator + 1).c_str(),
      O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  int saved = errno;
  directory_owner.Reset();
  if (fd < 0)
    return saved == ENOENT ? -ESTALE : -saved;
  consent::Descriptor descriptor(fd);
  struct stat info = {};
  int result = 0;
  if (fstat(fd, &info) < 0)
    result = -errno;
  else if (!S_ISREG(info.st_mode) || info.st_nlink != 1 ||
      info.st_uid != 0 || (info.st_mode & 0022))
    result = -EACCES;
  else if (info.st_size <= 0 || info.st_size > 1048576)
    result = -EINVAL;
  std::string content;
  char buffer[4096];
  while (result == 0) {
    ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0) {
      result = -errno;
      break;
    }
    if (count == 0)
      break;
    if (content.size() + static_cast<size_t>(count) > 1048576) {
      result = -E2BIG;
      break;
    }
    content.append(buffer, static_cast<size_t>(count));
  }
  if (close(descriptor.Release()) < 0 && result == 0) result = -errno;
  if (result != 0)
    return result;
  if (content.empty() || content.find('\0') != std::string::npos ||
      !g_key_file_load_from_data(file, content.data(), content.size(),
                                 G_KEY_FILE_NONE, nullptr) ||
      consentd::KeyValue(file, "authority", "schema") != "1")
    return -EINVAL;
  gsize count = 0;
  std::unique_ptr<gchar*, decltype(&g_strfreev)> groups_owner(
      g_key_file_get_groups(file, &count), g_strfreev);
  auto** groups = groups_owner.get();
  bool valid = count <= 4096;
  for (gsize i = 0; valid && i < count; ++i) {
    const std::string group = groups[i];
    if (group == "authority")
      continue;
    auto generation = consentd::KeyValue(file, groups[i], "generation");
    valid = !generation.empty() && generation.size() <= 128;
    if (group.compare(0, 10, "operation ") == 0) {
      valid = valid && group.size() > 10 &&
              !consentd::KeyValue(file, groups[i], "fingerprint").empty();
      continue;
    }
    auto state = consentd::KeyValue(file, groups[i], "state");
    valid = valid && (state == "active" || state == "pending" || state == "removed");
    if (group.compare(0, 8, "package ") == 0) {
      valid = valid && group.size() > 8 && group.size() <= 263;
    } else {
      auto owner = consentd::KeyValue(file, groups[i], "package");
      valid = valid && !group.empty() && group.size() <= 255 &&
          !owner.empty() && owner.size() <= 255;
    }
  }
  if (!valid)
    return -EINVAL;
  return 0;
}

}  // namespace

namespace consentd {

int OpenProtected(const std::string& path, bool directory) {
  if (path.empty() || path[0] != '/' || path.back() == '/')
    return -1;
  consent::Descriptor current(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  size_t offset = 1;
  while (current.Get() >= 0 && offset < path.size()) {
    size_t end = path.find('/', offset);
    bool last = end == std::string::npos;
    auto part = path.substr(offset, last ? std::string::npos : end - offset);
    if (part.empty() || part == "." || part == "..") {
      return -1;
    }
    int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
    if (!last || directory)
      flags |= O_DIRECTORY;
    int next = openat(current.Get(), part.c_str(), flags);
    current.Reset();
    if (next < 0)
      return -1;
    struct stat st = {};
    bool valid = fstat(next, &st) == 0 && st.st_uid == 0 &&
        (st.st_mode & 0022) == 0;
#ifdef CONSENT_TEST_BUILD
    // Only this sticky ancestor is permitted for the isolated test prefix.
    if (offset == 1 && part == "tmp" && !last && st.st_uid == 0 &&
        S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX))
      valid = true;
#endif
    if (last && !directory)
      valid = valid && S_ISREG(st.st_mode) && st.st_nlink == 1;
    if (!valid) {
      close(next);
      return -1;
    }
    current.Reset(next);
    if (last) return current.Release();
    offset = end + 1;
  }
  return -1;
}

ProcessIdentity::~ProcessIdentity() {
  if (pid_fd >= 0)
    close(pid_fd);
}

bool IdentityPolicy::Load(const std::string& path, std::string* error) {
  rules_.clear();
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file_owner(
      g_key_file_new(), g_key_file_unref);
  auto* file = file_owner.get();
  if (!ReadKeyFile(path, file)) {
    *error = "role configuration is missing, malformed, or not protected";
    return false;
  }
  gsize count = 0;
  std::unique_ptr<gchar*, decltype(&g_strfreev)> groups_owner(
      g_key_file_get_groups(file, &count), g_strfreev);
  auto** groups = groups_owner.get();
  bool valid = true;
  std::set<std::string> names;
  for (gsize i = 0; i < count && valid; ++i) {
    std::string group = groups[i];
    if (group == "policy")
      continue;
    if (group.compare(0, 9, "identity ") != 0 || group.size() == 9) {
      valid = false;
      break;
    }
    Rule rule;
    rule.peer.identity = group.substr(9);
    auto uid = consentd::KeyValue(file, groups[i], "uid");
    char* end = nullptr;
    errno = 0;
    auto number = strtoul(uid.c_str(), &end, 10);
    if (uid.empty() || uid[0] == '-' || errno || !end || *end ||
        number > std::numeric_limits<uid_t>::max()) {
      valid = false;
      break;
    }
    rule.peer.uid = static_cast<uid_t>(number);
    rule.executable = consentd::KeyValue(file, groups[i], "executable");
    rule.label = consentd::KeyValue(file, groups[i], "label");
    rule.peer.roles = consentd::KeyValues(file, groups[i], "roles");
    rule.peer.subjects = consentd::KeyValues(file, groups[i], "subjects");
    rule.peer.profiles = consentd::KeyValues(file, groups[i], "profiles");
    rule.peer.enforcers = consentd::KeyValues(file, groups[i], "enforcers");
    rule.peer.packages = consentd::KeyValues(file, groups[i], "packages");
    int fd = OpenProtected(rule.executable);
    consent::Descriptor descriptor(fd);
    struct stat st = {};
    if (fd < 0 || fstat(fd, &st) < 0 || !(st.st_mode & 0111) ||
        rule.label.empty() || rule.peer.roles.empty() ||
        !names.insert(rule.peer.identity).second ||
        rule.peer.subjects.count("*") || rule.peer.profiles.count("*")) {
      valid = false;
    } else {
      rule.device = st.st_dev;
      rule.inode = st.st_ino;
      rules_.push_back(std::move(rule));
    }
  }
  if (!valid) {
    rules_.clear();
    *error = "invalid identity rule; denying all roles";
  }
  return valid;
}

bool IdentityPolicy::Authenticate(int fd, Peer* peer, ProcessIdentity* process,
                                  std::string* error) const {
  struct ucred credential = {};
  socklen_t length = sizeof(credential);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credential, &length) < 0 ||
      length != sizeof(credential) || credential.pid <= 0) {
    *error = "peer credential unavailable";
    return false;
  }
  peer->pid = credential.pid;
  peer->uid = credential.uid;
  peer->gid = credential.gid;
  char label[4096] = {};
  length = sizeof(label) - 1;
  if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &length) < 0 ||
      length == 0 || length >= sizeof(label)) {
    *error = "kernel peer security label unavailable";
    return false;
  }
  process->label.assign(label, strnlen(label, length));
  process->start_time = StartTime(peer->pid);
  if (!process->start_time || !CheckUid(peer->pid, peer->uid)) {
    *error = "peer process identity unavailable";
    return false;
  }
#ifdef SYS_pidfd_open
  process->pid_fd = static_cast<int>(syscall(SYS_pidfd_open, peer->pid, 0));
#endif
  std::string proc_exe = "/proc/" + std::to_string(peer->pid) + "/exe";
  char executable[4096] = {};
  ssize_t size = readlink(proc_exe.c_str(), executable, sizeof(executable) - 1);
  if (size <= 0 || size == static_cast<ssize_t>(sizeof(executable) - 1)) {
    *error = "peer executable unavailable";
    return false;
  }
  process->executable.assign(executable, static_cast<size_t>(size));
  int exe = open(proc_exe.c_str(), O_RDONLY | O_CLOEXEC);
  struct stat st = {};
  bool valid = exe >= 0 && fstat(exe, &st) == 0 && st.st_uid == 0 &&
      !(st.st_mode & 0022) && S_ISREG(st.st_mode) && st.st_nlink == 1;
  if (exe >= 0)
    close(exe);
  if (!valid) {
    *error = "peer executable not protected";
    return false;
  }
  process->device = st.st_dev;
  process->inode = st.st_ino;
  for (const auto& rule : rules_) {
    if (rule.peer.uid != peer->uid || rule.executable != process->executable ||
        rule.label != process->label || rule.device != st.st_dev ||
        rule.inode != st.st_ino)
      continue;
    pid_t pid = peer->pid;
    gid_t gid = peer->gid;
    *peer = rule.peer;
    peer->pid = pid;
    peer->gid = gid;
    peer->instance = std::to_string(pid) + ":" +
        std::to_string(process->start_time);
    if (IsAlive(*peer, *process))
      return true;
    break;
  }
  *error = "no matching live trusted identity";
  return false;
}

bool IdentityPolicy::IsAlive(const Peer& peer,
                             const ProcessIdentity& process) const {
  if (process.pid_fd >= 0) {
    struct pollfd fd = {process.pid_fd, POLLIN, 0};
    if (poll(&fd, 1, 0) != 0)
      return false;
  }
  if (!process.start_time || StartTime(peer.pid) != process.start_time ||
      !CheckUid(peer.pid, peer.uid))
    return false;
  std::string path = "/proc/" + std::to_string(peer.pid) + "/exe";
  struct stat st = {};
  if (stat(path.c_str(), &st) < 0 || st.st_dev != process.device ||
      st.st_ino != process.inode || st.st_uid != 0 || (st.st_mode & 0022) ||
      st.st_nlink != 1)
    return false;
  int current = OpenProtected(process.executable);
  bool valid = current >= 0 && fstat(current, &st) == 0 &&
      st.st_dev == process.device && st.st_ino == process.inode;
  if (current >= 0)
    close(current);
  return valid && StartTime(peer.pid) == process.start_time;
}

bool IdentityPolicy::Allows(const Peer& peer, const std::string& method) {
  auto role = [&peer](const char* value) { return peer.roles.count(value) != 0; };
  if (method == "hello")
    return !peer.roles.empty();
  if (method == "register" || method == "update" || method == "unregister")
    return role("installer");
  if (method == "request" || method == "result" || method == "cancel" ||
      method == "get_request_result" || method == "cancel_request")
    return role("argo");
  if (method == "check")
    return role("checker") || role("cm") || role("ce") || role("holder");
  if (method == "prompt" || method == "get_prompt" || method == "respond")
    return role("ui");
  if (method == "revoke")
    return role("admin");
  if (method == "session_get_state" || method == "session_state" ||
      method == "cleanup_get_state" || method == "cleanup")
    return role("session") || role("holder") || role("argo") || role("checker");
  if (method.compare(0, 8, "session_") == 0)
    return role("session");
  if (method.compare(0, 5, "data_") == 0 || method == "cleanup" ||
      method == "cleanup_list" || method == "cleanup_ack")
    return role("holder") || role("session");
  return false;
}

bool GetInstallationIdentity(const std::string& package, const std::string& app,
                             std::string* identity) {
  if (package.empty() || app.empty() || package.size() > 255 || app.size() > 255)
    return false;
#ifndef CONSENT_TEST_BUILD
  pkgmgrinfo_appinfo_h handle = nullptr;
  if (pkgmgrinfo_appinfo_get_appinfo(app.c_str(), &handle) != 0)
    return false;
  char* actual = nullptr;
  bool valid = pkgmgrinfo_appinfo_get_pkgid(handle, &actual) == 0 &&
      actual && package == actual;
  pkgmgrinfo_appinfo_destroy_appinfo(handle);
  if (!valid)
    return false;
#endif
  // A signed identity or timestamp is not an installation generation. Require
  // a protected Installer-published generation, independent of the registry.
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> inventory_owner(
      g_key_file_new(), g_key_file_unref);
  auto* inventory = inventory_owner.get();
  if (!ReadKeyFile(CONSENT_INSTALLATIONS, inventory, 1048576)) {
    return false;
  }
  auto actual_package = consentd::KeyValue(inventory, app.c_str(), "package");
  auto generation = consentd::KeyValue(inventory, app.c_str(), "generation");
  auto state = consentd::KeyValue(inventory, app.c_str(), "state");
  auto package_group = "package " + package;
  bool active = consentd::KeyValue(inventory, package_group.c_str(), "state") ==
                    "active" &&
                consentd::KeyValue(inventory, package_group.c_str(),
                                   "generation") == generation &&
                consentd::KeyValue(inventory, "authority", "schema") == "1";
  if (!active || state != "active" || actual_package != package ||
      generation.empty() || generation.size() > 128)
    return false;
  *identity = generation;
  return true;
}

bool ValidateInstallation(const std::string& package, const std::string& app,
                          const std::string& identity) {
  std::string current;
  return GetInstallationIdentity(package, app, &current) && current == identity;
}

bool ValidatePackageGeneration(const std::string& package,
                               const std::string& generation) {
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> inventory_owner(
      g_key_file_new(), g_key_file_unref);
  auto* inventory = inventory_owner.get();
  if (!ReadKeyFile(CONSENT_INSTALLATIONS, inventory, 1048576)) {
    return false;
  }
  std::string group = "package " + package;
  auto state = consentd::KeyValue(inventory, group.c_str(), "state");
  bool valid = !generation.empty() &&
               consentd::KeyValue(inventory, "authority", "schema") == "1" &&
               consentd::KeyValue(inventory, group.c_str(), "generation") ==
                   generation &&
               (state == "active" || state == "pending" || state == "removed");
  return valid;
}

int CheckOfflineAuthority() {
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> inventory(
      g_key_file_new(), g_key_file_unref);
  return ReadOfflineAuthority(inventory.get());
}

int ValidateOfflineInstallation(const std::string& package,
    const std::string& app, const std::string& generation) {
  if (package.empty() || app.empty() || package.size() > 255 || app.size() > 255 ||
      generation.empty() || generation.size() > 128)
    return -EINVAL;
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> inventory(
      g_key_file_new(), g_key_file_unref);
  int result = ReadOfflineAuthority(inventory.get());
  if (result != 0)
    return result;
  auto* file = inventory.get();
  auto group = "package " + package;
  for (const auto& name : {group, app}) {
    if (!g_key_file_has_group(file, name.c_str()))
      return -ESTALE;
    auto state = consentd::KeyValue(file, name.c_str(), "state");
    auto current = consentd::KeyValue(file, name.c_str(), "generation");
    if ((state != "active" && state != "pending" && state != "removed") ||
        current.empty() || current.size() > 128)
      return -EINVAL;
  }
  auto owner = consentd::KeyValue(file, app.c_str(), "package");
  if (owner.empty() || owner.size() > 255)
    return -EINVAL;
  if (consentd::KeyValue(file, group.c_str(), "state") != "active" ||
      consentd::KeyValue(file, app.c_str(), "state") != "active" ||
      owner != package ||
      consentd::KeyValue(file, group.c_str(), "generation") != generation ||
      consentd::KeyValue(file, app.c_str(), "generation") != generation)
    return -ESTALE;
#ifndef CONSENT_TEST_BUILD
  pkgmgrinfo_appinfo_h handle = nullptr;
  if (pkgmgrinfo_appinfo_get_appinfo(app.c_str(), &handle) != 0)
    return -ESTALE;
  char* actual = nullptr;
  bool valid = pkgmgrinfo_appinfo_get_pkgid(handle, &actual) == 0 &&
      actual && package == actual;
  pkgmgrinfo_appinfo_destroy_appinfo(handle);
  if (!valid)
    return -ESTALE;
#endif
  return 0;
}

}  // namespace consentd
