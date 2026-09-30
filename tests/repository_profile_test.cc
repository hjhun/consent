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
#include "consentd/profile_state.hh"
#include "consent.h"

#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using consent::Get;
using consent::Message;
using consentd::Peer;
std::shared_ptr<consentd::ProfileState> injected_state;
bool inject = false;
bool armed = false;
bool compensation_failure = false;
bool fail_begin = false;

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

Peer Identity(const char* id, const char* role) {
  Peer peer;
  peer.identity = id;
  peer.instance = std::string(id) + ":instance";
  peer.roles = {role};
  peer.subjects = {"agent"};
  peer.profiles = {"profile.A", "profile.B"};
  peer.packages = {"package"};
  peer.enforcers = {"checker"};
  return peer;
}

class Fixture final {
 public:
  Fixture() {
    char pattern[] = "/tmp/consent-profile-XXXXXX";
    auto* path = mkdtemp(pattern);
    Check(path != nullptr, "isolated profile state");
    directory = path;
    Check(mkdir((directory + "/registry").c_str(), 0700) == 0, "registry");
    state = std::make_shared<consentd::ProfileState>();
    Check(state->Configure({{"agent", "A", "profile.A"},
                            {"agent", "B", "profile.B"}}), "mapping");
    repository = std::make_unique<consentd::Repository>(
        directory + "/consent.db", directory + "/registry");
    repository->SetProfileState(state);
    repository->SetInstallationValidator([](const std::string& package,
        const std::string& app, const std::string& generation) {
      return package == "package" && app == "app" && generation == "g1";
    });
    std::string error;
    Check(repository->Open(&error), error.c_str());
    repository->FenceProfiles(true);
    Check(state->Activate(state->Generation(), "A"), "initial profile");
    Call(installer, {{"method", "register"}, {"package", "package"},
        {"app", "app"}, {"definition", "definition"},
        {"enforcer", "checker"}, {"operation_id", "install"},
        {"expected_generation", "g1"}, {"_install_identity", "g1"},
        {"policy_version", "1"}, {"text_revision", "1"}, {"level", "1"},
        {"modes", "ONCE,SESSION,PERSISTENT"}, {"retention_ms", "600000"},
        {"default_locale", "en"}, {"message.en.title", "Fixture"},
        {"message.en.body", "Read fixture"}});
  }
  ~Fixture() {
    repository.reset();
    for (const auto& path : {directory + "/registry", directory}) {
      auto* entries = opendir(path.c_str());
      if (!entries)
        continue;
      while (auto* entry = readdir(entries)) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
          continue;
        unlink((path + "/" + entry->d_name).c_str());
      }
      closedir(entries);
      rmdir(path.c_str());
    }
  }
  Message Call(const Peer& peer, Message request, int status = 0) {
    auto result = repository->Execute(peer, request);
    Check(Get(result, "status") == std::to_string(status),
          Get(result, "reason", "unexpected API status").c_str());
    return result;
  }
  Message Params(const std::string& operation, const std::string& scope) {
    return {{"method", "check"}, {"subject", "agent"},
        {"profile", "profile.A"}, {"count", "1"},
        {"r0.definition", "definition"}, {"r0.operation", "read"},
        {"r0.scope", scope}, {"r0.purpose", "answer"},
        {"operation_id", operation}, {"step_id", "read"},
        {"client_request_id", operation}, {"mode", "AUTHORIZE"}};
  }
  Message Prompt(Message params) {
    params["method"] = "request";
    auto pending = Call(argo, params);
    return Call(ui, {{"method", "get_prompt"}, {"locale", "en"},
                    {"request_id", Get(pending, "request_id")}});
  }
  Message Respond(const Message& prompt, int expected = 0) {
    return Call(ui, {{"method", "respond"}, {"decision", "ALLOWED"},
        {"grant_mode", "PERSISTENT"},
        {"request_id", Get(prompt, "request_id")},
        {"prompt_token", Get(prompt, "prompt_token")}}, expected);
  }
  void Switch(const char* user, bool gap = false) {
    auto generation = state->Fence();
    repository->FenceProfiles(gap);
    Check(state->Activate(generation, user), "fresh profile activation");
  }

  std::string directory;
  std::shared_ptr<consentd::ProfileState> state;
  std::unique_ptr<consentd::Repository> repository;
  Peer installer = Identity("installer", "installer");
  Peer argo = Identity("argo", "argo");
  Peer checker = Identity("checker", "checker");
  Peer ui = Identity("ui", "ui");
};

