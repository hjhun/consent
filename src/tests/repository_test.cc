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
#include "consent.h"

#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>
#include <dirent.h>
#include <cerrno>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using consent::Message;
using consentd::Peer;
using consentd::Repository;

void Check(bool condition, const char* description) {
  if (!condition)
    throw std::runtime_error(description);
}

void Status(const Message& response, int expected, const char* description) {
  auto status = consent::Get(response, "status");
  if (status != std::to_string(expected)) {
    std::cerr << description << ": expected " << expected << ", got " << status
              << "; " << consent::Get(response, "reason") << '\n';
    throw std::runtime_error(description);
  }
}

Peer Identity(const std::string& id, const std::set<std::string>& roles) {
  Peer peer;
  peer.identity = id;
  peer.instance = id + ":instance1";
  peer.roles = roles;
  peer.subjects = {"agent"};
  peer.profiles = {"profile"};
  peer.packages = {"package", "other"};
  return peer;
}

class Fixture final {
 public:
  Fixture() {
    char pattern[] = "/tmp/consent-repository-XXXXXX";
    char* path = mkdtemp(pattern);
    Check(path != nullptr, "create isolated test state");
    directory = path;
    database = directory + "/consent.db";
    registry = directory + "/registry";
    Check(mkdir(registry.c_str(), 0700) == 0, "create protected registry directory");
    Start();
  }
  ~Fixture() {
    repository.reset();
    // Leave no shell expansion or broad deletion in this destructive test.
    for (const auto& path : {registry, directory}) {
      DIR* entries = opendir(path.c_str());
      if (!entries)
        continue;
      while (auto* entry = readdir(entries)) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' ||
            (entry->d_name[1] == '.' && entry->d_name[2] == '\0')))
          continue;
        unlink((path + "/" + entry->d_name).c_str());
      }
      closedir(entries);
    }
    rmdir(registry.c_str());
    rmdir(directory.c_str());
  }
  void Start() {
    repository.reset(new Repository(database, registry));
    repository->SetInstallationValidator([this](const std::string& package,
        const std::string& app, const std::string& installation) {
      return ((package == "package" && (app == "app1" || app == "app2")) ||
          (package == "other" && app == "app3")) && installation == generation;
    });
    repository->SetPackageGenerationValidator([this](const std::string& package,
        const std::string& installation) {
      return (package == "package" || package == "other") && installation == generation;
    });
    std::string error;
    Check(repository->Open(&error), error.c_str());
  }
  Message Call(const Peer& peer, Message request, int expected = 0) {
    auto result = repository->Execute(peer, request);
    Status(result, expected, consent::Get(request, "method").c_str());
    return result;
  }
  Message Definition(const std::string& id = "definition", const std::string& app = "app1",
      const std::string& package = "package") {
    return {{"method", "register"}, {"operation_id", "install-" + id + "-" + generation},
        {"definition", id}, {"package", package}, {"app", app}, {"enforcer", "enforcer"},
        {"expected_generation", generation}, {"_install_identity", generation},
        {"policy_version", "1"}, {"text_revision", "1"}, {"level", "1"},
        {"modes", "ONCE,SESSION,TIMED,PERSISTENT"}, {"retention_ms", "600000"},
        {"default_locale", "en"}, {"message.en.title", "Read calendar"},
        {"message.en.body", "Read the exact scope shown below"},
        {"message.ko.title", "일정 읽기"}, {"message.ko.body", "아래 표시된 범위를 읽습니다"}};
  }
  Message Context(const std::string& method = "check") {
    return {{"method", method}, {"subject", "agent"}, {"profile", "profile"},
        {"count", "1"}, {"r0.definition", "definition"}, {"r0.scope", "today"},
        {"r0.operation", "read"}, {"r0.purpose", "answer"}, {"r0.holder", "holder"},
        {"operation_id", "operation"}, {"step_id", "step"},
        {"client_request_id", "request"}, {"mode", "QUERY"}};
  }
  Message Approve(Message request, const std::string& mode = "ONCE", int duration_ms = 300000) {
    request["method"] = "request";
    auto pending = Call(argo, request);
    Check(consent::Get(pending, "decision") == "PENDING", "request creates pending state");
    auto prompt = Call(ui, {{"method", "get_prompt"},
        {"request_id", consent::Get(pending, "request_id")}, {"locale", "ko-KR"}});
    Check(consent::Get(prompt, "r0.locale") == "ko", "explicit locale fallback");
    Check(consent::Get(prompt, "r0.scope") == "today", "prompt exact scope binding");
    return Call(ui, {{"method", "respond"}, {"request_id", consent::Get(pending, "request_id")},
        {"prompt_token", consent::Get(prompt, "prompt_token")}, {"decision", "ALLOWED"},
        {"grant_mode", mode}, {"duration_ms", std::to_string(duration_ms)}});
  }

  std::string directory;
  std::string database;
  std::string registry;
  std::string generation = "generation1";
  std::unique_ptr<Repository> repository;
  Peer installer = Identity("installer", {"installer"});
  Peer argo = Identity("argo", {"argo", "session"});
  Peer checker = Identity("enforcer", {"checker"});
  Peer ui = Identity("ui", {"ui", "admin"});
  Peer holder = Identity("holder", {"holder"});
};

