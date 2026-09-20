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
#include "consent.h"

#include "client.hh"
#include "common/localization.hh"

#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

namespace {
using consent::Message;

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

bool Reserved(const std::string& key) {
  return key.empty() || key[0] == '_' || key == "v" || key == "id" ||
      key == "method" || key == "status" || key == "role" || key == "pid" ||
      key == "uid" || key == "gid";
}

int Call(consent_client_h client, const char* method,
    const consent_params_t* params, unsigned timeout, consent_result_t** result) {
  if (result)
    *result = nullptr;
  if (!client || !params || !result)
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
  if (!client || !params || !callback || !operation)
    return CONSENT_ERROR_INVALID_PARAMETER;
  Message message = params->values;
  message["method"] = method;
  return client->impl->Submit(std::move(message), callback, data, operation);
}

int Register(consent_client_h client, const char* package, const char* app,
    const consent_params_t* params) {
  if (!client || !package || !*package || !app || !*app || !params ||
      !consent::ValidField("package", package) || !consent::ValidField("app", app))
    return CONSENT_ERROR_INVALID_PARAMETER;
  Message message = params->values;
  message["method"] = "register";
  message["package"] = package;
  message["app"] = app;
  Message result;
  return client->impl->Call(std::move(message), 5000, &result);
}
}  // namespace

extern "C" {

int consent_client_create(consent_client_h* client) {
  return consent_client_create_with_context(nullptr, client);
}

int consent_client_create_with_context(GMainContext* context, consent_client_h* client) {
  if (client)
    *client = nullptr;
  return Guard([&]() -> int {
    if (!client)
      return CONSENT_ERROR_INVALID_PARAMETER;
    auto value = std::unique_ptr<consent_client>(new consent_client);
    value->impl.reset(new consent::Client(context));
    int status = value->impl->Connect();
    if (!status)
      *client = value.release();
    return status;
  });
}

int consent_client_destroy(consent_client_h client) {
  return Guard([&]() -> int {
    if (!client || !client->impl->IsOwner() || !client->impl->IsCurrentProcess())
      return CONSENT_ERROR_INVALID_PARAMETER;
    int status = client->impl->Close();
    delete client;
    return status;
  });
}

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
    return client ? client->impl->Detach(operation) : CONSENT_ERROR_INVALID_PARAMETER;
  });
}

int consent_register(consent_client_h client, const char* package_name,
    const char* app_id, const consent_params_t* params) {
  return Guard([&]() { return Register(client, package_name, app_id, params); });
}

int consent_update(consent_client_h client, const char* package_name,
    const char* app_id, const consent_params_t* params) {
  return consent_register(client, package_name, app_id, params);
}

int consent_unregister(consent_client_h client, const char* package_name,
    const consent_params_t* params) {
  return Guard([&]() -> int {
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

#define CONSENT_MANAGEMENT(function, method) \
  int function(consent_client_h client, const consent_params_t* params, \
      consent_result_t** result) { \
    return Guard([&]() { return Call(client, method, params, 5000, result); }); \
  }
CONSENT_MANAGEMENT(consent_get_prompt, "prompt")
CONSENT_MANAGEMENT(consent_respond, "respond")
CONSENT_MANAGEMENT(consent_get_request_result, "result")
CONSENT_MANAGEMENT(consent_cancel_request, "cancel")
CONSENT_MANAGEMENT(consent_revoke, "revoke")
CONSENT_MANAGEMENT(consent_session_open, "session_open")
CONSENT_MANAGEMENT(consent_session_suspend, "session_suspend")
CONSENT_MANAGEMENT(consent_session_resume, "session_resume")
CONSENT_MANAGEMENT(consent_session_close, "session_close")
CONSENT_MANAGEMENT(consent_session_get_state, "session_state")
CONSENT_MANAGEMENT(consent_data_register, "data_register")
CONSENT_MANAGEMENT(consent_data_register_derived, "data_derived")
CONSENT_MANAGEMENT(consent_data_release, "data_release")
CONSENT_MANAGEMENT(consent_cleanup_get_state, "cleanup")
CONSENT_MANAGEMENT(consent_cleanup_get_pending, "cleanup_list")
#undef CONSENT_MANAGEMENT

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
