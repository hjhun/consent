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

/**
 * @defgroup CONSENT_MODULE Consent
 * @brief Provides consent definitions, approval requests and protected-use checks.
 * @since 0.1.0
 *
 * @section CONSENT_HEADER Required Header
 * @code
 * #include <consent.h>
 * @endcode
 * Link with the pkg-config package @c consent. Dispatcher examples also use
 * @c glib-2.0. Feature headers can be included independently.
 *
 * @section CONSENT_CONTRACT Common Contract
 * Online role authentication is performed by consentd using kernel identity and
 * trusted role policy, never caller-supplied role strings. The shipped policy
 * grants no roles. No Tizen platform version or privilege URI is assigned here.
 * This documentation's @c since value is the consent framework version.
 *
 * A zero status reports operation success, not necessarily an ALLOWED decision.
 * Every nonzero status blocks protected execution. Only a successful current
 * AUTHORIZE check permits its bound operation; requests and QUERY are advisory.
 * OUTCOME_UNKNOWN requires reconciliation with the same stable IDs and payload.
 * A local timeout, callback detach or handle destruction is not remote cancel.
 *
 * Builders remain caller-owned and are copied before API return. Synchronous
 * result outputs and clones are caller-owned; callback results are borrowed.
 * Use the matching free function. Never free or mutate an object concurrently
 * with a call using it. C++ exceptions do not cross the C API boundary.
 *
 * Create/destroy and ASYNC calls run on the creating thread. Iterate its captured
 * GLib context on that thread, without nested iteration inside an ASYNC call.
 * SYNC calls may use other threads, but fail with WOULD_DEADLOCK when their
 * caller owns the callback context. Destroy must not race a call using the raw
 * handle. Fork/exec or identity changes require a new handle.
 *
 * Management calls have a 5,000 ms local wait. Request/check accept a local
 * timeout of 1-300,000 ms; ASYNC uses 300,000 ms. Remote approval deadline and
 * session/grant/data lifetimes are separate. Errors include transport/storage
 * failures beyond operation-specific retval lists; see #consent_error_e.
 *
 * @section CONSENT_EXAMPLES Examples
 * Complete C examples and schemas are in docs/guides/02-c-api.en.md and
 * docs/guides/02-c-api.ko.md, with executable sources under src/examples/.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__)
#define CONSENT_API __attribute__((visibility("default")))
#else
#define CONSENT_API
#endif

/**
 * @brief Opaque online or offline client handle.
 * @details Release with consent_client_destroy() on the creating thread. Never copy a handle to transfer ownership or use it after fork.
 */
typedef struct consent_client* consent_client_h;
/**
 * @brief Opaque caller-owned parameter builder.
 * @details Create/free with consent_params_create()/consent_params_free(). Builders are single-threaded and contain copied fields.
 */
typedef struct consent_params consent_params_t;
/**
 * @brief Opaque immutable result.
 * @details SYNC results and clones are caller-owned. Callback results are borrowed until callback return. Use consent_result_free() only for owned results.
 */
typedef struct consent_result consent_result_t;
/**
 * @brief Forward declaration of the GLib dispatcher context.
 * @details Include <glib.h> to create and iterate an explicit context.
 */
typedef struct _GMainContext GMainContext;
/**
 * @brief Local asynchronous callback registration ID.
 * @details Zero is invalid. It is distinct from request_id, client_request_id, operation_id and authorization receipts.
 */
typedef uint64_t consent_async_id_t;

/**
 * @brief Operation status values.
 * @details Standard values alias tizen.h. The six consent-specific values start
 * at TIZEN_ERROR_MIN_MODULE_ERROR and are module-local, not a platform-wide
 * module allocation. Use consent_error_string() for their descriptions.
 */
