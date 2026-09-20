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
#include "consentd/repository.hh"

#include <cerrno>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static dev_t sync_device;
static ino_t sync_inode;
static bool fail_parent_sync;
static unsigned parent_sync_calls;
static int FixtureSync(int fd) {
  struct stat st = {};
  if (sync_inode && fstat(fd, &st) == 0 && st.st_dev == sync_device && st.st_ino == sync_inode) {
    ++parent_sync_calls;
    if (fail_parent_sync) {
      errno = EIO;
      return -1;
    }
  }
  return syscall(SYS_fsync, fd);
}

#define CONSENT_STORAGE_PREPARE_TEST
#define fsync FixtureSync
#include "../tools/storage_prepare.cc"
#undef fsync

#include <grp.h>

#include <iostream>
#include <memory>

namespace {
using consent::Get;
using consent::Message;

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

void Remove(const std::string& path) {
  struct stat st = {};
  if (lstat(path.c_str(), &st) < 0)
    return;
  if (!S_ISDIR(st.st_mode)) {
    unlink(path.c_str());
    return;
  }
  DIR* directory = opendir(path.c_str());
  if (!directory)
    return;
  while (auto* item = readdir(directory)) {
    if (std::strcmp(item->d_name, ".") && std::strcmp(item->d_name, ".."))
      Remove(path + "/" + item->d_name);
  }
  closedir(directory);
  rmdir(path.c_str());
}

void Write(const std::string& path, const std::string& contents) {
  Descriptor fd(open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
  Check(fd.Get() >= 0, "create isolated fixture file");
  Check(write(fd.Get(), contents.data(), contents.size()) == static_cast<ssize_t>(contents.size()) &&
      fsync(fd.Get()) == 0, "persist isolated fixture file");
}

std::string Read(const std::string& path, uid_t owner = 0) {
  Descriptor directory(open(path.substr(0, path.rfind('/')).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  auto file = File(directory.Get(), path.substr(path.rfind('/') + 1), owner, 0);
  return file.identity.st_size == 0 ? std::string() : Contents(file);
}

struct Account {
  uid_t user;
  gid_t group;
};

class Fixture final {
 public:
  explicit Fixture(Account account) : account_(account) {
    char pattern[] = "/tmp/consent-storage-prepare-XXXXXX";
    auto* created = mkdtemp(pattern);
    Check(created, "create isolated migration fixture");
    directory = created;
    Check(chmod(directory.c_str(), 0755) == 0, "allow selected test account to traverse fixture root");
    state = directory + "/state";
    authority = directory + "/authority";
    Check(mkdir(state.c_str(), 0700) == 0, "create legacy root-owned state");
    Write(state + "/installations.conf", "[authority]\nschema=1\n[package package]\ngeneration=installed\nstate=active\n[app]\npackage=package\ngeneration=installed\nstate=active\n");
    Write(state + "/installations.lock", "");
    consentd::Peer peer = Identity();
    consentd::Repository repository(state + "/consent.db", state);
    Validator(repository);
    std::string error;
    Check(repository.Open(&error), error.c_str());
    Call(repository, peer, {{"method", "register"}, {"definition", "definition"},
        {"package", "package"}, {"app", "app"}, {"enforcer", "actor"},
        {"operation_id", "install"}, {"_install_identity", "installed"},
        {"policy_version", "1"}, {"text_revision", "1"}, {"level", "1"},
        {"modes", "PERSISTENT"}, {"default_locale", "en"},
        {"message.en.title", "Allow"}, {"message.en.body", "Read exact test scope"}});
    auto request = Query();
    request["method"] = "request";
    auto pending = Call(repository, peer, request);
    auto prompt = Call(repository, peer, {{"method", "get_prompt"},
        {"request_id", Get(pending, "request_id")}, {"locale", "en"}});
    Call(repository, peer, {{"method", "respond"}, {"request_id", Get(pending, "request_id")},
        {"prompt_token", Get(prompt, "prompt_token")}, {"decision", "ALLOWED"}, {"grant_mode", "PERSISTENT"}});
    Check(Get(Call(repository, peer, Query()), "decision") == "ALLOWED", "seed real persistent grant");
    Check(stat((state + "/consent.db").c_str(), &before) == 0, "record original DB identity");
    registry = Read(state + "/definitions.registry");
    installation = Read(state + "/installations.conf");
  }
  ~Fixture() { Remove(directory); }

  static consentd::Peer Identity() {
    consentd::Peer peer;
    peer.identity = "actor";
    peer.instance = "instance";
    peer.roles = {"installer", "argo", "checker", "ui"};
    peer.packages = {"package"};
    peer.subjects = {"subject"};
    peer.profiles = {"profile"};
    return peer;
  }
  static Message Query() {
    return {{"method", "check"}, {"subject", "subject"}, {"profile", "profile"},
        {"count", "1"}, {"r0.definition", "definition"}, {"r0.operation", "read"},
        {"r0.scope", "exact"}, {"r0.purpose", "test"}, {"client_request_id", "request"},
        {"operation_id", "operation"}, {"step_id", "step"}};
  }
  static void Validator(consentd::Repository& repository) {
    repository.SetInstallationValidator([](const std::string& package,
        const std::string& app, const std::string& generation) {
      return package == "package" && app == "app" && generation == "installed";
    });
  }
  static Message Call(consentd::Repository& repository, const consentd::Peer& peer,
      const Message& request) {
    auto response = repository.Execute(peer, request);
    Check(Get(response, "status") == "0", Get(response, "reason").c_str());
    return response;
  }
  void Migrate() { Prepare(state, authority, account_.user, account_.group); }
  void Refuse() {
    bool rejected = false;
    try { Migrate(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "unsafe migration must fail closed");
    struct stat after = {};
    Check(lstat((state + "/consent.db").c_str(), &after) == 0, "refusal must preserve DB entry");
  }
  void Preserved() {
    struct stat after = {};
    Check(stat((state + "/consent.db").c_str(), &after) == 0 &&
        after.st_dev == before.st_dev && after.st_ino == before.st_ino,
        "migration preserves actual database device and inode");
    Check(Read(state + "/definitions.registry", account_.user) == registry,
        "migration leaves definition and incarnation registry content untouched");
    Check(Read(authority + "/installations.conf") == installation,
        "authority generation metadata is transferred verbatim");
    Check(access((state + "/installations.conf").c_str(), F_OK) < 0 && errno == ENOENT,
        "root authority no longer exists in daemon-writable tree");
    pid_t child = fork();
    Check(child >= 0, "fork selected-account verification");
    if (child == 0) {
      try {
        Check(setgroups(0, nullptr) == 0 && setgid(account_.group) == 0 && setuid(account_.user) == 0,
            "drop to real selected account");
        Check(geteuid() == account_.user && geteuid() != 0, "verify actual nonroot execution");
        Descriptor inventory(open((authority + "/installations.conf").c_str(), O_RDONLY | O_NOFOLLOW));
        Check(inventory.Get() >= 0, "selected account can read root installation authority");
        Check(access(authority.c_str(), W_OK) < 0 &&
            access((authority + "/installations.conf").c_str(), W_OK) < 0,
            "selected account cannot replace or write authority");
        consentd::Repository repository(state + "/consent.db", state);
        Validator(repository);
        std::string error;
        Check(repository.Open(&error), error.c_str());
        Check(Get(Call(repository, Identity(), Query()), "decision") == "ALLOWED",
            "real persistent approval survives ownership migration and nonroot reopen");
        _exit(0);
      } catch (const std::exception& error) {
        fprintf(stderr, "nonroot migration verification failed: %s\n", error.what());
        _exit(1);
      }
    }
    int status = 0;
    Check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "selected account repository verification");
  }

  std::string directory;
  std::string state;
  std::string authority;
  struct stat before = {};
  std::string registry;
  std::string installation;
 private:
  Account account_;
};

void Migration(Account account) {
  Fixture fixture(account);
  fixture.Migrate();
  fixture.Preserved();
  fixture.Migrate();
  fixture.Preserved();
  std::cout << "PASS ownership migration and retry preserve inode, registry, generation and persistent approval under real nonroot UID\n";
}

void Interrupted(Account account) {
  Fixture fixture(account);
  Check(mkdir(fixture.authority.c_str(), 0750) == 0 &&
      chown(fixture.authority.c_str(), 0, account.group) == 0, "create interrupted authority directory");
  Check(rename((fixture.state + "/installations.conf").c_str(),
      (fixture.authority + "/installations.conf").c_str()) == 0, "simulate completed authority rename");
  Check(chown((fixture.state + "/consent.db").c_str(), account.user, account.group) == 0,
      "simulate partially transferred state ownership");
  fixture.Migrate();
  fixture.Preserved();
  std::cout << "PASS interrupted authority transfer and mixed root/selected file owners resume without DB recreation\n";
}

void DirectoryDurability(Account account) {
  Fixture fixture(account);
  struct stat parent = {};
  Check(stat(fixture.directory.c_str(), &parent) == 0, "locate parent directory durability gate");
  sync_device = parent.st_dev;
  sync_inode = parent.st_ino;
  parent_sync_calls = 0;
  fail_parent_sync = true;
  fixture.Refuse();
  Check(parent_sync_calls == 1 && access(fixture.authority.c_str(), F_OK) == 0,
      "mkdir succeeded but parent durability failed");
  fixture.Refuse();
  Check(parent_sync_calls == 2, "existing directory retry repeats required parent durability barrier");
  fail_parent_sync = false;
  fixture.Migrate();
  Check(parent_sync_calls >= 3, "migration succeeds only after an actual successful parent sync");
  sync_inode = 0;
  fixture.Preserved();
  std::cout << "PASS failed mkdir parent synchronization stays fenced until successful retry barrier\n";
}

void Refusals(Account account) {
  for (const char* failure : {"conflict", "symlink", "hardlink", "foreign", "unknown", "busy"}) {
    Fixture fixture(account);
    if (std::strcmp(failure, "conflict") == 0) {
      Check(mkdir(fixture.authority.c_str(), 0750) == 0, "create conflicting authority directory");
      Write(fixture.authority + "/installations.conf", "conflicting authority");
    } else if (std::strcmp(failure, "symlink") == 0) {
      Check(symlink("consent.db", (fixture.state + "/consent.db-journal").c_str()) == 0,
          "create rejected symlink");
    } else if (std::strcmp(failure, "hardlink") == 0) {
      Check(link((fixture.state + "/consent.db").c_str(), (fixture.directory + "/other-link").c_str()) == 0,
          "create rejected hardlink");
    } else if (std::strcmp(failure, "foreign") == 0) {
      uid_t foreign = account.user == 65533 ? 65532 : 65533;
      Check(chown((fixture.state + "/consent.db").c_str(), foreign, 0) == 0, "create foreign ownership");
    } else if (std::strcmp(failure, "unknown") == 0) {
      Write(fixture.state + "/unexpected", "do not erase");
    } else {
      Check(mkdir(fixture.authority.c_str(), 0750) == 0, "create lifecycle directory");
      Write(fixture.authority + "/lifecycle.lock", "");
      int fd = open((fixture.authority + "/lifecycle.lock").c_str(), O_RDONLY | O_CLOEXEC);
      Check(fd >= 0 && flock(fd, LOCK_SH | LOCK_NB) == 0, "hold real shared daemon lifecycle lock");
      Descriptor locked(fd);
      fixture.Refuse();
      continue;
    }
    fixture.Refuse();
    Check(Read(fixture.state + "/installations.conf") == fixture.installation,
        "refused preflight preserves legacy installation generation authority");
  }
  std::cout << "PASS conflicting authority, symlink, hardlink, foreign owner, unexpected entry and active lifecycle lock refuse without deleting state\n";
}

struct SavedSpoolFile {
  std::string path;
  std::string bytes;
  struct stat identity;
};

SavedSpoolFile SaveSpoolFile(const std::string& path, const std::string& bytes) {
  Write(path, bytes);
  SavedSpoolFile saved{path, bytes, {}};
  Check(lstat(path.c_str(), &saved.identity) == 0, "capture offline file identity");
  return saved;
}

void CheckSpoolFile(const SavedSpoolFile& saved, gid_t group, mode_t mode) {
  struct stat current = {};
  Check(lstat(saved.path.c_str(), &current) == 0 && S_ISREG(current.st_mode) &&
      current.st_dev == saved.identity.st_dev && current.st_ino == saved.identity.st_ino &&
      current.st_uid == 0 && current.st_gid == group && (current.st_mode & 07777) == mode,
      "offline metadata preparation preserves inode and expected root ownership/mode");
  Check(Read(saved.path) == saved.bytes, "offline metadata preparation preserves bytes exactly");
}

std::string Spool(Fixture& fixture) {
  Check(mkdir(fixture.authority.c_str(), 0700) == 0, "create root-only image authority");
  std::string spool = fixture.authority + "/" + consent::offline::kRegistrationDirectory;
  Check(mkdir(spool.c_str(), 0700) == 0, "create root-only offline spool");
  return spool;
}

std::string RecordName(unsigned index) {
  char name[72];
  std::snprintf(name, sizeof(name), "%064x.parcel", index);
  return name;
}

std::string PendingName(unsigned index) {
  char name[46];
  std::snprintf(name, sizeof(name), ".pending-00000000-0000-4000-8000-%012x", index);
  return name;
}

void ChildCompleted(pid_t child, const char* reason) {
  gint64 deadline = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
  while (true) {
    int status = 0;
    pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child) {
      Check(WIFEXITED(status) && WEXITSTATUS(status) == 0, reason);
      return;
    }
    Check(result == 0 || (result < 0 && errno == EINTR), "wait for bounded spool fixture child");
    if (g_get_monotonic_time() >= deadline) {
      kill(child, SIGKILL);
      waitpid(child, nullptr, 0);
      throw std::runtime_error("offline preparation child exceeded three-second bound");
    }
    usleep(1000);
  }
}

void SpoolReadable(const std::string& spool, const std::vector<SavedSpoolFile>& files,
    Account account) {
  pid_t child = fork();
  Check(child >= 0, "fork real nonroot spool access verification");
  if (child == 0) {
    try {
      Check(setgroups(0, nullptr) == 0 && setgid(account.group) == 0 && setuid(account.user) == 0 &&
          geteuid() == account.user && geteuid() != 0, "drop to real selected spool reader");
      Check(access(spool.c_str(), W_OK) < 0, "selected account cannot replace spool entries");
      for (const auto& file : files) {
        Check(Read(file.path) == file.bytes, "selected account reads complete record/orphan bytes");
        Descriptor write_access(open(file.path.c_str(), O_WRONLY | O_NOFOLLOW | O_CLOEXEC));
        Check(write_access.Get() < 0 && errno == EACCES,
            "actual nonroot write open of root-owned spool must fail");
      }
      _exit(0);
    } catch (const std::exception& error) {
      fprintf(stderr, "nonroot offline spool verification failed: %s\n", error.what());
      _exit(1);
    }
  }
  ChildCompleted(child, "selected account must read but never write offline spool");
}

void OfflineSpool(Account account) {
  Fixture fixture(account);
  auto spool = Spool(fixture);
  // These bytes deliberately are not valid Parcel payloads. Root preparation
  // handles metadata only; the daemon validates payloads before opening its DB.
  const char opaque[] = {'o', 'p', 'a', 'q', 'u', 'e', '\0', '\xff', '\x01'};
  std::vector<SavedSpoolFile> files;
  files.push_back(SaveSpoolFile(spool + "/" + RecordName(1), std::string(opaque, sizeof(opaque))));
  files.push_back(SaveSpoolFile(spool + "/" + PendingName(1), std::string("\0\x01", 2)));
  files.push_back(SaveSpoolFile(spool + "/" + PendingName(2), ""));
  for (unsigned attempt = 0; attempt < 2; ++attempt) {
    fixture.Migrate();
    fixture.Preserved();
    struct stat directory = {};
    Check(stat(spool.c_str(), &directory) == 0 && directory.st_uid == 0 &&
        directory.st_gid == account.group && (directory.st_mode & 07777) == 0750,
        "prepared spool directory is root:daemon0750");
    for (const auto& file : files)
      CheckSpoolFile(file, account.group, 0640);
    SpoolReadable(spool, files, account);
  }
  std::cout << "PASS offline record and partial/empty orphan bytes/inodes survive preparation/retry; actual selected UID reads but cannot write\n";
}

void OfflineSpoolDurability(Account account) {
  Fixture fixture(account);
  auto spool = Spool(fixture);
  auto record = SaveSpoolFile(spool + "/" + RecordName(1), "opaque bounded record");
  struct stat directory = {};
  Check(stat(spool.c_str(), &directory) == 0, "identify spool durability boundary");
  sync_device = directory.st_dev;
  sync_inode = directory.st_ino;
  parent_sync_calls = 0;
  fail_parent_sync = true;
  fixture.Refuse();
  Check(parent_sync_calls == 1, "first spool directory synchronization failed");
  CheckSpoolFile(record, account.group, 0640);
  fixture.Refuse();
  Check(parent_sync_calls == 2, "retry must repeat spool synchronization after partial metadata transfer");
  fail_parent_sync = false;
  fixture.Migrate();
  Check(parent_sync_calls >= 3, "successful retry includes a successful spool synchronization");
  sync_inode = 0;
  CheckSpoolFile(record, account.group, 0640);
  fixture.Preserved();
  std::cout << "PASS offline spool fsync failure remains fenced and retries without replacing bytes or inodes\n";
}

void OfflineSpoolRefusals(Account account) {
  using namespace consent::offline;
  for (const char* failure : {"fifo", "unknown", "symlink", "hardlink", "oversize", "foreign",
      "writable", "records", "entries", "bytes"}) {
    Fixture fixture(account);
    auto spool = Spool(fixture);
    auto valid = SaveSpoolFile(spool + "/" + RecordName(1), "unchanged sibling");
    auto outside = SaveSpoolFile(fixture.directory + "/outside", "outside remains unchanged");
    auto bad = spool + "/" + RecordName(2);
    if (!std::strcmp(failure, "fifo")) {
      Check(mkfifo(bad.c_str(), 0600) == 0, "create FIFO with valid offline record name");
    } else if (!std::strcmp(failure, "unknown")) {
      Write(spool + "/unexpected-entry", "unknown file");
    } else if (!std::strcmp(failure, "symlink")) {
      Check(symlink(outside.path.c_str(), bad.c_str()) == 0, "create offline symlink to outside file");
    } else if (!std::strcmp(failure, "hardlink")) {
      Check(link(outside.path.c_str(), bad.c_str()) == 0, "create offline hardlink to outside file");
    } else if (!std::strcmp(failure, "oversize")) {
      Write(bad, std::string(kMaxRegistrationFileBytes + 1, 'x'));
    } else if (!std::strcmp(failure, "foreign")) {
      Write(bad, "nonroot file");
      Check(chown(bad.c_str(), account.user, account.group) == 0, "create nonroot-owned offline file");
    } else if (!std::strcmp(failure, "writable")) {
      Write(bad, "writable file");
      Check(chmod(bad.c_str(), 0660) == 0, "create group-writable offline file");
    } else if (!std::strcmp(failure, "records")) {
      for (unsigned i = 2; i <= kMaxRegistrations + 1; ++i)
        Write(spool + "/" + RecordName(i), "");
    } else if (!std::strcmp(failure, "entries")) {
      for (unsigned i = 0; i < kMaxRegistrationEntries; ++i)
        Write(spool + "/" + PendingName(i), "");
    } else {
      // Include orphan bytes in the same aggregate budget as committed records.
      for (unsigned i = 0; i < kMaxRegistrationBytes / kMaxRegistrationFileBytes; ++i)
        Write(spool + "/" + PendingName(i), std::string(kMaxRegistrationFileBytes, 'x'));
    }
    pid_t child = fork();
    Check(child >= 0, "fork bounded rejected spool preparation");
    if (child == 0) {
      try { fixture.Migrate(); } catch (const std::exception&) { _exit(0); }
      _exit(1);
    }
    ChildCompleted(child, "invalid offline spool must fail before publishing readiness");
    CheckSpoolFile(valid, 0, 0600);
    if (!std::strcmp(failure, "hardlink"))
      Check(unlink(bad.c_str()) == 0, "remove only fixture-created hardlink before reading outside file");
    CheckSpoolFile(outside, 0, 0600);
    struct stat directory = {};
    Check(stat(spool.c_str(), &directory) == 0 && directory.st_gid == 0 &&
        (directory.st_mode & 07777) == 0700, "rejected spool remains unchanged before metadata transfer");
    Check(Read(fixture.state + "/installations.conf") == fixture.installation,
        "rejected spool preserves legacy authority before migration");
  }
  std::cout << "PASS offline FIFO/unknown/symlink/hardlink/oversize/ownership/mode and entry/record/byte limits fail closed within bounded child wait\n";
}
}  // namespace

int main(int argc, char** argv) {
  if (geteuid() != 0) {
    std::cout << "SKIP real ownership migration requires root; this fixture does not simulate root or account ownership\n";
    return 77;
  }
  try {
    Check(argc == 1 || (argc == 3 && std::strcmp(argv[1], "--account") == 0),
        "Usage: storage-prepare-test [--account EXISTING_NONROOT_ACCOUNT]");
    const char* name = argc == 3 ? argv[2] : "nobody";
    auto* record = getpwnam(name);
    Check(record && record->pw_uid != 0, "a real nonroot fixture account must exist");
    Account account{record->pw_uid, record->pw_gid};
    Migration(account);
    Interrupted(account);
    DirectoryDurability(account);
    Refusals(account);
    OfflineSpool(account);
    OfflineSpoolDurability(account);
    OfflineSpoolRefusals(account);
    std::cout << "NOTE this private fixture omits production systemctl and SMACK setup; real service startup and labels are validated separately\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
