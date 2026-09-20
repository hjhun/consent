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

#include "common/localization.hh"

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

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

class Fixture final {
 public:
  Fixture() {
    char pattern[] = "/tmp/consent-localization-XXXXXX";
    auto* directory = mkdtemp(pattern);
    Check(directory != nullptr, "create isolated localization fixture");
    directory_ = directory;
    Check(mkdir((directory_ + "/registry").c_str(), 0700) == 0, "create trusted registry");
    peer.identity = "actor";
    peer.instance = "actor-instance";
    peer.roles = {"installer", "argo", "checker", "ui", "holder", "session"};
    peer.subjects = {"subject"};
    peer.profiles = {"profile"};
    peer.packages = {"package"};
    Open();
  }
  ~Fixture() {
    repository.reset();
    for (const auto& path : {directory_ + "/registry", directory_}) {
      DIR* entries = opendir(path.c_str());
      if (!entries)
        continue;
      while (auto* entry = readdir(entries)) {
        std::string name = entry->d_name;
        if (name != "." && name != "..")
          unlink((path + "/" + name).c_str());
      }
      closedir(entries);
    }
    rmdir((directory_ + "/registry").c_str());
    rmdir(directory_.c_str());
  }
  void Open() {
    repository.reset(new consentd::Repository(directory_ + "/consent.db", directory_ + "/registry"));
    repository->SetInstallationValidator([](const std::string& package,
        const std::string& app, const std::string& generation) {
      return package == "package" && app == "app" && generation == "installation";
    });
    std::string error;
    Check(repository->Open(&error), error.c_str());
  }
  void DeleteDatabase() {
    Check(unlink((directory_ + "/consent.db").c_str()) == 0, "delete only isolated consent database");
  }
  Message Call(const Message& request, int expected = 0) {
    auto result = repository->Execute(peer, request);
    if (Get(result, "status") != std::to_string(expected)) {
      std::cerr << Get(request, "method") << ": expected " << expected << " actual "
          << Get(result, "status") << " reason=" << Get(result, "reason") << '\n';
      throw std::runtime_error("unexpected localization repository status");
    }
    Check(result.count("_prompt_locale") == 0, "private displayed locale must not escape repository");
    return result;
  }
  Message Definition(const std::string& id = "history") {
    return {{"method", "register"}, {"operation_id", "install-" + std::to_string(++sequence_)},
        {"package", "package"}, {"app", "app"}, {"definition", id}, {"enforcer", "actor"},
        {"_install_identity", "installation"}, {"expected_generation", "installation"},
        {"policy_version", "1"}, {"text_revision", "1"}, {"level", "1"},
        {"modes", "ONCE,TIMED,PERSISTENT"}, {"retention_ms", "60000"}, {"default_locale", "en"},
        {"template_version", "1"}, {"parameter.days.source", "scope"},
        {"parameter.days.type", "integer"}, {"parameter.days.min", "1"}, {"parameter.days.max", "365"},
        {"parameter.purpose.source", "purpose"}, {"parameter.purpose.type", "string"},
        {"parameter.purpose.max_bytes", "32"}, {"parameter.recipient.source", "recipient"},
        {"parameter.recipient.type", "string"}, {"parameter.recipient.max_bytes", "64"},
        {"parameter.retention.source", "retention_ms"}, {"parameter.retention.type", "integer"},
        {"parameter.retention.min", "0"}, {"parameter.retention.max", "86400000"},
        {"message.en.title", "Read {days} past days"},
        {"message.en.body", "For {purpose}, send to {recipient}; retain {retention} milliseconds"},
        {"message.ko.title", "지난 {days}일 조회"},
        {"message.ko.body", "목적 {purpose}, 수신 {recipient}, 취득 후 보관 {retention}밀리초"},
        {"locale_fallback.es-MX", "en"}};
  }
  Message Request(const std::string& method = "request", const std::string& definition = "history") {
    std::string sequence = std::to_string(++sequence_);
    Message request{{"method", method}, {"subject", "subject"}, {"profile", "profile"},
        {"client_request_id", "request-" + sequence}, {"operation_id", "operation-" + sequence},
        {"step_id", "step-" + sequence}, {"mode", "QUERY"}, {"count", "1"},
        {"r0.definition", definition}, {"r0.policy_version", "1"}, {"r0.scope", "30"},
        {"r0.operation", "read"}, {"r0.purpose", "answer"},
        {"r0.recipient", "sink{name}%<tag>"}, {"r0.holder", "actor"}};
    if (!session.empty()) {
      request["session"] = session;
      request["generation"] = "1";
    }
    return request;
  }
  void Session() {
    auto result = Call({{"method", "session_open"}, {"subject", "subject"}, {"profile", "profile"}});
    session = Get(result, "session");
  }
  Message Prompt(const Message& pending, const std::string& locale = "en", int expected = 0) {
    return Call({{"method", "get_prompt"}, {"request_id", Get(pending, "request_id")},
        {"locale", locale}, {"template_version", "1"}}, expected);
  }
  Message Response(const Message& prompt, const std::string& mode = "PERSISTENT") {
    return {{"method", "respond"}, {"request_id", Get(prompt, "request_id")},
        {"prompt_token", Get(prompt, "prompt_token")}, {"locale", Get(prompt, "locale")},
        {"decision", "ALLOWED"}, {"grant_mode", mode}};
  }
  void Approve(const Message& request, const std::string& mode = "PERSISTENT") {
    auto pending = Call(request);
    Check(Get(pending, "decision") == "PENDING", "unapproved exact typed context must pend");
    auto result = Call(Response(Prompt(pending), mode));
    Check(Get(result, "decision") == "ALLOWED", "typed approval succeeds");
  }
  Message Data(const std::string& method) {
    return {{"method", method}, {"subject", "subject"}, {"profile", "profile"},
        {"session", session}, {"generation", "1"}, {"scope", "30"}, {"purpose", "answer"},
        {"recipient", "sink{name}%<tag>"}};
  }
  void Update(Message definition, int expected = 0) {
    definition["method"] = "update";
    definition["operation_id"] = "update-" + std::to_string(++sequence_);
    Call(definition, expected);
  }