void PolicyAndRegistry() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  auto retry = fixture.Definition();
  fixture.Call(fixture.installer, retry);
  retry["message.en.title"] = "changed";
  fixture.Call(fixture.installer, retry, CONSENT_ERROR_CONFLICT);
  auto mismatch = fixture.Definition("bad", "app3");
  fixture.Call(fixture.installer, mismatch, -EACCES);
  auto request = fixture.Context();
  fixture.Call(fixture.checker, fixture.Context("request"), -EACCES);
  auto required = fixture.Call(fixture.checker, request);
  Check(consent::Get(required, "decision") == "CONSENT_REQUIRED", "check never opens UI");
  fixture.Approve(request);
  request["mode"] = "AUTHORIZE";
  auto authorized = fixture.Call(fixture.checker, request);
  Check(consent::Get(authorized, "decision") == "ALLOWED", "ONCE first consumption");
  auto repeated = fixture.Call(fixture.checker, request);
  Check(consent::Get(repeated, "receipt") == consent::Get(authorized, "receipt"),
      "same operation retry does not consume twice");
  request["r0.scope"] = "tomorrow";
  fixture.Call(fixture.checker, request, CONSENT_ERROR_CONFLICT);
  request["r0.scope"] = "today";
  request["step_id"] = "step2";
  auto consumed = fixture.Call(fixture.checker, request);
  Check(consent::Get(consumed, "decision") == "CONSENT_REQUIRED", "ONCE cannot be reused");
  fixture.Call(fixture.ui, {{"method", "revoke"}, {"subject", "agent"},
      {"profile", "profile"}, {"definition", "definition"}});
  request["step_id"] = "step";
  fixture.Call(fixture.checker, request, -ESTALE);

  fixture.Call(fixture.installer, fixture.Definition("second", "app2"));
  fixture.Call(fixture.installer, fixture.Definition("otherdef", "app3", "other"));
  fixture.Call(fixture.installer, {{"method", "unregister"}, {"package", "package"},
      {"operation_id", "remove"}, {"expected_generation", fixture.generation}});
  request = fixture.Context();
  auto removed = fixture.Call(fixture.checker, request);
  Check(consent::Get(removed, "decision") == "DENIED", "package primary definition removed");
  request["r0.definition"] = "second";
  Check(consent::Get(fixture.Call(fixture.checker, request), "decision") == "DENIED",
      "all apps of package removed");
  request["r0.definition"] = "otherdef";
  Check(consent::Get(fixture.Call(fixture.checker, request), "decision") == "CONSENT_REQUIRED",
      "unrelated package remains available");
  fixture.generation = "generation2";
  fixture.Call(fixture.installer, fixture.Definition());
  fixture.Call(fixture.installer, {{"method", "unregister"}, {"package", "package"},
      {"operation_id", "late-remove"}, {"expected_generation", "generation1"}}, -ESTALE);
  std::cout << "PASS registry dedup, ownership, ONCE, revoke, package removal and reinstall\n";
}

