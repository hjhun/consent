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
#ifndef CONSENT_API_HH_
#define CONSENT_API_HH_

#include "client.hh"

#include <new>

namespace consent {
namespace api {

/* Every allocating C ABI path passes through this exception boundary. */
template<typename Function>
int Guard(Function&& function) noexcept {
  try {
    return function();
  } catch (const std::bad_alloc&) {
    return CONSENT_ERROR_OUT_OF_MEMORY;
  } catch (...) {
    return CONSENT_ERROR_PROTOCOL;
  }
}

int Call(consent_client_h client, const char* method,
    const consent_params_t* params, unsigned timeout, consent_result_t** result);
int Submit(consent_client_h client, const char* method,
    const consent_params_t* params, consent_result_cb callback, void* data,
    consent_async_id_t* operation);
int OnlineStatus(consent_client_h client);

}  // namespace api
}  // namespace consent
#endif  // CONSENT_API_HH_