typedef enum {
  CONSENT_ERROR_NONE = TIZEN_ERROR_NONE,
      /**< Operation succeeded; this does not itself imply approval. */
  CONSENT_ERROR_INVALID_PARAMETER = TIZEN_ERROR_INVALID_PARAMETER,
      /**< Invalid argument, field schema, owning thread or process. */
  CONSENT_ERROR_OUT_OF_MEMORY = TIZEN_ERROR_OUT_OF_MEMORY,
      /**< Allocation failed. */
  CONSENT_ERROR_PERMISSION_DENIED = TIZEN_ERROR_PERMISSION_DENIED,
      /**< Identity, role, delegation, context or protected-path validation failed. */
  CONSENT_ERROR_BUSY = TIZEN_ERROR_RESOURCE_BUSY,
      /**< A bounded queue/resource is full or an exclusive image lock is held. */
  CONSENT_ERROR_NOT_FOUND = TIZEN_ERROR_NO_SUCH_FILE,
      /**< The requested object or pending callback registration does not exist. */
  CONSENT_ERROR_TIMEOUT = TIZEN_ERROR_CONNECTION_TIME_OUT,
      /**< The local wait or a remote deadline expired; no automatic cancellation. */
  CONSENT_ERROR_DISCONNECTED = TIZEN_ERROR_ENDPOINT_NOT_CONNECTED,
      /**< The transport is unavailable; create a new online handle to reconnect. */
  CONSENT_ERROR_WOULD_DEADLOCK = TIZEN_ERROR_WOULD_CAUSE_DEADLOCK,
      /**< A synchronous API was called while owning its callback context. */
  CONSENT_ERROR_STALE = TIZEN_ERROR_STALE_NFS_FILE_HANDLE,
      /**< Policy, installation, session generation, receipt or provenance is stale. */
  CONSENT_ERROR_TOO_LARGE = TIZEN_ERROR_ARGUMENT_LIST_TOO_LONG,
      /**< A frame, stored record or rendered result exceeds its bound. */
  CONSENT_ERROR_NO_SPACE = TIZEN_ERROR_FILE_NO_SPACE_ON_DEVICE,
      /**< Persistent capacity or storage space is exhausted. */
  CONSENT_ERROR_INVALID_OPERATION = TIZEN_ERROR_INVALID_OPERATION,
      /**< The method is unavailable, including online methods on an offline handle. */
  CONSENT_ERROR_IO = TIZEN_ERROR_IO_ERROR,
      /**< A storage I/O or synchronization operation failed. */
  CONSENT_ERROR_PROTOCOL = TIZEN_ERROR_MIN_MODULE_ERROR,
      /**< Invalid protocol or an unexpected C++ failure at the C boundary. */
  CONSENT_ERROR_OUTCOME_UNKNOWN = TIZEN_ERROR_MIN_MODULE_ERROR + 1,
      /**< Publication or a sent operation is uncertain; preserve retry IDs and payload. */
  CONSENT_ERROR_SESSION_INACTIVE = TIZEN_ERROR_MIN_MODULE_ERROR + 2,
      /**< The session is suspended, expired or cannot perform this transition. */
  CONSENT_ERROR_SESSION_CLOSED = TIZEN_ERROR_MIN_MODULE_ERROR + 3,
      /**< The session is closing or closed. */
  CONSENT_ERROR_CONFLICT = TIZEN_ERROR_MIN_MODULE_ERROR + 4,
      /**< A stable retry identifier or ownership binding conflicts with existing content. */
  CONSENT_ERROR_STORAGE = TIZEN_ERROR_MIN_MODULE_ERROR + 5
      /**< Trusted consent storage cannot safely serve the operation. */
} consent_error_e;

/** @brief Advisory/request decisions and AUTHORIZE outcomes. */
typedef enum {
  CONSENT_DECISION_UNKNOWN = 0, /**< Absent or unrecognized decision; never permission. */
  CONSENT_DECISION_ALLOWED, /**< Satisfied in the reported context; only AUTHORIZE permits execution. */
  CONSENT_DECISION_DENIED, /**< User denial or unavailable definition. */
  CONSENT_DECISION_CONSENT_REQUIRED, /**< A required approval is missing. */
  CONSENT_DECISION_PENDING, /**< A stored request awaits UI; lookup may return this state. */
  CONSENT_DECISION_CANCELLED, /**< A pending request was cancelled. */
  CONSENT_DECISION_EXPIRED, /**< The remote approval deadline expired. */
  CONSENT_DECISION_INVALIDATED /**< The request lost current policy/session/condition validity. */
} consent_decision_e;

/** @brief Selects an advisory query or an atomic protected-action authorization. */
typedef enum {
  CONSENT_CHECK_QUERY, /**< Does not consume ONCE grants and never opens UI. */
  CONSENT_CHECK_AUTHORIZE /**< Checks all conditions and atomically consumes ONCE on ALLOWED. */
} consent_check_mode_e;

/**
 * @brief Receives one asynchronous operation's completion on its dispatcher.
 * @since 0.1.0
 * @details Accepted live registrations deliver at most once after API return,
 * including immediate/cache results, without library locks. Detach/destroy
 * suppresses queued delivery. Do not throw a C++ exception from this callback.
 * @param[in] status Zero for a result, otherwise a negative consent error.
 * @param[in] result Borrowed until callback return; NULL when status is nonzero.
 *            Clone with consent_result_clone() to retain it. Never free it.
 * @param[in] user_data The caller-owned pointer supplied at submission.
 * @remarks Calling destroy from this callback is supported. Synchronous calls
 * on the same dispatcher fail with WOULD_DEADLOCK; continue outside iteration
 * or submit asynchronous work. Approval results do not authorize execution.
 * @see consent_request_async()
 * @see consent_check_async()
 */
typedef void (*consent_result_cb)(int status, const consent_result_t* result,
    void* user_data);

/**
 * @brief Returns a static English description of a status code.
 * @since 0.1.0
 *
 * @details The returned pointer is always non-NULL, is not owned by the caller
 *     and must not be freed. Unknown codes return a generic description.
 *     Prefer symbolic consent_error_e names over copied numeric literals.
 *
 * @param[in] status A consent status value.
 *
 * @return A static NUL-terminated description.
 */
CONSENT_API const char* consent_error_string(int status);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_COMMON_H_
