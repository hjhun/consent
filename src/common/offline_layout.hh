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
#ifndef CONSENT_OFFLINE_LAYOUT_HH_
#define CONSENT_OFFLINE_LAYOUT_HH_

#include <cstddef>
#include <string>

namespace consent {
namespace offline {
constexpr size_t kMaxRegistrations = 128;
constexpr size_t kMaxRegistrationEntries = 256;
constexpr size_t kMaxRegistrationBytes = 4 * 1024 * 1024;
constexpr size_t kMaxRegistrationFileBytes = 65536;
constexpr const char* kRegistrationDirectory = "registrations";

inline bool RegistrationName(const std::string& name) {
  if (name.size() != 71 || name.compare(64, 7, ".parcel") != 0)
    return false;
  for (size_t i = 0; i < 64; ++i) {
    if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f')))
      return false;
  }
  return true;
}

inline bool PendingRegistrationName(const std::string& name) {
  if (name.size() != 45 || name.compare(0, 9, ".pending-") != 0)
    return false;
  for (size_t i = 0; i < 36; ++i) {
    char ch = name[9 + i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (ch != '-')
        return false;
    } else if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
        (ch >= 'A' && ch <= 'F'))) {
      return false;
    }
  }
  return true;
}
}  // namespace offline
}  // namespace consent
#endif  // CONSENT_OFFLINE_LAYOUT_HH_
