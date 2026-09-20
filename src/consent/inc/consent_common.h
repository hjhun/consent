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
#ifndef TIZEN_CONSENT_COMMON_H_
#define TIZEN_CONSENT_COMMON_H_

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

CONSENT_API const char* consent_error_string(int status);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_COMMON_H_
