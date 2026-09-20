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

#include "common/registration.hh"
#include "consent.h"

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using consent::Get;
using consent::Message;

bool watch_definition = false;
bool fail_projection = false;
sqlite3* watched_database = nullptr;
std::function<void()> after_commit;
unsigned watched_commits = 0;

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

class Fixture final {
 public:
  Fixture() {
    char pattern[] = "/tmp/consent-offline-repository-XXXXXX";
    auto* created = mkdtemp(pattern);
    Check(created, "create isolated offline repository");
    directory = created;
    Check(mkdir((directory + "/registry").c_str(), 0700) == 0, "create independent registry");
    peer.identity = "offline-image-v1";  // Must not collide with offline receipt namespace.
    peer.instance = "test-instance";
    peer.roles = {"installer", "argo", "ui", "checker"};
    peer.packages = {"package-a", "package-b"};
    peer.subjects = {"subject"};
    peer.profiles = {"profile"};
    generations = {{"package-a", "generation-a"}, {"package-b", "generation-b"}};
    Open();
  }
  ~Fixture() {
    watch_definition = false;
    fail_projection = false;
    watched_database = nullptr;
    after_commit = {};
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
  void CreateRepository() {
    repository.reset(new consentd::Repository(directory + "/consent.db", directory + "/registry"));
    repository->SetInstallationValidator([this](const std::string& package,
        const std::string& app, const std::string& generation) {
      return generations.count(package) && app == package + ".app" &&
          !generations[package].empty() && generation == generations[package];
    });
    repository->SetPackageGenerationValidator([this](const std::string& package,
        const std::string& generation) { return generations.count(package) && generations[package] == generation; });
  }
  void Open() {
    CreateRepository();
    std::string error;
    Check(repository->Open(&error), error.c_str());
  }
  Message Record(const std::string& definition = "a", const std::string& package = "package-a") {
    return {{"definition", definition}, {"package", package}, {"app", package + ".app"},
        {"operation_id", "image-" + std::to_string(++sequence_)},
        {"expected_generation", generations[package]}, {"enforcer", peer.identity},
        {"policy_version", "1"}, {"text_revision", "1"}, {"level", "1"},
        {"modes", "ONCE,PERSISTENT"}, {"default_locale", "en"},
        {"message.en.title", "Read"}, {"message.en.body", "Read exact scope"}};
  }
  Message CheckStatus(Message result, int expected, const char* operation) {
    if (Get(result, "status") != std::to_string(expected)) {
      std::cerr << operation << ": expected=" << expected << " actual=" << Get(result, "status")
          << " reason=" << Get(result, "reason") << '\n';
      throw std::runtime_error("unexpected offline repository status");
    }
    if (expected)
      Check(!Get(result, "reason").empty() && Get(result, "decision") != "ALLOWED" &&
          Get(result, "receipt").empty(), "offline failure must carry explicit error and no approval");
    return result;
  }
  Message Import(const Message& record, int expected = 0) {
    return CheckStatus(repository->ImportOfflineRegistration(record), expected, "offline import");
  }
  Message Call(const Message& request, int expected = 0) {
    return CheckStatus(repository->Execute(peer, request), expected, Get(request, "method").c_str());
  }
  Message Online(Message record, int expected = 0) {
    record["method"] = "register";
    record["_install_identity"] = Get(record, "expected_generation");
    return Call(record, expected);
  }
  Message Query(const std::string& definition = "a") {
    auto sequence = std::to_string(++sequence_);
    return {{"method", "check"}, {"subject", "subject"}, {"profile", "profile"},
        {"operation_id", "operation-" + sequence}, {"step_id", "step-" + sequence},
        {"client_request_id", "request-" + sequence}, {"count", "1"},
        {"r0.definition", definition}, {"r0.operation", "read"},
        {"r0.scope", "scope"}, {"r0.purpose", "answer"}};
  }
  std::string Pending(const std::string& definition = "a", const std::string& scope = "scope") {
    auto request = Query(definition);
    request["method"] = "request";
    request["r0.scope"] = scope;
    auto pending = Call(request);
    Check(Get(pending, "decision") == "PENDING", "registration alone must not grant consent");
    return Get(pending, "request_id");
  }
  void Approve(const std::string& definition = "a") {
    auto pending = Pending(definition);
    auto prompt = Call({{"method", "get_prompt"}, {"request_id", pending}, {"locale", "en"}});
    Call({{"method", "respond"}, {"request_id", pending}, {"prompt_token", Get(prompt, "prompt_token")},
        {"decision", "ALLOWED"}, {"grant_mode", "PERSISTENT"}});
    Check(Get(Call(Query(definition)), "decision") == "ALLOWED", "seed explicit UI approval");
  }

  std::string directory;
  consentd::Peer peer;
  std::map<std::string, std::string> generations;
  std::unique_ptr<consentd::Repository> repository;
 private:
  unsigned sequence_ = 0;
};

void NamespaceAndIdentity() {
  Fixture fixture;
  auto record = fixture.Record();
  Check(consent::registration::ValidateDefinition(record), "shared syntax validation needs no private installation identity");
  auto imported = fixture.Import(record);
  Check(Get(fixture.Call(fixture.Query()), "decision") == "CONSENT_REQUIRED", "offline definition has no grant");
  Check(Get(fixture.Import(record), "revision") == Get(imported, "revision"), "exact offline retry changes no revision");
  auto conflict = record;
  conflict["text_revision"] = "2";
  fixture.Import(conflict, CONSENT_ERROR_CONFLICT);
  auto online = record;
  online["definition"] = "online";
  fixture.Online(online);
  Check(Get(fixture.Call(fixture.Query("online")), "decision") == "CONSENT_REQUIRED",
      "same operation and online actor name cannot collide with offline namespace");
  auto intruder = fixture.peer;
  intruder.roles = {"checker"};
  auto forged = record;
  forged["method"] = "register";
  forged["_install_identity"] = Get(record, "expected_generation");
  fixture.CheckStatus(fixture.repository->Execute(intruder, forged), -EACCES, "online role gate");
  forged["method"] = "offline_import";
  fixture.CheckStatus(fixture.repository->Execute(intruder, forged), -ENOSYS, "no offline IPC dispatch");
  fixture.generations["package-a"] = "new-generation";
  fixture.Import(record, -ESTALE);
  fixture.Open();
  fixture.Import(record, -ESTALE);
  fixture.Call(fixture.Query(), -ESTALE);
  std::cout << "PASS separate durable namespaces, unchanged online role gate and stale-generation rejection before dedup\n";
}

void PolicyAndPackageIsolation() {
  Fixture fixture;
  auto first = fixture.Record();
  fixture.Import(first);
  auto second = fixture.Record("b", "package-b");
  fixture.Import(second);
  fixture.Approve("a");
  fixture.Approve("b");
  auto pending = fixture.Pending("a", "other-scope");
  auto wrong_owner = fixture.Record("a", "package-b");
  fixture.Import(wrong_owner, -EACCES);
  auto wrong_enforcer = fixture.Record();
  wrong_enforcer["enforcer"] = "other-enforcer";
  fixture.Import(wrong_enforcer, CONSENT_ERROR_CONFLICT);
  auto changed_text = fixture.Record();
  changed_text["policy_version"] = "2";
  changed_text["message.en.title"] = "Changed";
  fixture.Import(changed_text, CONSENT_ERROR_CONFLICT);
  changed_text["text_revision"] = "2";
  fixture.Import(changed_text);
  Check(Get(fixture.Call(fixture.Query("a")), "decision") == "CONSENT_REQUIRED" &&
      Get(fixture.Call(fixture.Query("b")), "decision") == "ALLOWED",
      "offline update invalidates only affected package approval");
  Check(Get(fixture.Call({{"method", "get_request_result"}, {"request_id", pending}}),
      "decision") == "INVALIDATED", "offline update invalidates existing pending request");
  auto rollback = fixture.Record();
  fixture.Import(rollback, -ESTALE);
  rollback["policy_version"] = "3";
  fixture.Import(rollback, CONSENT_ERROR_CONFLICT);
  auto body = changed_text;
  body["operation_id"] = "another-operation";
  body["_install_identity"] = "forged";
  fixture.Import(body, -EINVAL);
  body.erase("_install_identity");
  body["grant_mode"] = "PERSISTENT";
  fixture.Import(body, -EINVAL);
  std::cout << "PASS common definition owner/enforcer/revision rules, pending invalidation and unrelated package approval isolation\n";
}

void RecoveryReceipt() {
  Fixture fixture;
  auto record = fixture.Record();
  fixture.Import(record);
  fixture.Approve();
  auto before = fixture.Call(fixture.Query());
  Check(unlink((fixture.directory + "/consent.db").c_str()) == 0, "delete isolated projection DB");
  auto recovered = fixture.Call(fixture.Query());
  Check(Get(recovered, "epoch") != Get(before, "epoch") &&
      Get(recovered, "decision") == "CONSENT_REQUIRED", "recovery restores offline definition without approval");
  auto snapshot = fixture.repository->Snapshot();
  Check(Get(fixture.Import(record), "revision") == Get(snapshot, "revision"),
      "offline operation receipt survives DB loss in independent registry");
  auto conflict = record;
  conflict["text_revision"] = "2";
  fixture.Import(conflict, CONSENT_ERROR_CONFLICT);
  std::cout << "PASS DB loss preserves offline operation dedup and definitions but never restores user approval\n";
}

void SeedOrdering(bool reverse) {
  Fixture fixture;
  auto low = fixture.Record();
  auto high = fixture.Record();
  high["policy_version"] = "2";
  high["text_revision"] = "2";
  high["message.en.title"] = "New policy";
  if (reverse) {
    fixture.Import(high);
    auto obsolete = fixture.Import(low, -ESTALE);
    Check(Get(obsolete, "reason").compare(0, 14, "obsolete-seed:") == 0,
        "newly seen older seed has an explicit obsolete outcome");
  } else {
    fixture.Import(low);
    fixture.Import(high);
  }
  fixture.Approve();
  fixture.Open();
  auto before = fixture.repository->Snapshot();
  fixture.Import(low, reverse ? -ESTALE : 0);
  fixture.Import(high);
  Check(Get(fixture.repository->Snapshot(), "revision") == Get(before, "revision") &&
      Get(fixture.Call(fixture.Query()), "decision") == "ALLOWED",
      "second boot preserves recorded applied/obsolete outcomes and current grant");
  auto query = fixture.Call(fixture.Query());
  Check(Get(query, "r0.policy_version") == "2", "seed ordering cannot restore lower policy");
  std::cout << "PASS " << (reverse ? "reverse" : "ascending")
      << " seed order and second boot retain newest definition and immutable operation outcomes\n";
}

void ObsoleteOnlinePolicy() {
  Fixture fixture;
  auto current = fixture.Record();
  current["policy_version"] = "2";
  current["text_revision"] = "2";
  current["message.en.title"] = "Online update";
  fixture.Online(current);
  fixture.Approve();
  auto pending = fixture.Pending("a", "other-scope");
  auto low = fixture.Record();
  auto obsolete = fixture.Import(low, -ESTALE);
  auto repeat = fixture.Import(low, -ESTALE);
  Check(Get(repeat, "revision") == Get(obsolete, "revision") &&
      Get(repeat, "reason") == Get(obsolete, "reason"), "obsolete retry returns its stored outcome without reapplying");
  Check(Get(fixture.Call(fixture.Query()), "decision") == "ALLOWED" &&
      Get(fixture.Call({{"method", "get_request_result"}, {"request_id", pending}}), "decision") == "PENDING",
      "obsolete receipt persistence does not invalidate current grant or pending request");
  auto conflict = low;
  conflict["message.en.body"] = "Different old payload";
  fixture.Import(conflict, CONSENT_ERROR_CONFLICT);
  for (const auto& versions : {std::pair<const char*, const char*>{"1", "3"}, {"3", "1"}}) {
    auto crossed = fixture.Record();
    crossed["policy_version"] = versions.first;
    crossed["text_revision"] = versions.second;
    fixture.Import(crossed, CONSENT_ERROR_CONFLICT);
  }
  auto same_policy = fixture.Record();
  same_policy["policy_version"] = "2";
  same_policy["enforcer"] = "changed-without-policy-bump";
  fixture.Import(same_policy, CONSENT_ERROR_CONFLICT);
  auto same_text = fixture.Record();
  same_text["text_revision"] = "2";
  fixture.Import(same_text, CONSENT_ERROR_CONFLICT);
  auto identical_versions = current;
  identical_versions["operation_id"] = "same-versions-conflict";
  identical_versions["message.en.body"] = "Different current payload";
  fixture.Import(identical_versions, CONSENT_ERROR_CONFLICT);
  auto other = fixture.Record("b", "package-b");
  fixture.Import(other);
  Check(Get(fixture.Call(fixture.Query("b")), "decision") == "CONSENT_REQUIRED",
      "other package remains importable after an obsolete result");
  Check(unlink((fixture.directory + "/consent.db").c_str()) == 0, "delete isolated DB after obsolete receipt");
  auto recovered = fixture.Call(fixture.Query());
  Check(Get(recovered, "decision") == "CONSENT_REQUIRED" && Get(recovered, "r0.policy_version") == "2",
      "DB recovery retains newer policy and requires fresh consent");
  fixture.Import(low, -ESTALE);
  fixture.Import(conflict, CONSENT_ERROR_CONFLICT);
  std::cout << "PASS newer online policy makes unseen dominated seeds obsolete without grant/pending invalidation; conflict ledger survives DB loss\n";
}

void TombstonedSeed() {
  Fixture fixture;
  auto current = fixture.Record();
  fixture.Online(current);
  fixture.Approve();
  fixture.Call({{"method", "unregister"}, {"package", "package-a"},
      {"expected_generation", "generation-a"}, {"operation_id", "remove-package"}});
  auto unseen = fixture.Record();
  fixture.Import(unseen, -ESTALE);
  fixture.Open();
  fixture.Import(unseen, -ESTALE);
  Check(Get(fixture.Call(fixture.Query()), "decision") == "DENIED",
      "unseen equal-version seed never reactivates a tombstone in the same installation");
  auto changed = unseen;
  changed["message.en.body"] = "Tampered obsolete receipt";
  fixture.Import(changed, CONSENT_ERROR_CONFLICT);
  fixture.generations["package-a"] = "new-installation";
  auto reinstall = fixture.Record();
  fixture.Import(reinstall);
  Check(Get(fixture.Call(fixture.Query()), "decision") == "CONSENT_REQUIRED",
      "actual new installation generation follows normal registration and has no old grant");
  fixture.Import(unseen, -ESTALE);
  std::cout << "PASS same-generation tombstone blocks unseen seed resurrection; actual new generation can register without old approvals\n";
}

void RegistryBeforeProjection() {
  Fixture fixture;
  auto record = fixture.Record();
  watch_definition = true;
  fail_projection = true;
  fixture.Import(record, CONSENT_ERROR_STORAGE);
  Check(!fail_projection, "projection failed only after durable registry publication");
  watch_definition = false;
  fixture.Open();
  auto before = fixture.repository->Snapshot();
  fixture.Import(record);
  Check(Get(fixture.repository->Snapshot(), "revision") == Get(before, "revision"),
      "startup replay and same operation retry apply exactly once");
  Check(Get(fixture.Call(fixture.Query()), "decision") == "CONSENT_REQUIRED",
      "replayed offline operation still carries no approval");
  std::cout << "PASS failure after registry publication replays definition and receipt atomically on reopening\n";
}

void CommitBoundary(bool delete_database) {
  Fixture fixture;
  auto record = fixture.Record();
  unsigned before = watched_commits;
  watch_definition = true;
  after_commit = [&fixture, delete_database] {
    if (delete_database)
      Check(unlink((fixture.directory + "/consent.db").c_str()) == 0, "unlink immediately after real COMMIT");
    else
      fixture.generations["package-a"] = "replacement";
  };
  fixture.Import(record, -ESTALE);
  Check(watched_commits == before + 1 && !after_commit,
      "external change occurs at the actual successful COMMIT return boundary");
  if (!delete_database)
    fixture.Import(record, -ESTALE);
  fixture.repository->Tick();
  if (delete_database)
    Check(Get(fixture.Call(fixture.Query()), "decision") == "CONSENT_REQUIRED",
        "committed but unpublished offline import cannot publish a stale approval");
  else
    fixture.Call(fixture.Query(), -ESTALE);
  std::cout << "PASS offline postcommit " << (delete_database ? "DB loss" : "installation rotation")
      << " blocks success publication\n";
}

void TypedValidationErrors(int failure) {
  for (int boundary : {1, 2, 3}) {
    Fixture fixture;
    auto record = fixture.Record();
    unsigned calls = 0;
    unsigned boolean_calls = 0;
    fixture.repository->SetInstallationValidator([&boolean_calls](const std::string&,
        const std::string&, const std::string&) { ++boolean_calls; return false; });
    fixture.repository->SetOfflineInstallationValidator([&calls, boundary, failure](const std::string&,
        const std::string&, const std::string&) { return ++calls >= static_cast<unsigned>(boundary) ? failure : 0; });
    fixture.Import(record, failure);
    Check(calls == static_cast<unsigned>(boundary) && boolean_calls == 0,
        "offline precheck, common registration and replay all preserve typed validation errors");
    if (boundary == 3) {
      // WriteRegistry already advanced. A fenced retry must keep the typed
      // context through Ensure's reconciliation, rather than flatten to bool.
      fixture.Import(record, failure);
      Check(boolean_calls == 0, "offline fenced recovery does not use the boolean authority adapter");
    }
    fixture.repository->SetOfflineInstallationValidator([](const std::string&,
        const std::string&, const std::string&) { return 0; });
    fixture.Import(record);
    Check(boolean_calls == 0, "successful typed import is independent of the online bool callback");
    auto online = fixture.Record("online");
    fixture.Online(online, -EACCES);
    Check(boolean_calls > 0, "online registration still uses its unchanged boolean permission contract");
  }
  std::cout << "PASS typed offline error " << failure
      << " remains fatal at precheck/common/replay and fenced retry; online bool behavior unchanged\n";
}

void TypedPostcommitError(int failure) {
  Fixture fixture;
  auto record = fixture.Record();
  int status = 0;
  unsigned boolean_calls = 0;
  fixture.repository->SetInstallationValidator([&boolean_calls](const std::string&,
      const std::string&, const std::string&) { ++boolean_calls; return false; });
  fixture.repository->SetOfflineInstallationValidator([&status](const std::string&,
      const std::string&, const std::string&) { return status; });
  unsigned before = watched_commits;
  watch_definition = true;
  after_commit = [&status, failure] { status = failure; };
  fixture.Import(record, failure);
  Check(watched_commits == before + 1 && !after_commit && boolean_calls == 0,
      "typed authority fails immediately after the real COMMIT without publishing success or flattening error");
  fixture.Import(record, failure);
  status = 0;
  fixture.Import(record);
  Check(boolean_calls == 0, "postcommit typed failure retry retains namespace receipt and typed validation");
  std::cout << "PASS actual postcommit authority error " << failure << " is preserved, including exact receipt retries\n";
}

Message ReadDurableState(const Fixture& fixture) {
  Check(!fixture.repository, "inspect durable DB only after its repository connection is closed");
  sqlite3* database = nullptr;
  int status = sqlite3_open_v2((fixture.directory + "/consent.db").c_str(), &database,
      SQLITE_OPEN_READONLY, nullptr);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> owner(database, sqlite3_close);
  Check(status == SQLITE_OK, "read isolated durable approval state");
  Message result;
  for (const char* sql : {
      "SELECT 'meta.'||key,value FROM meta WHERE key IN ('revision','registry_revision')",
      "SELECT 'definition.'||id,CAST(active AS TEXT)||':'||config FROM definitions ORDER BY id",
      "SELECT 'grant.'||id,mode||':'||revoked||':'||remaining FROM grants ORDER BY id"}) {
    sqlite3_stmt* statement = nullptr;
    status = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement_owner(statement, sqlite3_finalize);
    Check(status == SQLITE_OK, "prepare durable state evidence query");
    while ((status = sqlite3_step(statement)) == SQLITE_ROW) {
      const auto* key = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
      const auto* value = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
      result[key] = std::string(value, sqlite3_column_bytes(statement, 1));
    }
    Check(status == SQLITE_DONE, "finish durable state evidence query");
  }
  return result;
}

void RequireRegistryReplay(const Fixture& fixture) {
  // Change only the protected registry's modification time. Ensure must read
  // and reconcile the same authority; no DB bytes or approved policy change.
  const std::string path = fixture.directory + "/registry/definitions.registry";
  struct stat status = {};
  Check(lstat(path.c_str(), &status) == 0, "inspect isolated registry timestamp");
  struct timespec times[] = {status.st_atim, status.st_mtim};
  ++times[1].tv_sec;
  Check(utimensat(AT_FDCWD, path.c_str(), times, AT_SYMLINK_NOFOLLOW) == 0,
      "force final snapshot registry reconciliation");
}

void ReconciliationFailure(bool during_open, bool stale_first) {
  Fixture fixture;
  auto normal_seed = fixture.Record();
  fixture.Import(normal_seed);
  fixture.Import(fixture.Record("b", "package-b"));
  fixture.Approve("a");
  fixture.Approve("b");
  fixture.repository.reset();
  auto before = ReadDurableState(fixture);
  fixture.CreateRepository();
  bool fail = during_open;
  unsigned boolean_calls = 0;
  unsigned failed_checks = 0;
  fixture.repository->SetInstallationValidator([&boolean_calls](const std::string&,
      const std::string&, const std::string&) { ++boolean_calls; return false; });
  fixture.repository->SetOfflineInstallationValidator([&](const std::string& package,
      const std::string&, const std::string&) {
    if (!fail)
      return 0;
    if (package == "package-b") {
      ++failed_checks;
      return -EINVAL;
    }
    return stale_first ? -ESTALE : 0;
  });
  {
    consentd::Repository::OfflineReconciliation reconciliation(*fixture.repository);
    std::string error;
    if (during_open) {
      Check(!fixture.repository->Open(&error) && !error.empty(),
          "startup rejects malformed authority for an existing package outside the normal seed");
    } else {
      Check(fixture.repository->Open(&error), error.c_str());
      fixture.Import(normal_seed);
      before["meta.revision"] = Get(fixture.repository->Snapshot(), "revision");
      fail = true;
      RequireRegistryReplay(fixture);
      bool rejected = false;
      try {
        fixture.repository->Snapshot();
      } catch (const std::exception&) {
        rejected = true;
      }
      Check(rejected, "final snapshot rejects fatal authority failure after normal seed import");
    }
    Check(failed_checks == 1 && boolean_calls == 0,
        "strict reconciliation never flattens unrelated authority failure through the online bool adapter");
  }
  fixture.repository.reset();
  Check(ReadDurableState(fixture) == before,
      "fatal reconciliation rolls back every definition, approval and revision change");
  fixture.Open();
  Check(Get(fixture.Call(fixture.Query("a")), "decision") == "ALLOWED" &&
      Get(fixture.Call(fixture.Query("b")), "decision") == "ALLOWED",
      "healthy restart preserves both explicit persistent approvals after failed reconciliation");
  std::cout << "PASS strict " << (during_open ? "startup Open" : "final Snapshot replay")
      << " authority failure preserves durable approvals and revisions"
      << (stale_first ? " after an earlier tentative invalidation" : " for a normal unrelated seed") << '\n';
}

void ReconciliationScopeRestoration(bool fail) {
  Fixture fixture;
  auto record = fixture.Record();
  fixture.Import(record);
  unsigned boolean_calls = 0;
  unsigned typed_calls = 0;
  int status = 0;
  fixture.repository->SetInstallationValidator([&boolean_calls](const std::string&,
      const std::string&, const std::string&) { ++boolean_calls; return true; });
  fixture.repository->SetOfflineInstallationValidator([&](const std::string&,
      const std::string&, const std::string&) { ++typed_calls; return status; });
  bool rejected = false;
  try {
    consentd::Repository::OfflineReconciliation reconciliation(*fixture.repository);
    {
      consentd::Repository::OfflineReconciliation nested(*fixture.repository);
      fixture.Import(record);
    }
    status = fail ? -EINVAL : 0;
    RequireRegistryReplay(fixture);
    fixture.repository->Snapshot();
  } catch (const std::exception&) {
    rejected = true;
  }
  Check(rejected == fail && boolean_calls == 0 && typed_calls > 0,
      "nested scope and import restore the outer strict mode on success and failure");
  unsigned previous_calls = typed_calls;
  status = -EIO;
  RequireRegistryReplay(fixture);
  fixture.repository->Snapshot();
  Check(boolean_calls > 0 && typed_calls == previous_calls,
      "scope exit restores the online bool mode including exception unwinding");
  fixture.Online(fixture.Record("online"));
  Check(typed_calls == previous_calls, "online registration remains independent of typed offline failures");
  std::cout << "PASS nested strict reconciliation restores online behavior after "
      << (fail ? "exception unwinding" : "successful publication") << '\n';
}
}  // namespace