void AllOrNothingAndPrompt() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  fixture.Call(fixture.installer, fixture.Definition("second", "app2"));
  auto request = fixture.Context();
  fixture.Approve(request);
  request["mode"] = "AUTHORIZE";
  request["count"] = "2";
  request["r1.definition"] = "second";
  request["r1.operation"] = "read";
  request["r1.scope"] = "today";
  request["r1.purpose"] = "answer";
  Check(consent::Get(fixture.Call(fixture.checker, request), "decision") == "CONSENT_REQUIRED",
      "AND missing requirement blocks operation");
  request = fixture.Context();
  request["mode"] = "AUTHORIZE";
  Check(consent::Get(fixture.Call(fixture.checker, request), "decision") == "ALLOWED",
      "failed AND did not consume first ONCE grant");
  auto fresh = fixture.Context("request");
  fresh["client_request_id"] = "pending-two";
  auto pending = fixture.Call(fixture.argo, fresh);
  auto prompt = fixture.Call(fixture.ui, {{"method", "get_prompt"},
      {"request_id", consent::Get(pending, "request_id")}, {"locale", "en"}});
  auto updated = fixture.Definition();
  updated["policy_version"] = "2";
  updated["operation_id"] = "policy-two";
  fixture.Call(fixture.installer, updated);
  fixture.Call(fixture.ui, {{"method", "respond"}, {"request_id", consent::Get(pending, "request_id")},
      {"prompt_token", consent::Get(prompt, "prompt_token")}, {"decision", "ALLOWED"}}, -ESTALE);
  std::cout << "PASS AND atomicity and policy-bound stale prompt rejection\n";
}

void SessionsAndData() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  auto session = fixture.Call(fixture.argo, {{"method", "session_open"}, {"subject", "agent"},
      {"profile", "profile"}, {"lifecycle", "RESUMABLE_CONVERSATION"}});
  auto request = fixture.Context();
  request["session"] = consent::Get(session, "session");
  request["generation"] = "1";
  fixture.Approve(request, "ONCE");
  request["mode"] = "AUTHORIZE";
  auto receipt = fixture.Call(fixture.checker, request);
  Message data = {{"method", "data_register"}, {"subject", "agent"}, {"profile", "profile"},
      {"session", request["session"]}, {"generation", "1"},
      {"receipt", consent::Get(receipt, "receipt")}, {"scope", "today"}, {"purpose", "answer"}};
  auto artifact = fixture.Call(fixture.holder, data);
  auto repeat = fixture.Call(fixture.holder, data);
  Check(consent::Get(artifact, "artifact") == consent::Get(repeat, "artifact"),
      "receipt registration is retry-safe");
  data["method"] = "data_check";
  data["artifact"] = consent::Get(artifact, "artifact");
  Check(consent::Get(fixture.Call(fixture.holder, data), "decision") == "ALLOWED",
      "consumed ONCE result remains reusable by permit");
  auto other_holder = fixture.holder;
  other_holder.instance = "holder:other-instance";
  fixture.Call(other_holder, data, -EACCES);
  Message control = {{"method", "session_suspend"}, {"session", request["session"]},
      {"subject", "agent"}, {"profile", "profile"}, {"generation", "1"}};
  auto suspended = fixture.Call(fixture.argo, control);
  fixture.Call(fixture.holder, data, CONSENT_ERROR_SESSION_INACTIVE);
  control["method"] = "session_resume";
  control["generation"] = consent::Get(suspended, "generation");
  control["resume_token"] = consent::Get(session, "resume_token");
  auto resumed = fixture.Call(fixture.argo, control);
  fixture.Call(fixture.holder, data, -ESTALE);
  data["generation"] = consent::Get(resumed, "generation");
  fixture.Call(fixture.holder, data);
  control["method"] = "session_close";
  control["generation"] = consent::Get(resumed, "generation");
  auto closing = fixture.Call(fixture.argo, control);
  Check(consent::Get(closing, "state") == "CLOSING", "close waits for holder evidence");
  fixture.Call(fixture.holder, data, CONSENT_ERROR_SESSION_CLOSED);
  fixture.Call(fixture.holder, {{"method", "cleanup_ack"},
      {"artifact", consent::Get(artifact, "artifact")}, {"success", "0"}});
  control["method"] = "session_get_state";
  Check(consent::Get(fixture.Call(fixture.argo, control), "state") == "CLOSING",
      "failed cleanup remains blocked");
  fixture.Call(fixture.holder, {{"method", "cleanup_ack"},
      {"artifact", consent::Get(artifact, "artifact")}, {"success", "1"}});
  Check(consent::Get(fixture.Call(fixture.argo, control), "state") == "CLOSED",
      "holder ACK confirms closed session");
  std::cout << "PASS session generations, ONCE retention, holder ownership and cleanup ACK\n";
}

