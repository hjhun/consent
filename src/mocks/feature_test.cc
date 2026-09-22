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
// This target alone compiles the private fixture constructor and API/action
// boundary substitutes. Production executables have neither test macro nor runtime bypass.
#include "feature_coordinator.cc"
#include "feature_worker.cc"

#include <cassert>
#include <dirent.h>
#include <dlfcn.h>

namespace {
unsigned channel_stat_interruptions = 0;
bool channel_chmod_failure = false;
}
// Test executable only: exercise the real helper's early EINTR retry and
// post-bind failure cleanup without runtime fault controls in any mock.
extern "C" int fstatat(int directory, const char* name, struct stat* info, int flags) {
  using Function = int (*)(int, const char*, struct stat*, int);
  static auto real = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "fstatat"));
  if (channel_stat_interruptions && strncmp(name, ".worker-", 8) == 0) {
    --channel_stat_interruptions; errno = EINTR; return -1;
  }
  assert(real); return real(directory, name, info, flags);
}
extern "C" int fchmodat(int directory, const char* name, mode_t mode, int flags) {
  using Function = int (*)(int, const char*, mode_t, int);
  static auto real = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "fchmodat"));
  if (channel_chmod_failure && strncmp(name, ".worker-", 8) == 0) {
    channel_chmod_failure = false; errno = EIO; return -1;
  }
  assert(real); return real(directory, name, mode, flags);
}

