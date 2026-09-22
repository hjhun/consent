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
#include <consent.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
  fprintf(stderr, "FAIL public C API line=%d expression=%s\n", __LINE__, #expression); \
  exit(1); \
} } while (0)
#define SAME_ERROR(consent, platform) \
  _Static_assert((int)(consent) == (int)(platform), "Tizen error alias changed: " #consent)

SAME_ERROR(CONSENT_ERROR_NONE, TIZEN_ERROR_NONE);
SAME_ERROR(CONSENT_ERROR_INVALID_PARAMETER, TIZEN_ERROR_INVALID_PARAMETER);
SAME_ERROR(CONSENT_ERROR_OUT_OF_MEMORY, TIZEN_ERROR_OUT_OF_MEMORY);
SAME_ERROR(CONSENT_ERROR_PERMISSION_DENIED, TIZEN_ERROR_PERMISSION_DENIED);
SAME_ERROR(CONSENT_ERROR_BUSY, TIZEN_ERROR_RESOURCE_BUSY);
SAME_ERROR(CONSENT_ERROR_NOT_FOUND, TIZEN_ERROR_NO_SUCH_FILE);
SAME_ERROR(CONSENT_ERROR_TIMEOUT, TIZEN_ERROR_CONNECTION_TIME_OUT);
SAME_ERROR(CONSENT_ERROR_DISCONNECTED, TIZEN_ERROR_ENDPOINT_NOT_CONNECTED);
SAME_ERROR(CONSENT_ERROR_WOULD_DEADLOCK, TIZEN_ERROR_WOULD_CAUSE_DEADLOCK);
SAME_ERROR(CONSENT_ERROR_STALE, TIZEN_ERROR_STALE_NFS_FILE_HANDLE);
SAME_ERROR(CONSENT_ERROR_TOO_LARGE, TIZEN_ERROR_ARGUMENT_LIST_TOO_LONG);
SAME_ERROR(CONSENT_ERROR_NO_SPACE, TIZEN_ERROR_FILE_NO_SPACE_ON_DEVICE);
SAME_ERROR(CONSENT_ERROR_INVALID_OPERATION, TIZEN_ERROR_INVALID_OPERATION);
SAME_ERROR(CONSENT_ERROR_IO, TIZEN_ERROR_IO_ERROR);
_Static_assert(CONSENT_ERROR_TIMEOUT == -ETIMEDOUT, "timeout remains standard -ETIMEDOUT");
_Static_assert(CONSENT_ERROR_WOULD_DEADLOCK == -EDEADLK, "deadlock uses standard errno");
_Static_assert(CONSENT_ERROR_STALE == -ESTALE, "stale state uses standard errno");
_Static_assert(CONSENT_ERROR_TOO_LARGE == -E2BIG, "frame/expansion limit uses standard errno");
_Static_assert(CONSENT_ERROR_NO_SPACE == -ENOSPC, "capacity uses standard errno");
_Static_assert(CONSENT_ERROR_INVALID_OPERATION == -ENOSYS, "invalid operation uses standard errno");
_Static_assert(CONSENT_ERROR_IO == -EIO, "I/O error uses standard errno");
_Static_assert(CONSENT_ERROR_PROTOCOL == TIZEN_ERROR_MIN_MODULE_ERROR,
    "first consent module-local code starts at the Tizen module boundary");
_Static_assert(CONSENT_ERROR_STORAGE == TIZEN_ERROR_MIN_MODULE_ERROR + 5,
    "six consent module-local errors");
_Static_assert(CONSENT_ERROR_PROTOCOL >= INT_MIN && CONSENT_ERROR_STORAGE <= INT_MAX,
    "module-local error codes fit the existing int C ABI and int32 wire field");

static void errors(void) {
  const struct {
    int value;
    const char* description;
  } module[] = {
    {CONSENT_ERROR_PROTOCOL, "invalid protocol"},
    {CONSENT_ERROR_OUTCOME_UNKNOWN, "remote outcome unknown"},
    {CONSENT_ERROR_SESSION_INACTIVE, "session inactive"},
    {CONSENT_ERROR_SESSION_CLOSED, "session closed"},
    {CONSENT_ERROR_CONFLICT, "operation conflict"},
    {CONSENT_ERROR_STORAGE, "storage unavailable"}
  };
  for (size_t index = 0; index < sizeof(module) / sizeof(module[0]); ++index) {
    CHECK((int64_t)module[index].value == TIZEN_ERROR_MIN_MODULE_ERROR + (int64_t)index);
    CHECK((int64_t)module[index].value <= TIZEN_ERROR_MAX_MODULE_ERROR);
    CHECK(!strcmp(consent_error_string(module[index].value), module[index].description));
    /* The public signed int survives canonical decimal conversion at INT_MIN.
     * Actual native Parcel and socket transport are covered by client-test. */
    char decimal[32];
    int length = snprintf(decimal, sizeof(decimal), "%d", module[index].value);
    CHECK(length > 0 && (size_t)length < sizeof(decimal));
    errno = 0;
    char* end = NULL;
    long parsed = strtol(decimal, &end, 10);
    CHECK(errno == 0 && end && *end == '\0' && parsed == module[index].value);
  }
  const struct {
    int value;
    const char* description;
  } common[] = {
    {CONSENT_ERROR_NONE, "success"},
    {CONSENT_ERROR_INVALID_PARAMETER, "invalid parameter or thread"},
    {CONSENT_ERROR_OUT_OF_MEMORY, "out of memory"},
    {CONSENT_ERROR_PERMISSION_DENIED, "permission denied"},
    {CONSENT_ERROR_BUSY, "resource limit reached"},
    {CONSENT_ERROR_NOT_FOUND, "not found"},
    {CONSENT_ERROR_TIMEOUT, "operation timed out"},
    {CONSENT_ERROR_DISCONNECTED, "disconnected"},
    {CONSENT_ERROR_WOULD_DEADLOCK, "synchronous wait on callback context"},
    {CONSENT_ERROR_STALE, "stale request or state"},
    {CONSENT_ERROR_TOO_LARGE, "message or result too large"},
    {CONSENT_ERROR_NO_SPACE, "capacity exhausted"},
    {CONSENT_ERROR_INVALID_OPERATION, "invalid operation"},
    {CONSENT_ERROR_IO, "I/O error"}
  };
  for (size_t index = 0; index < sizeof(common) / sizeof(common[0]); ++index)
    CHECK(!strcmp(consent_error_string(common[index].value), common[index].description));
  CHECK(!strcmp(consent_error_string(-2001), "daemon or transport error"));
  CHECK(!strcmp(consent_error_string(INT_MAX), "daemon or transport error"));
  puts("PASS public C API: Tizen common aliases, module range, signed-int boundary and error strings");
}

static void existing_symbols(void) {
  /* Exercise the shared C ABI without a daemon, role policy or writable socket.
   * Invalid-handle calls also verify output initialization at those boundaries. */
  CHECK(consent_client_create(NULL) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_client_create_with_context(NULL, NULL) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_client_create_offline_registration(NULL, NULL) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_client_destroy(NULL) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_async_detach(NULL, 1) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_params_create(NULL) == CONSENT_ERROR_INVALID_PARAMETER);
  consent_params_t* params = NULL;
  CHECK(consent_params_create(&params) == 0 && params);
  CHECK(consent_params_set(params, "subject", "test.subject") == 0);
  CHECK(consent_params_set_int64(params, "value", INT64_MIN) == 0);
  CHECK(consent_params_set_check_mode(params, CONSENT_CHECK_QUERY) == 0);
  CHECK(consent_params_add_requirement(params, "definition", "read", "scope", "purpose", "") == 0);
  CHECK(consent_register(NULL, "package", "app", params) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_update(NULL, "package", "app", params) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_unregister(NULL, "package", params) == CONSENT_ERROR_INVALID_PARAMETER);
  consent_result_t* result = (consent_result_t*)1;
  CHECK(consent_request(NULL, params, 1, &result) == CONSENT_ERROR_INVALID_PARAMETER && !result);
  result = (consent_result_t*)1;
  CHECK(consent_check(NULL, params, 1, &result) == CONSENT_ERROR_INVALID_PARAMETER && !result);
  consent_async_id_t operation = 1;
  CHECK(consent_request_async(NULL, params, NULL, NULL, &operation) == CONSENT_ERROR_INVALID_PARAMETER && !operation);
  operation = 1;
  CHECK(consent_check_async(NULL, params, NULL, NULL, &operation) == CONSENT_ERROR_INVALID_PARAMETER && !operation);
  typedef int (*management_function)(consent_client_h, const consent_params_t*, consent_result_t**);
  const management_function functions[] = {
    consent_get_prompt, consent_respond, consent_get_request_result, consent_cancel_request,
    consent_revoke, consent_session_open, consent_session_suspend, consent_session_resume,
    consent_session_heartbeat, consent_session_close, consent_session_get_state, consent_data_register,
    consent_data_register_derived, consent_data_release, consent_cleanup_get_state,
    consent_cleanup_get_pending
  };
  for (size_t index = 0; index < sizeof(functions) / sizeof(functions[0]); ++index) {
    result = (consent_result_t*)1;
    CHECK(functions[index](NULL, params, &result) == CONSENT_ERROR_INVALID_PARAMETER && !result);
  }
  result = (consent_result_t*)1;
  CHECK(consent_result_clone(NULL, &result) == CONSENT_ERROR_INVALID_PARAMETER && !result);
  CHECK(consent_result_get_decision(NULL) == CONSENT_DECISION_UNKNOWN);
  CHECK(consent_result_get(NULL, "key") == NULL);
  CHECK(consent_result_size(NULL) == 0);
  const char* key = NULL;
  const char* value = NULL;
  CHECK(consent_result_get_at(NULL, 0, &key, &value) == CONSENT_ERROR_INVALID_PARAMETER);
  char* formatted = (char*)1;
  CHECK(consent_prompt_format(NULL, 0, "title", &formatted) == CONSENT_ERROR_INVALID_PARAMETER && !formatted);
  consent_result_free(NULL);
  consent_params_free(params);
  consent_params_free(NULL);
  puts("PASS public C API: shared existing symbols and invalid-parameter output ownership");
}

int main(void) {
  errors();
  existing_symbols();
  return 0;
}