  std::unique_ptr<consentd::Repository> repository;
  consentd::Peer peer;
  std::string session;

 private:
  std::string directory_;
  unsigned sequence_ = 0;
};

void BoundValues() {
  Fixture fixture;
  fixture.Call(fixture.Definition());
  for (const char* method : {"request", "check"}) {
    auto request = fixture.Request(method);
    request.erase("r0.policy_version");
    fixture.Call(request, -ESTALE);
    request["r0.policy_version"] = "2";
    fixture.Call(request, -ESTALE);
    request["r0.policy_version"] = "1";
    for (const char* value : {"030", "+30", " 30", "3e1", "0", "366", "9223372036854775808"}) {
      request["r0.scope"] = value;
      fixture.Call(request, -EINVAL);
    }
    request["r0.scope"] = "30";
    request["r0.recipient"] = std::string(65, 'x');
    fixture.Call(request, -EINVAL);
    request["r0.recipient"] = std::string(64, 'x');
    fixture.Call(request);
    request.erase("r0.recipient");
    fixture.Call(request, -EINVAL);
  }
  for (const char* key : {"display_args", "display_args.days", "r0.display_args",
      "r0.display_args.days", "r0.arg_count", "r0.arg0.value"}) {
    auto request = fixture.Request("check");
    request[key] = "90";
    fixture.Call(request, -EINVAL);
  }
  auto original = fixture.Request();
  auto pending = fixture.Call(original);
  auto retry = original;
  retry.erase("r0.policy_version");
  fixture.Call(retry, -ESTALE);
  auto prompt = fixture.Prompt(pending);
  Check(Get(prompt, "r0.arg_count") == "4" && Get(prompt, "r0.arg0.name") == "days" &&
      Get(prompt, "r0.arg0.type") == "integer" && Get(prompt, "r0.arg0.value") == "30" &&
      Get(prompt, "r0.arg3.name") == "retention" && Get(prompt, "r0.arg3.value") == "60000",
      "prompt descriptors derive exact query scope and independent policy retention");
  std::string rendered;
  Check(consent::localization::FormatPrompt(prompt, 0, "body", &rendered) &&
      rendered == "For answer, send to sink{name}%<tag>; retain 60000 milliseconds",
      "bound values containing braces, percent and markup remain literal");
  fixture.Call(fixture.Response(prompt));
  auto query = fixture.Request("check");
  Check(Get(fixture.Call(query), "decision") == "ALLOWED", "approved exact typed scope is queryable");
  query["r0.scope"] = "90";
  Check(Get(fixture.Call(query), "decision") == "CONSENT_REQUIRED", "larger typed scope has no grant");
  query["mode"] = "AUTHORIZE";
  Check(Get(fixture.Call(query), "decision") == "CONSENT_REQUIRED", "larger scope cannot authorize");
  std::cout << "PASS typed schema requires explicit version and canonical source values before request retries\n";
}

