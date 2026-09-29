/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights reserved.
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

#ifndef CONSENT_COMMON_RECOVERY_STATE_HH_
#define CONSENT_COMMON_RECOVERY_STATE_HH_

#include <sys/types.h>

#include <cstddef>
#include <string>

namespace consent {
namespace recovery {

enum class Phase {
  kAbsent,
  kFresh,
  kClaimed,
  kInitialized,
  kRecoveryRequired,
  kInvalid
};
enum class Boot { kReject, kFirst, kExisting, kRecoverDatabase };

struct Receipt {
  Phase phase = Phase::kAbsent;
  std::string unit;
  std::string invocation;
  std::string nonce;
};

bool HexId(const std::string& value, size_t width) noexcept;
Boot Assess(Phase phase, bool registry, bool database, bool ticket) noexcept;
bool ReadReceipt(int authority, gid_t group, Receipt* receipt) noexcept;
bool WriteReceipt(int authority, gid_t group, const Receipt& receipt) noexcept;

}  // namespace recovery
}  // namespace consent
#endif  // CONSENT_COMMON_RECOVERY_STATE_HH_
