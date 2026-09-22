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

#include "common/approval.hh"
#include "common/localization.hh"
#include "consent_common.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using consent::Get;
using consent::Message;
using consentd::Peer;
using consentd::Repository;

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

Peer Identity(const std::string& identity, const std::string& role) {
  Peer peer;
  peer.identity = identity;
  peer.instance = identity + ":instance";
  peer.roles = {role};
  peer.packages = {"package"};
  peer.subjects = {"subject"};
  peer.profiles = {"profile"};
  return peer;
}

class Fixture final {
 public:
  explicit Fixture(bool register_defaults = true) {
    char pattern[] = "/tmp/consent-feature-XXXXXX";
    auto* directory = mkdtemp(pattern);
    Check(directory != nullptr, "create feature fixture directory");
    directory_ = directory;
    registry_ = directory_ + "/registry";
    Check(mkdir(registry_.c_str(), 0700) == 0, "create feature registry");
    repository = std::make_unique<Repository>(directory_ + "/consent.db", registry_);
    repository->SetInstallationValidator([](const std::string& package,
        const std::string& app, const std::string& generation) {
      return package == "package" && (app == "app-a" || app == "app-b") &&
          generation == "installation";
    });
    std::string error;
    Check(repository->Open(&error), error.c_str());
    argo.roles.insert("session");
    if (register_defaults) {
      Register("a");
      Register("b");
    }
  }

  ~Fixture() {
    repository.reset();
    for (const auto& directory : {registry_, directory_}) {
      DIR* entries = opendir(directory.c_str());
      if (!entries)
        continue;
      while (auto* entry = readdir(entries)) {
        std::string name = entry->d_name;
        if (name != "." && name != "..")
          unlink((directory + "/" + name).c_str());
      }
      closedir(entries);
    }
    rmdir(registry_.c_str());
    rmdir(directory_.c_str());
  }

  Message Call(const Peer& peer, const Message& request, int expected = 0) {
    auto result = repository->Execute(peer, request);
    if (Get(result, "status") != std::to_string(expected)) {
      std::cerr << Get(request, "method") << ": expected=" << expected
          << " actual=" << Get(result, "status") << " reason="
          << Get(result, "reason") << '\n';
      throw std::runtime_error("unexpected feature repository status");
    }
    return result;
  }

  void Reject(const Peer& peer, const Message& request) {
    auto result = repository->Execute(peer, request);
    Check(!Get(result, "status").empty() && Get(result, "status")[0] == '-',
        "invalid selection must return a negative status");
    Check(!result.count("receipt") && Get(result, "decision") != "ALLOWED",
        "invalid selection must not publish authorization");
  }

  void Register(const std::string& definition, const std::string& policy = "1",
      const Message& extra = {}) {
    Message registration{{"method", "register"}, {"operation_id", Next("install")},
        {"definition", definition}, {"package", "package"}, {"app", "app-" + definition},
        {"enforcer", "checker"}, {"expected_generation", "installation"},
        {"_install_identity", "installation"}, {"policy_version", policy},
        {"text_revision", "1"}, {"level", "1"},
        {"modes", "ONCE,SESSION,TIMED,PERSISTENT"}, {"default_locale", "en-US"},
        {"message.en-US.title", "Approve the selected operation"},
        {"message.en-US.body", "Read the displayed scope for the selected task"}};
    for (const auto& field : extra)
      registration[field.first] = field.second;
    Call(installer, registration);
  }

  Message Context(const std::string& method = "request", bool pair = false,
      const std::string& definition = "a") {
    auto sequence = Next("operation");
    Message request{{"method", method}, {"subject", "subject"}, {"profile", "profile"},
        {"operation_id", sequence}, {"client_request_id", sequence + "-request"},
        {"step_id", sequence + "-step"}, {"mode", "QUERY"}, {"count", pair ? "2" : "1"}};
    for (int i = 0; i < (pair ? 2 : 1); ++i) {
      auto row = "r" + std::to_string(i) + ".";
      request[row + "definition"] = i == 0 ? definition : "b";
      request[row + "policy_version"] = "1";
      request[row + "scope"] = "scope";
      request[row + "operation"] = "read";
      request[row + "purpose"] = "answer";
      request[row + "recipient"] = "consumer";
      request[row + "holder"] = "holder";
    }
    if (!session.empty()) {
      request["session"] = session;
      request["generation"] = generation;
    }
    return request;
  }

  static void Seal(Message* request) {
    (*request)["selection_digest"] = consent::approval::SelectionDigest(*request);
  }