void LocaleTokenBinding() {
  Fixture fixture;
  auto definition = fixture.Definition();
  definition["message.zh-Hant.title"] = "Chinese {days}";
  definition["message.zh-Hant.body"] = "{purpose} {recipient} {retention}";
  fixture.Call(definition);
  auto pending = fixture.Call(fixture.Request());
  auto prompt = fixture.Prompt(pending, "ko-KR");
  Check(Get(prompt, "locale") == "ko-KR" && Get(prompt, "r0.locale") == "ko",
      "requested locale and selected locale are distinct bindings");
  auto response = fixture.Response(prompt);
  response["locale"] = "ko";
  fixture.Call(response, -EACCES);
  response.erase("locale");
  fixture.Call(response, -EACCES);
  auto next = fixture.Prompt(pending, "es-MX");
  Check(Get(next, "r0.locale") == "en", "registered direct locale alias resolves");
  fixture.Call(fixture.Response(prompt), -EACCES);
  auto script = fixture.Prompt(pending, "zh-Hant-HK");
  Check(Get(script, "r0.locale") == "en", "unregistered script-region locale does not infer stripping");
  auto valid = fixture.Prompt(pending, "ko");
  for (const char* version : {"", "2"}) {
    Message invalid{{"method", "get_prompt"}, {"request_id", Get(pending, "request_id")}, {"locale", "en"}};
    if (*version)
      invalid["template_version"] = version;
    fixture.Call(invalid, -EINVAL);
  }
  fixture.Prompt(pending, "invalid/locale", -EINVAL);
  fixture.Call(fixture.Response(valid));
  auto final = fixture.Call({{"method", "get_request_result"}, {"request_id", Get(pending, "request_id")}});
  Check(Get(final, "decision") == "ALLOWED", "invalid capability and locale attempts preserve prior valid token");
  std::cout << "PASS explicit alias, requested locale and latest token bind approval; failed prompts leave token intact\n";
}

void PromptBudgets() {
  Fixture fixture;
  auto definition = fixture.Definition("expanded");
  for (auto it = definition.begin(); it != definition.end();) {
    if (it->first.compare(0, 10, "parameter.") == 0 || it->first.compare(0, 8, "message.") == 0)
      it = definition.erase(it);
    else
      ++it;
  }
  definition["parameter.value.source"] = "scope";
  definition["parameter.value.type"] = "string";
  definition["parameter.value.max_bytes"] = "512";
  definition["message.en.title"] = "Value";
  definition["message.en.body"] = "{value}";
  definition["message.ko.title"] = "Value";
  for (int i = 0; i < 17; ++i)
    definition["message.ko.body"] += "{value}";
  fixture.Call(definition);
  auto request = fixture.Request("request", "expanded");
  request["r0.scope"] = std::string(512, 'x');
  auto pending = fixture.Call(request);
  auto valid = fixture.Prompt(pending);
  fixture.Prompt(pending, "ko", -E2BIG);
  fixture.Call(fixture.Response(valid));
  auto short_request = fixture.Request("request", "expanded");
  short_request["r0.scope"] = "short";
  auto short_pending = fixture.Call(short_request);
  fixture.Call(fixture.Response(fixture.Prompt(short_pending, "ko")));

  // Several individually bounded rows still must fit one bounded protocol frame.
  auto big = fixture.Definition("large");
  big["message.en.body"] += std::string(3900, 'x');
  fixture.Call(big);
  auto many = fixture.Request("request", "large");
  many["count"] = "16";
  for (int i = 0; i < 16; ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    for (const char* key : {"definition", "policy_version", "scope", "operation", "purpose", "recipient", "holder"})
      many[prefix + key] = Get(many, std::string("r0.") + key);
    many[prefix + "scope"] = std::to_string(i + 1);
  }
  auto many_pending = fixture.Call(many);
  fixture.Prompt(many_pending, "en", -E2BIG);

  // Locale-dependent wire overflow must not replace a token for a short locale.
  auto wire = definition;
  wire["definition"] = "wire";
  wire["operation_id"] = "install-wire";
  wire["message.ko.title"] = std::string(4090, 'x');
  wire["message.ko.body"] = std::string(4080, 'x') + "{value}";
  fixture.Call(wire);
  auto rows = fixture.Request("request", "wire");
  rows["count"] = "9";
  for (int i = 0; i < 9; ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    for (const char* key : {"definition", "policy_version", "operation", "purpose", "recipient", "holder"})
      rows[prefix + key] = Get(rows, std::string("r0.") + key);
    rows[prefix + "scope"] = std::to_string(i + 1);
  }
  auto rows_pending = fixture.Call(rows);
  auto short_prompt = fixture.Prompt(rows_pending, "en");
  fixture.Prompt(rows_pending, "ko", -E2BIG);
  fixture.Call(fixture.Response(short_prompt));
  std::cout << "PASS selected expansion and aggregate prompt budgets reject before token update, short values remain valid\n";
}

