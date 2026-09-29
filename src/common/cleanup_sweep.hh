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
#ifndef CONSENT_COMMON_CLEANUP_SWEEP_HH_
#define CONSENT_COMMON_CLEANUP_SWEEP_HH_

#include "common/message.hh"

#include <functional>

namespace consent {

struct CleanupProgress {
  Message scope;
  std::string cursor;
  int failure = 0;
  bool finished = true;
};

// EINPROGRESS preserves continuation and the first individual error. Resume
// with the same scope; after finished=true a call begins a fresh retry sweep.
// Page errors stop the sweep. Individual deletion/ACK errors are remembered
// while later entries/pages are still attempted. Retry failures in a new sweep.
int CleanupSweep(Message input, CleanupProgress* progress,
    const std::function<int(const Message&, Message*)>& fetch,
    const std::function<int(const std::string&)>& cleanup);

}  // namespace consent
#endif  // CONSENT_COMMON_CLEANUP_SWEEP_HH_
