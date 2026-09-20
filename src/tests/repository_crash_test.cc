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
#include "consentd/repository.hh"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sqlite3.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

enum class Boundary { kNone, kJournalSync, kDatabaseSync, kCommitted };
Boundary boundary = Boundary::kNone;
bool revocation_started = false;
bool release_writer = false;
int gate_write = -1;
int gate_read = -1;
char database_path[512] = {};

void Gate() {
  const char ready = 'R';
  if (write(gate_write, &ready, 1) != 1)
    _exit(95);
  char command = 0;
  ssize_t received;
  do {
    received = read(gate_read, &command, 1);
  } while (received < 0 && errno == EINTR);
  if (release_writer && received == 1 && command == 'C') {
    // Permit only the explicit live-writer scenario to resume its original
    // SQLite call. Recovery must not pause again at a later fsync.
    boundary = Boundary::kNone;
    revocation_started = false;
    return;
  }
  _exit(96);  // The parent must SIGKILL this process while it is paused.
}

void MaybeGate(int fd) {
  if (!revocation_started || boundary == Boundary::kNone || boundary == Boundary::kCommitted)
    return;
  char proc[64];
  char target[1024];
  snprintf(proc, sizeof(proc), "/proc/self/fd/%d", fd);
  ssize_t size = readlink(proc, target, sizeof(target) - 1);
  if (size <= 0)
    return;
  target[size] = '\0';
  if ((boundary == Boundary::kJournalSync && std::strstr(target, "consent.db-journal")) ||
      (boundary == Boundary::kDatabaseSync && std::strcmp(target, database_path) == 0))
    Gate();
}

void Check(bool condition, const char* description) {
  if (!condition)
    throw std::runtime_error(description);
}

using consent::Message;
using consentd::Repository;

consentd::Peer Peer() {
  consentd::Peer peer;
  peer.identity = "test";
  peer.instance = "test-instance";
  peer.roles = {"installer", "checker", "argo", "ui", "admin"};
  peer.packages = {"package"};
  peer.subjects = {"subject"};
  peer.profiles = {"profile"};
  return peer;
}

std::unique_ptr<Repository> Open(const std::string& directory) {
  std::unique_ptr<Repository> repository(new Repository(directory + "/consent.db",
      directory + "/registry"));
  repository->SetInstallationValidator([](const std::string& package,
      const std::string& app, const std::string& generation) {
    return package == "package" && app == "app" && generation == "generation";
  });
  std::string error;
  Check(repository->Open(&error), error.c_str());
  return repository;
}

Message Call(Repository* repository, Message request) {
  auto response = repository->Execute(Peer(), request);
  if (consent::Get(response, "status") != "0")
    throw std::runtime_error(consent::Get(response, "reason"));
  return response;
}

Message Query() {
  return {{"method", "check"}, {"subject", "subject"}, {"profile", "profile"},
      {"count", "1"}, {"r0.definition", "definition"}, {"r0.operation", "read"},
      {"r0.scope", "scope"}, {"r0.purpose", "purpose"},
      {"client_request_id", "request"}, {"operation_id", "operation"}};
}

void RemoveFiles(const std::string& directory) {
  for (const auto& path : {directory + "/registry", directory}) {
    DIR* entries = opendir(path.c_str());
    if (!entries)
      continue;
    while (auto* entry = readdir(entries)) {
      if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
        continue;
      unlink((path + "/" + entry->d_name).c_str());
    }
    closedir(entries);
  }
  rmdir((directory + "/registry").c_str());
  rmdir(directory.c_str());
}

