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
#ifndef TIZEN_CONSENT_H_
#define TIZEN_CONSENT_H_

#include <stddef.h>
#include <stdint.h>
#include <tizen.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define CONSENT_API __attribute__((visibility("default")))
#else
#define CONSENT_API
#endif

typedef struct consent_client* consent_client_h;
typedef struct consent_params consent_params_t;
typedef struct consent_result consent_result_t;
typedef struct _GMainContext GMainContext;
typedef uint64_t consent_async_id_t;

typedef enum {
  CONSENT_ERROR_NONE = TIZEN_ERROR_NONE,
  CONSENT_ERROR_INVALID_PARAMETER = TIZEN_ERROR_INVALID_PARAMETER,
  CONSENT_ERROR_OUT_OF_MEMORY = TIZEN_ERROR_OUT_OF_MEMORY,
  CONSENT_ERROR_PERMISSION_DENIED = TIZEN_ERROR_PERMISSION_DENIED,
  CONSENT_ERROR_BUSY = TIZEN_ERROR_RESOURCE_BUSY,
  CONSENT_ERROR_NOT_FOUND = TIZEN_ERROR_NO_SUCH_FILE,
  CONSENT_ERROR_TIMEOUT = TIZEN_ERROR_CONNECTION_TIME_OUT,
  CONSENT_ERROR_DISCONNECTED = TIZEN_ERROR_ENDPOINT_NOT_CONNECTED,
  CONSENT_ERROR_WOULD_DEADLOCK = TIZEN_ERROR_WOULD_CAUSE_DEADLOCK,
  CONSENT_ERROR_STALE = TIZEN_ERROR_STALE_NFS_FILE_HANDLE, /* -ESTALE */
  CONSENT_ERROR_TOO_LARGE = TIZEN_ERROR_ARGUMENT_LIST_TOO_LONG, /* -E2BIG */
  CONSENT_ERROR_NO_SPACE = TIZEN_ERROR_FILE_NO_SPACE_ON_DEVICE, /* -ENOSPC */
  CONSENT_ERROR_INVALID_OPERATION = TIZEN_ERROR_INVALID_OPERATION, /* -ENOSYS */
  CONSENT_ERROR_IO = TIZEN_ERROR_IO_ERROR, /* -EIO */
  /* Module-local errors, not a platform-wide Tizen module allocation.
   * These replace the unpublished -200x values; rebuild and upgrade the
   * daemon, library and consumers together. */
  CONSENT_ERROR_PROTOCOL = TIZEN_ERROR_MIN_MODULE_ERROR,
  CONSENT_ERROR_OUTCOME_UNKNOWN = TIZEN_ERROR_MIN_MODULE_ERROR + 1,
  CONSENT_ERROR_SESSION_INACTIVE = TIZEN_ERROR_MIN_MODULE_ERROR + 2,
  CONSENT_ERROR_SESSION_CLOSED = TIZEN_ERROR_MIN_MODULE_ERROR + 3,
  CONSENT_ERROR_CONFLICT = TIZEN_ERROR_MIN_MODULE_ERROR + 4,
  CONSENT_ERROR_STORAGE = TIZEN_ERROR_MIN_MODULE_ERROR + 5
} consent_error_e;

typedef enum {
  CONSENT_DECISION_UNKNOWN = 0,
  CONSENT_DECISION_ALLOWED,
  CONSENT_DECISION_DENIED,
  CONSENT_DECISION_CONSENT_REQUIRED,
  CONSENT_DECISION_PENDING,
  CONSENT_DECISION_CANCELLED,
  CONSENT_DECISION_EXPIRED,
  CONSENT_DECISION_INVALIDATED
} consent_decision_e;

typedef enum {
  CONSENT_CHECK_QUERY,
  CONSENT_CHECK_AUTHORIZE
} consent_check_mode_e;

typedef void (*consent_result_cb)(int status, const consent_result_t* result,
    void* user_data);

/* Create/destroy and asynchronous APIs belong to the creating thread. NULL
 * context captures its thread-default GLib context. Iterate that context only
 * from this thread; do not run nested iterations during an asynchronous API.
 * Inputs are copied before return. Synchronous APIs may run on other threads;
 * they return WOULD_DEADLOCK while the caller owns the callback context.
 * Destroy suppresses queued callbacks and joins I/O. It is safe in a callback,
 * but must not race another call using the same raw client handle. Fork/exec or
 * identity changes require a new handle; inherited handles are rejected. */
CONSENT_API int consent_client_create(consent_client_h* client);
CONSENT_API int consent_client_create_with_context(GMainContext* context,
    consent_client_h* client);
CONSENT_API int consent_client_destroy(consent_client_h client);

/* Params are single-threaded builders. Strings are copied, UTF-8, <=8192 bytes.
 * Protocol metadata (v,id,method,status and underscore-prefixed keys) is reserved.
 * Fields and accepted values are documented in docs/developer-guide.*.md. */
