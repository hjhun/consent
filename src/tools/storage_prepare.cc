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
#include <sys/utsname.h>
#include <sys/xattr.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>
#include <vector>

#include "common/offline_layout.hh"
#include "common/recovery_state.hh"

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
#ifndef CONSENT_MIGRATION_ANCHOR
#define CONSENT_MIGRATION_ANCHOR "/etc/consent/bootstrap-upgrade-7"
#endif
#ifndef CONSENT_RPM_NAME
#define CONSENT_RPM_NAME "consentd"
#endif

namespace {
class Descriptor final {
 public:
  explicit Descriptor(int fd = -1) : fd_(fd) {}
  ~Descriptor() {
    if (fd_ >= 0)
      close(fd_);
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  int Get() const { return fd_; }
  void Close() {
    if (fd_ >= 0)
      close(fd_);
    fd_ = -1;
  }

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

Descriptor Directory(const std::string& path, uid_t permitted_owner,
                     bool create, gid_t group, mode_t mode) {
  Require(!path.empty() && path[0] == '/' && path.back() != '/',
          "invalid absolute directory");
  int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  Require(current >= 0, "cannot open filesystem root");
  size_t offset = 1;
  while (offset < path.size()) {
    Descriptor parent(current);
    auto end = path.find('/', offset);
    bool last = end == std::string::npos;
    auto name = path.substr(offset, last ? std::string::npos : end - offset);
    Require(!name.empty() && name != "." && name != "..",
            "invalid directory component");
    bool created = false;
    int next = openat(parent.Get(), name.c_str(),
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT && create && last) {
      Require(mkdirat(parent.Get(), name.c_str(), 0700) == 0,
              "cannot create protected directory");
      created = true;
      next = openat(parent.Get(), name.c_str(),
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }
    Require(next >= 0, "cannot open protected directory component");
    Descriptor opened(next);
    struct stat st = {};
    Require(fstat(next, &st) == 0 && S_ISDIR(st.st_mode),
            "directory type rejected");
    bool valid = (st.st_uid == 0 || (last && st.st_uid == permitted_owner)) &&
                 !(st.st_mode & 0022);
#ifdef CONSENT_STORAGE_PREPARE_TEST
    // Only fixture builds allow the root-owned sticky /tmp ancestor.
    if (offset == 1 && name == "tmp" && !last && st.st_uid == 0 &&
        (st.st_mode & S_ISVTX))
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

Entry File(int directory, const std::string& name, uid_t selected,
           mode_t forbidden, bool optional = false) {
  int fd = openat(directory, name.c_str(),
                  O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (optional && fd < 0 && errno == ENOENT)
    return {name, Descriptor(), {}};
  Require(fd >= 0, "cannot open protected file");
  Entry entry{name, Descriptor(fd), {}};
  Require(
      fstat(fd, &entry.identity) == 0 && S_ISREG(entry.identity.st_mode) &&
          entry.identity.st_nlink == 1 &&
          (entry.identity.st_uid == 0 || entry.identity.st_uid == selected) &&
          !(entry.identity.st_mode & forbidden),
      "file ownership, mode, type or link count rejected");
  return entry;
}

void SameEntry(int directory, const Entry& entry) {
  struct stat st = {};
  Require(
      fstatat(directory, entry.name.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 &&
          st.st_dev == entry.identity.st_dev &&
          st.st_ino == entry.identity.st_ino && S_ISREG(st.st_mode) &&
          st.st_nlink == 1,
      "file changed during ownership migration");
}

void Ownership(const Entry& entry, uid_t user, gid_t group, mode_t mode) {
  Require(fchown(entry.fd.Get(), user, group) == 0 &&
              fchmod(entry.fd.Get(), mode) == 0,
          "file ownership change failed");
  Label(entry.fd.Get());
  Require(fsync(entry.fd.Get()) == 0, "file ownership durability failed");
}

Descriptor Lock(int directory, const char* name, gid_t group, mode_t mode) {
  int fd = openat(directory, name,
                  O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
  bool created = fd >= 0;
  if (fd < 0 && errno == EEXIST)
    fd =
        openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  Require(fd >= 0, "cannot open migration lock");
  Descriptor lock(fd);
  struct stat st = {};
  Require(fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 &&
              st.st_nlink == 1 && !(st.st_mode & 0137),
          "migration lock protection rejected");
  Require(flock(fd, LOCK_EX | LOCK_NB) == 0,
          "daemon or Installer is using consent state");
  if (created || st.st_gid != group || (st.st_mode & 0777) != mode) {
    Require(fchown(fd, 0, group) == 0 && fchmod(fd, mode) == 0,
            "migration lock ownership failed");
  }
  Label(fd);
  Require(fsync(fd) == 0 && fsync(directory) == 0,
          "migration lock durability failed");
  return lock;
}

std::string Contents(const Entry& entry) {
  Require(entry.identity.st_size > 0 && entry.identity.st_size <= 1048576,
          "installation authority size rejected");
  std::string data(static_cast<size_t>(entry.identity.st_size), '\0');
  size_t position = 0;
  while (position < data.size()) {
    ssize_t count = pread(entry.fd.Get(), &data[position],
                          data.size() - position, position);
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
  for (const char* prefix : {"consent.db", "consent.db-journal",
                             "consent.db-wal", "consent.db-shm"}) {
    if (name == prefix || name.compare(0, std::strlen(prefix) + 9,
                                       std::string(prefix) + ".retired-") == 0)
      return true;
  }
  return false;
}

void PrepareRegistrations(int authority, gid_t group) {
  using namespace consent::offline;
  int raw_fd = openat(authority, kRegistrationDirectory,
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (raw_fd < 0 && errno == ENOENT)
    return;
  Require(raw_fd >= 0, "cannot open offline registration directory");
  Descriptor directory(raw_fd);
  struct stat identity = {};
  Require(fstat(raw_fd, &identity) == 0 && S_ISDIR(identity.st_mode) &&
              identity.st_uid == 0 && !(identity.st_mode & 0027),
          "offline registration directory protection rejected");
  std::vector<Entry> entries;
  size_t bytes = 0;
  size_t records = 0;
  DIR* listing = fdopendir(dup(raw_fd));
  Require(listing != nullptr, "cannot enumerate offline registrations");
  try {
    errno = 0;
    while (auto* found = readdir(listing)) {
      std::string name = found->d_name;
      if (name == "." || name == "..")
        continue;
      bool record = RegistrationName(name);
      Require(record || PendingRegistrationName(name),
              "unexpected offline registration entry");
      Require(entries.size() < kMaxRegistrationEntries,
              "offline entry count exceeded");
      auto entry = File(raw_fd, name, 0, 07137);
      Require(entry.identity.st_size >= 0 &&
                  static_cast<size_t>(entry.identity.st_size) <=
                      kMaxRegistrationFileBytes,
              "offline registration file size exceeded");
      bytes += static_cast<size_t>(entry.identity.st_size);
      records += record ? 1 : 0;
      Require(bytes <= kMaxRegistrationBytes && records <= kMaxRegistrations,
              "offline registration storage budget exceeded");
      entries.push_back(std::move(entry));
      errno = 0;
    }
    Require(errno == 0, "cannot enumerate complete offline registrations");
    closedir(listing);
  } catch (...) {
    closedir(listing);
    throw;
  }
  // The lifecycle EX lock excludes both image writers and the daemon. Verify
  // every entry before changing metadata; never parse payloads as root.
  for (const auto& entry : entries) {
    SameEntry(raw_fd, entry);
    Ownership(entry, 0, group, 0640);
  }
  Require(fchown(raw_fd, 0, group) == 0 && fchmod(raw_fd, 0750) == 0,
          "offline registration directory ownership failed");
  Label(raw_fd);
  Require(fsync(raw_fd) == 0 && fsync(authority) == 0,
          "offline registration labeling durability failed");
}

bool PairFile(int directory, const char* name, uid_t user, gid_t group,
              bool* present) {
  struct stat info = {};
  if (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) < 0) {
    Require(errno == ENOENT, "cannot inspect bootstrap pair");
    *present = false;
    return true;
  }
  *present = true;
  return S_ISREG(info.st_mode) && info.st_uid == user && info.st_gid == group &&
         (info.st_mode & 07777) == 0600 && info.st_nlink == 1;
}

std::string RandomId() {
  Descriptor random(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
  Require(random.Get() >= 0, "cannot open bootstrap random source");
  unsigned char bytes[16];
  size_t offset = 0;
  while (offset < sizeof(bytes)) {
    ssize_t count = read(random.Get(), bytes + offset, sizeof(bytes) - offset);
    if (count < 0 && errno == EINTR)
      continue;
    Require(count > 0, "cannot obtain bootstrap nonce");
    offset += static_cast<size_t>(count);
  }
  char hex[33];
  for (size_t index = 0; index < sizeof(bytes); ++index)
    snprintf(hex + index * 2, 3, "%02x", bytes[index]);
  return hex;
}

bool InUnit(const char* unit) {
  char* actual = nullptr;
  int result = sd_pid_get_unit(0, &actual);
  bool match = result >= 0 && actual && unit == std::string(actual);
  free(actual);
  return match;
}

bool RpmUpgradePair() {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0)
    return false;
  Descriptor reader(fds[0]);
  Descriptor writer(fds[1]);
  pid_t child = fork();
  if (child < 0)
    return false;
  if (child == 0) {
    if (dup2(writer.Get(), STDOUT_FILENO) < 0)
      _exit(126);
    execl("/usr/bin/rpm", "rpm", "-q", "--qf",
          "%{NAME} %{VERSION}-%{RELEASE} %{ARCH}\n", CONSENT_RPM_NAME,
          static_cast<char*>(nullptr));
    _exit(127);
  }
  writer.Close();
  std::string output;
  bool exited = false;
  int status = 0;
  for (int attempt = 0; attempt < 40; ++attempt) {
    struct pollfd event = {reader.Get(), POLLIN, 0};
    poll(&event, 1, 50);
    char bytes[64];
    ssize_t count = read(reader.Get(), bytes, sizeof(bytes));
    if (count > 0)
      output.append(bytes, static_cast<size_t>(count));
    if (output.size() > 256)
      break;
    if (waitpid(child, &status, WNOHANG) == child) {
      exited = true;
      break;
    }
  }
  if (!exited) {
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return false;
  }
  char bytes[64];
  ssize_t count;
  do {
    count = read(reader.Get(), bytes, sizeof(bytes));
    if (count > 0)
      output.append(bytes, static_cast<size_t>(count));
  } while (count > 0 && output.size() <= 256);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || output.size() > 256)
    return false;
  struct utsname machine = {};
  if (uname(&machine) < 0)
    return false;
  int old_count = 0;
  int new_count = 0;
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream fields(line);
    std::string name;
    std::string version;
    std::string arch;
    std::string extra;
    if (!(fields >> name >> version >> arch) || fields >> extra ||
        name != CONSENT_RPM_NAME || arch != machine.machine)
      return false;
    if (version == "0.1.0-7")
      ++old_count;
    else if (version == "0.1.0-8")
      ++new_count;
    else
      return false;
  }
  fprintf(stderr, "bootstrap migration rpmdb: %s", output.c_str());
  return old_count == 1 && new_count == 1;
}

bool Pair(int state, uid_t user, gid_t group, bool* registry, bool* database) {
  return PairFile(state, "definitions.registry", user, group, registry) &&
         PairFile(state, "consent.db", user, group, database);
}

void ProtectReceipt(int authority, gid_t group) {
  auto entry = File(authority, "bootstrap.receipt", 0, 0137);
  Require(entry.identity.st_uid == 0 && entry.identity.st_gid == group &&
              (entry.identity.st_mode & 07777) == 0640,
          "bootstrap receipt protection rejected");
  Label(entry.fd.Get());
  Require(fsync(entry.fd.Get()) == 0 && fsync(authority) == 0,
          "bootstrap receipt label durability failed");
}

void PersistReceipt(int authority, gid_t group,
                    const consent::recovery::Receipt& receipt) {
  Require(consent::recovery::WriteReceipt(authority, group, receipt),
          "cannot persist bootstrap receipt");
  ProtectReceipt(authority, group);
}

void BootstrapPreflight(int authority, int state, uid_t user, gid_t group) {
  using consent::recovery::Phase;
  using consent::recovery::Receipt;
  Require(InUnit(CONSENT_SERVICE_UNIT),
          "bootstrap preflight has the wrong systemd unit");
  bool registry = false;
  bool database = false;
  Require(Pair(state, user, group, &registry, &database),
          "bootstrap pair protection rejected");
  Receipt receipt;
  Require(consent::recovery::ReadReceipt(authority, group, &receipt),
          "bootstrap receipt invalid");
  Require(receipt.phase != Phase::kAbsent,
          "bootstrap receipt missing; recovery required");
  Require(receipt.unit == CONSENT_SERVICE_UNIT,
          "bootstrap receipt belongs to another unit");
  if (receipt.phase == Phase::kFresh) {
    Require(!registry && !database, "fresh bootstrap found a previous pair");
    const char* invocation = getenv("INVOCATION_ID");
    Require(invocation && consent::recovery::HexId(invocation, 32),
            "fresh bootstrap lacks a systemd invocation");
    receipt.phase = Phase::kClaimed;
    receipt.invocation = invocation;
    receipt.nonce = RandomId();
    PersistReceipt(authority, group, receipt);
    return;
  }
  Require(registry, "registry loss requires trusted-source recovery");
  Require(receipt.phase == Phase::kInitialized,
          "bootstrap receipt requires explicit recovery");
  ProtectReceipt(authority, group);
}

void Prepare(const std::string& state_path, const std::string& authority_path,
             uid_t user, gid_t group, bool bootstrap = false) {
  Require(geteuid() == 0 && user != 0,
          "root execution and a nonroot service account are required");
  Require(state_path != authority_path,
          "state and installation authority must be separate");
  auto authority = Directory(authority_path, 0, true, group, 0750);
  auto lifecycle = Lock(authority.Get(), "lifecycle.lock", group, 0640);
  if (bootstrap) {
    consent::recovery::Receipt prior;
    Require(consent::recovery::ReadReceipt(authority.Get(), group, &prior) &&
                prior.phase != consent::recovery::Phase::kAbsent &&
                prior.phase != consent::recovery::Phase::kClaimed &&
                prior.phase != consent::recovery::Phase::kRecoveryRequired &&
                prior.phase != consent::recovery::Phase::kInvalid,
            "bootstrap receipt requires recovery before state creation");
    struct stat state_before = {};
    int observed = lstat(state_path.c_str(), &state_before);
    if (prior.phase == consent::recovery::Phase::kFresh)
      Require(observed < 0 && errno == ENOENT,
              "fresh bootstrap found an existing state path");
    else
      Require(observed == 0 && S_ISDIR(state_before.st_mode),
              "initialized bootstrap lost its state directory");
  }
  auto state = Directory(state_path, user, true, 0, 0700);
  struct stat state_identity = {};
  Require(fstat(state.Get(), &state_identity) == 0,
          "cannot inspect state directory");
  auto old_authority = File(state.Get(), "installations.conf", 0, 0137, true);
  auto new_authority =
      File(authority.Get(), "installations.conf", 0, 0137, true);
  Require(old_authority.fd.Get() < 0 || state_identity.st_uid == 0,
          "legacy authority has an untrusted writable parent");
  auto old_lock = File(state.Get(), "installations.lock", 0, 0177, true);
  if (old_lock.fd.Get() >= 0)
    Require(flock(old_lock.fd.Get(), LOCK_EX | LOCK_NB) == 0,
            "legacy Installer is active");
  auto installer = Lock(authority.Get(), "installations.lock", group, 0600);
  PrepareRegistrations(authority.Get(), group);

  std::vector<Entry> entries;
  DIR* raw = fdopendir(dup(state.Get()));
  Require(raw != nullptr, "cannot enumerate existing state");
  try {
    errno = 0;
    while (auto* found = readdir(raw)) {
      std::string name = found->d_name;
      if (name == "." || name == ".." || name == "installations.conf" ||
          name == "installations.lock")
        continue;
      Require(StateName(name),
              "unexpected state entry; manual review required");
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
      Require(renameat(state.Get(), "installations.conf", authority.Get(),
                       "installations.conf") == 0,
              "cannot move legacy installation authority");
    } else {
      SameEntry(authority.Get(), new_authority);
      Ownership(new_authority, 0, group, 0640);
      Require(unlinkat(state.Get(), "installations.conf", 0) == 0,
              "cannot retire duplicate legacy authority");
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
    Require(unlinkat(state.Get(), "installations.lock", 0) == 0,
            "cannot retire legacy Installer lock");
  }
  Label(authority.Get());
  Label(state.Get());
  Require(fchown(authority.Get(), 0, group) == 0 &&
              fchmod(authority.Get(), 0750) == 0 && fsync(authority.Get()) == 0,
          "authority directory ownership durability failed");
  Require(fchown(state.Get(), user, group) == 0 &&
              fchmod(state.Get(), 0700) == 0 && fsync(state.Get()) == 0,
          "state directory ownership durability failed");
  if (bootstrap)
    BootstrapPreflight(authority.Get(), state.Get(), user, group);
}

void FirstInstall(const std::string& state_path,
                  const std::string& authority_path, gid_t group) {
  struct stat info = {};
  Require(lstat(state_path.c_str(), &info) < 0 && errno == ENOENT,
          "fresh state path already exists");
  // RPM scriptlets may not hold the service's privileged SMACK context.
  // The first ExecStartPre labels this directory and the claimed receipt.
  auto slash = authority_path.rfind('/');
  Require(slash != std::string::npos && slash > 0 &&
              slash + 1 < authority_path.size(),
          "invalid fresh authority path");
  auto parent = Directory(authority_path.substr(0, slash), 0, false, 0, 0);
  std::string leaf = authority_path.substr(slash + 1);
  Require(leaf != "." && leaf != ".." &&
              mkdirat(parent.Get(), leaf.c_str(), 0700) == 0,
          "fresh authority already exists or cannot be created");
  Descriptor authority(openat(parent.Get(), leaf.c_str(),
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  Require(authority.Get() >= 0 && fchown(authority.Get(), 0, group) == 0 &&
              fchmod(authority.Get(), 0750) == 0 &&
              fsync(authority.Get()) == 0 && fsync(parent.Get()) == 0,
          "fresh authority protection failed");
  Require(lstat(state_path.c_str(), &info) < 0 && errno == ENOENT,
          "fresh state appeared during receipt creation");
  consent::recovery::Receipt receipt;
  receipt.phase = consent::recovery::Phase::kFresh;
  receipt.unit = CONSENT_SERVICE_UNIT;
  Require(consent::recovery::WriteReceipt(authority.Get(), group, receipt),
          "cannot persist fresh bootstrap receipt");
}

void MigrationAnchor() {
  std::string path = CONSENT_MIGRATION_ANCHOR;
  auto slash = path.rfind('/');
  Require(slash != std::string::npos && slash > 0 && slash + 1 < path.size(),
          "invalid migration anchor path");
  auto parent = Directory(path.substr(0, slash), 0, false, 0, 0);
  std::string leaf = path.substr(slash + 1);
  Descriptor anchor(openat(parent.Get(), leaf.c_str(),
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                           0600));
  Require(anchor.Get() >= 0, "migration anchor already spent");
  constexpr char record[] = "schema=1\nfrom=0.1.0-7\n";
  Require(write(anchor.Get(), record, sizeof(record) - 1) ==
                  static_cast<ssize_t>(sizeof(record) - 1) &&
              fsync(anchor.Get()) == 0 && fsync(parent.Get()) == 0,
          "cannot persist one-shot migration anchor");
}

void MigrateRelease7(const std::string& state_path,
                     const std::string& authority_path, uid_t user, gid_t group,
                     bool fixture) {
  auto authority = Directory(authority_path, 0, false, group, 0750);
  Descriptor lock(openat(authority.Get(), "lifecycle.lock",
                         O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  struct stat identity = {};
  Require(lock.Get() >= 0 && fstat(lock.Get(), &identity) == 0 &&
              S_ISREG(identity.st_mode) && identity.st_uid == 0 &&
              identity.st_gid == group && (identity.st_mode & 07777) == 0640 &&
              identity.st_nlink == 1 &&
              flock(lock.Get(), LOCK_EX | LOCK_NB) == 0,
          "migration requires the stopped daemon and protected lock");
  consent::recovery::Receipt receipt;
  Require(consent::recovery::ReadReceipt(authority.Get(), group, &receipt),
          "migration receipt invalid");
  if (receipt.phase == consent::recovery::Phase::kInitialized) {
    Require(receipt.unit == CONSENT_SERVICE_UNIT,
            "migration receipt belongs to another unit");
    return;
  }
  Require(receipt.phase == consent::recovery::Phase::kAbsent,
          "migration receipt is already claimed");
  Require(fixture || RpmUpgradePair(),
          "exact Release7-to-Release8 RPM pair is absent");
  struct stat state_info = {};
  bool state_exists = lstat(state_path.c_str(), &state_info) == 0;
  Require(state_exists || errno == ENOENT,
          "cannot inspect Release7 state path");
  bool registry = false;
  bool database = false;
  if (state_exists) {
    auto state = Directory(state_path, user, false, 0, 0700);
    Require(Pair(state.Get(), user, group, &registry, &database),
            "Release7 pair protection rejected");
  }
#ifndef CONSENT_POC_MIGRATION_CLASSIFY
  Require(registry && database,
          "migration requires an intact protected Release7 pair");
#endif
  MigrationAnchor();
  receipt.phase = registry && database
                      ? consent::recovery::Phase::kInitialized
                      : consent::recovery::Phase::kRecoveryRequired;
  receipt.unit = CONSENT_SERVICE_UNIT;
  Require(consent::recovery::WriteReceipt(authority.Get(), group, receipt),
          "cannot write one-shot migration receipt");
  if (receipt.phase == consent::recovery::Phase::kRecoveryRequired)
    fprintf(stderr, "bootstrap migration fenced: prior pair incomplete\n");
}

void CompleteBootstrap(const std::string& state_path,
                       const std::string& authority_path, uid_t user,
                       gid_t group) {
  using consent::recovery::Phase;
  Require(InUnit(CONSENT_SERVICE_UNIT),
          "bootstrap acknowledgement has the wrong systemd unit");
  auto authority = Directory(authority_path, 0, false, group, 0750);
  auto state = Directory(state_path, user, false, 0, 0700);
  consent::recovery::Receipt receipt;
  Require(consent::recovery::ReadReceipt(authority.Get(), group, &receipt) &&
              receipt.unit == CONSENT_SERVICE_UNIT,
          "bootstrap acknowledgement receipt invalid");
  if (receipt.phase == Phase::kInitialized) {
    ProtectReceipt(authority.Get(), group);
    return;
  }
  const char* invocation = getenv("INVOCATION_ID");
  Require(receipt.phase == Phase::kClaimed && invocation &&
              receipt.invocation == invocation,
          "bootstrap acknowledgement invocation mismatch");
  bool registry = false;
  bool database = false;
  Require(Pair(state.Get(), user, group, &registry, &database) && registry &&
              database,
          "bootstrap pair is incomplete after READY");
  receipt.phase = Phase::kInitialized;
  receipt.invocation.clear();
  receipt.nonce.clear();
  PersistReceipt(authority.Get(), group, receipt);
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
    execl("/usr/bin/systemctl", "systemctl", "show", "--property=MainPID",
          "--value", CONSENT_SERVICE_UNIT, static_cast<char*>(nullptr));
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
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  return finished && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
         output == "0\n";
}
#endif
}  // namespace

#ifndef CONSENT_STORAGE_PREPARE_TEST
int main(int argc, char** argv) {
  try {
    Require(geteuid() == 0, "consent storage helper requires root");
    if (argc == 3 && std::strcmp(argv[1], "--begin") == 0) {
      Require(consent::recovery::HexId(argv[2], 32),
              "invalid recovery identifier");
      throw std::runtime_error(
          "complete current desired-definition source is unavailable; "
          "recovery state was not changed");
    }
    bool first = argc == 2 && std::strcmp(argv[1], "--first-install") == 0;
    bool image_first = argc == 3 &&
                       std::strcmp(argv[1], "--first-install") == 0 &&
                       std::strncmp(argv[2], "--image-root=", 13) == 0;
    bool complete = argc == 2 && std::strcmp(argv[1], "--complete") == 0;
    bool migrate = argc == 2 && std::strcmp(argv[1], "--migrate-release7") == 0;
#ifdef CONSENT_STORAGE_PREPARE_FIXTURE
    bool fixture = argc == 2 && std::strcmp(argv[1], "--migrate-fixture") == 0;
#else
    bool fixture = false;
#endif
    Require(argc == 1 || first || image_first || complete || migrate || fixture,
            "invalid consent storage helper command");
    auto* account = getpwnam(CONSENT_SERVICE_USER);
    Require(account && account->pw_uid != 0,
            "configured nonroot consent service account is unavailable");
    uid_t user = account->pw_uid;
    gid_t group = account->pw_gid;
    if (first || image_first) {
      std::string root;
      if (image_first) {
        root = argv[2] + 13;
        Require(root.size() > 1 && root.front() == '/' && root.back() != '/',
                "invalid image root");
        auto image = Directory(root, 0, false, 0, 0);
        (void)image;
      }
      FirstInstall(root + CONSENT_STATE_DIR, root + CONSENT_AUTHORITY_DIR,
                   group);
    } else if (complete) {
      CompleteBootstrap(CONSENT_STATE_DIR, CONSENT_AUTHORITY_DIR, user, group);
    } else {
      Require(Stopped(),
              "consentd MainPID must be zero; stop socket/service before "
              "ownership migration");
      if (migrate || fixture)
        MigrateRelease7(CONSENT_STATE_DIR, CONSENT_AUTHORITY_DIR, user, group,
                        fixture);
      else
        Prepare(CONSENT_STATE_DIR, CONSENT_AUTHORITY_DIR, user, group, true);
    }
    return 0;
  } catch (const std::exception& error) {
    fprintf(stderr,
            "consent state preparation refused: %s; state is retained, "
            "retry after correcting the cause\n",
            error.what());
    return 1;
  }
}
#endif