  Message Selection(const std::string& kind = "TASK", const std::string& mode = "ONCE",
      bool pair = false, const std::string& definition = "a", const std::string& duration = "") {
    auto request = Context("request", pair, definition);
    request["approval_version"] = "1";
    request["request_kind"] = kind;
    request["selection_id"] = Next("selection");
    request["selection_revision"] = "1";
    request["grant_mode"] = mode;
    if (!duration.empty())
      request["duration_ms"] = duration;
    for (int i = 0; i < (pair ? 2 : 1); ++i) {
      auto row = "r" + std::to_string(i) + ".";
      request[row + "feature_id"] = "feature-" + Get(request, row + "definition");
      request[row + "feature_revision"] = "1";
    }
    Seal(&request);
    return request;
  }

  Message Prompt(const Message& pending, bool feature = true) {
    Message request{{"method", "get_prompt"}, {"request_id", Get(pending, "request_id")},
        {"locale", "en-US"}};
    if (feature)
      request["approval_version"] = "1";
    return Call(ui, request);
  }

  Message Response(const Message& prompt, bool feature = true,
      const std::string& mode = "ONCE", const std::string& duration = "") {
    Message response{{"method", "respond"}, {"request_id", Get(prompt, "request_id")},
        {"prompt_token", Get(prompt, "prompt_token")}, {"locale", "en-US"},
        {"decision", "ALLOWED"}, {"grant_mode", mode}};
    if (feature)
      consent::approval::CopyContext(prompt, &response);
    else if (!duration.empty())
      response["duration_ms"] = duration;
    return response;
  }

  void Seed(const std::string& mode, const std::string& definition = "a",
      const std::string& duration = "4000") {
    auto pending = Call(argo, Context("request", false, definition));
    Check(Get(pending, "decision") == "PENDING", "legacy seed must require approval");
    auto approved = Call(ui, Response(Prompt(pending, false), false, mode, duration));
    Check(Get(approved, "decision") == "ALLOWED", "legacy seed approval succeeds");
  }

  Message Authorize(const std::string& definition = "a") {
    auto request = Context("check", false, definition);
    request["mode"] = "AUTHORIZE";
    return Call(checker, request);
  }

  Message Result(const Message& pending) {
    return Call(argo, {{"method", "get_request_result"},
        {"request_id", Get(pending, "request_id")}});
  }

  void OpenSession() {
    auto opened = Call(argo, {{"method", "session_open"},
        {"subject", "subject"}, {"profile", "profile"}});
    session = Get(opened, "session");
    generation = Get(opened, "generation");
    Check(!session.empty() && !generation.empty(), "session identity returned");
  }

  std::unique_ptr<Repository> repository;
  Peer installer = Identity("installer", "installer");
  Peer argo = Identity("argo", "argo");
  Peer checker = Identity("checker", "checker");
  Peer ui = Identity("approval-ui", "ui");
  Peer admin = Identity("administrator", "admin");
  std::string session;
  std::string generation;

 private:
  std::string Next(const std::string& prefix) {
    return prefix + "-" + std::to_string(++sequence_);
  }
  std::string directory_;
  std::string registry_;
  unsigned sequence_ = 0;
};

void ExistingGrantCoverage() {
  struct Coverage {
    const char* existing;
    const char* kind;
    const char* requested;
    const char* duration;
    bool allowed;
  };
  const Coverage cases[] = {
      {"ONCE", "PREAPPROVAL", "ONCE", "", true},
      {"TIMED", "PREAPPROVAL", "ONCE", "", true},
      {"SESSION", "PREAPPROVAL", "ONCE", "", true},
      {"PERSISTENT", "PREAPPROVAL", "ONCE", "", true},
      {"SESSION", "PREAPPROVAL", "SESSION", "", true},
      {"PERSISTENT", "PREAPPROVAL", "SESSION", "", true},
      {"ONCE", "PREAPPROVAL", "SESSION", "", false},
      {"TIMED", "PREAPPROVAL", "SESSION", "", false},
      {"TIMED", "PREAPPROVAL", "TIMED", "1000", true},
      {"TIMED", "PREAPPROVAL", "TIMED", "10000", false},
      {"PERSISTENT", "PREAPPROVAL", "TIMED", "10000", true},
      {"SESSION", "PREAPPROVAL", "TIMED", "1000", false},
      {"ONCE", "PREAPPROVAL", "TIMED", "1000", false},
      {"ONCE", "TASK", "TIMED", "10000", true},
      {"TIMED", "TASK", "SESSION", "", true},
      {"PERSISTENT", "TASK", "ONCE", "", true}};
  for (const auto& item : cases) {
    Fixture fixture;
    fixture.OpenSession();
    fixture.Seed(item.existing);
    auto request = fixture.Selection(item.kind, item.requested, false, "a", item.duration);
    auto result = fixture.Call(fixture.argo, request);
    if (Get(result, "decision") != (item.allowed ? "ALLOWED" : "PENDING")) {
      std::cerr << "coverage " << item.existing << " -> " << item.kind << '/'
          << item.requested << ": " << Get(result, "decision") << '\n';
      throw std::runtime_error("incorrect preapproval/task grant coverage");
    }
  }
  std::cout << "PASS legacy grant keys reused with PREAPPROVAL duration and TASK coverage rules\n";
}

