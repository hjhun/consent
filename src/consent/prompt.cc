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
#include "consent_prompt.h"

#include "api.hh"
#include "common/localization.hh"

#include <cstdlib>
#include <cstring>

namespace {
using consent::api::Guard;
using consent::api::Call;
}  // namespace

extern "C" {

int consent_prompt_format(const consent_result_t* prompt,
    unsigned int requirement_index, const char* field, char** formatted) {
  if (formatted)
    *formatted = nullptr;
  return Guard([&]() -> int {
    if (!prompt || !field || !formatted)
      return CONSENT_ERROR_INVALID_PARAMETER;
    std::string value;
    if (!consent::localization::FormatPrompt(prompt->values, requirement_index,
            field, &value))
      return CONSENT_ERROR_INVALID_PARAMETER;
    auto* output = static_cast<char*>(std::malloc(value.size() + 1));
    if (!output)
      return CONSENT_ERROR_OUT_OF_MEMORY;
    std::memcpy(output, value.c_str(), value.size() + 1);
    *formatted = output;
    return 0;
  });
}

int consent_get_prompt(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "prompt", params, 5000, result); });
}

int consent_respond(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "respond", params, 5000, result); });
}

}  // extern "C"