void InactiveCleanup() {
  Fixture fixture;
  fixture.argo.roles.insert("session");
  auto holder = Identity("holder", "holder");
  auto session = fixture.Call(fixture.argo,
      {{"method", "session_open"}, {"subject", "agent"},
       {"profile", "profile.A"}, {"lifecycle", "RESUMABLE_CONVERSATION"}});
  auto params = fixture.Params("retained-A", "today");
  params["session"] = Get(session, "session");
  params["generation"] = Get(session, "generation");
  params["r0.holder"] = "holder";
  fixture.Respond(fixture.Prompt(params));
  auto receipt = fixture.Call(fixture.checker, params);
  Message data{{"method", "data_register"}, {"subject", "agent"},
      {"profile", "profile.A"}, {"session", Get(session, "session")},
      {"generation", Get(session, "generation")},
      {"receipt", Get(receipt, "receipt")}, {"scope", "today"},
      {"purpose", "answer"}};
  auto artifact = fixture.Call(holder, data);
  data["method"] = "data_check";
  data["artifact"] = Get(artifact, "artifact");
  Check(Get(fixture.Call(holder, data), "decision") == "ALLOWED",
        "A retained permit before switch");
  fixture.Switch("B");
  fixture.Call(holder, data, -EACCES);
  Message inspect{{"method", "session_get_state"}, {"subject", "agent"},
      {"profile", "profile.A"}, {"session", Get(session, "session")},
      {"generation", Get(session, "generation")}};
  Check(Get(fixture.Call(fixture.argo, inspect), "state") == "CLOSING",
        "inactive session waits for holder ACK");
  auto cleanup = fixture.Call(holder,
      {{"method", "cleanup_list"}, {"subject", "agent"},
       {"profile", "profile.A"}});
  Check(Get(cleanup, "count") == "1" &&
        Get(cleanup, "a0.artifact") == Get(artifact, "artifact"),
        "inactive A authenticated cleanup listing");
  fixture.Call(holder, {{"method", "cleanup_ack"}, {"subject", "agent"},
      {"profile", "profile.A"}, {"artifact", Get(artifact, "artifact")},
      {"success", "1"}});
  Check(Get(fixture.Call(fixture.argo, inspect), "state") == "CLOSED",
        "matching metadata ACK completes inactive session");
  fixture.Switch("A");
  inspect["method"] = "session_resume";
  inspect["resume_token"] = Get(session, "resume_token");
  fixture.Call(fixture.argo, inspect, -ESTALE);
  fixture.Call(holder, data, CONSENT_ERROR_SESSION_CLOSED);
  std::puts("PASS inactive A cleanup list/ACK, CLOSING to CLOSED; "
            "metadata evidence only, no session revival");
}

void Test(bool failure) {
  Fixture fixture;
  auto original = fixture.Params("original", "today");
  fixture.Respond(fixture.Prompt(original));
  auto receipt = fixture.Call(fixture.checker, original);
  Check(Get(receipt, "decision") == "ALLOWED", "persistent A");
  auto absent = original;
  absent.erase("profile");
  fixture.Call(fixture.checker, absent, -EINVAL);
  absent["profile"] = "unknown";
  fixture.Call(fixture.checker, absent, -EACCES);
  absent["profile"] = "profile.B";
  fixture.Call(fixture.checker, absent, -EACCES);
  auto late = fixture.Params("late", "late-scope");
  auto prompt = fixture.Prompt(late);
  injected_state = fixture.state;
  inject = true;
  compensation_failure = failure;
  fixture.Respond(prompt, failure ? CONSENT_ERROR_STORAGE : -ESTALE);
  Check(!armed && !inject, "commit race actually injected");
  if (failure) {
    bool blocked = false;
    try {
      fixture.repository->FenceProfiles(false);
    } catch (...) {
      blocked = true;
    }
    Check(blocked, "failed retirement hard-fences barrier");
    fixture.Call(fixture.checker, original, CONSENT_ERROR_STORAGE);
    std::puts("PASS retirement BUSY hard-fence; no activation/ACK/effect");
  } else {
    fixture.Switch("B");
    fixture.Call(fixture.checker, original, -EACCES);
    auto lookup = fixture.Call(fixture.argo,
        {{"method", "result"}, {"request_id", Get(prompt, "request_id")}});
    Check(Get(lookup, "decision") == "INVALIDATED", "late UI invalidated");
    fixture.Switch("A");
    fixture.Call(fixture.checker, original, -ESTALE);
    original["operation_id"] = "fresh-A";
    Check(Get(fixture.Call(fixture.checker, original), "decision") == "ALLOWED",
          "old persistent preserved for fresh A operation");
    Check(Get(fixture.Call(fixture.checker, late), "decision") ==
              "CONSENT_REQUIRED", "late grant did not survive A-B-A");
    fixture.Switch("A", true);
    original["operation_id"] = "after-gap";
    Check(Get(fixture.Call(fixture.checker, original), "decision") ==
              "CONSENT_REQUIRED", "unverified gap retires persistent");
    std::puts("PASS explicit profiles, late UI compensation, stale receipt, "
              "A-B-A persistence and gap retirement; protected effect0");
  }
  injected_state.reset();
}

}  // namespace

// Executable-only injection: production has no fault option or extra ABI.
extern "C" int sqlite3_prepare_v2(sqlite3* db, const char* sql, int size,
                                  sqlite3_stmt** statement, const char** tail) {
  using Function = int (*)(sqlite3*, const char*, int, sqlite3_stmt**,
                           const char**);
  static auto real = reinterpret_cast<Function>(
      dlsym(RTLD_NEXT, "sqlite3_prepare_v2"));
  if (inject && strstr(sql, "INSERT INTO grants"))
    armed = true;
  return real(db, sql, size, statement, tail);
}

extern "C" int sqlite3_exec(sqlite3* db, const char* sql,
    int (*callback)(void*, int, char**, char**), void* data, char** error) {
  using Function = int (*)(sqlite3*, const char*,
      int (*)(void*, int, char**, char**), void*, char**);
  static auto real = reinterpret_cast<Function>(
      dlsym(RTLD_NEXT, "sqlite3_exec"));
  if (fail_begin && !strcmp(sql, "BEGIN IMMEDIATE")) {
    fail_begin = false;
    return SQLITE_BUSY;
  }
  int status = real(db, sql, callback, data, error);
  if (status == SQLITE_OK && armed && !strcmp(sql, "COMMIT")) {
    armed = false;
    inject = false;
    injected_state->Fence();
    fail_begin = compensation_failure;
  }
  return status;
}

int main() {
  try {
    InactiveCleanup();
    Test(false);
    Test(true);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL profile repository: %s\n", error.what());
    return 1;
  }
}
