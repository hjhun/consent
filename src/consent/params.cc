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
#include "consent_params.h"

#include "api.hh"

namespace {
using consent::api::Guard;

bool Reserved(const std::string& key) {
  return key.empty() || key[0] == '_' || key == "v" || key == "id" ||
      key == "method" || key == "status" || key == "role" || key == "pid" ||
      key == "uid" || key == "gid";
}
}  // namespace

extern "C" {

int consent_params_create(consent_params_t** params) {
  if (params)
    *params = nullptr;
  return Guard([&]() -> int {
    if (!params)
      return CONSENT_ERROR_INVALID_PARAMETER;
    *params = new consent_params;
    return 0;
  });
}

void consent_params_free(consent_params_t* params) { delete params; }

int consent_params_set(consent_params_t* params, const char* key, const char* value) {
  return Guard([&]() -> int {
    if (!params || !key || !value || Reserved(key) ||
        !consent::ValidField(key, value))
      return CONSENT_ERROR_INVALID_PARAMETER;
    if (!params->values.count(key) && params->values.size() >= consent::kMaxFields - 3)
      return CONSENT_ERROR_BUSY;
    params->values[key] = value;
    return 0;
  });
}

int consent_params_set_int64(consent_params_t* params, const char* key, int64_t value) {
  return Guard([&]() -> int {
    return consent_params_set(params, key, std::to_string(value).c_str());
  });
}

int consent_params_set_check_mode(consent_params_t* params, consent_check_mode_e mode) {
  if (mode != CONSENT_CHECK_QUERY && mode != CONSENT_CHECK_AUTHORIZE)
    return CONSENT_ERROR_INVALID_PARAMETER;
  return consent_params_set(params, "mode", mode == CONSENT_CHECK_QUERY ? "QUERY" : "AUTHORIZE");
}

int consent_params_add_requirement(consent_params_t* params,
    const char* definition, const char* operation, const char* scope,
    const char* purpose, const char* recipient) {
  return Guard([&]() -> int {
    if (!params || !definition || !*definition || !operation || !*operation ||
        !scope || !purpose || !recipient)
      return CONSENT_ERROR_INVALID_PARAMETER;
    int64_t count = consent::Number(params->values, "count", 0);
    if (count < 0 || count >= static_cast<int64_t>(consent::kMaxRequirements))
      return CONSENT_ERROR_INVALID_PARAMETER;
    consent_params updated = *params;
    std::string prefix = "r" + std::to_string(count) + ".";
    const char* keys[] = {"definition", "operation", "scope", "purpose", "recipient"};
    const char* values[] = {definition, operation, scope, purpose, recipient};
    for (size_t i = 0; i < 5; ++i) {
      int status = consent_params_set(&updated, (prefix + keys[i]).c_str(), values[i]);
      if (status)
        return status;
    }
    int status = consent_params_set_int64(&updated, "count", count + 1);
    if (!status)
      params->values.swap(updated.values);
    return status;
  });
}

}  // extern "C"
