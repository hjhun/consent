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
#include "api.hh"

#include <memory>
#include <utility>

namespace consent {
namespace api {

int OnlineStatus(consent_client_h client) {
  if (!client)
    return CONSENT_ERROR_INVALID_PARAMETER;
  if (client->offline) {
    if (!client->offline->IsCurrentProcess() || !client->offline->IsOwner())
      return CONSENT_ERROR_INVALID_PARAMETER;
    return CONSENT_ERROR_INVALID_OPERATION;
  }
  return 0;
}

int Call(consent_client_h client, const char* method,
    const consent_params_t* params, unsigned timeout, consent_result_t** result) {
  if (result)
    *result = nullptr;
  int valid = OnlineStatus(client);
  if (valid)
    return valid;
  if (!params || !result)
    return CONSENT_ERROR_INVALID_PARAMETER;
  Message message = params->values;
  message["method"] = method;
  auto output = std::unique_ptr<consent_result>(new consent_result);
  int status = client->impl->Call(std::move(message), timeout, &output->values);
  if (!status)
    *result = output.release();
  return status;
}

int Submit(consent_client_h client, const char* method,
    const consent_params_t* params, consent_result_cb callback, void* data,
    consent_async_id_t* operation) {
  if (operation)
    *operation = 0;
  int valid = OnlineStatus(client);
  if (valid)
    return valid;
  if (!params || !callback || !operation)
    return CONSENT_ERROR_INVALID_PARAMETER;
  Message message = params->values;
  message["method"] = method;
  return client->impl->Submit(std::move(message), callback, data, operation);
}

}  // namespace api
}  // namespace consent
