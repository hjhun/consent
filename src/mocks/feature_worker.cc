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

#include <sys/prctl.h>
#include <unistd.h>
#include <signal.h>

#include <algorithm>
#include <cstdio>
#include <map>

namespace consent_mock {
namespace {
class Worker final {
  friend class WorkerFixture;
 public:
  explicit Worker(std::string role) : role_(std::move(role)) {}
  ~Worker() {
    for (auto& buffer : buffers_) std::fill(buffer.second.begin(), buffer.second.end(), 0);
    if (client_) consent_client_destroy(client_);
  }
  int Run() {
    Peer parent;
    int status = 0;
    if (!parent.Authenticate(3, false, &status)) {
      WorkerDiagnostic("parent-authentication", status); return 1;
    }
    if (prctl(PR_SET_PDEATHSIG, SIGTERM)) {
      WorkerDiagnostic("parent-death-signal", -errno); return 1;
    }
    if (!parent.Alive()) { WorkerDiagnostic("parent-liveness", -ESTALE); return 1; }
    status = consent_client_create(&client_);
    if (status) { WorkerDiagnostic("consent-client-create", status); return 1; }
    WorkerDiagnostic("worker-ready", 0);
    for (;;) {
      Message input;
      int status = ReceiveFrame(3, &input, 60000);
      if (status == -ETIMEDOUT && parent.Alive()) continue;
      if (status || !parent.Alive()) {
        WorkerDiagnostic("worker-input", status ? status : -ESTALE);
        return status == -ECONNRESET ? 0 : 1;
      }
      Message output;
      const auto id = consent::Get(input, "id");
      const auto method = consent::Get(input, "method");
      input.erase("v"); input.erase("id"); input.erase("method");
      try { status = Execute(method, input, &output); }
      catch (...) { status = -EINVAL; }
      output["v"] = "1"; output["id"] = id; output["method"] = "reply";
      output["status"] = std::to_string(status);
      status = SendFrame(3, output);
      if (status) { WorkerDiagnostic("worker-output", status); return 1; }
    }
  }
 private:
  int Authorize(const Message& input, Message* output) {
#ifdef CONSENT_FEATURE_UNIT_TEST
    if (!client_) { ++test_authorizations_; *output = {{"decision", "ALLOWED"}}; return 0; }
#endif
    return Check(client_, input, output);
  }
  int PreviousAction(const Message& input, Message* output) const {
    auto found = actions_.find(consent::Get(input, "operation_id"));
    if (found == actions_.end()) return 1;
    if (found->second.first != input) return -EEXIST;
    *output = found->second.second;
    (*output)["action_retry"] = "1";
    return 0;
  }
  int Execute(const std::string& method, Message input, Message* output) {
    if (role_ == "holder") {
      if (method == "cleanup") {
        Message pending;
        int status = Invoke(client_, consent_cleanup_get_pending, input, &pending);
        if (status) return status;
        for (int i = 0; i < consent::Number(pending, "count"); ++i) {
          auto artifact = consent::Get(pending, "a" + std::to_string(i) + ".artifact");
          auto buffer = buffers_.find(artifact);
          if (buffer == buffers_.end()) return -ENOENT;  // No invented deletion evidence.
          std::fill(buffer->second.begin(), buffer->second.end(), 0);
          Message ack{{"artifact", artifact}, {"success", "1"}};
          Message ignored;
          status = Invoke(client_, consent_data_release, ack, &ignored);
          if (status) return status;
          proofs_.erase(artifact);
          buffers_.erase(buffer);
        }
        status = Invoke(client_, consent_cleanup_get_pending, input, output);
        (*output)["resident_artifacts"] = std::to_string(buffers_.size());
        return status;
      }
      if (method == "register") {
        if (buffers_.size() >= 16) return -EBUSY;
        int status = Invoke(client_, consent_data_register, input, output);
        if (!status) {
          auto artifact = consent::Get(*output, "artifact");
          auto permit = consent::Get(*output, "permit");
          if (artifact.empty() || permit != artifact || consent::Get(input, "receipt").empty()) return -EPROTO;
          buffers_[artifact] = std::vector<char>(64, 'M');
          proofs_[artifact] = {permit, consent::Get(input, "receipt")};
        }
        return status;
      }
      if (method == "reuse-authorize" || method == "reuse-start") {
        auto artifact = consent::Get(input, "artifact");
        if (!buffers_.count(artifact) || !proofs_.count(artifact)) return -ENOENT;
        input["mode"] = "AUTHORIZE";
        input["operation"] = "reuse-data";
        auto permit = consent::Get(input, "permit");
        auto receipt = consent::Get(input, "original_receipt");
        auto digest = consent::Get(input, "context_digest");
        input.erase("permit"); input.erase("original_receipt"); input.erase("context_digest");
        const auto& proof = proofs_.at(artifact);
        if (method == "reuse-authorize") {
          if (!permit.empty() || !receipt.empty() || !digest.empty()) return -EINVAL;
          int status = Authorize(input, output);
          if (!status && consent::Get(*output, "decision") == "ALLOWED") {
            reuse_authorized_ = input;
            (*output)["permit"] = proof.first;
            (*output)["original_receipt"] = proof.second;
            (*output)["context_digest"] = ContextDigest(input);
          }
          return status;
        }
        if (input != reuse_authorized_ || permit != proof.first || receipt != proof.second ||
            digest != ContextDigest(input)) return -EACCES;
        int prior = PreviousAction(input, output);
        if (prior != 1) return prior;
        if (actions_.size() >= 1024) return -EBUSY;
        int status = Authorize(input, output);
        if (status || consent::Get(*output, "decision") != "ALLOWED") return status;
        (*output)["permit"] = proof.first; (*output)["original_receipt"] = proof.second;
        (*output)["context_digest"] = digest; (*output)["artifact"] = artifact;
        (*output)["action_started"] = "1"; (*output)["action_retry"] = "0";
        (*output)["mock_action"] = "calendar.reuse";
        actions_[consent::Get(input, "operation_id")] = {input, *output};
        std::printf("{\"event\":\"action\",\"feature\":\"calendar.reuse\",\"operation\":%s,\"artifact\":%s,\"retry\":false}\n",
            Json(consent::Get(input, "operation_id")).c_str(), Json(artifact).c_str());
        std::fflush(stdout);
        return 0;
      }
      if (method == "release") {
        auto buffer = buffers_.find(consent::Get(input, "artifact"));
        if (buffer == buffers_.end()) return -ENOENT;
        std::fill(buffer->second.begin(), buffer->second.end(), 0);
        proofs_.erase(consent::Get(input, "artifact"));
        buffers_.erase(buffer);
        input["success"] = "1";
        return Invoke(client_, consent_data_release, input, output);
      }
      return -ENOSYS;
    }
    if (role_ != "cm" && role_ != "ce") return -EPERM;
    auto feature = FindFeature(consent::Get(input, "r0.feature_id"));
    if (!feature || feature->worker != role_ || consent::Get(input, "count") != "1") return -EACCES;
    auto expected = feature->row;
    if (feature->id == "calendar.read" && consent::Get(input, "r0.scope") == "calendar.default.next30days")
      expected = ExpandedCalendarRow();
    for (const auto& field : expected)
      if (consent::Get(input, "r0." + field.first) != field.second) return -EACCES;
    input["mode"] = "AUTHORIZE";
    if (method == "authorize") {
      int status = Authorize(input, output);
      if (!status && consent::Get(*output, "decision") == "ALLOWED") authorized_ = input;
      return status;
    }
    if (method != "start" || input != authorized_) return -EACCES;
    int prior = PreviousAction(input, output);
    if (prior != 1) return prior;
    // Retry the exact authoritative operation before the mock action. ONCE
    // receipts remain deduplicated; policy/session changes still block use.
    int status = Authorize(input, output);
    if (status || consent::Get(*output, "decision") != "ALLOWED") return status;
    auto operation = consent::Get(input, "operation_id");
    if (actions_.size() >= 1024) return -EBUSY;

    (*output)["mock_action"] = feature->id;
    (*output)["action_started"] = "1";
    (*output)["action_retry"] = "0";
    actions_[operation] = {input, *output};
    std::printf("{\"event\":\"action\",\"feature\":%s,\"operation\":%s,\"retry\":%s}\n",
        Json(feature->id).c_str(), Json(operation).c_str(), "false");
    std::fflush(stdout);
    return 0;
  }
  std::string role_;
  consent_client_h client_ = nullptr;
  Message authorized_, reuse_authorized_;
  std::map<std::string, std::pair<Message, Message>> actions_;
  std::map<std::string, std::pair<std::string, std::string>> proofs_;
#ifdef CONSENT_FEATURE_UNIT_TEST
  unsigned test_authorizations_ = 0;
#endif
  std::map<std::string, std::vector<char>> buffers_;
};
}
}  // namespace consent_mock
extern "C" int consent_feature_worker_main(const char* role) {
  try { return consent_mock::Worker(role).Run(); }
  catch (...) { return 1; }
}
