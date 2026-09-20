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
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using consent::Message;

// Faults belong only to this executable. They run immediately after SQLite's
// real COMMIT succeeds, before the repository can validate/publish a response.
const char* arm_statement = nullptr;
bool target_transaction = false;
std::string* generation_to_rotate = nullptr;
int rotation_count = 0;

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

class Fixture final {
 public:
  Fixture() {
    char pattern[] = "/tmp/consent-provenance-XXXXXX";
    char* created = mkdtemp(pattern);
    Check(created != nullptr, "create provenance fixture");
    directory = created;
    Check(mkdir((directory + "/registry").c_str(), 0700) == 0, "create registry");
    peer.identity = "actor";
    peer.instance = "actor-instance";
    peer.roles = {"installer", "argo", "ui", "checker", "holder", "session", "admin"};
    peer.subjects = {"subject"};
    peer.profiles = {"profile"};
    peer.packages = {"pkg-a", "pkg-b", "pkg-c"};
    generations = {{"pkg-a", "a-generation"}, {"pkg-b", "b-generation"}, {"pkg-c", "c-generation"}};
    repository.reset(new consentd::Repository(directory + "/consent.db", directory + "/registry"));
    repository->SetInstallationValidator([this](const std::string& package,
        const std::string& app, const std::string& generation) {
      auto it = generations.find(package);
      return it != generations.end() && app == package + ".app" && it->second == generation;
    });
    std::string error;
    if (!repository->Open(&error))
      throw std::runtime_error(error);
    for (const char* id : {"a", "b", "c"}) {
      std::string package = std::string("pkg-") + id;
      Call({{"method", "register"}, {"package", package}, {"app", package + ".app"},
          {"definition", id}, {"enforcer", "actor"}, {"operation_id", package},
          {"_install_identity", generations[package]}, {"policy_version", "1"},
          {"text_revision", "1"}, {"level", "1"}, {"modes", "ONCE,TIMED,PERSISTENT"},
          {"retention_ms", "600000"}, {"default_locale", "en"},
          {"message.en.title", "Read"}, {"message.en.body", "Read displayed scope"}});
    }
    auto opened = Call({{"method", "session_open"}, {"subject", "subject"}, {"profile", "profile"}});
    session = consent::Get(opened, "session");
  }

  ~Fixture() {
    arm_statement = nullptr;
    generation_to_rotate = nullptr;
    target_transaction = false;
    repository.reset();
    for (const auto& path : {directory + "/registry", directory}) {
      DIR* entries = opendir(path.c_str());
      if (!entries)
        continue;
      while (auto* entry = readdir(entries)) {
        if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, ".."))
          unlink((path + "/" + entry->d_name).c_str());
      }
      closedir(entries);
    }
    rmdir((directory + "/registry").c_str());
    rmdir(directory.c_str());
  }

  Message Call(const Message& request, int expected = 0) {
    auto result = repository->Execute(peer, request);
    if (consent::Get(result, "status") != std::to_string(expected)) {
      std::cerr << consent::Get(request, "method") << ": " << consent::Get(result, "status")
                << " " << consent::Get(result, "reason") << '\n';
      throw std::runtime_error("unexpected provenance result status");
    }
    return result;
  }

  Message Data(const char* method) const {
    return {{"method", method}, {"subject", "subject"}, {"profile", "profile"},
        {"session", session}, {"generation", "1"}, {"scope", "scope"}, {"purpose", "purpose"}};
  }

  Message Acquisition(const char* definition, const char* mode = "ONCE") {
    std::string tag = std::string(definition) + std::to_string(++sequence);
    auto request = Data("request");
    request.insert({{"count", "1"}, {"r0.definition", definition}, {"r0.scope", "scope"},
        {"r0.purpose", "purpose"}, {"r0.operation", "read"}, {"r0.holder", "actor"},
        {"client_request_id", tag}, {"operation_id", tag}, {"step_id", "step"}});
    auto pending = Call(request);
    Check(consent::Get(pending, "decision") == "PENDING", "acquisition requires fresh approval");
    auto prompt = Call({{"method", "get_prompt"}, {"request_id", consent::Get(pending, "request_id")},
        {"locale", "en"}});
    Call({{"method", "respond"}, {"request_id", consent::Get(pending, "request_id")},
        {"prompt_token", consent::Get(prompt, "prompt_token")}, {"decision", "ALLOWED"},
        {"grant_mode", mode}, {"duration_ms", "1000"}});
    request["method"] = "check";
    request["mode"] = "AUTHORIZE";
    auto receipt = Call(request);
    Check(consent::Get(receipt, "decision") == "ALLOWED", "acquisition consumes access grant");
    auto data = Data("data_register");
    data["receipt"] = consent::Get(receipt, "receipt");
    return data;
  }

  Message Derived(const Message& first, const Message* second = nullptr) const {
    auto request = Data("data_register_derived");
    request["count"] = second ? "2" : "1";
    request["parent0"] = consent::Get(first, "artifact");
    if (second)
      request["parent1"] = consent::Get(*second, "artifact");
    return request;
  }

  void Use(const Message& artifact, bool alias = false, int expected = 0) {
    auto request = Data(alias ? "check" : "data_check");
    request["artifact"] = consent::Get(artifact, "artifact");
    if (alias)
      request["operation"] = "reuse-data";
    auto result = Call(request, expected);
    if (!expected)
      Check(consent::Get(result, "decision") == "ALLOWED", "retained artifact remains usable");
  }

  std::string directory;
  std::string session;
  std::map<std::string, std::string> generations;
  consentd::Peer peer;
  std::unique_ptr<consentd::Repository> repository;
  int sequence = 0;
};