void SessionReuse() {
  Fixture fixture;
  fixture.OpenSession();
  fixture.Seed("SESSION");
  auto same = fixture.Call(fixture.argo, fixture.Selection("PREAPPROVAL", "SESSION"));
  Check(Get(same, "decision") == "ALLOWED", "same session reuses existing grant");
  fixture.OpenSession();
  auto other = fixture.Call(fixture.argo, fixture.Selection("PREAPPROVAL", "SESSION"));
  Check(Get(other, "decision") == "PENDING", "another session cannot reuse SESSION grant");
  std::cout << "PASS SESSION coverage is bound to the actual logical session\n";
}

void SessionControllerAndArgoRoles() {
  Fixture fixture;
  fixture.OpenSession();
  Message heartbeat{{"method", "session_heartbeat"}, {"subject", "subject"},
      {"profile", "profile"}, {"session", fixture.session}, {"generation", fixture.generation}};
  auto query = heartbeat;
  query["method"] = "session_get_state";
  auto before = fixture.Call(fixture.argo, query);
  auto renewed = fixture.Call(fixture.argo, heartbeat);
  Check(Get(renewed, "state") == "ACTIVE" &&
      Get(renewed, "generation") == fixture.generation &&
      consent::Number(renewed, "lease_deadline") >= consent::Number(before, "lease_deadline") &&
      Get(renewed, "idle_deadline") == Get(before, "idle_deadline") &&
      Get(renewed, "absolute_deadline") == Get(before, "absolute_deadline"),
      "owner heartbeat renews only lease without changing session lifetime or generation");
  auto no_session_role = fixture.argo;
  no_session_role.roles.erase("session");
  fixture.Call(no_session_role, heartbeat, -EACCES);
  auto wrong_instance = fixture.argo;
  wrong_instance.instance = "argo:another-instance";
  fixture.Call(wrong_instance, heartbeat, -EACCES);
  fixture.Call(fixture.ui, heartbeat, -EACCES);
  auto wrong_generation = heartbeat;
  wrong_generation["generation"] = "2";
  fixture.Call(fixture.argo, wrong_generation, -ESTALE);
  auto request = fixture.Selection("PREAPPROVAL", "SESSION");
  fixture.Call(fixture.ui, request, -EACCES);
  fixture.Call(fixture.checker, request, -EACCES);
  auto close = heartbeat;
  close["method"] = "session_close";
  auto closed = fixture.Call(fixture.argo, close);
  Check(Get(closed, "state") == "CLOSED", "session closes without retained artifacts");
  fixture.Call(fixture.argo, heartbeat, -ESTALE);
  heartbeat["generation"] = Get(closed, "generation");
  fixture.Call(fixture.argo, heartbeat, CONSENT_ERROR_SESSION_INACTIVE);
  fixture.Call(fixture.argo, request, CONSENT_ERROR_SESSION_CLOSED);
  std::cout << "PASS session heartbeat owner/role/generation checks and argo-only approval requests\n";
}

void MultipleGrantCandidates() {
  Fixture fixture;
  fixture.OpenSession();
  fixture.Seed("TIMED", "a", "10000");
  auto session = fixture.Call(fixture.argo, fixture.Selection("PREAPPROVAL", "SESSION"));
  Check(Get(session, "decision") == "PENDING", "TIMED does not cover the whole session");
  fixture.Call(fixture.ui, fixture.Response(fixture.Prompt(session)));
  auto timed = fixture.Call(fixture.argo,
      fixture.Selection("PREAPPROVAL", "TIMED", false, "a", "1000"));
  Check(Get(timed, "decision") == "ALLOWED",
      "coverage must inspect older sufficient TIMED grant after newer insufficient SESSION");
  std::cout << "PASS coverage searches all live grants without changing legacy grant keys\n";
}