void Recovery() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  fixture.Approve(fixture.Context(), "PERSISTENT");
  fixture.repository.reset();
  // Capture a complete, closed database to demonstrate stale valid rollback.
  std::string saved = fixture.directory + "/saved.db";
  {
    std::ifstream source(fixture.database, std::ios::binary);
    std::ofstream target(saved, std::ios::binary);
    target << source.rdbuf();
  }
  chmod(saved.c_str(), 0600);
  fixture.Start();
  Check(consent::Get(fixture.Call(fixture.checker, fixture.Context()), "decision") == "ALLOWED",
      "durable PERSISTENT decision survives normal restart");
  fixture.Call(fixture.ui, {{"method", "revoke"}, {"subject", "agent"}, {"profile", "profile"},
      {"definition", "definition"}});
  fixture.repository.reset();
  Check(rename(saved.c_str(), fixture.database.c_str()) == 0, "replace DB with valid old copy");
  fixture.Start();
  Check(consent::Get(fixture.Call(fixture.checker, fixture.Context()), "decision") == "CONSENT_REQUIRED",
      "expected inode rejects old valid database and reconstructs definitions only");
  auto before = fixture.repository->Snapshot();
  Check(unlink(fixture.database.c_str()) == 0, "delete live database");
  auto recovered = fixture.Call(fixture.checker, fixture.Context());
  Check(consent::Get(recovered, "decision") == "CONSENT_REQUIRED", "runtime deletion restores only definitions");
  Check(consent::Get(recovered, "epoch") != consent::Get(before, "epoch"), "recovery changes epoch");
  Check(consent::Get(recovered, "cleanup_reconciliation_required") == "1", "total loss exposes uncertain cleanup");
  fixture.repository.reset();
  Check(unlink(fixture.database.c_str()) == 0, "delete stopped database");
  {
    std::ofstream journal(fixture.database + "-journal", std::ios::binary);
    journal << "stale interrupted journal";
  }
  chmod((fixture.database + "-journal").c_str(), 0600);
  fixture.Start();
  fixture.Call(fixture.checker, fixture.Context());
  Check(access((fixture.database + "-journal").c_str(), F_OK) != 0,
      "stale journal quarantined with missing main database");
  fixture.repository.reset();
  {
    std::ofstream corrupt(fixture.database, std::ios::binary | std::ios::trunc);
    corrupt << "not a sqlite database";
  }
  fixture.Start();
  fixture.Call(fixture.checker, fixture.Context());
  fixture.repository.reset();
  sqlite3* db = nullptr;
  Check(sqlite3_open(fixture.database.c_str(), &db) == SQLITE_OK, "open final DB for integrity check");
  sqlite3_stmt* statement = nullptr;
  Check(sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &statement, nullptr) == SQLITE_OK,
      "prepare integrity check");
  Check(sqlite3_step(statement) == SQLITE_ROW &&
      std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == "ok",
      "recovered database integrity");
  sqlite3_finalize(statement);
  sqlite3_close(db);
  std::cout << "PASS restart, stale DB replacement, live/stopped deletion, journal and corruption recovery\n";
}

