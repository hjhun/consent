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
#include "consent_request.h"

#include "api.hh"

namespace {
using consent::api::Guard;
using consent::api::Call;
using consent::api::Submit;
}  // namespace

extern "C" {

int consent_request(consent_client_h client, const consent_params_t* params,
    unsigned int wait_timeout_ms, consent_result_t** result) {
  return Guard([&]() { return Call(client, "request", params, wait_timeout_ms, result); });
}

int consent_check(consent_client_h client, const consent_params_t* params,
    unsigned int wait_timeout_ms, consent_result_t** result) {
  return Guard([&]() { return Call(client, "check", params, wait_timeout_ms, result); });
}

int consent_request_async(consent_client_h client, const consent_params_t* params,
    consent_result_cb callback, void* user_data, consent_async_id_t* operation) {
  return Guard([&]() { return Submit(client, "request", params, callback, user_data, operation); });
}

int consent_check_async(consent_client_h client, const consent_params_t* params,
    consent_result_cb callback, void* user_data, consent_async_id_t* operation) {
  return Guard([&]() { return Submit(client, "check", params, callback, user_data, operation); });
}

int consent_async_detach(consent_client_h client, consent_async_id_t operation) {
  return Guard([&]() -> int {
    int status = consent::api::OnlineStatus(client);
    return status ? status : client->impl->Detach(operation);
  });
}

int consent_get_request_result(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "result", params, 5000, result); });
}

int consent_cancel_request(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "cancel", params, 5000, result); });
}

}  // extern "C"
