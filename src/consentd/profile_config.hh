/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
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
#ifndef CONSENTD_PROFILE_CONFIG_HH_
#define CONSENTD_PROFILE_CONFIG_HH_

#include "profile_state.hh"

namespace consentd {

struct ProfileConfig {
  bool fixture = false;
  int session_uid = 0;
  std::vector<ProfileBinding> bindings;
};

// The caller opens a protected file; this consumes its complete bounded bytes.
// No partial result is published. A missing file is handled by the caller.
bool ReadProfileConfig(int fd, ProfileConfig* config);

}  // namespace consentd
#endif  // CONSENTD_PROFILE_CONFIG_HH_