void SelectionDigestBinding() {
  const Message vector{{"approval_version", "1"}, {"request_kind", "PREAPPROVAL"},
      {"selection_id", "settings-1"}, {"selection_revision", "1"}, {"grant_mode", "TIMED"},
      {"duration_ms", "1800000"}, {"subject", "owner"}, {"profile", "default"},
      {"session", ""}, {"generation", ""}, {"count", "1"}, {"r0.definition", "calendar.read"},
      {"r0.policy_version", "1"}, {"r0.scope", "30"}, {"r0.operation", "read"},
      {"r0.purpose", "answer"}, {"r0.recipient", "local"}, {"r0.holder", "holder"},
      {"r0.feature_id", "calendar"}, {"r0.feature_revision", "1"}};
  Check(consent::approval::SelectionDigest(vector) ==
      "112eebdb4fcd0b7f0a3bebeb488d95be216259cfcaf36d0cd4da26382b9c9bdf",
      "documented canonical selection digest vector must remain stable");
  Fixture fixture;
  fixture.OpenSession();
  auto original = fixture.Selection("PREAPPROVAL", "TIMED", false, "a", "1000");
  Check(consent::approval::Validate(original), "original selection digest validates");
  const Message changes{{"approval_version", "2"}, {"request_kind", "TASK"},
      {"selection_id", "different-selection"}, {"selection_revision", "2"},
      {"grant_mode", "ONCE"}, {"duration_ms", "2000"}, {"subject", "different-subject"},
      {"profile", "different-profile"}, {"session", "different-session"},
      {"generation", "2"}, {"count", "2"}, {"r0.definition", "b"},
      {"r0.policy_version", "2"}, {"r0.scope", "different-scope"},
      {"r0.operation", "write"}, {"r0.purpose", "different-purpose"},
      {"r0.recipient", "different-recipient"}, {"r0.holder", "different-holder"},
      {"r0.feature_id", "different-feature"}, {"r0.feature_revision", "2"}};
  for (const auto& field : changes) {
    auto changed = original;
    changed[field.first] = field.second;
    Check(consent::approval::SelectionDigest(changed) != Get(original, "selection_digest"),
        "every selected context field must change the digest");
    Check(!consent::approval::Validate(changed), "unsealed selected context change is invalid");
    fixture.Reject(fixture.argo, changed);
  }
  auto transport = original;
  transport["method"] = "check";
  transport["mode"] = "AUTHORIZE";
  transport["operation_id"] = "actual-protected-operation";
  transport["client_request_id"] = "actual-check";
  transport["step_id"] = "actual-step";
  Check(consent::approval::SelectionDigest(transport) == Get(original, "selection_digest"),
      "transport operation identifiers and check mode do not replace selected context");
  std::cout << "PASS selection digest binds every selected context field independently of transport\n";
}

void PublicResultMetadata() {
  Fixture fixture;
  const auto no_private_fields = [](const Message& result) {
    for (const auto& field : result)
      Check(field.first.empty() || field.first.front() != '_',
          "public output must not expose stored private approval metadata");
    for (const char* field : {"prompt_token", "ui_owner", "ui_instance"})
      Check(!result.count(field), "request result must not expose UI token or owner");
  };
  auto pending = fixture.Call(fixture.argo,
      fixture.Selection("PREAPPROVAL", "TIMED", false, "a", "1000"));
  no_private_fields(pending);
  auto prompt = fixture.Prompt(pending);
  Check(!Get(prompt, "prompt_token").empty(), "UI token was actually issued before result check");
  auto polled = fixture.Result(pending);
  Check(Get(polled, "decision") == "PENDING", "pending request remains pending while UI displays");
  no_private_fields(polled);
  auto cancelled = fixture.Call(fixture.argo,
      {{"method", "cancel_request"}, {"request_id", Get(pending, "request_id")}});
  Check(Get(cancelled, "decision") == "CANCELLED", "owner may cancel selected approval");
  no_private_fields(cancelled);
  no_private_fields(fixture.Result(pending));
  pending = fixture.Call(fixture.argo,
      fixture.Selection("PREAPPROVAL", "TIMED", false, "a", "1000"));
  prompt = fixture.Prompt(pending);
  auto final = fixture.Call(fixture.ui, fixture.Response(prompt));
  Check(Get(final, "decision") == "ALLOWED", "second displayed request completes");
  no_private_fields(final);
  no_private_fields(fixture.Result(pending));
  std::cout << "PASS pending, cancelled and final public results hide private approval metadata\n";
}

