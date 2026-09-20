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
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <sqlite3.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// These symbols exist only in this executable. The production repository has
// no environment flag, runtime switch or fault-injection API.
int failed_directory_syncs = 0;
dev_t registry_device = 0;
ino_t registry_inode = 0;
bool corrupt_next_definition_read = false;
bool corrupt_next_incarnation_read = false;
sqlite3* corrupt_metadata_connection = nullptr;
int failed_definition_reads = 0;
int definition_failure_code = SQLITE_OK;

void Check(bool condition, const char* description) {
  if (!condition)
    throw std::runtime_error(description);
}

using consent::Message;
using consentd::Repository;

consentd::Peer Peer() {
  consentd::Peer peer;
  peer.identity = "enforcer";
  peer.instance = "test-instance";
  peer.roles = {"installer", "checker", "argo", "ui"};
  peer.packages = {"package"};
  peer.subjects = {"subject"};
  peer.profiles = {"profile"};
  return peer;
}

Message Definition(int version) {
  return {{"method", "register"}, {"package", "package"}, {"app", "app"},
      {"definition", "definition"}, {"enforcer", "enforcer"},
      {"operation_id", "operation" + std::to_string(version)},
      {"_install_identity", "generation"}, {"policy_version", std::to_string(version)},
      {"text_revision", "1"}, {"level", "1"}, {"modes", "PERSISTENT"},
      {"default_locale", "en"}, {"message.en.title", "Title"}, {"message.en.body", "Body"}};
}

Message Query() {
  return {{"method", "check"}, {"subject", "subject"}, {"profile", "profile"},
      {"count", "1"}, {"r0.definition", "definition"}, {"r0.operation", "read"},
      {"r0.scope", "scope"}, {"r0.purpose", "purpose"}};
}

void Approve(Repository* repository) {
  Message request = Query();
  request["method"] = "request";
  request["client_request_id"] = "persistent-before-storage-errors";
  request["operation_id"] = "persistent-before-storage-errors";
  auto pending = repository->Execute(Peer(), request);
  if (consent::Get(pending, "status") != "0")
    std::cerr << "approval fixture: " << consent::Get(pending, "reason") << '\n';
  Check(consent::Get(pending, "status") == "0" &&
      consent::Get(pending, "decision") == "PENDING", "create persistent approval request");
  Message response = {{"method", "get_prompt"}, {"locale", "en"},
      {"request_id", consent::Get(pending, "request_id")}};
  auto prompt = repository->Execute(Peer(), response);
  Check(consent::Get(prompt, "status") == "0", "get approval prompt");
  response["method"] = "respond";
  response["prompt_token"] = consent::Get(prompt, "prompt_token");
  response["decision"] = "ALLOWED";
  response["grant_mode"] = "PERSISTENT";
  Check(consent::Get(repository->Execute(Peer(), response), "status") == "0",
      "record persistent approval before generic storage failures");
  Check(consent::Get(repository->Execute(Peer(), Query()), "decision") == "ALLOWED",
      "persistent approval baseline exists");
}

int RetiredFiles(const std::string& directory) {
  DIR* entries = opendir(directory.c_str());
  Check(entries != nullptr, "inspect isolated quarantine inventory");
  int count = 0;
  while (auto* entry = readdir(entries)) {
    if (std::strstr(entry->d_name, ".retired-"))
      ++count;
  }
  closedir(entries);
  return count;
}

std::unique_ptr<Repository> Create(const std::string& directory) {
  std::unique_ptr<Repository> repository(new Repository(directory + "/consent.db",
      directory + "/registry"));
  repository->SetInstallationValidator([](const std::string& package,
      const std::string& app, const std::string& generation) {
    return package == "package" && app == "app" && generation == "generation";
  });
  return repository;
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

}  // namespace

extern "C" __attribute__((visibility("default"))) int fsync(int fd) {
  struct stat st = {};
  if (failed_directory_syncs > 0 && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) &&
      st.st_dev == registry_device && st.st_ino == registry_inode) {
    --failed_directory_syncs;
    errno = EIO;
    return -1;
  }
  return static_cast<int>(syscall(SYS_fsync, fd));
}

extern "C" __attribute__((visibility("default"))) int sqlite3_step(sqlite3_stmt* statement) {
  using Step = int (*)(sqlite3_stmt*);
  static Step real_step = reinterpret_cast<Step>(dlsym(RTLD_NEXT, "sqlite3_step"));
  if (!real_step)
    _exit(99);
  const char* sql = sqlite3_sql(statement);
  if (failed_definition_reads > 0 && sql &&
      std::strstr(sql, "SELECT config FROM definitions WHERE id=") == sql) {
    --failed_definition_reads;
    return definition_failure_code;
  }
  if (sql && std::strcmp(sql, "SELECT value FROM meta WHERE key='incarnation'") == 0) {
    if (corrupt_next_incarnation_read) {
      corrupt_next_incarnation_read = false;
      corrupt_metadata_connection = sqlite3_db_handle(statement);
    }
    if (sqlite3_db_handle(statement) == corrupt_metadata_connection)
      return SQLITE_CORRUPT;  // Sticky until the original connection is retired.
  }
  if (corrupt_next_definition_read && sql &&
      std::strstr(sql, "SELECT config FROM definitions WHERE id=") == sql) {
    corrupt_next_definition_read = false;
    // Intentionally leave sqlite3_errcode(db) unchanged: only the thrown
    // error's captured return code can preserve this failure across ROLLBACK.
    return SQLITE_CORRUPT;
  }
  return real_step(statement);
}