void RevisionContract() {
  Fixture fixture;
  auto definition = fixture.Definition();
  definition["text_revision"] = "2";
  fixture.Call(definition);
  auto pending = fixture.Call(fixture.Request());
  auto token = fixture.Prompt(pending);
  auto changed = definition;
  changed["policy_version"] = "2";
  changed["text_revision"] = "1";
  fixture.Update(changed, -2005);
  for (const auto& field : Message{{"message.en.title", "New {days} title"},
      {"default_locale", "ko"}, {"locale_fallback.es-MX", "ko"}}) {
    changed = definition;
    changed["policy_version"] = "2";
    changed[field.first] = field.second;
    fixture.Update(changed, -2005);
  }
  changed = definition;
  changed["policy_version"] = "2";
  changed["text_revision"] = "3";
  changed["message.en.title"] = "New {days} title";
  fixture.Update(changed);
  fixture.Call(fixture.Response(token), -ESTALE);
  auto final = fixture.Call({{"method", "get_request_result"}, {"request_id", Get(pending, "request_id")}});
  Check(Get(final, "decision") == "INVALIDATED", "translation update invalidates existing pending request");

  fixture.repository->SetInstallationValidator([](const std::string& package,
      const std::string& app, const std::string& generation) {
    return package == "package" && app == "app" && generation == "new-installation";
  });
  changed["_install_identity"] = "new-installation";
  changed["expected_generation"] = "new-installation";
  changed["text_revision"] = "2";
  fixture.Update(changed, -2005);
  changed["text_revision"] = "3";
  fixture.Update(changed);
  auto query = fixture.Request("check");
  query["r0.policy_version"] = "2";
  Check(Get(fixture.Call(query), "decision") == "CONSENT_REQUIRED", "reinstall may retain identical text revision but no approval");
  std::cout << "PASS text revision never rolls back; changed text/default/aliases require independent increase across installs\n";
}

void PolicyAndRecovery() {
  Fixture fixture;
  auto definition = fixture.Definition();
  fixture.Call(definition);
  fixture.Approve(fixture.Request());
  auto pending_request = fixture.Request();
  pending_request["r0.scope"] = "40";
  auto pending = fixture.Call(pending_request);
  auto prompt = fixture.Prompt(pending);
  auto changed = definition;
  changed["parameter.days.max"] = "90";
  changed["text_revision"] = "2";
  fixture.Update(changed, -2005);
  changed = definition;
  changed["locale_fallback.es-MX"] = "ko";
  fixture.Update(changed, -2005);
  changed["text_revision"] = "2";
  fixture.Update(changed);
  fixture.Call(fixture.Response(prompt), -ESTALE);
  auto query = fixture.Request("check");
  Check(Get(fixture.Call(query), "decision") == "CONSENT_REQUIRED", "text alias update invalidates previous grants");
  auto schema = changed;
  schema["parameter.days.max"] = "90";
  schema["policy_version"] = "2";
  fixture.Update(schema);
  fixture.Call(query, -ESTALE);
  query["r0.policy_version"] = "2";
  auto approve = query;
  approve["method"] = "request";
  fixture.Approve(approve);
  auto before = fixture.Call(query);
  Check(Get(before, "decision") == "ALLOWED", "new policy approval exists");
  fixture.Open();
  Check(Get(fixture.Call(query), "decision") == "ALLOWED", "normal restart preserves persistent typed approval");
  fixture.DeleteDatabase();
  auto recovered = fixture.Call(query);
  Check(Get(recovered, "epoch") != Get(before, "epoch") &&
      Get(recovered, "decision") == "CONSENT_REQUIRED", "DB recovery restores typed schema without approvals");
  auto again = fixture.Call(approve);
  auto restored = fixture.Prompt(again, "es-MX");
  Check(Get(restored, "r0.locale") == "ko" && Get(restored, "r0.arg_count") == "4",
      "independent registry preserves typed schema and locale aliases");
  std::cout << "PASS schema changes require policy bump, aliases require text bump, registry recovery retains bindings only\n";
}

