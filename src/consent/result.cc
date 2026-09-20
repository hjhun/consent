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
#include "consent_result.h"

#include "api.hh"

#include <cstring>

namespace {
using consent::api::Guard;
}  // namespace

extern "C" {

void consent_result_free(consent_result_t* result) { delete result; }

int consent_result_clone(const consent_result_t* result, consent_result_t** copy) {
  if (copy)
    *copy = nullptr;
  return Guard([&]() -> int {
    if (!result || !copy)
      return CONSENT_ERROR_INVALID_PARAMETER;
    *copy = new consent_result(*result);
    return 0;
  });
}

consent_decision_e consent_result_get_decision(const consent_result_t* result) {
  const char* value = consent_result_get(result, "decision");
  if (!value)
    return CONSENT_DECISION_UNKNOWN;
  const char* values[] = {"", "ALLOWED", "DENIED", "CONSENT_REQUIRED", "PENDING",
      "CANCELLED", "EXPIRED", "INVALIDATED"};
  for (unsigned i = 1; i < sizeof(values) / sizeof(values[0]); ++i) {
    if (!strcmp(value, values[i]))
      return static_cast<consent_decision_e>(i);
  }
  return CONSENT_DECISION_UNKNOWN;
}

const char* consent_result_get(const consent_result_t* result, const char* key) {
  if (!result || !key)
    return nullptr;
  try {
    auto it = result->values.find(key);
    return it == result->values.end() ? nullptr : it->second.c_str();
  } catch (...) {
    return nullptr;
  }
}

size_t consent_result_size(const consent_result_t* result) {
  return result ? result->values.size() : 0;
}

int consent_result_get_at(const consent_result_t* result, size_t index,
    const char** key, const char** value) {
  if (!result || !key || !value || index >= result->values.size())
    return CONSENT_ERROR_INVALID_PARAMETER;
  auto it = result->values.begin();
  std::advance(it, index);
  *key = it->first.c_str();
  *value = it->second.c_str();
  return 0;
}

const char* consent_error_string(int status) {
  switch (status) {
    case CONSENT_ERROR_NONE: return "success";
    case CONSENT_ERROR_INVALID_PARAMETER: return "invalid parameter or thread";
    case CONSENT_ERROR_OUT_OF_MEMORY: return "out of memory";
    case CONSENT_ERROR_PERMISSION_DENIED: return "permission denied";
    case CONSENT_ERROR_BUSY: return "resource limit reached";
    case CONSENT_ERROR_NOT_FOUND: return "not found";
    case CONSENT_ERROR_TIMEOUT: return "operation timed out";
    case CONSENT_ERROR_DISCONNECTED: return "disconnected";
    case CONSENT_ERROR_PROTOCOL: return "invalid protocol";
    case CONSENT_ERROR_OUTCOME_UNKNOWN: return "remote outcome unknown";
    case CONSENT_ERROR_SESSION_INACTIVE: return "session inactive";
    case CONSENT_ERROR_SESSION_CLOSED: return "session closed";
    case CONSENT_ERROR_CONFLICT: return "operation conflict";
    case CONSENT_ERROR_STORAGE: return "storage unavailable";
    case CONSENT_ERROR_WOULD_DEADLOCK: return "synchronous wait on callback context";
    case CONSENT_ERROR_STALE: return "stale request or state";
    case CONSENT_ERROR_TOO_LARGE: return "message or result too large";
    case CONSENT_ERROR_NO_SPACE: return "capacity exhausted";
    case CONSENT_ERROR_INVALID_OPERATION: return "invalid operation";
    case CONSENT_ERROR_IO: return "I/O error";
    default: return "daemon or transport error";
  }
}

}  // extern "C"
