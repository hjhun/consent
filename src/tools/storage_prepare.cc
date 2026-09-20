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
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CONSENT_STATE_DIR
#define CONSENT_STATE_DIR "/opt/var/lib/consentd"
#endif
#ifndef CONSENT_AUTHORITY_DIR
#define CONSENT_AUTHORITY_DIR "/opt/var/lib/consent-authority"
#endif
#ifndef CONSENT_SERVICE_USER
#define CONSENT_SERVICE_USER "security"
#endif
#ifndef CONSENT_SERVICE_UNIT
#define CONSENT_SERVICE_UNIT "consentd.service"
#endif

namespace {
class Descriptor final {
 public:
  explicit Descriptor(int fd = -1) : fd_(fd) {}
  ~Descriptor() { if (fd_ >= 0) close(fd_); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  int Get() const { return fd_; }
 private:
  int fd_;
};

void Require(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

void Label(int fd) {
#ifdef CONSENT_STORAGE_PREPARE_TEST
  // The private fixture can run on an SDK filesystem without SMACK. Production
  // never treats missing xattr support as successful label provisioning.
  (void)fd;
#else
  Require(fsetxattr(fd, "security.SMACK64", "System", 6, 0) == 0,
      "cannot establish the required System SMACK label");
#endif
}

struct Entry {
  std::string name;
  Descriptor fd;
  struct stat identity;
};

Descriptor Directory(const std::string& path, uid_t permitted_owner, bool create,
    gid_t group, mode_t mode) {
  Require(!path.empty() && path[0] == '/' && path.back() != '/', "invalid absolute directory");
  int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  Require(current >= 0, "cannot open filesystem root");
  size_t offset = 1;
  while (offset < path.size()) {
    Descriptor parent(current);
    auto end = path.find('/', offset);
    bool last = end == std::string::npos;
    auto name = path.substr(offset, last ? std::string::npos : end - offset);
    Require(!name.empty() && name != "." && name != "..", "invalid directory component");
    bool created = false;
    int next = openat(parent.Get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT && create && last) {
      Require(mkdirat(parent.Get(), name.c_str(), 0700) == 0, "cannot create protected directory");
      created = true;
      next = openat(parent.Get(), name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }
    Require(next >= 0, "cannot open protected directory component");
    Descriptor opened(next);
    struct stat st = {};
    Require(fstat(next, &st) == 0 && S_ISDIR(st.st_mode), "directory type rejected");
    bool valid = (st.st_uid == 0 || (last && st.st_uid == permitted_owner)) && !(st.st_mode & 0022);
#ifdef CONSENT_STORAGE_PREPARE_TEST
    // Only fixture builds allow the root-owned sticky /tmp ancestor.
    if (offset == 1 && name == "tmp" && !last && st.st_uid == 0 && (st.st_mode & S_ISVTX))
      valid = true;
#endif
    Require(valid, "directory ownership or protection rejected");
    if (created) {
      Require(fchown(next, 0, group) == 0 && fchmod(next, mode) == 0,
          "new directory ownership failed");
      Label(next);
    }
    if (last) {
      // A previous mkdir may have succeeded before parent fsync failed. Retry
      // must perform the same barrier even when the directory already exists.
      Require(fsync(next) == 0 && fsync(parent.Get()) == 0,
          "protected directory durability failed");
      return opened;
    }
    current = dup(next);
    Require(current >= 0, "cannot retain directory descriptor");
    offset = end + 1;
  }
  throw std::runtime_error("missing final directory component");
}

Entry File(int directory, const std::string& name, uid_t selected, mode_t forbidden,
    bool optional = false) {
  int fd = openat(directory, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (optional && fd < 0 && errno == ENOENT)
    return {name, Descriptor(), {}};
  Require(fd >= 0, "cannot open protected file");
  Entry entry{name, Descriptor(fd), {}};
  Require(fstat(fd, &entry.identity) == 0 && S_ISREG(entry.identity.st_mode) &&
      entry.identity.st_nlink == 1 &&
      (entry.identity.st_uid == 0 || entry.identity.st_uid == selected) &&
      !(entry.identity.st_mode & forbidden), "file ownership, mode, type or link count rejected");
  return entry;
}

void SameEntry(int directory, const Entry& entry) {
  struct stat st = {};
  Require(fstatat(directory, entry.name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 &&
      st.st_dev == entry.identity.st_dev && st.st_ino == entry.identity.st_ino &&
      S_ISREG(st.st_mode) && st.st_nlink == 1, "file changed during ownership migration");
}

void Ownership(const Entry& entry, uid_t user, gid_t group, mode_t mode) {
  Require(fchown(entry.fd.Get(), user, group) == 0 && fchmod(entry.fd.Get(), mode) == 0,
      "file ownership change failed");
  Label(entry.fd.Get());
  Require(fsync(entry.fd.Get()) == 0, "file ownership durability failed");
}

Descriptor Lock(int directory, const char* name, gid_t group, mode_t mode) {
  int fd = openat(directory, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
  bool created = fd >= 0;
  if (fd < 0 && errno == EEXIST)
    fd = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  Require(fd >= 0, "cannot open migration lock");
  Descriptor lock(fd);
  struct stat st = {};
  Require(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
      st.st_nlink == 1 && !(st.st_mode & 0137), "migration lock protection rejected");
  Require(flock(fd, LOCK_EX | LOCK_NB) == 0, "daemon or Installer is using consent state");
  if (created || st.st_gid != group || (st.st_mode & 0777) != mode) {
    Require(fchown(fd, 0, group) == 0 && fchmod(fd, mode) == 0,
        "migration lock ownership failed");
  }
  Label(fd);
  Require(fsync(fd) == 0 && fsync(directory) == 0, "migration lock durability failed");
  return lock;
}

std::string Contents(const Entry& entry) {
  Require(entry.identity.st_size > 0 && entry.identity.st_size <= 1048576,
      "installation authority size rejected");
  std::string data(static_cast<size_t>(entry.identity.st_size), '\0');
  size_t position = 0;
  while (position < data.size()) {
    ssize_t count = pread(entry.fd.Get(), &data[position], data.size() - position, position);
    if (count < 0 && errno == EINTR)
      continue;
    Require(count > 0, "installation authority read failed");
    position += count;
  }
  return data;
}

bool StateName(const std::string& name) {
  if (name == "definitions.registry" || name.compare(0, 10, ".registry-") == 0)
    return true;
  for (const char* prefix : {"consent.db", "consent.db-journal", "consent.db-wal", "consent.db-shm"}) {
    if (name == prefix || name.compare(0, std::strlen(prefix) + 9,
        std::string(prefix) + ".retired-") == 0)
      return true;
  }
  return false;
}

void Prepare(const std::string& state_path, const std::string& authority_path,
    uid_t user, gid_t group) {
  Require(geteuid() == 0 && user != 0, "root execution and a nonroot service account are required");
  Require(state_path != authority_path, "state and installation authority must be separate");
  auto authority = Directory(authority_path, 0, true, group, 0750);
  auto lifecycle = Lock(authority.Get(), "lifecycle.lock", group, 0640);
  auto state = Directory(state_path, user, true, 0, 0700);
  struct stat state_identity = {};
  Require(fstat(state.Get(), &state_identity) == 0, "cannot inspect state directory");
  auto old_authority = File(state.Get(), "installations.conf", 0, 0137, true);
  auto new_authority = File(authority.Get(), "installations.conf", 0, 0137, true);
  Require(old_authority.fd.Get() < 0 || state_identity.st_uid == 0,
      "legacy authority has an untrusted writable parent");
  auto old_lock = File(state.Get(), "installations.lock", 0, 0177, true);
  if (old_lock.fd.Get() >= 0)
    Require(flock(old_lock.fd.Get(), LOCK_EX | LOCK_NB) == 0, "legacy Installer is active");
  auto installer = Lock(authority.Get(), "installations.lock", group, 0600);

  std::vector<Entry> entries;
  DIR* raw = fdopendir(dup(state.Get()));
  Require(raw != nullptr, "cannot enumerate existing state");
  try {
    errno = 0;
    while (auto* found = readdir(raw)) {
      std::string name = found->d_name;
      if (name == "." || name == ".." || name == "installations.conf" || name == "installations.lock")
        continue;
      Require(StateName(name), "unexpected state entry; manual review required");
      entries.push_back(File(state.Get(), name, user, 0177));
      errno = 0;
    }
    Require(errno == 0, "cannot enumerate complete state");
    closedir(raw);
  } catch (...) {
    closedir(raw);
    throw;
  }
  if (old_authority.fd.Get() >= 0 && new_authority.fd.Get() >= 0)
    Require(Contents(old_authority) == Contents(new_authority),
        "legacy and current installation authorities conflict");
  if (old_authority.fd.Get() >= 0) {
    SameEntry(state.Get(), old_authority);
    if (new_authority.fd.Get() < 0) {
      Ownership(old_authority, 0, group, 0640);
      Require(renameat(state.Get(), "installations.conf", authority.Get(), "installations.conf") == 0,
          "cannot move legacy installation authority");
    } else {
      SameEntry(authority.Get(), new_authority);
      Ownership(new_authority, 0, group, 0640);
      Require(unlinkat(state.Get(), "installations.conf", 0) == 0, "cannot retire duplicate legacy authority");
    }
    Require(fsync(authority.Get()) == 0 && fsync(state.Get()) == 0,
        "authority transfer directory synchronization failed");
  } else if (new_authority.fd.Get() >= 0) {
    SameEntry(authority.Get(), new_authority);
    Ownership(new_authority, 0, group, 0640);
  }
  // Authority must already be outside the daemon-writable tree before any
  // ownership transfer. Files stay on their original device/inode throughout.
  for (const auto& entry : entries) {
    SameEntry(state.Get(), entry);
    Ownership(entry, user, group, 0600);
  }
  if (old_lock.fd.Get() >= 0) {
    SameEntry(state.Get(), old_lock);
    Require(unlinkat(state.Get(), "installations.lock", 0) == 0, "cannot retire legacy Installer lock");
  }
  Label(authority.Get());
  Label(state.Get());
  Require(fchown(authority.Get(), 0, group) == 0 && fchmod(authority.Get(), 0750) == 0 &&
      fsync(authority.Get()) == 0, "authority directory ownership durability failed");
  Require(fchown(state.Get(), user, group) == 0 && fchmod(state.Get(), 0700) == 0 &&
      fsync(state.Get()) == 0, "state directory ownership durability failed");
}

#ifndef CONSENT_STORAGE_PREPARE_TEST
bool Stopped() {
  int pipe_fds[2];
  if (pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) < 0)
    return false;
  Descriptor reader(pipe_fds[0]);
  Descriptor writer(pipe_fds[1]);
  pid_t child = fork();
  if (child < 0)
    return false;
  if (child == 0) {
    if (dup2(writer.Get(), STDOUT_FILENO) < 0)
      _exit(126);
    execl("/usr/bin/systemctl", "systemctl", "show", "--property=MainPID", "--value",
        CONSENT_SERVICE_UNIT, static_cast<char*>(nullptr));
    _exit(127);
  }
  // Keep the check bounded even if the system manager does not respond.
  std::string output;
  bool finished = false;
  int status = 0;
  for (int attempt = 0; attempt < 60; ++attempt) {
    struct pollfd event = {reader.Get(), POLLIN, 0};
    poll(&event, 1, 50);
    char data[64];
    ssize_t bytes = read(reader.Get(), data, sizeof(data));
    if (bytes > 0)
      output.append(data, static_cast<size_t>(bytes));
    if (output.size() > 32)
      break;
    if (waitpid(child, &status, WNOHANG) == child) {
      finished = true;
      break;
    }
  }
  if (!finished) {
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  }
  return finished && WIFEXITED(status) && WEXITSTATUS(status) == 0 && output == "0\n";
}
#endif
}  // namespace

#ifndef CONSENT_STORAGE_PREPARE_TEST
int main(int argc, char**) {
  try {
    Require(argc == 1 && geteuid() == 0, "consent-storage-prepare takes no arguments and requires root");
    auto* account = getpwnam(CONSENT_SERVICE_USER);
    Require(account && account->pw_uid != 0, "configured nonroot consent service account is unavailable");
    uid_t user = account->pw_uid;
    gid_t group = account->pw_gid;
    Require(Stopped(), "consentd MainPID must be zero; stop socket/service before ownership migration");
    Prepare(CONSENT_STATE_DIR, CONSENT_AUTHORITY_DIR, user, group);
    return 0;
  } catch (const std::exception& error) {
    fprintf(stderr, "consent state preparation refused: %s; state is retained, retry after correcting the cause\n",
        error.what());
    return 1;
  }
}
#endif