void ExpiryAndFailureFences() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  auto request = fixture.Context();
  fixture.Approve(request, "TIMED", 100);
  request["mode"] = "AUTHORIZE";
  fixture.Call(fixture.checker, request);
  usleep(130000);
  fixture.Call(fixture.checker, request, -ESTALE);
  fixture.argo.profiles.insert("profile2");
  auto first = fixture.Context("request");
  first["client_request_id"] = "shared";
  auto pending1 = fixture.Call(fixture.argo, first);
  first["profile"] = "profile2";
  auto pending2 = fixture.Call(fixture.argo, first);
  Check(consent::Get(pending1, "request_id") != consent::Get(pending2, "request_id"),
      "same client id in different profiles has different request identity");
  auto found = fixture.Call(fixture.argo, {{"method", "result"}, {"client_request_id", "shared"},
      {"subject", "agent"}, {"profile", "profile2"}});
  Check(consent::Get(found, "request_id") == consent::Get(pending2, "request_id"),
      "result lookup includes full profile context");
  fixture.Call(fixture.argo, {{"method", "result"}, {"client_request_id", "shared"}}, -EINVAL);
  Check(chmod(fixture.database.c_str(), 0644) == 0, "change isolated database permissions");
  fixture.Call(fixture.checker, fixture.Context(), CONSENT_ERROR_STORAGE);
  Check(chmod(fixture.database.c_str(), 0600) == 0, "restore isolated database permissions");
  fixture.Call(fixture.checker, fixture.Context());
  Check(unlink((fixture.registry + "/definitions.registry").c_str()) == 0,
      "delete isolated recovery registry");
  fixture.Call(fixture.checker, fixture.Context(), CONSENT_ERROR_STORAGE);
  Check(access(fixture.database.c_str(), F_OK) == 0, "registry loss must not wipe database");
  std::cout << "PASS timed retry expiry, scoped lookup and permission/registry failure fences\n";
}

void CacheAndSessionDeadlines() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  fixture.Approve(fixture.Context(), "PERSISTENT");
  auto request = fixture.Context("request");
  request["client_request_id"] = "cached-request";
  auto cached = fixture.Call(fixture.argo, request);
  Check(consent::Get(cached, "cacheable") == "1" &&
      consent::Number(cached, "cache_ttl_ms") <= 500, "persistent hint has bounded cache lease");
  request["method"] = "check";
  request["mode"] = "AUTHORIZE";
  Check(consent::Get(fixture.Call(fixture.checker, request), "cacheable") == "0",
      "AUTHORIZE never permits local-cache authorization");
  fixture.generation = "generation2";
  auto revision = consent::Number(fixture.repository->Snapshot(), "revision");
  fixture.repository->Tick();
  Check(consent::Number(fixture.repository->Snapshot(), "revision") > revision,
      "installation authority change invalidates cache revision");
  fixture.Call(fixture.installer, fixture.Definition());
  Check(consent::Get(fixture.Call(fixture.checker, fixture.Context()), "decision") == "CONSENT_REQUIRED",
      "installation rotation without old uninstall does not revive grant");

  Message open = {{"method", "session_open"}, {"subject", "agent"}, {"profile", "profile"},
      {"lifecycle", "RESUMABLE_CONVERSATION"}, {"lease_ms", "100"},
      {"idle_timeout_ms", "1000"}, {"max_lifetime_ms", "2000"}, {"reconnect_grace_ms", "1000"}};
  auto session = fixture.Call(fixture.argo, open);
  usleep(130000);
  Message control = {{"method", "session_get_state"}, {"subject", "agent"}, {"profile", "profile"},
      {"session", consent::Get(session, "session")}, {"generation", "1"}};
  auto state = fixture.Call(fixture.argo, control);
  Check(consent::Get(state, "state") == "SUSPENDED", "resumable lease expiry suspends session");
  control["method"] = "session_resume";
  control["generation"] = consent::Get(state, "generation");
  control["resume_token"] = consent::Get(session, "resume_token");
  fixture.Call(fixture.argo, control);

  open["lifecycle"] = "CONNECTION_BOUND";
  open["idle_timeout_ms"] = "100";
  open["lease_ms"] = "1000";
  session = fixture.Call(fixture.argo, open);
  control["method"] = "session_heartbeat";
  control["session"] = consent::Get(session, "session");
  control["generation"] = "1";
  fixture.Call(fixture.argo, control);
  usleep(130000);
  control["method"] = "session_get_state";
  Check(consent::Get(fixture.Call(fixture.argo, control), "state") == "CLOSED",
      "heartbeat does not extend user idle lifetime");
  std::cout << "PASS cache metadata, installation reconciliation and logical session deadlines\n";
}

