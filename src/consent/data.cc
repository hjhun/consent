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
#include "consent_data.h"

#include "api.hh"

namespace {
using consent::api::Guard;
using consent::api::Call;
}  // namespace

extern "C" {

int consent_data_register(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "data_register", params, 5000, result); });
}

int consent_data_register_derived(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "data_derived", params, 5000, result); });
}

int consent_data_release(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "data_release", params, 5000, result); });
}

int consent_cleanup_get_state(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "cleanup", params, 5000, result); });
}

int consent_cleanup_get_pending(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "cleanup_list", params, 5000, result); });
}

}  // extern "C"