void CompactPromptAndAuthorize() {
  Fixture fixture;
  fixture.Seed("PERSISTENT");
  auto request = fixture.Selection("TASK", "ONCE", true);
  auto pending = fixture.Call(fixture.argo, request);
  Check(Get(pending, "r0.decision") == "ALLOWED" &&
      Get(pending, "r1.decision") == "CONSENT_REQUIRED", "mixed request preserves full decisions");
  auto prompt = fixture.Prompt(pending);
  Check(Get(prompt, "count") == "1" && Get(prompt, "total_count") == "2" &&
      Get(prompt, "r0.original_index") == "1" && Get(prompt, "r0.definition") == "b",
      "prompt contains only missing B with original index");
  Check(!prompt.count("r1.definition") && Get(prompt, "r0.feature_id") == "feature-b" &&
      Get(prompt, "r0.feature_revision") == "1", "compact row retains selected feature");
  Check(Get(prompt, "r0.provider_package") == "package" &&
      Get(prompt, "r0.provider_app") == "app-b", "provider comes from registered definition owner");
  for (const char* field : {"approval_version", "request_kind", "selection_id",
      "selection_revision", "selection_digest", "grant_mode"})
    Check(Get(prompt, field) == Get(request, field), "displayed selection snapshot is unchanged");
  auto final = fixture.Call(fixture.ui, fixture.Response(prompt));
  Check(Get(final, "decision") == "ALLOWED", "missing-only approval completes full AND");
  request["method"] = "check";
  request["mode"] = "AUTHORIZE";
  auto authorized = fixture.Call(fixture.checker, request);
  Check(Get(authorized, "decision") == "ALLOWED" && !Get(authorized, "receipt").empty(),
      "trusted enforcer authorizes exact selected snapshot");
  auto retry = fixture.Call(fixture.checker, request);
  Check(Get(retry, "retry") == "1" && Get(retry, "receipt") == Get(authorized, "receipt"),
      "exact AUTHORIZE retry reuses one receipt without consuming again");
  Check(Get(fixture.Authorize("b"), "decision") == "CONSENT_REQUIRED",
      "ONCE is consumed exactly once after approval");
  Check(Get(fixture.Authorize("a"), "decision") == "ALLOWED",
      "broader preexisting A grant is unchanged");
  auto imposter = fixture.checker;
  imposter.identity = "untrusted-checker";
  fixture.Call(imposter, request, -EACCES);
  std::cout << "PASS missing-only provider snapshot, trusted AUTHORIZE and exact ONCE retry\n";
}

void InvalidatedWhileDisplaying() {
  Fixture fixture;
  fixture.Seed("PERSISTENT");
  auto pending = fixture.Call(fixture.argo, fixture.Selection("TASK", "ONCE", true));
  auto prompt = fixture.Prompt(pending);
  fixture.Call(fixture.admin, {{"method", "revoke"}, {"subject", "subject"},
      {"profile", "profile"}, {"definition", "a"}});
  auto final = fixture.Call(fixture.ui, fixture.Response(prompt));
  Check(Get(final, "decision") == "INVALIDATED" &&
      Get(final, "r0.decision") == "CONSENT_REQUIRED" &&
      Get(final, "r1.decision") == "ALLOWED", "revoked hidden A invalidates full AND, retains approved B");
  auto stored = fixture.Result(pending);
  Check(Get(stored, "decision") == "INVALIDATED" && Get(stored, "r1.decision") == "ALLOWED",
      "terminal result preserves approved B without automatic reprompt");
  fixture.Call(fixture.ui, {{"method", "get_prompt"}, {"request_id", Get(pending, "request_id")},
      {"approval_version", "1"}, {"locale", "en-US"}}, -ESTALE);
  Check(Get(fixture.Authorize("b"), "decision") == "ALLOWED" &&
      Get(fixture.Authorize("b"), "decision") == "CONSENT_REQUIRED",
      "only explicitly displayed B gains one allowance");
  Check(Get(fixture.Authorize("a"), "decision") == "CONSENT_REQUIRED",
      "hidden revoked A is never silently regranted");
  std::cout << "PASS hidden A revocation invalidates full AND while retaining only approved B\n";
}

void CoveredDuringDisplayDoesNotDuplicate() {
  Fixture fixture;
  fixture.Seed("PERSISTENT");
  auto pending = fixture.Call(fixture.argo, fixture.Selection("TASK", "ONCE", true));
  auto prompt = fixture.Prompt(pending);
  fixture.Seed("ONCE", "b");
  auto final = fixture.Call(fixture.ui, fixture.Response(prompt));
  Check(Get(final, "decision") == "ALLOWED", "another request may satisfy displayed B");
  Check(Get(fixture.Authorize("b"), "decision") == "ALLOWED" &&
      Get(fixture.Authorize("b"), "decision") == "CONSENT_REQUIRED",
      "respond does not duplicate an ONCE grant acquired while UI was open");
  std::cout << "PASS concurrently satisfied B does not receive an additional ONCE grant\n";
}