namespace consent_mock {
namespace {
std::set<std::string> ChannelNames() {
  std::set<std::string> names;
  DIR* directory = opendir(kFeatureDirectory);
  assert(directory);
  while (dirent* entry = readdir(directory))
    if (strncmp(entry->d_name, ".worker-", 8) == 0) names.insert(entry->d_name);
  closedir(directory);
  return names;
}
void WorkerChannelFixture(bool required) {
  // A different connected process is rejected by the kernel PID binding,
  // independently of whether this host provides a SMACK label.
  char directory[] = "/tmp/consent-feature-parent-XXXXXX";
  assert(mkdtemp(directory));
  std::string path = std::string(directory) + "/socket";
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  assert(path.size() < sizeof(address.sun_path)); strcpy(address.sun_path, path.c_str());
  int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0); assert(listener >= 0);
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  assert(listen(listener, 1) == 0);
  pid_t child = fork(); assert(child >= 0);
  if (!child) {
    close(listener);
    int connection = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (connection < 0 || connect(connection, reinterpret_cast<sockaddr*>(&address), sizeof(address))) _exit(1);
    close(connection); _exit(0);
  }
  pollfd wait{listener, POLLIN, 0}; assert(poll(&wait, 1, 1000) == 1);
  int accepted = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC); assert(accepted >= 0);
  ucred peer{}; socklen_t size = sizeof(peer);
  assert(getsockopt(accepted, SOL_SOCKET, SO_PEERCRED, &peer, &size) == 0 && peer.pid == child);
  assert(VerifyWorkerParent(accepted, getpid()) == -EACCES);
  close(accepted); close(listener); assert(unlink(path.c_str()) == 0 && rmdir(directory) == 0);
  int result = 0; assert(waitpid(child, &result, 0) == child && WIFEXITED(result) && WEXITSTATUS(result) == 0);
  std::puts("PASS connected worker channel rejects a different kernel parent PID");
  if (!required) {
    std::puts("SKIP protected root/System worker channel: run --worker-channel on the SMACK target");
    return;
  }
  const auto before = ChannelNames();
  int channels[2] = {-1, -1};
  channel_stat_interruptions = 1;
  assert(CreateWorkerChannel(channels) == 0 && channel_stat_interruptions == 0);
  assert(VerifyWorkerParent(channels[0], getpid()) == 0 && VerifyWorkerParent(channels[1], getpid()) == 0);
  assert(VerifyWorkerParent(channels[0], getpid() + 1) == -EACCES);
  assert((fcntl(channels[0], F_GETFD) & FD_CLOEXEC) && (fcntl(channels[1], F_GETFD) & FD_CLOEXEC));
  Message input{{"v", "1"}, {"id", "1"}, {"method", "status"}}, output;
  assert(SendFrame(channels[0], input) == 0 && ReceiveFrame(channels[1], &output) == 0 && output == input);
  // This fixture is not the exact argo executable, even under root/System.
  Peer not_argo; assert(!not_argo.Authenticate(channels[1], false));
  close(channels[0]); close(channels[1]);
  assert(ChannelNames() == before);
  channel_chmod_failure = true;
  assert(CreateWorkerChannel(channels) == -EIO && !channel_chmod_failure);
  assert(channels[0] == -1 && channels[1] == -1 && ChannelNames() == before);
  std::puts("PASS protected root/System channel, early-stat EINTR, wrong executable, and post-bind cleanup");
}
class CoordinatorFixture final {
 public:
  static Message Select(const char* revision, const char* command, const char* selected) {
    return {{"v", "1"}, {"id", "1"}, {"method", "select"},
        {"expected_revision", revision}, {"command_id", command}, {"selected", selected},
        {"grant_mode", "SESSION"}, {"duration_ms", "0"}, {"catalog_revision", CatalogRevision()}, {"coordinator_epoch", "fixture-selection"}};
  }
  static int Execute(Coordinator* actor, const Message& input, std::string* output) {
    auto peer = std::make_unique<Peer>();
    return actor->Execute(input, &peer, output);
  }
  static Message Reply(Message message) {
    message["method"] = "reply"; message["status"] = "0"; return message;
  }
  static void Job(Coordinator* actor, const std::string& phase, const std::string& operation,
      std::vector<std::string> features = {"calendar.read"}) {
    auto job = std::make_unique<Coordinator::Job>();
    job->owner = actor; job->task = true; job->features = std::move(features);
    job->revision = actor->revision_; job->expires = g_get_monotonic_time() + 1000000;
    job->request = actor->Request(job->features, true); job->request["operation_id"] = "job-" + operation;
    job->phase = phase; job->worker = "holder";
    job->enforcement = {{"operation_id", operation}, {"artifact", "fixture-artifact"}};
    actor->job_ = std::move(job);
  }
  static void ReuseAndRetired() {
    Coordinator actor(nullptr); actor.test_auto_complete_ = false;
    std::string output;
    assert(Execute(&actor, Select("1", "enable-pair", "calendar.read,device.control"), &output) == 0);
    actor.session_ = "fixture-session"; actor.generation_ = "1";
    actor.artifact_ = "fixture-artifact"; actor.artifact_receipt_ = "fixture-original-receipt";
    Job(&actor, "", "reuse-and-device", {"calendar.read", "device.control"});
    actor.BeginAction();
    assert(actor.job_->phase == "reuse-authorize" && actor.test_last_method_ == "reuse-authorize");
    assert(actor.action_count_ == 0 && actor.test_dispatches_ == 0);
    Message proof = Reply({{"decision", "ALLOWED"}, {"permit", actor.artifact_},
        {"original_receipt", actor.artifact_receipt_}, {"context_digest", ContextDigest(actor.job_->enforcement)}});
    actor.HandleWorkerResult(0, proof);
    assert(actor.job_->phase == "reuse-start" && actor.test_dispatches_ == 1 && actor.action_count_ == 0);
    auto completed = proof; completed["action_started"] = "1"; completed["action_retry"] = "0";
    actor.HandleWorkerResult(0, completed);
    assert(actor.action_count_ == 1 && actor.job_->phase == "authorize" && actor.job_->worker == "cm");
    assert(actor.job_->start_method == "start" && actor.test_last_worker_ == "cm" && actor.test_last_method_ == "authorize");
    actor.HandleWorkerResult(0, Reply({{"decision", "ALLOWED"}, {"receipt", "fixture-device-receipt"}}));
    assert(actor.job_->phase == "start" && actor.test_dispatches_ == 2 && actor.action_count_ == 1);
    actor.HandleWorkerResult(0, Reply({{"decision", "ALLOWED"}, {"receipt", "fixture-device-receipt"},
        {"action_started", "1"}, {"action_retry", "0"}}));
    assert(!actor.job_ && actor.action_count_ == 2 && actor.execution_results_.size() == 2);

    Job(&actor, "reuse-authorize", "late-authorization"); actor.Cancel();
    assert(actor.retired_ && !actor.job_);
    actor.RetiredResult(0, proof);
    assert(!actor.retired_ && actor.action_count_ == 2 && actor.test_dispatches_ == 2);
    assert(consent::Get(actor.retired_results_.back(), "outcome") == "authorized-not-started");
    Job(&actor, "reuse-start", "late-effect"); actor.Cancel();
    actor.RetiredResult(0, completed);
    assert(actor.action_count_ == 3 && consent::Get(actor.retired_results_.back(), "outcome") == "action-started");
    assert(consent::Get(actor.retired_results_.back(), "phase") == "reuse-start" &&
        consent::Get(actor.retired_results_.back(), "operation_id") == "late-effect");
    actor.RetiredResult(0, completed); assert(actor.action_count_ == 3);
    Job(&actor, "start", "late-worker-failure"); actor.Cancel();
    actor.RetiredResult(-EACCES, Reply({{"decision", "DENIED"}}));
    assert(actor.action_count_ == 3 && consent::Get(actor.retired_results_.back(), "outcome") == "worker-failed");
    Job(&actor, "reuse-start", "late-transport-loss"); actor.Cancel();
    actor.RetiredResult(-ECONNRESET, {});
    assert(actor.action_count_ == 3 && consent::Get(actor.retired_results_.back(), "outcome") == "uncertain");
    Job(&actor, "reuse-start", "live-transport-loss");
    actor.HandleWorkerResult(-ECONNRESET, {});
    assert(!actor.job_ && actor.action_count_ == 3 &&
        consent::Get(actor.execution_results_.back(), "outcome") == "uncertain");
    Job(&actor, "start", "live-effect-with-error");
    actor.HandleWorkerResult(-EIO, completed);
    assert(!actor.job_ && actor.action_count_ == 4 &&
        consent::Get(actor.execution_results_.back(), "outcome") == "action-started");
    assert(consent::Get(actor.execution_results_.back(), "status") == std::to_string(-EIO));
    Job(&actor, "gate", "reuse-gate-disable"); actor.job_->start_method = "reuse-start";
    assert(Execute(&actor, Select("2", "disable-at-reuse-gate", ""), &output) == 0);
    assert(!actor.job_ && !actor.retired_ && actor.action_count_ == 4 && actor.test_dispatches_ == 2);
    actor.session_.clear();
    std::puts("PASS staged reuse -> device, live/retired effect and uncertainty evidence, reuse-gate disable");
  }
  static void Run() {
    Coordinator actor(nullptr);
    std::string output;
    auto first = Select("1", "first", "calendar.read");
    assert(Execute(&actor, first, &output) == 0 && actor.revision_ == 2);
    const auto original = output;
    assert(Execute(&actor, first, &output) == 0 && output == original && actor.revision_ == 2);
    auto changed = first; changed["selected"] = "device.control";
    assert(Execute(&actor, changed, &output) == -EEXIST);
    auto stale = Select("1", "late-enable", "calendar.read,device.control");
    assert(Execute(&actor, stale, &output) == -ESTALE && actor.selected_.size() == 1);
    auto unknown = Select("2", "new-catalog", "calendar.read,new.feature");
    assert(Execute(&actor, unknown, &output) == -EINVAL && actor.revision_ == 2);
    auto once = Select("2", "once-setting", "calendar.read"); once["grant_mode"] = "ONCE";
    assert(Execute(&actor, once, &output) == -EINVAL);
    Message task{{"v", "1"}, {"id", "2"}, {"method", "task"}, {"expected_revision", "2"},
        {"command_id", "unselected-task"}, {"task", "device-off"}, {"catalog_revision", CatalogRevision()}, {"coordinator_epoch", "fixture-selection"}};
    assert(Execute(&actor, task, &output) == -EACCES && !actor.job_);
    task["task"] = "unknown-task";
    assert(Execute(&actor, task, &output) == -EINVAL);

    // Actual actor check used after AUTHORIZE. Simulate the provider result
    // arriving with a valid exact TASK vector, then process a disable command
    // before the actor's action-start dispatch. No provider can be spawned.
    actor.session_ = "fixture-session"; actor.generation_ = "1";
    auto job = std::make_unique<Coordinator::Job>();
    job->owner = &actor; job->features = {"calendar.read"}; job->task = true;
    job->revision = actor.revision_; job->expires = g_get_monotonic_time() + 1000000;
    job->request = actor.Request(job->features, true);
    job->done = true; job->result = {{"decision", "ALLOWED"}};
    assert(actor.Current(*job));
    auto authorized_snapshot = job->request;
    actor.job_ = std::move(job);
    assert(Execute(&actor, Select("2", "disable", ""), &output) == 0);
    assert(!actor.job_ && actor.children_.empty() && actor.action_count_ == 0);
    Coordinator::Job delayed;
    delayed.revision = 2; delayed.expires = g_get_monotonic_time() + 1000000;
    delayed.features = {"calendar.read"}; delayed.request = authorized_snapshot;
    assert(!actor.Current(delayed));
    assert(Execute(&actor, first, &output) == 0 && actor.selected_.empty());
    assert(Execute(&actor, stale, &output) == -ESTALE && actor.selected_.empty());

    assert(Execute(&actor, Select("3", "re-enable", "calendar.read"), &output) == 0);
    delayed.revision = actor.revision_; delayed.request = actor.Request(delayed.features, true);
    assert(actor.Current(delayed));
    delayed.request["r0.scope"] = "calendar.default.next90days";
    assert(!actor.Current(delayed));
    delayed.request = actor.Request(delayed.features, true); delayed.expires = g_get_monotonic_time() - 1;
    assert(!actor.Current(delayed));
    delayed.expires = g_get_monotonic_time() + 1000000; actor.generation_ = "2";
    assert(!actor.Current(delayed));
    // Simulate server acceptance followed by lost transport reply. The test
    // target substitutes only the API/action boundary; retry exercises the
    // same production command ledger before CAS and cannot dispatch twice.
    Message once_task{{"v", "1"}, {"id", "9"}, {"method", "task"},
        {"catalog_revision", CatalogRevision()}, {"coordinator_epoch", "fixture-selection"}, {"expected_revision", "4"},
        {"command_id", "lost-reply-task"}, {"task", "device-off"}, {"task_mode", "ONCE"}};
    assert(Execute(&actor, once_task, &output) == 0 && actor.job_);
    const auto accepted = output;  // Discarded by the disconnected caller.
    actor.BeginAction();
    assert(actor.action_count_ == 1 && !actor.job_);
    assert(Execute(&actor, once_task, &output) == 0 && output == accepted && actor.action_count_ == 1);
    auto conflict = once_task; conflict["task"] = "calendar-expanded";
    assert(Execute(&actor, conflict, &output) == -EEXIST && actor.action_count_ == 1);
    Coordinator restarted(nullptr);
    restarted.selection_id_ = "fresh-coordinator-incarnation";
    assert(Execute(&restarted, once_task, &output) == -ESTALE && restarted.action_count_ == 0 && !restarted.job_);
    actor.session_.clear();  // No real daemon was involved in this actor fixture.
    std::puts("PASS actor CAS/idempotency, stale retry, catalog/unselected denial, post-AUTHORIZE disable, expiry and generation");
  }
};
class WorkerFixture final {
 public:
  static void Run() {
    Worker holder("holder");
    holder.buffers_["fixture-artifact"] = std::vector<char>(64, 'M');
    holder.proofs_["fixture-artifact"] = {"fixture-artifact", "original-receipt"};
    Message input{{"subject", "owner"}, {"profile", "default"}, {"session", "session"},
        {"generation", "1"}, {"artifact", "fixture-artifact"}, {"scope", "calendar.default.next7days"},
        {"purpose", "conversation-summary"}, {"recipient", "local-conversation"},
        {"operation_id", "exact-use"}, {"step_id", "conversation-use"},
        {"mode", "AUTHORIZE"}, {"operation", "reuse-data"}};
    Message proof;
    assert(holder.Execute("reuse-authorize", input, &proof) == 0 && holder.actions_.empty());
    assert(holder.test_authorizations_ == 1 && !proof.count("receipt") &&
        consent::Get(proof, "original_receipt") == "original-receipt" &&
        consent::Get(proof, "context_digest") == ContextDigest(input));
    Message start = input;
    for (const char* key : {"permit", "original_receipt", "context_digest"}) start[key] = consent::Get(proof, key);
    auto wrong = start; wrong["permit"] = "other-artifact";
    Message result;
    assert(holder.Execute("reuse-start", wrong, &result) == -EACCES && holder.actions_.empty());
    assert(holder.Execute("reuse-start", start, &result) == 0 && holder.actions_.size() == 1 &&
        consent::Get(result, "action_started") == "1" && holder.test_authorizations_ == 2);
    assert(holder.Execute("reuse-start", start, &result) == 0 && holder.actions_.size() == 1 &&
        consent::Get(result, "action_retry") == "1" && holder.test_authorizations_ == 2);
    auto changed = input; changed["step_id"] = "changed-step";
    assert(holder.Execute("reuse-authorize", changed, &proof) == 0);
    for (const char* key : {"permit", "original_receipt", "context_digest"}) changed[key] = consent::Get(proof, key);
    assert(holder.Execute("reuse-start", changed, &result) == -EEXIST && holder.actions_.size() == 1);
    std::puts("PASS holder authorization has no action, exact permit/context start, replay and conflicting retry");
  }
};
}
}
int main(int argc, char** argv) {
  using namespace consent_mock;
  if (argc > 1) {
    assert(argc == 2 && strcmp(argv[1], "--worker-channel") == 0);
    WorkerChannelFixture(true);
    return 0;
  }
  WorkerChannelFixture(false);
  std::set<std::string> values;
  assert(ParseSelection("", &values) && values.empty());
  assert(ParseSelection("device.control,calendar.read", &values) && values.size() == 2);
  for (const char* bad : {"calendar.read,", "calendar.read,calendar.read", "role=argo", "new.feature"})
    assert(!ParseSelection(bad, &values));
  auto catalog = CatalogJson("7");
  assert(catalog.find("\"selection_revision\":\"7\"") != std::string::npos);
  assert(catalog.find("\"mappings\":[") != std::string::npos && catalog.size() < 8192);
  assert(Json("\"\n\\") == "\"\\\"\\u000a\\\\\"");
  int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
  Message input{{"v", "1"}, {"id", "7"}, {"method", "select"}, {"selected", "calendar.read"}};
  assert(SendFrame(sockets[0], input) == 0);
  Message decoded; assert(ReceiveFrame(sockets[1], &decoded) == 0 && decoded == input);
  close(sockets[0]); close(sockets[1]);
  char path[] = "/tmp/consent-feature-backlog-XXXXXX";
  assert(mkdtemp(path));
  std::string endpoint = std::string(path) + "/socket";
  sockaddr_un address{}; address.sun_family = AF_UNIX;
  assert(endpoint.size() < sizeof(address.sun_path)); strcpy(address.sun_path, endpoint.c_str());
  int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0); assert(listener >= 0);
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(listener, 0) == 0);
  int queued = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); assert(queued >= 0);
  assert(ConnectSocket(queued, reinterpret_cast<sockaddr*>(&address), sizeof(address), 100) == 0);
  int blocked = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0); assert(blocked >= 0);
  int64_t before = g_get_monotonic_time();
  assert(ConnectSocket(blocked, reinterpret_cast<sockaddr*>(&address), sizeof(address), 100) == -ETIMEDOUT);
  assert(g_get_monotonic_time() - before < 500000);
  close(blocked); close(queued); close(listener); unlink(endpoint.c_str()); rmdir(path);
  std::puts("PASS actual AF_UNIX saturated-backlog connect deadline");
  consent_mock::CoordinatorFixture::Run();
  consent_mock::CoordinatorFixture::ReuseAndRetired();
  consent_mock::WorkerFixture::Run();
  return 0;
}