void Scenario(const std::string& state_root, Boundary selected, bool delete_during_write = false,
    bool continue_writer = false) {
  Check(!continue_writer || (delete_during_write && selected == Boundary::kDatabaseSync),
      "live writer requires verified main-DB synchronization boundary");
  std::string path_template = state_root + "/consent-repository-crash-XXXXXX";
  std::vector<char> pattern(path_template.begin(), path_template.end());
  pattern.push_back('\0');
  char* created = mkdtemp(pattern.data());
  Check(created != nullptr, "create crash test state");
  std::string directory(created);
  Check(mkdir((directory + "/registry").c_str(), 0700) == 0, "create protected registry");
  int length = snprintf(database_path, sizeof(database_path), "%s/consent.db", directory.c_str());
  Check(length > 0 && static_cast<size_t>(length) < sizeof(database_path), "test state path is too long");
  auto repository = Open(directory);
  Call(repository.get(), {{"method", "register"}, {"package", "package"}, {"app", "app"},
      {"definition", "definition"}, {"enforcer", "test"}, {"operation_id", "install"},
      {"_install_identity", "generation"}, {"policy_version", "1"}, {"text_revision", "1"},
      {"level", "1"}, {"modes", "PERSISTENT"}, {"default_locale", "en"},
      {"message.en.title", "Read"}, {"message.en.body", "Read the displayed scope"}});
  auto request = Query();
  request["method"] = "request";
  auto pending = Call(repository.get(), request);
  Message response = {{"method", "get_prompt"}, {"request_id", consent::Get(pending, "request_id")},
      {"locale", "en"}};
  auto prompt = Call(repository.get(), response);
  response["method"] = "respond";
  response["prompt_token"] = consent::Get(prompt, "prompt_token");
  response["decision"] = "ALLOWED";
  response["grant_mode"] = "PERSISTENT";
  Call(repository.get(), response);
  Check(consent::Get(Call(repository.get(), Query()), "decision") == "ALLOWED", "baseline grant exists");
  repository.reset();

  int ready[2];
  int release[2];
  Check(pipe(ready) == 0 && pipe(release) == 0, "create deterministic crash gates");
  pid_t child = fork();
  Check(child >= 0, "fork isolated writer");
  if (child == 0) {
    close(ready[0]);
    close(release[1]);
    gate_write = ready[1];
    gate_read = release[0];
    release_writer = continue_writer;
    try {
      auto writer = Open(directory);
      auto old_epoch = consent::Get(writer->Snapshot(), "epoch");
      boundary = selected;
      auto response = writer->Execute(Peer(), {{"method", "revoke"}, {"subject", "subject"},
          {"profile", "profile"}, {"definition", "definition"}});
      if (continue_writer) {
        Check(consent::Get(response, "status") != "0",
            "deleted DB write cannot publish success from its retired handle");
        auto recovered = Call(writer.get(), Query());
        Check(consent::Get(recovered, "decision") == "CONSENT_REQUIRED" &&
            consent::Get(recovered, "epoch") != old_epoch,
            "same live repository recovers a new epoch with no old approval");
        writer.reset();
        const char complete = 'L';
        Check(write(gate_write, &complete, 1) == 1, "report live-writer recovery");
        _exit(0);
      }
      Check(consent::Get(response, "status") == "0", "revocation failed before selected boundary");
    } catch (const std::exception& error) {
      fprintf(stderr, "writer failed before gate: %s\n", error.what());
      _exit(94);
    }
    _exit(93);  // Selected SQLite write boundary was not observed.
  }
  close(ready[1]);
  close(release[0]);
  pollfd waiting{ready[0], POLLIN, 0};
  int observed = poll(&waiting, 1, 5000);
  char event = 0;
  bool paused = observed > 0 && (waiting.revents & POLLIN) && read(ready[0], &event, 1) == 1 && event == 'R';
  bool valid_hot_header = true;
  if (paused && selected == Boundary::kDatabaseSync) {
    const unsigned char expected[] = {0xd9, 0xd5, 0x05, 0xf9, 0x20, 0xa1, 0x63, 0xd7};
    unsigned char header[8] = {};
    std::string journal_path = std::string(database_path) + "-journal";
    int journal = open(journal_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat journal_stat = {};
    valid_hot_header = journal >= 0 && fstat(journal, &journal_stat) == 0 &&
        journal_stat.st_size > 512 &&
        read(journal, header, sizeof(header)) == static_cast<ssize_t>(sizeof(header)) &&
        std::memcmp(header, expected, sizeof(header)) == 0;
    if (journal >= 0)
      close(journal);
  }
  bool deleted = !delete_during_write || (paused && valid_hot_header && unlink(database_path) == 0);
  bool continued = false;
  if (continue_writer && paused && valid_hot_header && deleted) {
    const char command = 'C';
    if (write(release[1], &command, 1) == 1) {
      waiting.revents = 0;
      observed = poll(&waiting, 1, 5000);
      continued = observed > 0 && (waiting.revents & POLLIN) &&
          read(ready[0], &event, 1) == 1 && event == 'L';
    }
  }
  if (!continued)
    kill(child, SIGKILL);
  int status = 0;
  while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
  close(ready[0]);
  close(release[1]);
  if (!paused)
    fprintf(stderr, "gate not observed: boundary=%d poll=%d events=%d child_status=%d started=%d\n",
        static_cast<int>(selected), observed, waiting.revents, status, revocation_started);
  if (continue_writer) {
    Check(paused && valid_hot_header && deleted && continued && WIFEXITED(status) &&
        WEXITSTATUS(status) == 0, "same writer continues after active-write unlink and recovers");
  } else {
    Check(paused && valid_hot_header && deleted && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
        "writer paused at selected boundary and killed before continuing");
  }

  repository = Open(directory);
  auto recovered = Call(repository.get(), Query());
  const char* expected = delete_during_write || selected == Boundary::kCommitted ?
      "CONSENT_REQUIRED" : "ALLOWED";
  Check(consent::Get(recovered, "decision") == expected,
      "uncommitted revocation rolls back; committed revocation remains effective");
  repository.reset();
  sqlite3* database = nullptr;
  Check(sqlite3_open(database_path, &database) == SQLITE_OK, "open recovered database");
  sqlite3_stmt* integrity = nullptr;
  Check(sqlite3_prepare_v2(database, "PRAGMA integrity_check", -1, &integrity, nullptr) == SQLITE_OK,
      "prepare integrity check");
  Check(sqlite3_step(integrity) == SQLITE_ROW &&
      std::strcmp(reinterpret_cast<const char*>(sqlite3_column_text(integrity, 0)), "ok") == 0,
      "write interruption preserves database integrity");
  sqlite3_finalize(integrity);
  sqlite3_close(database);
  RemoveFiles(directory);
  std::cout << "PASS " << (continue_writer ? "live-writer" : "SIGKILL")
      << " boundary=" << static_cast<int>(selected)
      << " validated_hot_journal=" << (selected == Boundary::kDatabaseSync)
      << " delete_during_write=" << delete_during_write
      << " decision=" << expected << " integrity=ok\n";
}

}  // namespace

