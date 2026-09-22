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
#ifndef CONSENT_MOCK_FEATURE_API_HH_
#define CONSENT_MOCK_FEATURE_API_HH_
#include "feature_common.hh"
#include <consent.h>
#include <memory>
#include <stdexcept>

namespace consent_mock {
using Params = std::unique_ptr<consent_params_t, decltype(&consent_params_free)>;
inline Params Parameters(const Message& message) {
  consent_params_t* value = nullptr;
  if (consent_params_create(&value)) throw std::bad_alloc();
  Params output(value, consent_params_free);
  for (const auto& field : message)
    if (consent_params_set(value, field.first.c_str(), field.second.c_str()))
      throw std::invalid_argument("invalid API field");
  return output;
}
inline Message Copy(const consent_result_t* result) {
  Message output;
  for (size_t i = 0; result && i < consent_result_size(result); ++i) {
    const char* key = nullptr;
    const char* value = nullptr;
    if (!consent_result_get_at(result, i, &key, &value) && key && value) output[key] = value;
  }
  return output;
}
using Api = int (*)(consent_client_h, const consent_params_t*, consent_result_t**);
inline int Invoke(consent_client_h client, Api api, const Message& request, Message* output) {
  auto parameters = Parameters(request);
  consent_result_t* result = nullptr;
  int status = api(client, parameters.get(), &result);
  std::unique_ptr<consent_result_t, decltype(&consent_result_free)> owned(result, consent_result_free);
  *output = Copy(owned.get());
  return status;
}
inline int Check(consent_client_h client, const Message& request, Message* output) {
  auto parameters = Parameters(request);
  consent_result_t* result = nullptr;
  int status = consent_check(client, parameters.get(), 5000, &result);
  std::unique_ptr<consent_result_t, decltype(&consent_result_free)> owned(result, consent_result_free);
  *output = Copy(owned.get());
  return status;
}
}
#endif
