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
#include "consent_registration.h"

#include "api.hh"

#include <cerrno>
#include <utility>

namespace {
using consent::api::Guard;
using consent::api::Call;

using consent::Message;

int Register(consent_client_h client, const char* package, const char* app,
    const consent_params_t* params) {
  if (client && client->offline &&
      (!client->offline->IsCurrentProcess() || !client->offline->IsOwner()))
    return CONSENT_ERROR_INVALID_PARAMETER;
  if (!client || !package || !*package || !app || !*app || !params ||
      !consent::ValidField("package", package) || !consent::ValidField("app", app))
    return CONSENT_ERROR_INVALID_PARAMETER;
  Message message = params->values;
  message["method"] = "register";
  message["package"] = package;
  message["app"] = app;
  if (client->offline) {
    int status = client->offline->Register(message);
    if (status == -EEXIST)
      return CONSENT_ERROR_CONFLICT;
    if (status == -EINPROGRESS)
      return CONSENT_ERROR_OUTCOME_UNKNOWN;
    return status;
  }
  Message result;
  return client->impl->Call(std::move(message), 5000, &result);
}
}  // namespace

extern "C" {

int consent_register(consent_client_h client, const char* package_name,
    const char* app_id, const consent_params_t* params) {
  return Guard([&]() { return Register(client, package_name, app_id, params); });
}

int consent_update(consent_client_h client, const char* package_name,
    const char* app_id, const consent_params_t* params) {
  int status = consent::api::OnlineStatus(client);
  if (status)
    return status;
  return consent_register(client, package_name, app_id, params);
}

int consent_unregister(consent_client_h client, const char* package_name,
    const consent_params_t* params) {
  return Guard([&]() -> int {
    int status = consent::api::OnlineStatus(client);
    if (status)
      return status;
    if (!client || !package_name || !*package_name || !params ||
        !consent::ValidField("package", package_name))
      return CONSENT_ERROR_INVALID_PARAMETER;
    Message message = params->values;
    message["method"] = "unregister";
    message["package"] = package_name;
    message.erase("app");
    Message result;
    return client->impl->Call(std::move(message), 5000, &result);
  });
}

int consent_revoke(consent_client_h client, const consent_params_t* params,
    consent_result_t** result) {
  return Guard([&]() { return Call(client, "revoke", params, 5000, result); });
}

}  // extern "C"
