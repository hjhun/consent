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

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using consent::Get;
using consent::Message;
using consentd::Peer;
using consentd::Repository;

void Check(bool condition, const char* description) {
  if (!condition)
    throw std::runtime_error(description);
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
  Fixture() {
    char pattern[] = "/tmp/consent-ui-XXXXXX";
    auto* directory = mkdtemp(pattern);
    Check(directory != nullptr, "create isolated UI repository directory");
    directory_ = directory;
    registry_ = directory_ + "/registry";
    Check(mkdir(registry_.c_str(), 0700) == 0, "create UI recovery registry");
    repository = std::make_unique<Repository>(directory_ + "/consent.db", registry_);
    RestoreValidator();
    std::string error;
    Check(repository->Open(&error), error.c_str());
    Register("a", "app-a");
    Register("b", "app-b");
    Check(ui.roles.size() == 1 && ui.roles.count("ui") == 1,
        "approval UI must not acquire checker or administrator roles");
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

  void RestoreValidator() {
    repository->SetInstallationValidator([](const std::string& package,
        const std::string& app, const std::string& generation) {
      return package == "package" && (app == "app-a" || app == "app-b") &&
          generation == "generation";
    });
  }
  Message Call(const Peer& peer, const Message& request, int expected = 0) {
    auto result = repository->Execute(peer, request);
    if (Get(result, "status") != std::to_string(expected)) {
      std::cerr << Get(request, "method") << ": expected status=" << expected
          << " actual=" << Get(result, "status") << " reason=" << Get(result, "reason") << '\n';
      throw std::runtime_error("unexpected repository status");
    }
    return result;
  }
  Message Context(const std::string& method, bool pair = false,
      const std::string& definition = "a") {
    const auto sequence = std::to_string(++sequence_);
    Message request{{"method", method}, {"subject", "subject"}, {"profile", "profile"},
        {"operation_id", "operation-" + sequence}, {"step_id", "step-" + sequence},
        {"client_request_id", "request-" + sequence}, {"mode", "QUERY"},
        {"count", pair ? "2" : "1"}, {"r0.definition", definition},
        {"r0.operation", "read"}, {"r0.scope", "scope"}, {"r0.purpose", "answer"}};
    if (pair) {
      request["r1.definition"] = "b";
      request["r1.operation"] = "read";
      request["r1.scope"] = "scope";
      request["r1.purpose"] = "answer";
    }
    return request;
  }
  Message Prompt(const Message& request) {
    auto pending = Call(argo, request);
    Check(Get(pending, "decision") == "PENDING", "request must await UI");
    if (Get(request, "count") == "2") {
      Check(Get(pending, "r0.decision") == "ALLOWED", "A initially allowed");
      Check(Get(pending, "r1.decision") == "CONSENT_REQUIRED", "B initially missing");
    }
    auto prompt = Call(ui, {{"method", "get_prompt"},
        {"request_id", Get(pending, "request_id")}, {"locale", "en"}});
    Check(!Get(prompt, "prompt_token").empty(), "prompt token exists");
    return {{"method", "respond"}, {"request_id", Get(pending, "request_id")},
        {"prompt_token", Get(prompt, "prompt_token")}, {"decision", "ALLOWED"},
        {"grant_mode", "ONCE"}};
  }
  std::string Seed(const std::string& mode) {
    auto response = Prompt(Context("request"));
    response["grant_mode"] = mode;
    response["duration_ms"] = "1000";
    auto approved = Call(ui, response);
    Check(Get(approved, "decision") == "ALLOWED" && Get(approved, "r0.decision") == "ALLOWED",
        "initial A approval succeeds");
    return Get(response, "request_id");
  }
  Message ReadResult(const std::string& request) {
    return Call(argo, {{"method", "get_request_result"}, {"request_id", request}});
  }
  Message Query(const std::string& definition) {
    return Call(checker, Context("check", false, definition));
  }
  Message Authorize(bool pair = false, const std::string& definition = "a") {
    auto request = Context("check", pair, definition);
    request["mode"] = "AUTHORIZE";
    return Call(checker, request);
  }
  void RevokeA() {
    Call(admin, {{"method", "revoke"}, {"subject", "subject"},
        {"profile", "profile"}, {"definition", "a"}});
  }
  void Historical(const std::string& request) {
    auto historical = ReadResult(request);
    Check(Get(historical, "decision") == "ALLOWED" &&
        Get(historical, "r0.decision") == "ALLOWED",
        "already terminal approval must not be rewritten by later authorization changes");
  }

  std::unique_ptr<Repository> repository;
  Peer installer = Identity("installer", "installer");
  Peer argo = Identity("argo", "argo");
  Peer checker = Identity("checker", "checker");
  Peer ui = Identity("approval-ui", "ui");
  Peer admin = Identity("administrator", "admin");

 private:
  void Register(const std::string& definition, const std::string& app) {
    Call(installer, {{"method", "register"}, {"operation_id", "install-" + definition},
        {"definition", definition}, {"package", "package"}, {"app", app},
        {"enforcer", "checker"}, {"expected_generation", "generation"},
        {"_install_identity", "generation"}, {"policy_version", "1"},
        {"text_revision", "1"}, {"level", "1"}, {"modes", "ONCE,TIMED,PERSISTENT"},
        {"default_locale", "en"}, {"message.en.title", "Approve exact scope"},
        {"message.en.body", "Read the displayed scope for answering"}});
  }
  std::string directory_;
  std::string registry_;
  unsigned sequence_ = 0;
};

void ChangedWhileWaiting(const std::string& change) {
  Fixture fixture;
  const std::string mode = change == "expired" ? "TIMED" :
      change == "consumed" ? "ONCE" : "PERSISTENT";
  auto historical = fixture.Seed(mode);
  auto response = fixture.Prompt(fixture.Context("request", true));
  if (change == "revoked")
    fixture.RevokeA();
  else if (change == "expired")
    g_usleep(1100000);
  else
    Check(Get(fixture.Authorize(), "decision") == "ALLOWED", "another operation consumes A ONCE");
  auto final = fixture.Call(fixture.ui, response);
  Check(Get(final, "decision") == "INVALIDATED", "stale A makes the whole AND terminal INVALIDATED");
  Check(Get(final, "reason") == "authorization conditions changed while awaiting approval",
      "invalidation explains changed conditions");
  Check(Get(final, "r0.decision") == "CONSENT_REQUIRED", "A reports its current missing approval");
  Check(Get(final, "r1.decision") == "ALLOWED", "new B approval remains represented");
  Check(Get(final, "cacheable") == "0", "terminal invalidation cannot be cached as allowed");
  auto stored = fixture.ReadResult(Get(response, "request_id"));
  for (const char* field : {"decision", "reason", "r0.decision", "r1.decision"})
    Check(Get(stored, field) == Get(final, field), "stored terminal result preserves final UI evaluation");
  fixture.Call(fixture.ui, {{"method", "get_prompt"}, {"locale", "en"},
      {"request_id", Get(response, "request_id")}}, -ESTALE);
  Check(Get(fixture.Query("a"), "decision") == "CONSENT_REQUIRED", "B approval does not regrant stale A");
  Check(Get(fixture.Query("b"), "decision") == "ALLOWED", "new B grant survives the terminal overall failure");
  Check(Get(fixture.Authorize(false, "b"), "decision") == "ALLOWED", "UI reevaluation did not consume B ONCE");
  Check(Get(fixture.Authorize(false, "b"), "decision") == "CONSENT_REQUIRED", "B still has exactly one allowance");
  fixture.Historical(historical);
  std::cout << "PASS UI " << change << " A: INVALIDATED, current conditions, retained unused B, no reprompt\n";
}

void NormalApprovalDoesNotConsume() {
  Fixture fixture;
  auto historical = fixture.Seed("ONCE");
  auto response = fixture.Prompt(fixture.Context("request", true));
  auto final = fixture.Call(fixture.ui, response);
  Check(Get(final, "decision") == "ALLOWED" && Get(final, "r0.decision") == "ALLOWED" &&
      Get(final, "r1.decision") == "ALLOWED", "unchanged AND approval succeeds");
  auto authorized = fixture.Authorize(true);
  Check(Get(authorized, "decision") == "ALLOWED", "respond leaves both ONCE grants available");
  auto consumed = fixture.Authorize(true);
  Check(Get(consumed, "decision") == "CONSENT_REQUIRED" &&
      Get(consumed, "r0.decision") == "CONSENT_REQUIRED" &&
      Get(consumed, "r1.decision") == "CONSENT_REQUIRED", "respond did not duplicate existing A ONCE");
  fixture.Historical(historical);
  Check(Get(fixture.ReadResult(Get(response, "request_id")), "decision") == "ALLOWED",
      "later grant consumption does not rewrite successful terminal request");
  std::cout << "PASS UI normal AND: no ONCE consumption or duplicate prior allowance\n";
}

void DenialUsesCurrentPriorConditions(bool revoke) {
  Fixture fixture;
  auto historical = fixture.Seed("PERSISTENT");
  auto response = fixture.Prompt(fixture.Context("request", true));
  if (revoke)
    fixture.RevokeA();
  response["decision"] = "DENIED";
  auto denied = fixture.Call(fixture.ui, response);
  Check(Get(denied, "decision") == "DENIED", "UI refusal is terminal DENIED");
  Check(Get(denied, "r0.decision") == (revoke ? "CONSENT_REQUIRED" : "ALLOWED"),
      "prior A is reevaluated instead of blindly preserved or relabelled DENIED");
  Check(Get(denied, "r1.decision") == "DENIED", "only newly refused B is labelled DENIED");
  Check(Get(fixture.Query("b"), "decision") == "CONSENT_REQUIRED", "UI refusal creates no B grant");
  auto stored = fixture.ReadResult(Get(response, "request_id"));
  for (const char* field : {"decision", "r0.decision", "r1.decision"})
    Check(Get(stored, field) == Get(denied, field), "stored denial preserves current per-condition results");
  fixture.Historical(historical);
  std::cout << "PASS UI DENIED with " << (revoke ? "revoked" : "still-allowed") << " A\n";
}

void ReevaluationFailureRollsBack() {
  Fixture fixture;
  fixture.Seed("PERSISTENT");
  auto response = fixture.Prompt(fixture.Context("request", true));
  unsigned calls_for_a = 0;
  // The first A check validates the displayed policy. The second observes an
  // authority change during final reevaluation, after the tentative B INSERT.
  // This public dependency models a real changing authority, not a product
  // fault-injection hook or direct edit to private SQLite grant records.
  fixture.repository->SetInstallationValidator([&calls_for_a](const std::string&,
      const std::string& app, const std::string&) {
    return app != "app-a" || ++calls_for_a == 1;
  });
  auto failed = fixture.Call(fixture.ui, response, -ESTALE);
  Check(calls_for_a == 2, "authority failure occurred inside final reevaluation");
  Check(!failed.count("decision") && !failed.count("r1.decision"),
      "reevaluation exception returns an error, never a fabricated decision");
  fixture.RestoreValidator();
  Check(Get(fixture.Query("b"), "decision") == "CONSENT_REQUIRED", "tentative B grant rolled back");
  Check(Get(fixture.ReadResult(Get(response, "request_id")), "decision") == "PENDING",
      "failed reevaluation leaves the request pending");
  auto retried = fixture.Call(fixture.ui, response);
  Check(Get(retried, "decision") == "ALLOWED", "same prompt token remains retryable after rollback");
  Check(Get(fixture.Authorize(false, "b"), "decision") == "ALLOWED", "retry commits B exactly once");
  Check(Get(fixture.Authorize(false, "b"), "decision") == "CONSENT_REQUIRED", "failed attempt leaked no extra ONCE grant");
  std::cout << "PASS UI reevaluation failure: transaction rollback, no fabricated decision, same-token retry\n";
}
}  // namespace

int main() {
  try {
    ChangedWhileWaiting("revoked");
    ChangedWhileWaiting("expired");
    ChangedWhileWaiting("consumed");
    NormalApprovalDoesNotConsume();
    DenialUsesCurrentPriorConditions(false);
    DenialUsesCurrentPriorConditions(true);
    ReevaluationFailureRollsBack();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL UI repository: " << error.what() << '\n';
    return 1;
  }
}