void CoveredBeforePromptFinishesNormally() {
  Fixture fixture;
  fixture.Seed("PERSISTENT");
  auto pending = fixture.Call(fixture.argo, fixture.Selection("TASK", "ONCE", true));
  fixture.Seed("ONCE", "b");
  auto terminal = fixture.Prompt(pending);
  Check(Get(terminal, "decision") == "ALLOWED" && !terminal.count("prompt_token") &&
      !terminal.count("r0.definition"), "concurrently satisfied selection returns terminal result, no empty UI");
  auto stored = fixture.Result(pending);
  Check(Get(stored, "decision") == "ALLOWED" && Get(stored, "r0.decision") == "ALLOWED" &&
      Get(stored, "r1.decision") == "ALLOWED", "all-satisfied terminal result preserves complete AND");
  Check(Get(fixture.Authorize("b"), "decision") == "ALLOWED" &&
      Get(fixture.Authorize("b"), "decision") == "CONSENT_REQUIRED",
      "terminal prompt handling creates no extra ONCE grant");
  std::cout << "PASS all rows covered before get_prompt finish normally without a token or duplicate grant\n";
}

void MissingOnlyModePolicy() {
  Fixture fixture(false);
  fixture.Register("a", "1", {{"modes", "ONCE"}});
  fixture.Register("b", "1", {{"modes", "SESSION"}});
  fixture.OpenSession();
  fixture.Seed("ONCE");
  auto pending = fixture.Call(fixture.argo, fixture.Selection("TASK", "SESSION", true));
  auto prompt = fixture.Prompt(pending);
  Check(Get(prompt, "count") == "1" && Get(prompt, "r0.original_index") == "1" &&
      Get(prompt, "grant_mode") == "SESSION", "only missing B must permit selected SESSION mode");
  auto result = fixture.Call(fixture.ui, fixture.Response(prompt));
  Check(Get(result, "decision") == "ALLOWED", "hidden ONCE-only A does not reject SESSION approval for B");
  Check(Get(fixture.Authorize("b"), "decision") == "ALLOWED" &&
      Get(fixture.Authorize("b"), "decision") == "ALLOWED", "B receives reusable SESSION grant");
  Check(Get(fixture.Authorize("a"), "decision") == "ALLOWED" &&
      Get(fixture.Authorize("a"), "decision") == "CONSENT_REQUIRED", "A retains exactly its original ONCE");
  std::cout << "PASS selected grant mode applies only to missing rows, preserving hidden grant policy\n";
}

void CompactTypedArguments() {
  Fixture fixture(false);
  fixture.Register("a");
  fixture.Register("b", "1", {{"template_version", "1"},
      {"parameter.days.source", "scope"}, {"parameter.days.type", "integer"},
      {"parameter.days.min", "1"}, {"parameter.days.max", "365"},
      {"parameter.recipient.source", "recipient"}, {"parameter.recipient.type", "string"},
      {"parameter.recipient.max_bytes", "64"},
      {"message.en-US.title", "Read {days} days"},
      {"message.en-US.body", "Send to {recipient}"}});
  fixture.Seed("PERSISTENT");
  auto request = fixture.Selection("TASK", "ONCE", true);
  request["r1.scope"] = "30";
  request["r1.recipient"] = "local{name}<tag>";
  Fixture::Seal(&request);
  auto pending = fixture.Call(fixture.argo, request);
  auto prompt = fixture.Call(fixture.ui, {{"method", "get_prompt"},
      {"request_id", Get(pending, "request_id")}, {"locale", "en-US"},
      {"approval_version", "1"}, {"template_version", "1"}});
  Check(Get(prompt, "count") == "1" && Get(prompt, "r0.original_index") == "1" &&
      Get(prompt, "r0.template_version") == "1" && Get(prompt, "r0.arg_count") == "2" &&
      Get(prompt, "r0.arg0.name") == "days" && Get(prompt, "r0.arg0.type") == "integer" &&
      Get(prompt, "r0.arg0.value") == "30" && Get(prompt, "r0.arg1.name") == "recipient" &&
      Get(prompt, "r0.arg1.value") == "local{name}<tag>" && !prompt.count("r1.arg_count"),
      "typed source row r1 is remapped completely to compact displayed row r0");
  std::string title;
  std::string body;
  Check(consent::localization::FormatPrompt(prompt, 0, "title", &title) &&
      consent::localization::FormatPrompt(prompt, 0, "body", &body) &&
      title == "Read 30 days" && body == "Send to local{name}<tag>",
      "actual formatter uses remapped typed values once as plain text");
  Check(Get(fixture.Call(fixture.ui, fixture.Response(prompt)), "decision") == "ALLOWED",
      "compact typed prompt remains bound to original request row");
  std::cout << "PASS compact typed row preserves original arguments and exact plain-text formatting\n";
}

