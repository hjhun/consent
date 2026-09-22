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
#include "consent_session.h"

#include "api.hh"

namespace {
using consent::api::Guard;
using consent::api::Call;
}  // namespace

extern "C" {

int consent_session_open(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "session_open", params, 5000, result); });
}

int consent_session_heartbeat(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "session_heartbeat", params, 5000, result); });
}

int consent_session_suspend(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "session_suspend", params, 5000, result); });
}

int consent_session_resume(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "session_resume", params, 5000, result); });
}

int consent_session_close(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "session_close", params, 5000, result); });
}

int consent_session_get_state(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "session_state", params, 5000, result); });
}

}  // extern "C"