void DerivedRetentionAndCleanupReconciliation() {
  Fixture fixture;
  auto first_definition = fixture.Definition();
  first_definition["retention_ms"] = "2000";
  fixture.Call(fixture.installer, first_definition);
  auto second_definition = fixture.Definition("second", "app2");
  second_definition["retention_ms"] = "10000";
  second_definition["level"] = "2";
  fixture.Call(fixture.installer, second_definition);
  auto session = fixture.Call(fixture.argo, {{"method", "session_open"}, {"subject", "agent"},
      {"profile", "profile"}});
  Message data = {{"method", "data_register"}, {"subject", "agent"}, {"profile", "profile"},
      {"session", consent::Get(session, "session")}, {"generation", "1"},
      {"scope", "today"}, {"purpose", "answer"}};
  auto acquire = [&](const char* definition, const char* tag) {
    auto request = fixture.Context();
    request["r0.definition"] = definition;
    request["session"] = consent::Get(session, "session");
    request["generation"] = "1";
    request["client_request_id"] = tag;
    request["operation_id"] = tag;
    fixture.Approve(request, "PERSISTENT");
    request["mode"] = "AUTHORIZE";
    auto authorized = fixture.Call(fixture.checker, request);
    data["receipt"] = consent::Get(authorized, "receipt");
    return fixture.Call(fixture.holder, data);
  };
  auto first = acquire("definition", "first-source");
  auto first_retry = fixture.Call(fixture.holder, data);
  Check(consent::Get(first, "expires") == consent::Get(first_retry, "expires"),
      "original registration retry cannot extend acquisition TTL");
  auto second = acquire("second", "second-source");
  auto restarted = fixture.holder;
  restarted.instance = "holder:restarted";
  fixture.Call(restarted, data, -EACCES);
  data["reconcile"] = "1";
  fixture.Call(restarted, data, -EACCES);
  data.erase("reconcile");
  Message derived = data;
  derived["method"] = "data_register_derived";
  derived["count"] = "2";
  derived["parent0"] = consent::Get(first, "artifact");
  derived["parent1"] = consent::Get(second, "artifact");
  auto child = fixture.Call(fixture.holder, derived);
  Check(consent::Number(child, "expires") == consent::Number(first, "expires"),
      "multi-parent derivative inherits earliest TTL");
  fixture.Call(restarted, derived, -EACCES);
  Message use = data;
  use["method"] = "data_check";
  use["artifact"] = consent::Get(second, "artifact");
  fixture.Call(restarted, use, -EACCES);
  use["artifact"] = consent::Get(child, "artifact");
  fixture.Call(fixture.holder, use);
  usleep(2100000);
  fixture.Call(fixture.holder, use, -ESTALE);
  use["artifact"] = consent::Get(first, "artifact");
  fixture.Call(fixture.holder, use, -ESTALE);
  use["artifact"] = consent::Get(second, "artifact");
  fixture.Call(fixture.holder, use);

  Message cleanup = {{"method", "cleanup_list"}, {"subject", "agent"},
      {"profile", "profile"}, {"reconcile", "1"}};
  auto list = fixture.Call(restarted, cleanup);
  Check(consent::Number(list, "count") == 2, "restarted holder sees only blocked copies for cleanup");
  cleanup["method"] = "cleanup_ack";
  cleanup["artifact"] = consent::Get(first, "artifact");
  cleanup["success"] = "0";
  fixture.Call(restarted, cleanup);
  auto wrong_profile = restarted;
  wrong_profile.profiles.insert("profile2");
  cleanup["profile"] = "profile2";
  fixture.Call(wrong_profile, cleanup, -EACCES);
  cleanup["profile"] = "profile";
  cleanup["success"] = "1";
  fixture.Call(restarted, cleanup);
  fixture.Call(restarted, cleanup);  // Duplicate successful ACK is harmless.
  cleanup["success"] = "0";
  fixture.Call(restarted, cleanup, -ESTALE);
  fixture.Call(fixture.ui, {{"method", "revoke"}, {"subject", "agent"},
      {"profile", "profile"}, {"definition", "second"}});
  Message close = {{"method", "session_close"}, {"subject", "agent"}, {"profile", "profile"},
      {"session", consent::Get(session, "session")}, {"generation", "1"}};
  Check(consent::Get(fixture.Call(fixture.argo, close), "state") == "CLOSING",
      "remaining holder copies prevent false CLOSED");
  cleanup["method"] = "cleanup_list";
  list = fixture.Call(restarted, cleanup);
  for (int i = 0; i < consent::Number(list, "count"); ++i) {
    cleanup["method"] = "cleanup_ack";
    cleanup["artifact"] = consent::Get(list, "a" + std::to_string(i) + ".artifact");
    cleanup["success"] = "1";
    fixture.Call(restarted, cleanup);
  }
  close["method"] = "session_get_state";
  Check(consent::Get(fixture.Call(fixture.argo, close), "state") == "CLOSED",
      "cleanup-only restarted holder ACK can finish old session");
  fixture.Call(restarted, use, CONSENT_ERROR_SESSION_CLOSED);
  std::cout << "PASS multi-parent TTL, deadline-preserving retries and cleanup-only holder reconciliation\n";
}

