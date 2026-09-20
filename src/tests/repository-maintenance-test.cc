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

#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using consent::Message;
int injected_step_error = SQLITE_OK;
int injected_failures = 0;
int registry_replays = 0;
gint64 clock_offset_us = 0;

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

struct Flow {
  Message request;
  Message pending;
  Message authorize;
  Message receipt;
};

class Fixture final {
 public:
  Fixture() {
    char pattern[] = "/tmp/consent-maintenance-XXXXXX";
    auto* created = mkdtemp(pattern);
    Check(created != nullptr, "create isolated maintenance directory");
    directory = created;
    Check(mkdir((directory + "/registry").c_str(), 0700) == 0, "create registry directory");
    peer.identity = "actor";
    peer.instance = "instance";
    peer.roles = {"installer", "argo", "ui", "checker", "holder", "session", "admin"};
    peer.subjects = {"subject"};
    peer.profiles = {"profile"};
    peer.packages = {"package"};
    repository = std::make_unique<consentd::Repository>(directory + "/consent.db", directory + "/registry");
    repository->SetInstallationValidator([](const std::string& package,
        const std::string& app, const std::string& generation) {
      return package == "package" && app == "app" && generation == "installation";
    });
    std::string error;
    Check(repository->Open(&error), error.c_str());
    for (const char* definition : {"a", "b"}) {
      Call({{"method", "register"}, {"package", "package"}, {"app", "app"},
          {"definition", definition}, {"operation_id", std::string("install-") + definition},
          {"_install_identity", "installation"}, {"enforcer", "actor"},
          {"policy_version", "1"}, {"text_revision", "1"}, {"level", "1"},
          {"modes", "ONCE,PERSISTENT"}, {"retention_ms", "600000"},
          {"template_version", "1"}, {"parameter.purpose.source", "purpose"},
          {"parameter.purpose.type", "string"}, {"parameter.purpose.max_bytes", "32"},
          {"default_locale", "en"}, {"message.en.title", "Read"},
          {"message.en.body", "Read for {purpose}"}});
    }
    auto opened = Call({{"method", "session_open"}, {"subject", "subject"},
        {"profile", "profile"}, {"lifecycle", "RESUMABLE_CONVERSATION"}});
    session = consent::Get(opened, "session");
    resume_token = consent::Get(opened, "resume_token");
  }

