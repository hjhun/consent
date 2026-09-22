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
#ifndef CONSENT_MOCK_FEATURE_GATE_HH_
#define CONSENT_MOCK_FEATURE_GATE_HH_

#ifndef CONSENT_FEATURE_GATE_TEST
#error FeatureGate is available only in the dedicated test executable.
#endif

#include <cstdint>
#include <string>

namespace consent_mock {

// Test-only action-dispatch gate. The actor must continue pumping UI commands.
// The script creates the fixed protected directory/FIFO; no runtime override.
class FeatureGate final {
 public:
  FeatureGate() = default;
  ~FeatureGate();
  FeatureGate(const FeatureGate&) = delete;
  FeatureGate& operator=(const FeatureGate&) = delete;

  // Arm only after a real successful AUTHORIZE reply, before sending "start".
  // 0 arms the gate; 1 skips the other configured test stage; negative errno
  // fails closed. The root-protected selector exists only in this test build.
  int Arm(const std::string& receipt, const std::string& operation_id,
      const std::string& feature_id, const std::string& job_id);
  // A data-use permit is an existing artifact, not a new acquisition receipt.
  int ArmReuse(const std::string& artifact, const std::string& session,
      const std::string& generation, const std::string& context_digest,
      const std::string& operation_id, const std::string& feature_id,
      const std::string& job_id);
  // 0: waiting; 1: explicitly released; negative errno: fail closed.
  int Poll();
  // Record cancellation and close the FIFO; repeated calls do nothing.
  void Cancel() noexcept;

 private:
  int Begin(const char* stage, const char* proof_kind, const std::string& proof_id,
      const std::string& operation_id, const std::string& feature_id,
      const std::string& job_id, const std::string& proof_fields);
  int Publish(const char* name, const char* state) noexcept;
  void Close() noexcept;
  int directory_ = -1;
  int release_ = -1;
  int64_t deadline_ = 0;
  bool armed_ = false;
  std::string fields_;
};

}  // namespace consent_mock
#endif  // CONSENT_MOCK_FEATURE_GATE_HH_