// Link-time test interposition only: no production fault controls are compiled.
extern "C" __attribute__((visibility("default"))) int fsync(int fd) {
  MaybeGate(fd);
  return static_cast<int>(syscall(SYS_fsync, fd));
}

extern "C" __attribute__((visibility("default"))) int fdatasync(int fd) {
  MaybeGate(fd);
  return static_cast<int>(syscall(SYS_fdatasync, fd));
}

extern "C" __attribute__((visibility("default"))) int sqlite3_step(sqlite3_stmt* statement) {
  using Step = int (*)(sqlite3_stmt*);
  static Step real_step = reinterpret_cast<Step>(dlsym(RTLD_NEXT, "sqlite3_step"));
  if (!real_step)
    _exit(99);
  const char* sql = sqlite3_sql(statement);
  if (boundary != Boundary::kNone && sql &&
      std::strstr(sql, "UPDATE grants SET revoked=1 WHERE definition=? AND subject=?") == sql)
    revocation_started = true;
  return real_step(statement);
}

extern "C" __attribute__((visibility("default"))) int sqlite3_exec(sqlite3* database, const char* sql,
    int (*callback)(void*, int, char**, char**), void* data, char** error) {
  using Exec = int (*)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
  static Exec real_exec = reinterpret_cast<Exec>(dlsym(RTLD_NEXT, "sqlite3_exec"));
  if (!real_exec)
    _exit(99);
  int result = real_exec(database, sql, callback, data, error);
  if (result == SQLITE_OK && revocation_started && boundary == Boundary::kCommitted &&
      std::strcmp(sql, "COMMIT") == 0)
    Gate();
  return result;
}

int main(int argc, char* argv[]) {
  try {
    Check(argc == 1 || (argc == 3 && std::strcmp(argv[1], "--state-root") == 0),
        "usage: repository-crash-test [--state-root /absolute/existing/directory]");
    std::string state_root = argc == 3 ? argv[2] : "/tmp";
    Check(!state_root.empty() && state_root[0] == '/' && state_root.size() < 400,
        "state root must be an absolute path shorter than 400 bytes");
    while (state_root.size() > 1 && state_root.back() == '/')
      state_root.pop_back();
    struct stat root = {};
    Check(lstat(state_root.c_str(), &root) == 0 && S_ISDIR(root.st_mode),
        "state root must be an existing nonsymlink directory");
    std::cout << "Test state root: " << state_root << '\n';
    Scenario(state_root, Boundary::kJournalSync);
    Scenario(state_root, Boundary::kDatabaseSync);
    Scenario(state_root, Boundary::kCommitted);
    Scenario(state_root, Boundary::kDatabaseSync, true);
    Scenario(state_root, Boundary::kDatabaseSync, true, true);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
