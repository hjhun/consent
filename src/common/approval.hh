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
#ifndef CONSENT_COMMON_APPROVAL_HH_
#define CONSENT_COMMON_APPROVAL_HH_

#include "message.hh"

namespace consent {
namespace approval {

// Canonical, bounded opt-in selection context. No field changes GrantKey.
// SelectionDigest hashes the selected fields using sorted UTF-8 keys and
// decimal byte-length prefixes, independently of the Parcel or DB encoding.
Message SelectionFields(const Message& request);
std::string SelectionDigest(const Message& request);
bool Validate(const Message& request, std::string* error = nullptr);
void CopyContext(const Message& source, Message* destination);
bool SameContext(const Message& left, const Message& right);
bool Covers(const Message& request, const std::string& mode, int64_t expires,
    int64_t now);

}  // namespace approval
}  // namespace consent
#endif  // CONSENT_COMMON_APPROVAL_HH_