void TransitiveGeneration() {
  Fixture fixture;
  auto first = fixture.Call(fixture.Acquisition("a"));
  auto second = fixture.Call(fixture.Acquisition("b"));
  auto unrelated = fixture.Call(fixture.Acquisition("c"));
  auto child = fixture.Call(fixture.Derived(first, &second));
  auto descendant = fixture.Call(fixture.Derived(child));
  Check(consent::Number(child, "expires") == std::min(consent::Number(first, "expires"),
      consent::Number(second, "expires")) &&
      consent::Get(descendant, "expires") == consent::Get(child, "expires"),
      "transitive derivative inherits earliest TTL without extension");
  fixture.Use(descendant);
  fixture.Use(descendant, true);
  auto repeat = fixture.Call(fixture.Derived(first));
  Check(consent::Get(repeat, "expires") == consent::Get(first, "expires"),
      "consumed ONCE permits reuse and derivation with fixed retention");
  fixture.generations["pkg-b"] = "reinstalled-b";
  // Deliberately omit Tick: external authority changes must be checked now.
  fixture.Call(fixture.Derived(first, &second), -ESTALE);
  fixture.Call(fixture.Derived(child), -ESTALE);
  for (const auto& item : {second, child, descendant}) {
    fixture.Use(item, false, -ESTALE);
    fixture.Use(item, true, -ESTALE);
  }
  fixture.Use(first);
  fixture.Use(unrelated);
  fixture.Call(fixture.Derived(unrelated));
  std::cout << "PASS no-Tick B-only rotation denies transitive provenance; unrelated package and ONCE retention survive\n";
}

void TimedRetention() {
  Fixture fixture;
  auto artifact = fixture.Call(fixture.Acquisition("a", "TIMED"));
  usleep(1100000);
  fixture.Use(artifact);
  auto child = fixture.Call(fixture.Derived(artifact));
  Check(consent::Get(child, "expires") == consent::Get(artifact, "expires"),
      "expired access grant does not extend or remove retained-data TTL");
  std::cout << "PASS TIMED access expiry leaves independent artifact retention valid\n";
}

void CommitBoundary(const char* method) {
  Fixture fixture;
  auto registration = fixture.Acquisition("a");
  Message parent;
  Message request = registration;
  if (std::strcmp(method, "data_register") != 0) {
    parent = fixture.Call(registration);
    if (std::strcmp(method, "data_register_derived") == 0) {
      request = fixture.Derived(parent);
    } else {
      request = fixture.Data(method);
      request["artifact"] = consent::Get(parent, "artifact");
      if (std::strcmp(method, "check") == 0)
        request["operation"] = "reuse-data";
    }
  }
  bool insert = std::strcmp(method, "data_register") == 0 ||
      std::strcmp(method, "data_register_derived") == 0;
  arm_statement = insert ? "INSERT INTO artifacts VALUES" :
      "SELECT session,holder,instance,purpose,recipient,scope,expires,state FROM artifacts";
  generation_to_rotate = &fixture.generations["pkg-a"];
  target_transaction = false;
  int previous_rotations = rotation_count;
  auto result = fixture.Call(request, -ESTALE);
  Check(rotation_count == previous_rotations + 1 && !generation_to_rotate &&
      consent::Get(result, "decision") != "ALLOWED", "rotation occurs at actual COMMIT-return boundary");
  if (!parent.empty())
    fixture.Use(parent, false, -ESTALE);
  // Reconciliation must make a committed-but-unpublished child unusable too.
  fixture.repository->Tick();
  auto cleanup = fixture.Data("cleanup_list");
  auto pending = fixture.Call(cleanup);
  int expected_copies = std::strcmp(method, "data_register_derived") == 0 ? 2 : 1;
  Check(consent::Number(pending, "count") == expected_copies,
      "committed unpublished copy and sources remain visible for holder cleanup");
  for (int i = 0; i < consent::Number(pending, "count"); ++i) {
    Message artifact = {{"artifact", consent::Get(pending, "a" + std::to_string(i) + ".artifact")}};
    fixture.Use(artifact, false, -ESTALE);
  }
  std::cout << "PASS real COMMIT-return generation rotation suppresses " << method
            << " response and blocks persisted artifacts\n";
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int sqlite3_step(sqlite3_stmt* statement) {
  using Step = int (*)(sqlite3_stmt*);
  static Step real_step = reinterpret_cast<Step>(dlsym(RTLD_NEXT, "sqlite3_step"));
  if (!real_step)
    _exit(99);
  const char* sql = sqlite3_sql(statement);
  if (arm_statement && sql && std::strstr(sql, arm_statement) == sql)
    target_transaction = true;
  return real_step(statement);
}

extern "C" __attribute__((visibility("default"))) int sqlite3_exec(sqlite3* database, const char* sql,
    int (*callback)(void*, int, char**, char**), void* data, char** error) {
  using Exec = int (*)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
  static Exec real_exec = reinterpret_cast<Exec>(dlsym(RTLD_NEXT, "sqlite3_exec"));
  if (!real_exec)
    _exit(99);
  int result = real_exec(database, sql, callback, data, error);
  if (result == SQLITE_OK && target_transaction && generation_to_rotate && std::strcmp(sql, "COMMIT") == 0) {
    *generation_to_rotate = "generation-rotated-after-commit";
    generation_to_rotate = nullptr;
    arm_statement = nullptr;
    target_transaction = false;
    ++rotation_count;
  }
  return result;
}

int main() {
  try {
    TransitiveGeneration();
    TimedRetention();
    for (const char* method : {"data_register", "data_register_derived", "data_check", "check"})
      CommitBoundary(method);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