void FinalizationAndOnceContenders() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  auto request = fixture.Context();
  auto allowed = fixture.Approve(request);
  auto final = fixture.Call(fixture.argo, {{"method", "result"},
      {"request_id", consent::Get(allowed, "request_id")}});
  Check(consent::Get(final, "r0.decision") == "ALLOWED", "final result agrees with condition decisions");
  request["mode"] = "AUTHORIZE";
  int allowed_count = 0;
  for (int i = 0; i < 2; ++i) {
    request["operation_id"] = "contender" + std::to_string(i);
    if (consent::Get(fixture.Call(fixture.checker, request), "decision") == "ALLOWED")
      ++allowed_count;
  }
  Check(allowed_count == 1, "serialized distinct operations consume ONCE exactly once");
  request = fixture.Context("request");
  request["client_request_id"] = "cancel-race";
  auto pending = fixture.Call(fixture.argo, request);
  Message response = {{"method", "get_prompt"}, {"request_id", consent::Get(pending, "request_id")},
      {"locale", "en"}};
  auto prompt = fixture.Call(fixture.ui, response);
  fixture.Call(fixture.argo, {{"method", "cancel"}, {"request_id", consent::Get(pending, "request_id")}});
  response["method"] = "respond";
  response["decision"] = "ALLOWED";
  response["prompt_token"] = consent::Get(prompt, "prompt_token");
  fixture.Call(fixture.ui, response, -ESTALE);
  final = fixture.Call(fixture.argo, {{"method", "result"}, {"request_id", consent::Get(pending, "request_id")}});
  Check(consent::Get(final, "decision") == "CANCELLED", "cancel wins one finalization order");
  request["client_request_id"] = "respond-race";
  allowed = fixture.Approve(request);
  final = fixture.Call(fixture.argo, {{"method", "cancel"}, {"request_id", consent::Get(allowed, "request_id")}});
  Check(consent::Get(final, "decision") == "ALLOWED", "respond wins reverse finalization order");
  request["r0.scope"] = "expired-scope";
  request["client_request_id"] = "deadline-race";
  request["deadline_ms"] = "100";
  pending = fixture.Call(fixture.argo, request);
  response["method"] = "get_prompt";
  response["request_id"] = consent::Get(pending, "request_id");
  prompt = fixture.Call(fixture.ui, response);
  usleep(130000);
  response["method"] = "respond";
  response["prompt_token"] = consent::Get(prompt, "prompt_token");
  fixture.Call(fixture.ui, response, -ESTALE);
  final = fixture.Call(fixture.argo, {{"method", "result"}, {"request_id", consent::Get(pending, "request_id")}});
  Check(consent::Get(final, "decision") == "EXPIRED", "deadline wins against late UI response");
  std::cout << "PASS condition results, serialized ONCE contenders and cancel/respond/deadline orders\n";
}