  ~Fixture() {
    injected_step_error = SQLITE_OK;
    clock_offset_us = 0;
    repository.reset();
    for (const auto& path : {directory + "/registry", directory}) {
      auto* entries = opendir(path.c_str());
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

  Message Call(const Message& request, int status = 0) {
    auto result = repository->Execute(peer, request);
    if (consent::Get(result, "status") != std::to_string(status)) {
      std::cerr << consent::Get(request, "method") << ": " << consent::Get(result, "status")
          << " " << consent::Get(result, "reason") << '\n';
      throw std::runtime_error("unexpected maintenance fixture API status");
    }
    return result;
  }

  Message Context(const char* method, bool with_session = true) const {
    Message result{{"method", method}, {"subject", "subject"}, {"profile", "profile"}};
    if (with_session) {
      result["session"] = session;
      result["generation"] = "1";
    }
    return result;
  }

  Message Request(const std::string& definition, const std::string& tag,
      bool with_session = true) const {
    auto result = Context("request", with_session);
    result.insert({{"client_request_id", tag}, {"operation_id", tag}, {"step_id", "step"},
        {"count", "1"}, {"r0.definition", definition}, {"r0.policy_version", "1"}, {"r0.scope", tag},
        {"r0.operation", "read"}, {"r0.purpose", "purpose"}, {"r0.holder", "actor"}});
    return result;
  }

  Message Prompt(const Message& pending) {
    return Call({{"method", "get_prompt"}, {"request_id", consent::Get(pending, "request_id")},
        {"locale", "en"}, {"template_version", "1"}});
  }

  Flow Acquire(const std::string& definition, const std::string& tag,
      const char* mode = "ONCE", bool with_session = true) {
    Flow flow;
    flow.request = Request(definition, tag, with_session);
    flow.pending = Call(flow.request);
    Check(consent::Get(flow.pending, "decision") == "PENDING", "fresh scope requires approval");
    auto prompt = Prompt(flow.pending);
    Call({{"method", "respond"}, {"request_id", consent::Get(flow.pending, "request_id")},
        {"prompt_token", consent::Get(prompt, "prompt_token")}, {"locale", "en"},
        {"decision", "ALLOWED"}, {"grant_mode", mode}});
    flow.authorize = flow.request;
    flow.authorize["method"] = "check";
    flow.authorize["mode"] = "AUTHORIZE";
    flow.receipt = Call(flow.authorize);
    Check(consent::Get(flow.receipt, "decision") == "ALLOWED", "approval permits acquisition");
    return flow;
  }

  Message DataRequest(const Flow& flow) const {
    auto request = Context("data_register");
    request.insert({{"receipt", consent::Get(flow.receipt, "receipt")},
        {"scope", consent::Get(flow.request, "r0.scope")}, {"purpose", "purpose"}});
    return request;
  }

  void Revoke(const char* definition = "a") {
    auto request = Context("revoke", false);
    request["definition"] = definition;
    Call(request);
  }

  size_t Maintain() {
    auto result = repository->Maintain();
    Check(consent::Get(result, "status") == "0", "maintenance succeeds");
    auto count = consent::Number(result, "compacted_records", -1);
    Check(count >= 0 && count <= 128, "one maintenance transaction is bounded to 128 targets");
    return static_cast<size_t>(count);
  }

  void Drain() {
    for (unsigned batch = 0; batch < 16; ++batch) {
      if (Maintain() == 0)
        return;
    }
    throw std::runtime_error("fixture maintenance did not finish in bounded batches");
  }

  std::string Read(const char* query, const std::string& argument = "") const {
    sqlite3* database = nullptr;
    Check(sqlite3_open_v2((directory + "/consent.db").c_str(), &database,
        SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK, "open fixture inspection connection");
    sqlite3_stmt* statement = nullptr;
    int status = sqlite3_prepare_v2(database, query, -1, &statement, nullptr);
    if (status == SQLITE_OK && sqlite3_bind_parameter_count(statement))
      status = sqlite3_bind_text(statement, 1, argument.c_str(), -1, SQLITE_TRANSIENT);
    if (status == SQLITE_OK)
      status = sqlite3_step(statement);
    std::string result;
    if (status == SQLITE_ROW) {
      const auto* data = sqlite3_column_text(statement, 0);
      if (data)
        result.assign(reinterpret_cast<const char*>(data), sqlite3_column_bytes(statement, 0));
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    Check(status == SQLITE_ROW, "read fixture inspection query");
    return result;
  }

  std::string Registry() const {
    std::ifstream file(directory + "/registry/definitions.registry", std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }

  std::string directory;
  std::string session;
  std::string resume_token;
  consentd::Peer peer;
  std::unique_ptr<consentd::Repository> repository;
};

void RetryAndOnce() {
  Fixture fixture;
  auto flow = fixture.Acquire("a", "once");
  auto repeat = fixture.Call(flow.authorize);
  Check(consent::Get(repeat, "receipt") == consent::Get(flow.receipt, "receipt") &&
      consent::Get(repeat, "retry") == "1", "ONCE retry reuses its execution receipt");
  auto changed = flow.authorize;
  changed["r0.scope"] = "different";
  fixture.Call(changed, CONSENT_ERROR_CONFLICT);
  auto fresh = flow.authorize;
  fresh["operation_id"] = "new-execution";
  Check(consent::Get(fixture.Call(fresh), "decision") == "CONSENT_REQUIRED", "ONCE was consumed");
  auto count = fixture.Read("SELECT count(*) FROM grants");
  fixture.Maintain();
  Check(fixture.Read("SELECT count(*) FROM grants") == count, "valid ONCE receipt retains its grant");
  Check(!fixture.Read("SELECT payload FROM authorizations WHERE id=?",
      consent::Get(flow.receipt, "receipt")).empty(), "valid authorization payload is retained");
  Check(consent::Get(fixture.Call(flow.authorize), "receipt") == consent::Get(flow.receipt, "receipt"),
      "maintenance never creates a new execution receipt");
  fixture.Call(changed, CONSENT_ERROR_CONFLICT);
  Check(consent::Get(fixture.Call(fresh), "decision") == "CONSENT_REQUIRED", "maintenance does not refill ONCE");
  fixture.Revoke();
  fixture.Call(flow.authorize, CONSENT_ERROR_STALE);
  auto result_request = Message{{"method", "get_request_result"},
      {"request_id", consent::Get(flow.pending, "request_id")}};
  auto request_result = fixture.Call(result_request);
  auto request_retry = fixture.Call(flow.request);
  auto registry = fixture.Registry();
  fixture.Drain();
  Check(fixture.Read("SELECT count(*) FROM authorizations") == "1", "invalid execution tombstone is retained");
  Check(fixture.Read("SELECT count(*) FROM authorization_grants") == "0", "invalid execution edges are removed");
  Check(fixture.Read("SELECT count(*) FROM grants") == "0", "unreferenced revoked grant is removed");
  fixture.Call(flow.authorize, CONSENT_ERROR_STALE);
  fixture.Call(changed, CONSENT_ERROR_CONFLICT);
  Check(fixture.Call(result_request) == request_result && fixture.Call(flow.request) == request_retry,
      "terminal request result and identical request retry are unchanged");
  auto changed_request = flow.request;
  changed_request["r0.scope"] = "changed-request";
  fixture.Call(changed_request, CONSENT_ERROR_CONFLICT);
  Check(fixture.Registry() == registry, "registration operation ledger is untouched");
  fixture.Call({{"method", "maintain"}}, -ENOSYS);
  std::cout << "PASS ONCE at-most-once, invalid receipt STALE, changed retry CONFLICT and request ledger preservation\n";
}

void ArtifactAndCleanup() {
  Fixture fixture;
  auto flow = fixture.Acquire("a", "retained");
  auto data_request = fixture.DataRequest(flow);
  auto parent = fixture.Call(data_request);
  auto derive = fixture.Context("data_register_derived");
  derive.insert({{"scope", "retained"}, {"purpose", "purpose"}, {"count", "1"},
      {"parent0", consent::Get(parent, "artifact")}});
  auto child = fixture.Call(derive);
  auto acknowledge = Message{{"method", "cleanup_ack"},
      {"artifact", consent::Get(parent, "artifact")}, {"success", "1"}};
  fixture.Call(acknowledge);
  auto use = fixture.Context("data_check");
  use.insert({{"artifact", consent::Get(child, "artifact")}, {"scope", "retained"}, {"purpose", "purpose"}});
  auto baseline = fixture.Call(use);
  fixture.Call(data_request, CONSENT_ERROR_STALE);
  auto provenance = fixture.Read("SELECT count(*) FROM artifact_grants");
  auto parents = fixture.Read("SELECT count(*) FROM artifact_parents");
  fixture.Maintain();
  Check(fixture.Call(use) == baseline, "live derived data remains usable after parent deletion and maintenance");
  fixture.Call(data_request, CONSENT_ERROR_STALE);
  fixture.Call(acknowledge);
  acknowledge["success"] = "0";
  fixture.Call(acknowledge, CONSENT_ERROR_STALE);
  fixture.Revoke();
  fixture.Call(use, CONSENT_ERROR_STALE);
  acknowledge["success"] = "1";
  acknowledge["artifact"] = consent::Get(child, "artifact");
  fixture.Call(acknowledge);
  auto acknowledged = fixture.Read("SELECT count(*) FROM cleanup_acknowledgements WHERE success=1");
  fixture.Maintain();
  fixture.Call(flow.authorize, CONSENT_ERROR_STALE);
  fixture.Call(data_request, CONSENT_ERROR_STALE);
  fixture.Call(acknowledge);
  Check(fixture.Read("SELECT count(*) FROM artifacts") == "2" &&
      fixture.Read("SELECT count(*) FROM artifact_grants") == provenance &&
      fixture.Read("SELECT count(*) FROM artifact_parents") == parents &&
      fixture.Read("SELECT count(*) FROM cleanup_acknowledgements WHERE success=1") == acknowledged,
      "artifact tombstones, provenance and positive cleanup evidence survive");
  Check(fixture.Read("SELECT count(*) FROM grants") == "1", "artifact provenance protects its revoked source grant");
  std::cout << "PASS live derived provenance, deleted artifact nonrevival and idempotent cleanup ACK preservation\n";
}

void UiAndSession() {
  Fixture fixture;
  auto request = fixture.Request("a", "cancelled", false);
  auto pending = fixture.Call(request);
  auto prompt = fixture.Prompt(pending);
  std::string id = consent::Get(pending, "request_id");
  Check(fixture.Read("SELECT payload FROM requests WHERE id=?", id).find("_prompt_locale") != std::string::npos,
      "typed pending payload contains the displayed locale");
  fixture.Maintain();
  Check(!fixture.Read("SELECT token FROM requests WHERE id=?", id).empty(), "live UI token is preserved");
  auto cancelled = fixture.Call({{"method", "cancel_request"}, {"request_id", id}});
  auto result_query = Message{{"method", "get_request_result"}, {"request_id", id}};
  fixture.Call(fixture.Context("session_close"));
  auto state_query = fixture.Context("session_get_state");
  auto session_baseline = fixture.Call(state_query);
  auto baseline = fixture.Call(result_query);
  Check(!fixture.Read("SELECT resume_hash FROM sessions WHERE id=?", fixture.session).empty(),
      "closed session retains its old resume hash before compaction");
  fixture.Maintain();
  Check(fixture.Read("SELECT token||ui_owner||ui_instance FROM requests WHERE id=?", id).empty() &&
      fixture.Read("SELECT payload FROM requests WHERE id=?", id).find("_prompt_locale") == std::string::npos,
      "terminal UI columns and hidden payload locale are cleared together");
  Check(fixture.Read("SELECT resume_hash FROM sessions WHERE id=?", fixture.session).empty(),
      "closed session resume secret is cleared");
  Check(fixture.Call(result_query) == baseline && fixture.Call(state_query) == session_baseline &&
      consent::Get(fixture.Call(request), "decision") == consent::Get(cancelled, "decision"),
      "cancelled request retry/result and closed session state remain unchanged");
  fixture.Call({{"method", "respond"}, {"request_id", id},
      {"prompt_token", consent::Get(prompt, "prompt_token")}, {"locale", "en"},
      {"decision", "ALLOWED"}}, CONSENT_ERROR_STALE);
  auto resume = fixture.Context("session_resume");
  resume["generation"] = consent::Get(session_baseline, "generation");
  resume["resume_token"] = fixture.resume_token;
  fixture.Call(resume, CONSENT_ERROR_SESSION_INACTIVE);
  std::cout << "PASS pending UI preservation, terminal payload scrubbing and closed session nonreactivation\n";
}

void BoundedBatch() {
  Fixture fixture;
  auto flow = fixture.Acquire("a", "many", "PERSISTENT", false);
  for (unsigned index = 0; index < 128; ++index) {
    auto request = flow.authorize;
    request["operation_id"] = "batch-" + std::to_string(index);
    fixture.Call(request);
  }
  fixture.Revoke();
  Check(fixture.Maintain() == 128, "first batch consumes exactly the shared 128-target budget");
  Check(fixture.Read("SELECT count(*) FROM authorizations WHERE payload<>''") == "1",
      "one of 129 payloads remains for a later transaction");
  fixture.Drain();
  Check(fixture.Read("SELECT count(*) FROM authorizations") == "129" &&
      fixture.Read("SELECT count(*) FROM authorizations WHERE payload<>''") == "0",
      "later batch finishes without deleting execution keys");
  Check(fixture.Maintain() == 0, "completed compaction is idempotent");
  std::cout << "PASS global 128-target transaction bound and repeated maintenance idempotence\n";
}

void Rollback(int error, int status) {
  Fixture fixture;
  auto flow = fixture.Acquire("a", "rollback", "PERSISTENT", false);
  auto unaffected = fixture.Acquire("b", "unaffected", "PERSISTENT", false);
  fixture.Revoke();
  auto receipt = consent::Get(flow.receipt, "receipt");
  auto payload = fixture.Read("SELECT payload FROM authorizations WHERE id=?", receipt);
  auto edges = fixture.Read("SELECT count(*) FROM authorization_grants");
  auto before = fixture.repository->Snapshot();
  auto registry = fixture.Registry();
  int failures_before = injected_failures;
  injected_step_error = error;
  auto result = fixture.repository->Maintain();
  Check(consent::Get(result, "status") == std::to_string(status) &&
      injected_failures == failures_before + 1, "injected maintenance write failure is reported");
  Check(fixture.Read("SELECT payload FROM authorizations WHERE id=?", receipt) == payload &&
      fixture.Read("SELECT count(*) FROM authorization_grants") == edges,
      "later edge-write failure rolls back the earlier payload clearing");
  Check(fixture.Registry() == registry, "storage failure does not wipe or rewrite the operation ledger");
  fixture.Maintain();
  Check(consent::Get(fixture.repository->Snapshot(), "epoch") == consent::Get(before, "epoch"),
      "I/O, full and busy errors do not recreate the database");
  fixture.Call(flow.authorize, CONSENT_ERROR_STALE);
  auto changed = flow.authorize;
  changed["r0.scope"] = "changed";
  fixture.Call(changed, CONSENT_ERROR_CONFLICT);
  Check(consent::Get(fixture.Call(unaffected.authorize), "receipt") ==
      consent::Get(unaffected.receipt, "receipt"), "unrelated valid receipt survives failure and retry");
  std::cout << "PASS rollback and explicit failure/retry for SQLite code " << error << '\n';
}

void TimerFailure() {
  Fixture fixture;
  auto flow = fixture.Acquire("a", "timer", "PERSISTENT", false);
  fixture.Revoke();
  auto receipt = consent::Get(flow.receipt, "receipt");
  auto payload = fixture.Read("SELECT payload FROM authorizations WHERE id=?", receipt);
  auto epoch = consent::Get(fixture.repository->Snapshot(), "epoch");
  int failures = injected_failures;
  injected_step_error = SQLITE_IOERR;
  fixture.repository->Tick();
  Check(injected_failures == failures, "ordinary timer tick does not compact before its minute interval");
  clock_offset_us += 61000000;
  fixture.repository->Tick();
  Check(injected_failures == failures + 1 &&
      fixture.Read("SELECT payload FROM authorizations WHERE id=?", receipt) == payload,
      "scheduled maintenance fault occurs and rolls back");
  int replays = registry_replays;
  fixture.Call(flow.authorize, CONSENT_ERROR_STALE);
  Check(registry_replays == replays + 1, "timer failure fences until a fresh registry durability replay");
  fixture.repository->Tick();
  Check(fixture.Read("SELECT payload FROM authorizations WHERE id=?", receipt) == payload,
      "failed batch is not retried on every timer tick");
  clock_offset_us += 61000000;
  fixture.repository->Tick();
  Check(fixture.Read("SELECT payload FROM authorizations WHERE id=?", receipt).empty() &&
      consent::Get(fixture.repository->Snapshot(), "epoch") == epoch,
      "next minute retries without replacing storage");
  std::cout << "PASS minute scheduling, timer failure fencing and delayed atomic retry\n";
}

}  // namespace

// Test-only injection after the real payload UPDATE, before its edge DELETE.
// Production code has no fault switch, environment hook or bypass.
extern "C" __attribute__((visibility("default"))) int sqlite3_step(sqlite3_stmt* statement) {
  using Step = int (*)(sqlite3_stmt*);
  static auto real_step = reinterpret_cast<Step>(dlsym(RTLD_NEXT, "sqlite3_step"));
  const char* sql = sqlite3_sql(statement);
  if (sql && std::strcmp(sql, "UPDATE meta SET value=? WHERE key='registry_revision'") == 0)
    ++registry_replays;
  if (injected_step_error != SQLITE_OK && sql &&
      std::strncmp(sql, "DELETE FROM authorization_grants WHERE receipt=?", 47) == 0) {
    int result = injected_step_error;
    injected_step_error = SQLITE_OK;
    ++injected_failures;
    return result;
  }
  return real_step(statement);
}

extern "C" __attribute__((visibility("default"))) gint64 g_get_monotonic_time() {
  using Clock = gint64 (*)();
  static auto real_clock = reinterpret_cast<Clock>(dlsym(RTLD_NEXT, "g_get_monotonic_time"));
  return real_clock() + clock_offset_us;
}

int main() {
  try {
    RetryAndOnce();
    ArtifactAndCleanup();
    UiAndSession();
    BoundedBatch();
    Rollback(SQLITE_IOERR, CONSENT_ERROR_STORAGE);
    Rollback(SQLITE_FULL, CONSENT_ERROR_STORAGE);
    Rollback(SQLITE_BUSY, -EBUSY);
    TimerFailure();
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << "FAIL maintenance: " << failure.what() << '\n';
    return 1;
  }
}