CONSENT_API int consent_params_create(consent_params_t** params);
CONSENT_API void consent_params_free(consent_params_t* params);
CONSENT_API int consent_params_set(consent_params_t* params, const char* key,
    const char* value);
CONSENT_API int consent_params_set_int64(consent_params_t* params,
    const char* key, int64_t value);
CONSENT_API int consent_params_set_check_mode(consent_params_t* params,
    consent_check_mode_e mode);
/* Append rN.definition/operation/scope/purpose/recipient; at most 16. */
CONSENT_API int consent_params_add_requirement(consent_params_t* params,
    const char* definition, const char* operation, const char* scope,
    const char* purpose, const char* recipient);

/* SYNC result ownership transfers to caller. wait_timeout_ms is a local wait,
 * not a remote cancellation. After OUTCOME_UNKNOWN use the same stable
 * client_request_id to retrieve/retry; never start protected work from an error.
 * A successful check QUERY is advisory; only AUTHORIZE permits access. */
CONSENT_API int consent_request(consent_client_h client,
    const consent_params_t* params, unsigned int wait_timeout_ms,
    consent_result_t** result);
CONSENT_API int consent_check(consent_client_h client,
    const consent_params_t* params, unsigned int wait_timeout_ms,
    consent_result_t** result);
/* 0 means local acceptance, never an approval. Every accepted live registration
 * gets at most one queued callback, including immediate ALLOWED/DENIED results.
 * Callback result is borrowed for that callback; clone it to retain it. */
CONSENT_API int consent_request_async(consent_client_h client,
    const consent_params_t* params, consent_result_cb callback, void* user_data,
    consent_async_id_t* operation);
CONSENT_API int consent_check_async(consent_client_h client,
    const consent_params_t* params, consent_result_cb callback, void* user_data,
    consent_async_id_t* operation);
/* Detaches local callback only. Remote work can be cancelled with
 * consent_cancel_request; request lookup by client_request_id remains valid. */
CONSENT_API int consent_async_detach(consent_client_h client,
    consent_async_id_t operation);

CONSENT_API int consent_register(consent_client_h client,
    const char* package_name, const char* app_id, const consent_params_t* params);
CONSENT_API int consent_update(consent_client_h client,
    const char* package_name, const char* app_id, const consent_params_t* params);
CONSENT_API int consent_unregister(consent_client_h client,
    const char* package_name, const consent_params_t* params);

/* Management operations wait for a short commit (5 seconds), not UI/cleanup.
 * Required role is always authenticated by consentd; no role claim is sent. */
CONSENT_API int consent_get_prompt(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_respond(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_get_request_result(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_cancel_request(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_revoke(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_open(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_suspend(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_resume(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_close(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_get_state(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_data_register(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_data_register_derived(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_data_release(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_cleanup_get_state(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/* Discover pending/failed cleanup for the authenticated holder and explicit
 * subject/profile. reconcile=1 includes earlier instances of the same holder;
 * this grants cleanup access only, never artifact-use or registration rights.
 * Results contain count and aN.artifact/session/state/error (at most 48).
 * Acknowledge actual deletion with consent_data_release(), using reconcile=1
 * and the same subject/profile when reconciling an earlier process instance. */
CONSENT_API int consent_cleanup_get_pending(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

/* Format the selected prompt requirement's "title" or "body" as plain UTF-8
 * text. The prompt supplies bounded typed arguments from consentd; this call
 * accepts no independent display values and never recursively expands values.
 * Literal prompts without a template version are supported. The caller must
 * pass template_version=1 to consent_get_prompt() for typed templates.
 * On success, free(*formatted) with the standard C free() function. On failure
 * *formatted is NULL. Invalid fields/indices/schema return INVALID_PARAMETER;
 * allocation failure returns OUT_OF_MEMORY. The result is at most 8192 bytes
 * excluding the NUL. Do not interpret the returned text as markup or a format
 * string. Locale-specific number formatting, plural and date rules are absent.
 */
CONSENT_API int consent_prompt_format(const consent_result_t* prompt,
    unsigned int requirement_index, const char* field, char** formatted);

CONSENT_API void consent_result_free(consent_result_t* result);
CONSENT_API int consent_result_clone(const consent_result_t* result,
    consent_result_t** copy);
CONSENT_API consent_decision_e consent_result_get_decision(
    const consent_result_t* result);
/* Borrowed pointers remain valid until the result is freed. */
CONSENT_API const char* consent_result_get(const consent_result_t* result,
    const char* key);
CONSENT_API size_t consent_result_size(const consent_result_t* result);
CONSENT_API int consent_result_get_at(const consent_result_t* result,
    size_t index, const char** key, const char** value);
CONSENT_API const char* consent_error_string(int status);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_H_