void SchemaMigration() {
  Fixture fixture;
  fixture.Call(fixture.installer, fixture.Definition());
  fixture.Approve(fixture.Context(), "PERSISTENT");
  auto session = fixture.Call(fixture.argo, {{"method", "session_open"}, {"subject", "agent"},
      {"profile", "profile"}});
  auto request = fixture.Context("request");
  request["r0.scope"] = "migration-pending";
  request["client_request_id"] = "migration-pending";
  auto pending = fixture.Call(fixture.argo, request);
  fixture.repository.reset();
  sqlite3* database = nullptr;
  Check(sqlite3_open(fixture.database.c_str(), &database) == SQLITE_OK, "open offline migration fixture");
  Check(sqlite3_exec(database, "DROP TABLE cleanup_acknowledgements; PRAGMA user_version=1;",
      nullptr, nullptr, nullptr) == SQLITE_OK, "construct foundation schema v1 fixture");
  sqlite3_close(database);
  fixture.Start();
  Check(consent::Get(fixture.Call(fixture.checker, fixture.Context()), "decision") == "ALLOWED",
      "v1 to v2 preserves persistent decision and definition");
  auto state = fixture.Call(fixture.argo, {{"method", "session_get_state"}, {"subject", "agent"},
      {"profile", "profile"}, {"session", consent::Get(session, "session")}});
  Check(consent::Get(state, "state") == "CLOSED", "migration applies normal transient-session retirement");
  auto result = fixture.Call(fixture.argo, {{"method", "result"},
      {"request_id", consent::Get(pending, "request_id")}});
  Check(consent::Get(result, "decision") == "INVALIDATED", "migration invalidates pending request");
  fixture.repository.reset();
  Check(sqlite3_open(fixture.database.c_str(), &database) == SQLITE_OK, "inspect migrated schema");
  sqlite3_stmt* query = nullptr;
  Check(sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &query, nullptr) == SQLITE_OK &&
      sqlite3_step(query) == SQLITE_ROW && sqlite3_column_int(query, 0) == 2,
      "schema revision advances transactionally to v2");
  sqlite3_finalize(query);
  Check(sqlite3_exec(database, "SELECT * FROM cleanup_acknowledgements; PRAGMA user_version=3;",
      nullptr, nullptr, nullptr) == SQLITE_OK, "cleanup evidence table exists; create unsupported version");
  sqlite3_close(database);
  Repository future(fixture.database, fixture.registry);
  std::string error;
  Check(!future.Open(&error) && error.find("unsupported database schema") != std::string::npos,
      "unknown future schema fails without wiping persistent database");
  std::cout << "PASS transactional v1-to-v2 migration and future-schema refusal\n";
}

}  // namespace

int main() {
  try {
    PolicyAndRegistry();
    AllOrNothingAndPrompt();
    SessionsAndData();
    Recovery();
    ExpiryAndFailureFences();
    CacheAndSessionDeadlines();
    DerivedRetentionAndCleanupReconciliation();
    FinalizationAndOnceContenders();
    SchemaMigration();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