void ReceiptAndArtifactBinding() {
  Fixture fixture;
  fixture.Call(fixture.Definition());
  fixture.Session();
  auto request = fixture.Request();
  fixture.Approve(request, "ONCE");
  request["method"] = "check";
  request["mode"] = "AUTHORIZE";
  auto authorization = fixture.Call(request);
  Check(Get(authorization, "decision") == "ALLOWED", "typed ONCE authorization succeeds");
  auto retry = fixture.Call(request);
  Check(Get(retry, "receipt") == Get(authorization, "receipt"), "identical typed retry returns original receipt");
  auto changed = request;
  changed["r0.scope"] = "90";
  fixture.Call(changed, -2005);
  changed = request;
  changed.erase("r0.policy_version");
  fixture.Call(changed, -ESTALE);
  changed = request;
  changed["r0.scope"] = "030";
  fixture.Call(changed, -EINVAL);
  auto registration = fixture.Data("data_register");
  registration["receipt"] = Get(authorization, "receipt");
  registration["scope"] = "90";
  fixture.Call(registration, -EACCES);
  registration["scope"] = "30";
  auto artifact = fixture.Call(registration);
  auto use = fixture.Data("data_check");
  use["artifact"] = Get(artifact, "artifact");
  Check(Get(fixture.Call(use), "decision") == "ALLOWED", "consumed ONCE artifact retains exact authorized scope");
  use["scope"] = "90";
  fixture.Call(use, -EACCES);
  use["method"] = "check";
  use["operation"] = "reuse-data";
  fixture.Call(use, -EACCES);
  auto derived = fixture.Data("data_register_derived");
  derived["count"] = "1";
  derived["parent0"] = Get(artifact, "artifact");
  derived["scope"] = "90";
  fixture.Call(derived, -EACCES);
  derived["scope"] = "30";
  auto child = fixture.Call(derived);
  Check(Get(child, "expires") == Get(artifact, "expires"), "typed child cannot extend independent retention");
  auto child_use = fixture.Data("data_check");
  child_use["artifact"] = Get(child, "artifact");
  fixture.Call(child_use);
  auto update = fixture.Definition();
  update["policy_version"] = "2";
  update["parameter.days.max"] = "90";
  fixture.Update(update);
  fixture.Call(child_use, -ESTALE);
  std::cout << "PASS 30-to-90 scope cannot cross retry, receipt or artifact boundaries; ONCE retention stays independent\n";
}

void InvalidDefinitions() {
  Fixture fixture;
  auto original = fixture.Definition();
  for (const auto& field : Message{{"parameter.days.max", "0365"},
      {"parameter.days.source", "display_args"}, {"parameter.extra.type", "string"},
      {"message.ko.body", "Missing all parameters"}, {"message.en.title", "Unknown {other}"},
      {"locale_fallback.es-MX", "missing"}, {"locale_fallback.en", "ko"},
      {"template_version", "2"}}) {
    auto invalid = original;
    invalid[field.first] = field.second;
    fixture.Call(invalid, -EINVAL);
  }
  fixture.Call(original);
  auto legacy = fixture.Definition("legacy");
  for (auto it = legacy.begin(); it != legacy.end();) {
    if (it->first == "template_version" || it->first.compare(0, 10, "parameter.") == 0)
      it = legacy.erase(it);
    else
      ++it;
  }
  fixture.Call(legacy, -EINVAL);
  for (auto& field : legacy) {
    if (field.first.compare(0, 8, "message.") == 0)
      field.second = "Legacy literal message";
  }
  fixture.Call(legacy);
  auto request = fixture.Request("request", "legacy");
  request.erase("r0.policy_version");
  request["count"] = "01";
  auto pending = fixture.Call(request);
  auto prompt = fixture.Call({{"method", "get_prompt"}, {"request_id", Get(pending, "request_id")}, {"locale", "en"}});
  Check(Get(prompt, "r0.template_version").empty(), "legacy literal prompts retain their protocol");
  Check(Get(prompt, "count") == "1", "legacy accepted count normalizes only in prompt output");
  fixture.Call(fixture.Response(prompt));
  auto typed = fixture.Request();
  typed["count"] = "01";
  auto typed_pending = fixture.Call(typed);
  auto typed_prompt = fixture.Prompt(typed_pending);
  Check(Get(typed_prompt, "count") == "1", "typed accepted count normalizes only in prompt output");
  fixture.Call(fixture.Response(typed_prompt));
  std::cout << "PASS malformed typed definitions reject atomically and legacy literal messages remain compatible\n";
}
}  // namespace

int main() {
  try {
    InvalidDefinitions();
    BoundValues();
    LocaleTokenBinding();
    PromptBudgets();
    RevisionContract();
    PolicyAndRecovery();
    ReceiptAndArtifactBinding();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