Message ManyRows(Fixture* fixture, unsigned count) {
  auto request = fixture->Selection();
  const auto original = request;
  request["count"] = std::to_string(count);
  for (unsigned i = 0; i < count; ++i) {
    const auto row = "r" + std::to_string(i) + ".";
    for (const char* key : {"definition", "policy_version", "scope", "operation", "purpose",
        "recipient", "holder", "feature_id", "feature_revision"})
      request[row + key] = Get(original, std::string("r0.") + key);
    request[row + "scope"] = "scope-" + std::to_string(i);
    request[row + "feature_id"] = "feature-" + std::to_string(i);
  }
  Fixture::Seal(&request);
  return request;
}

void PromptBoundsPreserveToken() {
  for (unsigned count : {11U, 12U, 16U}) {
    Fixture fixture;
    auto pending = fixture.Call(fixture.argo, ManyRows(&fixture, count));
    auto prompt = fixture.Call(fixture.ui, {{"method", "get_prompt"},
        {"request_id", Get(pending, "request_id")}, {"locale", "en-US"},
        {"approval_version", "1"}}, count == 11 ? 0 : -E2BIG);
    if (count == 11) {
      Check(Get(prompt, "count") == "11" && !Get(prompt, "prompt_token").empty(),
          "eleven short literal selected rows fit existing whole-prompt field budget");
    } else {
      Check(!prompt.count("prompt_token") &&
          Get(fixture.Result(pending), "decision") == "PENDING",
          "twelve or sixteen selected rows fail closed before token publication");
    }
  }
  Fixture fixture(false);
  fixture.Register("a", "1", {{"message.ko-KR.title", std::string(3000, 'T')},
      {"message.ko-KR.body", std::string(3000, 'B')}});
  auto pending = fixture.Call(fixture.argo, ManyRows(&fixture, 11));
  auto valid = fixture.Prompt(pending);
  fixture.Call(fixture.ui, {{"method", "get_prompt"},
      {"request_id", Get(pending, "request_id")}, {"locale", "ko-KR"},
      {"approval_version", "1"}}, -E2BIG);
  auto result = fixture.Call(fixture.ui, fixture.Response(valid));
  Check(Get(result, "decision") == "ALLOWED", "whole-prompt byte overflow preserves previous valid token");
  std::cout << "PASS selected prompt field/frame bounds preserve the previous valid UI token\n";
}

void ResponseTamperAndCapability() {
  Fixture fixture;
  auto request = fixture.Selection("PREAPPROVAL", "TIMED", false, "b", "1000");
  auto pending = fixture.Call(fixture.argo, request);
  fixture.Reject(fixture.ui, {{"method", "get_prompt"},
      {"request_id", Get(pending, "request_id")}, {"locale", "en-US"}});
  auto prompt = fixture.Prompt(pending);
  auto response = fixture.Response(prompt);
  const Message changes{{"approval_version", "2"}, {"request_kind", "TASK"},
      {"selection_id", "different-selection"}, {"selection_revision", "2"},
      {"selection_digest", std::string(64, '0')}, {"grant_mode", "ONCE"}, {"duration_ms", "2000"}};
  for (const auto& field : changes) {
    auto changed = response;
    changed[field.first] = field.second;
    fixture.Call(fixture.ui, changed, -EACCES);
    Check(Get(fixture.Result(pending), "decision") == "PENDING", "response tamper leaves request pending");
  }
  for (const char* field : {"approval_version", "selection_digest", "grant_mode", "duration_ms"}) {
    auto missing = response;
    missing.erase(field);
    fixture.Call(fixture.ui, missing, -EACCES);
  }
  auto final = fixture.Call(fixture.ui, response);
  Check(Get(final, "decision") == "ALLOWED", "unaltered displayed metadata still succeeds after rejection");
  std::cout << "PASS prompt capability and response selection/mode/duration immutability\n";
}

