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
  return Contents(file);
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
    std::cout << "NOTE this private fixture omits production systemctl and SMACK setup; real service startup and labels are validated separately\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
