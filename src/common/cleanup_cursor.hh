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
#ifndef CONSENT_COMMON_CLEANUP_CURSOR_HH_
#define CONSENT_COMMON_CLEANUP_CURSOR_HH_

#include <string>

namespace consent {

// A reading position, never authentication or a membership snapshot.
struct CleanupCursor {
  std::string incarnation;
  std::string scope;
  std::string last;
  std::string upper;

  static bool Parse(const std::string& token, CleanupCursor* cursor);
  std::string Encode() const;
};

std::string CleanupScope(const std::string& holder, const std::string& instance,
    const std::string& subject, const std::string& profile, bool reconcile);

}  // namespace consent
#endif  // CONSENT_COMMON_CLEANUP_CURSOR_HH_