void RetryAndMalformedSelection() {
  Fixture fixture;
  auto request = fixture.Selection("TASK", "ONCE");
  auto pending = fixture.Call(fixture.argo, request);
  auto retry = fixture.Call(fixture.argo, request);
  Check(Get(retry, "request_id") == Get(pending, "request_id"), "exact selection retry retains request ID");
  for (const auto& field : Message{{"selection_id", "another-selection"},
      {"selection_revision", "2"}, {"r0.feature_id", "another-feature"},
      {"r0.feature_revision", "2"}, {"r0.scope", "another-scope"}}) {
    auto changed = request;
    changed[field.first] = field.second;
    Fixture::Seal(&changed);
    fixture.Call(fixture.argo, changed, CONSENT_ERROR_CONFLICT);
  }
  std::vector<Message> invalid;
  for (const char* field : {"approval_version", "selection_digest", "selection_id",
      "selection_revision", "r0.feature_id", "r0.feature_revision", "r0.policy_version"}) {
    auto missing = fixture.Selection();
    missing.erase(field);
    if (std::string(field) != "selection_digest")
      Fixture::Seal(&missing);
    invalid.push_back(std::move(missing));
  }
  for (const auto& field : Message{{"approval_version", "2"}, {"grant_mode", "PERSISTENT"},
      {"duration_ms", "1000"}, {"selection_digest", std::string(64, 'f')},
      {"_approval_deadline", "9999999999999"}}) {
    auto changed = fixture.Selection();
    changed[field.first] = field.second;
    if (field.first != "selection_digest")
      Fixture::Seal(&changed);
    invalid.push_back(std::move(changed));
  }
  for (const auto& malformed : invalid)
    fixture.Reject(fixture.argo, malformed);
  for (const char* duration : {"", "99", "3600001", "-1"})
    fixture.Reject(fixture.argo, fixture.Selection("PREAPPROVAL", "TIMED", false, "a", duration));
  auto stale = fixture.Selection();
  stale["r0.policy_version"] = "2";
  Fixture::Seal(&stale);
  fixture.Call(fixture.argo, stale, -ESTALE);
  std::cout << "PASS stable retry binding, malformed opt-in rejection and explicit policy freshness\n";
}

void PolicyChangeAndDeniedRows() {
  Fixture fixture;
  fixture.Seed("PERSISTENT");
  auto pending = fixture.Call(fixture.argo, fixture.Selection("TASK", "ONCE", true));
  auto prompt = fixture.Prompt(pending);
  auto response = fixture.Response(prompt);
  response["decision"] = "DENIED";
  auto denied = fixture.Call(fixture.ui, response);
  Check(Get(denied, "decision") == "DENIED" && Get(denied, "r0.decision") == "ALLOWED" &&
      Get(denied, "r1.decision") == "DENIED", "only missing B is denied, existing A remains allowed");
  Check(Get(fixture.Authorize("b"), "decision") == "CONSENT_REQUIRED", "Deny never creates B grant");
  auto next = fixture.Call(fixture.argo, fixture.Selection("TASK", "ONCE", true));
  auto old_prompt = fixture.Prompt(next);
  fixture.Register("b", "2");
  fixture.Call(fixture.ui, fixture.Response(old_prompt), -ESTALE);
  Check(Get(fixture.Result(next), "decision") == "INVALIDATED", "policy update invalidates selection snapshot");
  std::cout << "PASS missing-only denial and policy update invalidation\n";
}

void TimedAdmissionIsNotSliding() {
  Fixture fixture;
  fixture.Seed("TIMED", "a", "4000");
  auto request = fixture.Selection("PREAPPROVAL", "TIMED", true, "a", "3000");
  auto pending = fixture.Call(fixture.argo, request);
  auto prompt = fixture.Prompt(pending);
  Check(Get(prompt, "count") == "1" && Get(prompt, "r0.original_index") == "1",
      "A covers the originally admitted timed window");
  g_usleep(1500000);
  auto refreshed = fixture.Prompt(pending);
  Check(Get(refreshed, "count") == "1" && Get(refreshed, "r0.original_index") == "1",
      "prompt refresh does not slide the requested timed coverage window");
  auto final = fixture.Call(fixture.ui, fixture.Response(refreshed));
  Check(Get(final, "decision") == "ALLOWED", "respond uses stored admission for timed coverage");
  std::cout << "PASS PREAPPROVAL timed coverage stays anchored to stored admission\n";
}
}  // namespace

int main() {
  try {
    ExistingGrantCoverage();
    SessionReuse();
    SessionControllerAndArgoRoles();
    MultipleGrantCandidates();
    SelectionDigestBinding();
    PublicResultMetadata();
    CompactPromptAndAuthorize();
    InvalidatedWhileDisplaying();
    CoveredDuringDisplayDoesNotDuplicate();
    CoveredBeforePromptFinishesNormally();
    MissingOnlyModePolicy();
    CompactTypedArguments();
    PromptBoundsPreserveToken();
    ResponseTamperAndCapability();
    RetryAndMalformedSelection();
    PolicyChangeAndDeniedRows();
    TimedAdmissionIsNotSliding();
  } catch (const std::exception& error) {
    std::cerr << "FAIL repository feature selection: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
