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
#ifndef CONSENT_ENDPOINT_HH_
#define CONSENT_ENDPOINT_HH_

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

#include <cstddef>
#include <cstring>

namespace consent {

/* This predicate accepts only observations supplied by kernel socket APIs.
 * It cannot establish trust from fields in an application protocol message. */
inline bool MatchActivatedPeer(pid_t pid, uid_t uid,
    const struct sockaddr_un& address, socklen_t address_size,
    const char* label, size_t label_size, const char* expected_path,
    const char* expected_label) {
  if (pid != 1 || uid != 0 || !expected_path || !expected_label ||
      !*expected_label || !label || !label_size ||
      strlen(expected_path) >= sizeof(address.sun_path) ||
      address.sun_family != AF_UNIX ||
      address_size != offsetof(struct sockaddr_un, sun_path) + strlen(expected_path) + 1 ||
      memcmp(address.sun_path, expected_path, strlen(expected_path) + 1))
    return false;
  if (label[label_size - 1] == '\0')
    --label_size;
  return !memchr(label, '\0', label_size) && label_size == strlen(expected_label) &&
      !memcmp(label, expected_label, label_size);
}

}  // namespace consent
#endif  // CONSENT_ENDPOINT_HH_
