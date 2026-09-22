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
#include "feature_common.hh"
#include "feature_api.hh"
#include "common/approval.hh"
#ifdef CONSENT_FEATURE_GATE_TEST
#include "feature_gate.hh"
#endif

#include <systemd/sd-daemon.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/un.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <memory>

namespace consent_mock {
namespace {
volatile sig_atomic_t stopping = 0;
void Stop(int) { stopping = 1; }
std::string Random() {
  gchar* value = g_uuid_string_random();
  std::string result(value);
  g_free(value);
  return result;
}
class Child final {
 public:
  explicit Child(const std::string& role) {
    const auto path = std::string(kWorkerPrefix) + role;
    int executable = Protected(path);
    if (executable < 0) {
      WorkerDiagnostic("executable-validation", -EACCES);
      throw std::runtime_error("unprotected worker executable");
    }
    int sockets[2];
    int status = CreateWorkerChannel(sockets);
    if (status) {
      close(executable); WorkerDiagnostic("create-channel", status);
      throw std::runtime_error("worker connection failed");
    }
    pid_ = fork();
    if (pid_ == 0) {
      close(sockets[0]);
      if (dup2(sockets[1], 3) < 0 || fcntl(3, F_SETFD, 0) < 0) {
        WorkerDiagnostic("inherit-channel", -errno); _exit(126);
      }
      if (sockets[1] != 3) close(sockets[1]);
      // fexecve binds execution to the already verified inode. Parent-created
      // connected socket credentials authenticate the root/System argo process.
      char* arguments[] = {const_cast<char*>(path.c_str()), const_cast<char*>("feature-worker"), nullptr};
      char* environment[] = {const_cast<char*>("PATH=/usr/bin:/bin"), nullptr};
      fexecve(executable, arguments, environment);
      WorkerDiagnostic("execute-worker", -errno);
      _exit(127);
    }
    int fork_error = errno;
    close(executable); close(sockets[1]);
    if (pid_ < 0) {
      close(sockets[0]); WorkerDiagnostic("fork-worker", -fork_error);
      throw std::runtime_error("worker fork failed");
    }
    fd_ = sockets[0];
    WorkerDiagnostic("worker-spawned", 0, pid_);
  }
  ~Child() {
    if (fd_ >= 0) close(fd_);
    if (pid_ > 0 && !ObserveExit()) {
      kill(pid_, SIGTERM);
      for (unsigned i = 0; i < 20; ++i) {
        if (ObserveExit()) return;
        usleep(10000);
      }
      kill(pid_, SIGKILL); ObserveExit(true);
    }
  }
  int Send(const std::string& method, Message message) {
    message["v"] = "1"; message["id"] = std::to_string(++serial_); message["method"] = method;
    return SendFrame(fd_, message);
  }
  pid_t Pid() const { return pid_; }
  bool Ready() const { pollfd wait{fd_, POLLIN, 0}; return poll(&wait, 1, 0) > 0; }
  int Receive(Message* message) {
    int status = ReceiveFrame(fd_, message);
    if (status || consent::Get(*message, "id") != std::to_string(serial_) ||
        consent::Get(*message, "method") != "reply") {
      WorkerDiagnostic("receive-worker", status ? status : -EPROTO, pid_);
      ObserveExit();
      message->clear(); return status ? status : -EPROTO;
    }
    return static_cast<int>(consent::Number(*message, "status", -EPROTO));
  }
 private:
  bool ObserveExit(bool wait = false) {
    if (reaped_) return true;
    int status = 0;
    pid_t result;
    do { result = waitpid(pid_, &status, wait ? 0 : WNOHANG); } while (result < 0 && errno == EINTR);
    if (result == pid_) {
      reaped_ = true;
      if (WIFEXITED(status)) WorkerDiagnostic("child-exit", WEXITSTATUS(status), pid_);
      else if (WIFSIGNALED(status)) WorkerDiagnostic("child-signal", WTERMSIG(status), pid_);
    } else if (result < 0) {
      int error = errno;
      WorkerDiagnostic("child-wait", -error, pid_);
      if (error == ECHILD) reaped_ = true;
    }
    return reaped_;
  }
  pid_t pid_ = -1;
  int fd_ = -1;
  uint64_t serial_ = 0;
  bool reaped_ = false;
};

class Coordinator final {
  friend class CoordinatorFixture;
 public:
#ifdef CONSENT_FEATURE_UNIT_TEST
  explicit Coordinator(std::nullptr_t) : selection_id_("fixture-selection") {}
#endif
  Coordinator() : context_(g_main_context_new()), selection_id_("selection-" + Random()) {
    if (!context_ || consent_client_create_with_context(context_, &client_))
      throw std::runtime_error("cannot connect to consent daemon");
  }
  ~Coordinator() {
    Cancel();
    if (!session_.empty()) {
      Message ignored;
      Invoke(client_, consent_session_close, Context(), &ignored);
    }
    children_.clear();
    if (client_) consent_client_destroy(client_);
    if (context_) g_main_context_unref(context_);
  }
  int Run() {
    if (geteuid() != 0 || sd_listen_fds(1) != 1 ||
        sd_is_socket_unix(3, SOCK_STREAM, 1, kFeatureSocket, 0) != 1)
      return 1;
    sockaddr_un address{};
    socklen_t address_size = sizeof(address);
    struct stat endpoint{};
    group* users = getgrnam("users");
    if (!users || getsockname(3, reinterpret_cast<sockaddr*>(&address), &address_size) ||
        address.sun_family != AF_UNIX || address_size != offsetof(sockaddr_un, sun_path) + strlen(kFeatureSocket) + 1 ||
        memcmp(address.sun_path, kFeatureSocket, strlen(kFeatureSocket) + 1) ||
        lstat(kFeatureSocket, &endpoint) || !S_ISSOCK(endpoint.st_mode) || endpoint.st_uid != 0 ||
        endpoint.st_gid != users->gr_gid || (endpoint.st_mode & 0777) != 0660) return 1;
    int directory = Protected(kFeatureDirectory, true);
    if (directory < 0) return 1;
    close(directory);
    fcntl(3, F_SETFL, fcntl(3, F_GETFL) | O_NONBLOCK);
    signal(SIGTERM, Stop); signal(SIGINT, Stop); signal(SIGPIPE, SIG_IGN);
    std::printf("{\"event\":\"feature-ready\",\"catalog_revision\":%s,\"pid\":%ld}\n", Json(CatalogRevision()).c_str(), static_cast<long>(getpid())); std::fflush(stdout);
    while (!stopping) {
      pollfd wait{3, POLLIN, 0};
      if (poll(&wait, 1, 50) > 0) {
        // Process settings before worker completions. All changes and the
        // action-start dispatch below share this actor's serialization point.
        for (unsigned i = 0; i < 8; ++i) {
          int fd = accept4(3, nullptr, nullptr, SOCK_CLOEXEC);
          if (fd < 0) break;
          Handle(fd); close(fd);
        }
      }
      Pump();
    }
    return 0;
  }
 private:
  struct Job {
    Coordinator* owner;
    Message request;
    consent_async_id_t async = 0;
    int status = 0;
    Message result;
    bool done = false;
    uint64_t revision = 0;
    int64_t expires = 0;
    std::vector<std::string> features;
    size_t next = 0;
    std::string phase;
    std::string worker;
    Message enforcement;
    std::string start_method = "start";
    std::string acquisition_receipt;
    bool task = false;
    bool ephemeral = false;
    bool alternative = false;
    bool cleanup = false;
  };
  struct Retired {
    std::string worker, phase, job_id, operation_id, feature;
    pid_t worker_pid = 0;
    int64_t deadline = 0;
  };
  Message Context() const {
    Message result{{"subject", "owner"}, {"profile", "default"}};
    if (!session_.empty()) { result["session"] = session_; result["generation"] = generation_; }
    return result;
  }
  std::string State() const {
    Message output{{"decision", decision_}, {"request_id", request_id_},
        {"selection_id", display_id_.empty() ? selection_id_ : display_id_}, {"selection_revision", std::to_string(revision_)},
        {"catalog_revision", CatalogRevision()}, {"coordinator_epoch", selection_id_}, {"settings_grant_mode", mode_},
        {"settings_duration_ms", std::to_string(duration_)},
        {"selection_digest", digest_}, {"grant_mode", display_mode_}, {"duration_ms", std::to_string(display_duration_)},
        {"session", session_}, {"generation", generation_}, {"reason", reason_},
        {"action_count", std::to_string(action_count_)}, {"artifact", artifact_},
        {"cleanup_state", cleanup_state_}, {"cleanup_pending", cleanup_pending_},
        {"execution_pending", (job_ && !job_->phase.empty()) || retired_ ? "1" : "0"},
        {"retired_result_count", std::to_string(retired_results_.size())},
        {"execution_result_count", std::to_string(execution_results_.size())},
        {"last_retired_outcome", retired_results_.empty() ? "" : consent::Get(retired_results_.back(), "outcome")}};
    auto json = JsonFields(output);
    json.pop_back(); json += ",\"schema\":1,\"selected\":[";
    for (const auto& id : selected_) { if (json.back() != '[') json += ','; json += Json(id); }
    return json + "]}";
  }
  void EmitState() const {
    std::printf("{\"event\":\"feature-state\",\"state\":%s}\n", State().c_str());
    std::fflush(stdout);
  }
  bool Current(const Job& job) const {
    if (job.cleanup) return g_get_monotonic_time() < job.expires;
    if (job.revision != revision_ ||
        g_get_monotonic_time() >= job.expires ||
        (!job.ephemeral && expires_ && g_get_monotonic_time() >= expires_)) return false;
    for (const auto& id : job.features) if (!job.ephemeral && !selected_.count(id)) return false;
    return consent::Get(job.request, "session") == session_ &&
        consent::Get(job.request, "generation") == generation_ &&
        consent::Get(job.request, "selection_digest") == consent::approval::SelectionDigest(job.request);
  }
  void CountAction(const std::string& operation, const Message& result) {
    if (consent::Get(result, "action_started") == "1" && counted_actions_.insert(operation).second)
      ++action_count_;
  }
  Retired CaptureJob() const {
    Retired record;
    record.worker = job_->worker; record.phase = job_->phase;
    record.job_id = consent::Get(job_->request, "operation_id");
    record.operation_id = consent::Get(job_->enforcement, "operation_id");
    record.feature = job_->phase.compare(0, 5, "reuse") == 0 ? "calendar.reuse" :
        job_->next < job_->features.size() ? job_->features[job_->next] : "cleanup";
    auto worker = children_.find(record.worker);
    if (worker != children_.end()) record.worker_pid = worker->second->Pid();
    record.deadline = g_get_monotonic_time() + 180000000;
    return record;
  }
  void Retire() { retired_ = std::make_unique<Retired>(CaptureJob()); }
  std::string RecordCompletion(const Retired& record, int status, const Message& result, bool retired) {
    const bool reply = consent::Get(result, "method") == "reply";
    std::string outcome = "uncertain";
    if (reply && consent::Get(result, "action_started") == "1") {
      outcome = "action-started";
      CountAction(record.operation_id, result);
    } else if (reply && status) outcome = "worker-failed";
    else if (reply && (record.phase == "authorize" || record.phase == "reuse-authorize"))
      outcome = consent::Get(result, "decision") == "ALLOWED" ? "authorized-not-started" : "authorization-denied";
    else if (reply && record.phase == "register") outcome = "artifact-registered";
    else if (reply && record.phase == "cleanup") outcome = "cleanup-replied";
    Message evidence{{"worker", record.worker}, {"phase", record.phase},
        {"job_id", record.job_id}, {"operation_id", record.operation_id},
        {"feature", record.feature}, {"worker_pid", std::to_string(record.worker_pid)},
        {"status", std::to_string(status)}, {"outcome", outcome}, {"retired", retired ? "1" : "0"},
        {"action_started", consent::Get(result, "action_started", "unknown")},
        {"action_retry", consent::Get(result, "action_retry", "unknown")}};
    for (const char* key : {"artifact", "permit", "receipt", "original_receipt", "context_digest"})
      if (result.count(key)) evidence[key] = consent::Get(result, key);
    // Accepted command and exact catalog bounds limit these non-evicting
    // histories. Keeping uncertainty never permits another start/retry.
    execution_results_.push_back(evidence);
    if (retired) retired_results_.push_back(evidence);
    std::printf("{\"event\":%s,\"evidence\":%s}\n",
        Json(retired ? "feature-retired-result" : "feature-execution-result").c_str(), JsonFields(evidence).c_str());
    std::fflush(stdout);
    return outcome;
  }
  void RetiredResult(int status, const Message& result) {
    if (!retired_) return;
    RecordCompletion(*retired_, status, result, true);
    retired_.reset();
    EmitState();
  }
  void Cancel() {
    if (!job_) return;
#ifdef CONSENT_FEATURE_GATE_TEST
    gate_.Cancel();
#endif
    if (job_->async) {
      Message query{{"subject", "owner"}, {"profile", "default"},
          {"client_request_id", consent::Get(job_->request, "client_request_id")}};
      Message ignored;
      Invoke(client_, consent_cancel_request, query, &ignored);
      consent_async_detach(client_, job_->async);
    }
    // A provider request already in flight is drained before another is sent.
    // Its result cannot start an action after the snapshot is invalidated.
    if (!job_->phase.empty() && job_->phase != "gate") Retire();
    job_.reset();
    decision_ = "INVALIDATED"; request_id_.clear(); reason_ = "selection or conversation changed";
  }
  void Handle(int fd) {
    auto peer = std::make_unique<Peer>();
    if (!peer->Authenticate(fd, true)) {
      ucred credentials{};
      socklen_t length = sizeof(credentials);
      if (!getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) &&
          length == sizeof(credentials)) {
        std::printf("{\"event\":\"feature-peer-rejected\",\"pid\":%ld,\"uid\":%lu,\"reason\":\"ui-peer-rejected\"}\n",
            static_cast<long>(credentials.pid), static_cast<unsigned long>(credentials.uid));
      } else {
        std::puts("{\"event\":\"feature-peer-rejected\",\"reason\":\"kernel-peer-credentials-unavailable\"}");
      }
      std::fflush(stdout);
      return;
    }
    Message request, reply;
    int status = ReceiveFrame(fd, &request, 1000);
    if (!status && !peer->Alive()) status = -EACCES;
    std::string json;
    if (!status) {
      try { status = Execute(request, &peer, &json); }
      catch (const std::bad_alloc&) { status = -ENOMEM; }
      catch (...) { status = -EIO; }
    }
    reply = {{"v", "1"}, {"id", consent::Get(request, "id", "1")}, {"method", "reply"},
        {"status", std::to_string(status)}};
    if (!status) reply["json"] = json;
    SendFrame(fd, reply, 1000);
  }
  int Execute(const Message& request, std::unique_ptr<Peer>* peer, std::string* json) {
    const auto method = consent::Get(request, "method");
    if (method == "catalog") { *json = CatalogJson(std::to_string(revision_), selection_id_); return 0; }
    if (method == "status") { *json = State(); return 0; }
    const auto command = consent::Get(request, "command_id");
    if (!Identifier(command)) return -EINVAL;
    (void)peer;  // Handle already authenticated each kernel connection.
    if (consent::Get(request, "coordinator_epoch") != selection_id_ ||
        consent::Get(request, "catalog_revision") != CatalogRevision()) return -ESTALE;
    const auto key = std::string("org.tizen.consentui:owner:default:") + command;
    auto prior = commands_.find(key);
    if (prior != commands_.end()) {
      if (prior->second.first != request) return -EEXIST;
      *json = prior->second.second; return 0;
    }
    if (commands_.size() >= 256 || retired_results_.size() >= 256 || execution_results_.size() >= 1024) return -EBUSY;
    if (consent::Get(request, "expected_revision") != std::to_string(revision_)) return -ESTALE;
    if (method == "select") {
      if (job_ && job_->cleanup) return -EBUSY;
      std::set<std::string> selected;
      auto mode = consent::Get(request, "grant_mode");
      int64_t duration = consent::Number(request, "duration_ms", -1);
      if (!ParseSelection(consent::Get(request, "selected"), &selected) ||
          (mode != "SESSION" && mode != "TIMED") ||
          (mode == "TIMED" ? duration != 1800000 : duration != 0)) return -EINVAL;
      Cancel(); cleanup_state_.clear(); cleanup_pending_.clear();
      selected_ = std::move(selected); mode_ = mode; duration_ = duration;
      expires_ = mode == "TIMED" ? g_get_monotonic_time() + duration * 1000 : 0;
      ++revision_; digest_.clear(); display_id_.clear(); decision_ = "IDLE"; reason_.clear();
      display_mode_ = mode_; display_duration_ = duration_;
    } else {
      if (method != "preapprove" && method != "task") return -ENOSYS;
      if (method == "task" && consent::Get(request, "task") == "conversation-close") {
        if (static_cast<bool>(retired_)) return -EBUSY;
        Cancel();
        if (static_cast<bool>(retired_)) return -EBUSY;
        if (session_.empty()) { cleanup_state_ = "CLOSED"; cleanup_pending_ = "0"; decision_ = "ALLOWED"; }
        else {
          Message closed;
          int status = Invoke(client_, consent_session_get_state, Context(), &closed);
          if (status) return status;
          generation_ = consent::Get(closed, "generation");
          status = Invoke(client_, consent_session_close, Context(), &closed);
          if (status) return status;
          generation_ = consent::Get(closed, "generation");
          cleanup_state_ = consent::Get(closed, "state", "CLOSING");
          cleanup_pending_ = consent::Get(closed, "cleanup_pending", "0");
          auto job = std::make_unique<Job>(); job->owner = this; job->cleanup = true;
          job->expires = g_get_monotonic_time() + 180000000;
          job->phase = "cleanup"; job->worker = "holder"; job_ = std::move(job);
          decision_ = "PENDING"; request_id_.clear(); reason_ = "waiting for holder cleanup ACK";
          int sent = SendWorker("holder", "cleanup", {{"subject", "owner"}, {"profile", "default"}});
          if (sent) { FailJob("cleanup dispatch failed"); return sent; }
        }
        *json = State(); commands_[key] = {request, *json}; EmitState(); return 0;
      }
      if (job_ || static_cast<bool>(retired_)) return -EBUSY;
      if (method == "preapprove" && (selected_.empty() || (expires_ && g_get_monotonic_time() >= expires_))) return -EACCES;
      std::vector<std::string> features;
      auto task_mode = consent::Get(request, "task_mode", "SESSION");
      if (task_mode != "SESSION" && task_mode != "ONCE") return -EINVAL;
      bool ephemeral = method == "task" && task_mode == "ONCE";
      const auto task = consent::Get(request, "task");
      if (method == "preapprove") features.assign(selected_.begin(), selected_.end());
      else {
        if (task == "calendar-summary" || task == "calendar-alternative" || task == "calendar-expanded") features = {"calendar.read"};
        else if (task == "device-off") features = {"device.control"};
        else if (task == "calendar-and-device") features = {"calendar.read", "device.control"};
        else return -EINVAL;
        if (task == "calendar-expanded" && !ephemeral) return -EACCES;
        if (task == "calendar-alternative" && (ephemeral || artifact_.empty())) return -EACCES;
        for (const auto& id : features) if (!ephemeral && !selected_.count(id)) return -EACCES;
      }
      int status = StartRequest(features, method == "task", ephemeral, task);
      if (status) return status;
    }
    *json = State(); commands_[key] = {request, *json}; EmitState(); return 0;
  }
  int OpenSession() {
    if (!session_.empty()) return 0;
    auto input = Context();
    input["lifecycle"] = "CONNECTION_BOUND";
    input["lease_ms"] = "30000"; input["idle_timeout_ms"] = "3600000"; input["max_lifetime_ms"] = "3600000";
    Message output;
    int status = Invoke(client_, consent_session_open, input, &output);
    if (!status) { session_ = consent::Get(output, "session"); generation_ = consent::Get(output, "generation");
      heartbeat_ = g_get_monotonic_time() + 10000000; }
    return status;
  }
  Message Request(const std::vector<std::string>& features, bool task, bool ephemeral = false) const {
    Message request = Context();
    request["approval_version"] = "1"; request["request_kind"] = task ? "TASK" : "PREAPPROVAL";
    request["selection_id"] = selection_id_; request["selection_revision"] = std::to_string(revision_);
    request["grant_mode"] = task ? (ephemeral ? "ONCE" : "SESSION") : mode_;
    if (!task && mode_ == "TIMED") request["duration_ms"] = std::to_string(duration_);
    request["count"] = std::to_string(features.size());
    for (size_t index = 0; index < features.size(); ++index)
      for (const auto& field : FindFeature(features[index])->row)
        request["r" + std::to_string(index) + '.' + field.first] = field.second;
    request["selection_digest"] = consent::approval::SelectionDigest(request);
    return request;
  }
  int StartRequest(const std::vector<std::string>& features, bool task, bool ephemeral, const std::string& task_name) {
    if (session_failed_) return -ESTALE;
    cleanup_state_.clear(); cleanup_pending_.clear();
    int status = 0;
#ifdef CONSENT_FEATURE_UNIT_TEST
    if (!client_) { session_ = "fixture-session"; generation_ = "1"; }
    else
#endif
    status = OpenSession();
    if (status) return status;
    auto job = std::make_unique<Job>(); job->owner = this; job->features = features; job->task = task;
    job->ephemeral = ephemeral; job->alternative = task_name == "calendar-alternative";
    job->revision = revision_; job->expires = g_get_monotonic_time() + 180000000;
    job->request = Request(features, task, ephemeral);
    if (ephemeral) job->request["selection_id"] = "task-" + Random();
    if (task_name == "calendar-expanded") job->request["r0.scope"] = "calendar.default.next30days";
    job->request["selection_digest"] = consent::approval::SelectionDigest(job->request);
    job->request["client_request_id"] = "feature-" + Random();
    job->request["operation_id"] = "feature-" + Random();
    job->request["deadline_ms"] = "180000";
    auto params = Parameters(job->request);
#ifdef CONSENT_FEATURE_UNIT_TEST
    if (!client_) { job->done = true; job->result["decision"] = "ALLOWED"; }
    else
#endif
    if (job->alternative) { job->done = true; job->result["decision"] = "ALLOWED"; }
    else status = consent_request_async(client_, params.get(), Complete, job.get(), &job->async);
    if (status) return status;
    digest_ = consent::Get(job->request, "selection_digest");
    display_id_ = consent::Get(job->request, "selection_id");
    display_mode_ = consent::Get(job->request, "grant_mode");
    display_duration_ = consent::Number(job->request, "duration_ms"); job_ = std::move(job);
    decision_ = "PENDING"; request_id_.clear(); reason_.clear(); lookup_ = 0;
    return 0;
  }
  static void Complete(int status, const consent_result_t* result, void* data) noexcept {
    auto* job = static_cast<Job*>(data);
    try { job->status = status; job->result = Copy(result); job->done = true; }
    catch (...) { job->status = -ENOMEM; job->done = true; }
  }
  Child& Worker(const std::string& role) {
    auto& child = children_[role];
    if (!child) child = std::make_unique<Child>(role);
    return *child;
  }
  int SendWorker(const std::string& role, const std::string& method, const Message& request) {
#ifdef CONSENT_FEATURE_UNIT_TEST
    if (!client_) { test_last_worker_ = role; test_last_method_ = method; return 0; }
#endif
    return Worker(role).Send(method, request);
  }
  void FailJob(const std::string& reason) {
    decision_ = "INVALIDATED"; reason_ = reason; request_id_.clear(); job_.reset(); EmitState();
  }
  void BeginAction() {
    if (!job_ || !Current(*job_)) { Cancel(); return; }

    if (job_->next == job_->features.size()) {
      decision_ = "ALLOWED"; reason_ = "protected mock task completed"; job_.reset(); EmitState(); return;
    }
#ifdef CONSENT_FEATURE_UNIT_TEST
    if (!client_ && test_auto_complete_) { ++action_count_; decision_ = "ALLOWED"; job_.reset(); return; }
#endif
    const auto& feature = *FindFeature(job_->features[job_->next]);
    job_->worker = feature.worker; job_->start_method = "start";
    job_->enforcement = Request({feature.id}, true, job_->ephemeral);
    job_->enforcement["selection_id"] = consent::Get(job_->request, "selection_id");
    for (const auto& field : feature.row)
      job_->enforcement["r0." + field.first] = consent::Get(job_->request,
          "r" + std::to_string(job_->next) + '.' + field.first);
    job_->enforcement["selection_digest"] = consent::approval::SelectionDigest(job_->enforcement);
    job_->enforcement["operation_id"] = consent::Get(job_->request, "operation_id") + '.' + std::to_string(job_->next);
    job_->enforcement["step_id"] = "mock-execute";
    job_->enforcement["mode"] = "AUTHORIZE";
    if (feature.id == "calendar.read" && !artifact_.empty() &&
        consent::Get(job_->enforcement, "r0.scope") == consent::Get(feature.row, "scope")) {
      Message reuse = Context(); reuse["artifact"] = artifact_;
      for (const char* key : {"scope", "purpose", "recipient"}) reuse[key] = consent::Get(feature.row, key);
      reuse["operation_id"] = consent::Get(job_->enforcement, "operation_id"); reuse["step_id"] = "conversation-use";
      reuse["mode"] = "AUTHORIZE"; reuse["operation"] = "reuse-data";
      job_->enforcement = reuse; job_->start_method = "reuse-start";
      job_->worker = "holder"; job_->phase = "reuse-authorize";
      if (SendWorker("holder", "reuse-authorize", reuse)) FailJob("holder authorization transport failed");
    } else {
      job_->phase = "authorize";
      if (SendWorker(feature.worker, "authorize", job_->enforcement)) FailJob("provider authorization transport failed");
    }
  }
  void WorkerResult() {
    if (!job_ || job_->phase.empty() || !Worker(job_->worker).Ready()) return;
    Message result;
    int status = Worker(job_->worker).Receive(&result);
    HandleWorkerResult(status, result);
  }
  void DispatchStart() {
    if (!job_ || !Current(*job_)) { Cancel(); return; }
    job_->phase = job_->start_method;
#ifdef CONSENT_FEATURE_UNIT_TEST
    if (!client_) { ++test_dispatches_; return; }
#endif
    if (SendWorker(job_->worker, job_->start_method, job_->enforcement)) {
      Retire(); FailJob("action dispatch outcome uncertain");
    }
  }
  void HandleWorkerResult(int status, const Message& result) {
    if (!Current(*job_)) { Retire(); Cancel(); RetiredResult(status, result); return; }
    auto phase = job_->phase;
    if (phase == "start" || phase == "reuse-start") {
      const auto outcome = RecordCompletion(CaptureJob(), status, result, false);
      if (outcome != "action-started") {
        FailJob(outcome == "uncertain" ? "action outcome uncertain after dispatch" : "worker reported action failure");
        return;
      }
      if (status || consent::Get(result, "decision") != "ALLOWED") {
        FailJob("action started before worker completion failed"); return;
      }
    }
    if (phase == "cleanup") {
      if (status || consent::Get(result, "count") != "0") { FailJob("holder cleanup remains pending"); return; }
      Message state;
      status = Invoke(client_, consent_session_get_state, Context(), &state);
      cleanup_state_ = consent::Get(state, "state"); cleanup_pending_ = consent::Get(state, "cleanup_pending");
      if (status || cleanup_state_ != "CLOSED" || cleanup_pending_ != "0") { FailJob("session cleanup not confirmed"); return; }
      session_.clear(); generation_.clear(); artifact_.clear(); selected_.clear(); expires_ = 0; session_failed_ = false; ++revision_;
      decision_ = "ALLOWED"; reason_ = "conversation closed; holder erased memory and acknowledged cleanup";
      job_.reset(); EmitState(); return;
    }
    if (status || ((phase == "authorize" || phase == "start" || phase == "reuse-authorize" || phase == "reuse-start") &&
        consent::Get(result, "decision") != "ALLOWED")) {
      FailJob("authoritative provider or holder check denied"); return;
    }
    if (phase == "authorize" || phase == "reuse-authorize") {
      const bool reuse = phase == "reuse-authorize";
      if (reuse) {
        if (consent::Get(result, "permit") != consent::Get(job_->enforcement, "artifact") ||
            consent::Get(result, "original_receipt") != artifact_receipt_ ||
            consent::Get(result, "context_digest") != ContextDigest(job_->enforcement)) {
          FailJob("holder authorization proof mismatch"); return;
        }
        for (const char* key : {"permit", "original_receipt", "context_digest"})
          job_->enforcement[key] = consent::Get(result, key);
      }
#ifdef CONSENT_FEATURE_GATE_TEST
      int armed = reuse ? gate_.ArmReuse(consent::Get(job_->enforcement, "artifact"),
          consent::Get(job_->enforcement, "session"), consent::Get(job_->enforcement, "generation"),
          consent::Get(result, "context_digest"), consent::Get(job_->enforcement, "operation_id"),
          "calendar.reuse", consent::Get(job_->request, "operation_id")) :
          gate_.Arm(consent::Get(result, "receipt"), consent::Get(job_->enforcement, "operation_id"),
              job_->features[job_->next], consent::Get(job_->request, "operation_id"));
      if (armed < 0) { FailJob("test authorization gate unavailable"); return; }
      if (armed == 0) { job_->phase = "gate"; return; }
#endif
      // Provider and holder authorization both return to this actor before
      // action-start dispatch. A late authorization can never perform use.
      DispatchStart();
      return;
    }
    if (phase == "start") {
      const auto& feature = *FindFeature(job_->features[job_->next]);
      if (feature.id == "calendar.read") {
        Message registration = Context();
        for (const char* key : {"scope", "purpose", "recipient"}) registration[key] = consent::Get(job_->enforcement, std::string("r0.") + key);
        job_->acquisition_receipt = consent::Get(result, "receipt");
        registration["receipt"] = job_->acquisition_receipt; registration["storage_class"] = "MEMORY_ONLY";
        job_->phase = "register"; job_->worker = "holder";
        if (SendWorker("holder", "register", registration)) FailJob("artifact registration dispatch failed");
        return;
      }
    }
    if (phase == "register" && consent::Get(job_->enforcement, "r0.scope") == "calendar.default.next7days") {
      artifact_ = consent::Get(result, "artifact"); artifact_receipt_ = job_->acquisition_receipt;
    }
    job_->phase.clear(); ++job_->next; BeginAction();
  }
  void Pump() {
    for (unsigned i = 0; i < 64 && g_main_context_iteration(context_, FALSE); ++i) {}
    if (expires_ && g_get_monotonic_time() >= expires_) {
      Cancel(); selected_.clear(); expires_ = 0; ++revision_; reason_ = "selection period expired";
    }
    if (!session_.empty() && !session_failed_ && !(job_ && job_->cleanup) && g_get_monotonic_time() >= heartbeat_) {
      Message output;
      int status = Invoke(client_, consent_session_heartbeat, Context(), &output);
      heartbeat_ = g_get_monotonic_time() + 10000000;
      if (status) { Cancel(); session_failed_ = true; ++revision_;
        selected_.clear(); cleanup_state_ = "UNKNOWN"; cleanup_pending_.clear();
        reason_ = "conversation heartbeat failed; cleanup reconciliation required"; }
    }
    if (retired_) {
      if (Worker(retired_->worker).Ready()) {
        Message result;
        int status = Worker(retired_->worker).Receive(&result);
        RetiredResult(status, result);
      } else if (g_get_monotonic_time() >= retired_->deadline) {
        children_.erase(retired_->worker);
        RetiredResult(-ETIMEDOUT, {});
      }
    }
    if (!job_) return;
    if (!Current(*job_)) { Cancel(); return; }
#ifdef CONSENT_FEATURE_GATE_TEST
    if (job_->phase == "gate") {
      int gate = gate_.Poll();
      if (gate < 0) { gate_.Cancel(); FailJob("test authorization gate failed"); }
      else if (gate == 1) {
        DispatchStart();
      }
      return;
    }
#endif
    if (!job_->phase.empty()) { WorkerResult(); return; }
    if (job_->done) {
      decision_ = job_->status ? "INVALIDATED" : consent::Get(job_->result, "decision", "DENIED");
      request_id_ = consent::Get(job_->result, "request_id");
      if (decision_ == "ALLOWED" && job_->task) {
        decision_ = "PENDING"; request_id_.clear(); reason_ = "waiting for authoritative execution";
        BeginAction();
      }
      else { reason_ = job_->status ? "approval request failed" : "approval request completed"; job_.reset(); EmitState(); }
      return;
    }
    if (g_get_monotonic_time() < lookup_) return;
    Message query{{"subject", "owner"}, {"profile", "default"},
        {"client_request_id", consent::Get(job_->request, "client_request_id")}};
    Message result;
    int status = Invoke(client_, consent_get_request_result, query, &result);
    if (!status) request_id_ = consent::Get(result, "request_id");
    lookup_ = g_get_monotonic_time() + 250000;
  }
#ifdef CONSENT_FEATURE_GATE_TEST
  FeatureGate gate_;
#endif
  GMainContext* context_ = nullptr;
  consent_client_h client_ = nullptr;
  std::string selection_id_;
  uint64_t revision_ = 1;
  std::set<std::string> selected_;
  std::string mode_ = "SESSION", display_mode_ = "SESSION";
  int64_t display_duration_ = 0;
  int64_t duration_ = 0, expires_ = 0, heartbeat_ = 0, lookup_ = 0;
  std::string display_id_, cleanup_state_, cleanup_pending_;
  std::string session_, generation_, decision_ = "IDLE", reason_, request_id_, digest_, artifact_;
  unsigned action_count_ = 0;
  bool session_failed_ = false;
  std::unique_ptr<Job> job_;
  std::map<std::string, std::unique_ptr<Child>> children_;
  std::unique_ptr<Retired> retired_;
  std::vector<Message> retired_results_, execution_results_;
  std::set<std::string> counted_actions_;
  std::string artifact_receipt_;
#ifdef CONSENT_FEATURE_UNIT_TEST
  unsigned test_dispatches_ = 0;
  bool test_auto_complete_ = true;
  std::string test_last_worker_, test_last_method_;
#endif
  std::map<std::string, std::pair<Message, std::string>> commands_;
};
}  // namespace
}  // namespace consent_mock
extern "C" int consent_feature_argo_main() {
  try { return consent_mock::Coordinator().Run(); }
  catch (const std::exception& error) { std::fprintf(stderr, "feature coordinator: %s\n", error.what()); return 1; }
}