extern "C" __attribute__((visibility("default"))) int sqlite3_close(sqlite3* database) {
  using Close = int (*)(sqlite3*);
  static Close real_close = reinterpret_cast<Close>(dlsym(RTLD_NEXT, "sqlite3_close"));
  if (!real_close)
    _exit(99);
  int status = real_close(database);
  if (status == SQLITE_OK && database == corrupt_metadata_connection)
    corrupt_metadata_connection = nullptr;
  return status;
}

int main() {
  std::string directory;
  try {
    char pattern[] = "/tmp/consent-repository-fault-XXXXXX";
    char* created = mkdtemp(pattern);
    Check(created != nullptr, "create fault test directory");
    directory = created;
    Check(mkdir((directory + "/registry").c_str(), 0700) == 0, "create registry directory");
    struct stat st = {};
    Check(stat((directory + "/registry").c_str(), &st) == 0, "stat registry directory");
    registry_device = st.st_dev;
    registry_inode = st.st_ino;
    auto repository = Create(directory);
    std::string error;
    Check(repository->Open(&error), error.c_str());
    Check(consent::Get(repository->Execute(Peer(), Definition(1)), "status") == "0", "initial registration");

    failed_directory_syncs = 2;
    Check(consent::Get(repository->Execute(Peer(), Definition(2)), "status") == "-2006",
        "rename followed by failed fsync must not report success");
    Check(consent::Get(repository->Execute(Peer(), Query()), "status") == "-2006",
        "readable registry must remain fenced until successful durability barrier");
    auto replayed = repository->Execute(Peer(), Query());
    Check(consent::Get(replayed, "status") == "0" &&
        consent::Get(replayed, "r0.policy_version") == "2",
        "successful barrier permits newest desired-state projection");

    failed_directory_syncs = 1;
    Check(consent::Get(repository->Execute(Peer(), Definition(3)), "status") == "-2006",
        "second uncertain registry commit");
    repository.reset();
    repository = Create(directory);
    failed_directory_syncs = 1;
    Check(!repository->Open(&error), "restart must also establish successful durability barrier");
    repository.reset();
    repository = Create(directory);
    Check(repository->Open(&error), error.c_str());
    auto restarted = repository->Execute(Peer(), Query());
    Check(consent::Get(restarted, "r0.policy_version") == "3", "restart replays revision after barrier");
    std::cout << "PASS directory fsync uncertainty stays fenced across retry and restart\n";

    Approve(repository.get());
    struct stat original = {};
    Check(lstat((directory + "/consent.db").c_str(), &original) == 0, "record original DB identity");
    int retired = RetiredFiles(directory);
    for (int code : {SQLITE_FULL, SQLITE_IOERR_WRITE}) {
      auto original_epoch = consent::Get(repository->Snapshot(), "epoch");
      definition_failure_code = code;
      failed_definition_reads = 2;
      for (int attempt = 0; attempt < 2; ++attempt) {
        auto failure = repository->Execute(Peer(), Query());
        Check(consent::Get(failure, "status") == "-2006" &&
            consent::Get(failure, "epoch") == original_epoch,
            "generic SQL failure remains explicit and does not retire epoch");
        struct stat current = {};
        Check(lstat((directory + "/consent.db").c_str(), &current) == 0 &&
            current.st_dev == original.st_dev && current.st_ino == original.st_ino &&
            RetiredFiles(directory) == retired,
            "FULL/IOERR must not replace or quarantine the existing database");
      }
      auto retry = repository->Execute(Peer(), Query());
      Check(consent::Get(retry, "status") == "0" &&
          consent::Get(retry, "epoch") == original_epoch &&
          consent::Get(retry, "decision") == "ALLOWED" &&
          consent::Get(retry, "r0.policy_version") == "3",
          "successful retry preserves the durable approval and definition");
    }
    std::cout << "PASS SQLITE_FULL/SQLITE_IOERR_WRITE preserve inode, epoch, grants and quarantine inventory\n";

    auto epoch = consent::Get(repository->Snapshot(), "epoch");
    corrupt_next_definition_read = true;
    Check(consent::Get(repository->Execute(Peer(), Query()), "status") == "-2006",
        "original SQLITE_CORRUPT becomes storage error");
    auto recovered = repository->Execute(Peer(), Query());
    Check(consent::Get(recovered, "status") == "0" &&
        consent::Get(recovered, "epoch") != epoch &&
        consent::Get(recovered, "decision") == "CONSENT_REQUIRED",
        "captured SQLite failure survives rollback and triggers conservative recovery");
    std::cout << "PASS captured SQLite corruption code survives rollback\n";
    epoch = consent::Get(repository->Snapshot(), "epoch");
    corrupt_next_incarnation_read = true;
    Check(consent::Get(repository->Execute(Peer(), Query()), "status") == "-2006",
        "incarnation metadata corruption becomes explicit storage failure");
    recovered = repository->Execute(Peer(), Query());
    Check(consent::Get(recovered, "status") == "0" &&
        consent::Get(recovered, "epoch") != epoch,
        "latched corruption is retired before another metadata query");
    std::cout << "PASS metadata corruption cannot trap recovery in repeated failing SELECT\n";
    repository.reset();
    RemoveFiles(directory);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    if (!directory.empty())
      RemoveFiles(directory);
    return 1;
  }
}