extern "C" __attribute__((visibility("default"))) int sqlite3_step(sqlite3_stmt* statement) {
  using Function = int (*)(sqlite3_stmt*);
  static auto real = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "sqlite3_step"));
  const char* sql = sqlite3_sql(statement);
  bool target = watch_definition && sql && std::strncmp(sql, "INSERT INTO definitions VALUES", 30) == 0;
  if (target && fail_projection) {
    fail_projection = false;
    return SQLITE_IOERR_WRITE;
  }
  int result = real(statement);
  if (target && result == SQLITE_DONE)
    watched_database = sqlite3_db_handle(statement);
  return result;
}

extern "C" __attribute__((visibility("default"))) int sqlite3_exec(sqlite3* database,
    const char* sql, int (*callback)(void*, int, char**, char**), void* data, char** error) {
  using Function = int (*)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**);
  static auto real = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "sqlite3_exec"));
  int result = real(database, sql, callback, data, error);
  if (result == SQLITE_OK && sql && std::strcmp(sql, "COMMIT") == 0 &&
      database == watched_database && after_commit) {
    watch_definition = false;
    watched_database = nullptr;
    ++watched_commits;
    auto action = std::move(after_commit);
    after_commit = {};
    Check(sqlite3_get_autocommit(database) != 0, "watched commit really finished");
    action();
  }
  return result;
}

int main() {
  try {
    NamespaceAndIdentity();
    PolicyAndPackageIsolation();
    RecoveryReceipt();
    SeedOrdering(false);
    SeedOrdering(true);
    ObsoleteOnlinePolicy();
    TombstonedSeed();
    RegistryBeforeProjection();
    CommitBoundary(false);
    CommitBoundary(true);
    for (int failure : {-EINVAL, -EIO}) {
      TypedValidationErrors(failure);
      TypedPostcommitError(failure);
    }
    for (bool during_open : {true, false}) {
      ReconciliationFailure(during_open, false);
      ReconciliationFailure(during_open, true);
    }
    ReconciliationScopeRestoration(false);
    ReconciliationScopeRestoration(true);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
