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
#ifndef TIZEN_CONSENT_REQUEST_H_
#define TIZEN_CONSENT_REQUEST_H_

#include "consent_common.h"

/**
 * @defgroup CONSENT_REQUEST_MODULE Requests and checks
 * @ingroup CONSENT_MODULE
 * @brief Approval requests, authoritative checks and local callback registration.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Requests user approval and waits for its decision.
 * @since 0.1.0
 *
 * @details Populate subject, profile, stable client_request_id and
 *     operation_id, and 1-16 requirements. Optional session requires its
 *     current generation. deadline_ms is a remote approval lifetime of
 *     100-300,000 ms (default 60,000); it is independent of the local wait.
 *     Only authenticated argo may request approval.
 *
 * @remarks The client polls a pending request until a terminal result or its
 *     local timeout. Immediate and cached ALLOWED results are advisory;
 *     perform AUTHORIZE before protected execution.
 *
 * @remarks Do not call while owning the callback context. A nonzero status
 *     blocks execution; retry uncertain work with the same IDs and identical
 *     payload.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
 * @param[in] wait_timeout_ms The local wait in milliseconds, from 1 through
 *     300,000. Timeout does not cancel remote work.
 * @param[out] result The owned result on success, or NULL on error. Release
 *     with consent_result_free().
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The authenticated caller lacks the
 *     required role or delegation.
 * @retval #CONSENT_ERROR_INVALID_OPERATION The handle is an offline
 *     registration handle.
 * @retval #CONSENT_ERROR_WOULD_DEADLOCK The caller owns the callback context.
 * @retval #CONSENT_ERROR_TIMEOUT The local wait expired; remote work may still
 *     exist.
 * @retval #CONSENT_ERROR_OUTCOME_UNKNOWN A sent operation has no confirmed
 *     outcome.
 * @retval #CONSENT_ERROR_CONFLICT A retry ID was reused with a different
 *     payload.
 * @see consent_result_get_decision()
 * @see consent_get_request_result()
 */
CONSENT_API int consent_request(consent_client_h client,
    const consent_params_t* params, unsigned int wait_timeout_ms,
    consent_result_t** result);
/**
 * @brief Queries or authorizes all required conditions without opening UI.
 * @since 0.1.0
 *
 * @details Populate subject, profile and 1-16 requirements. QUERY is the
 *     default and never consumes grants or opens UI. AUTHORIZE additionally
 *     requires stable operation_id and step_id; an ALLOWED response commits
 *     atomic ONCE consumption and supplies a receipt. The authenticated
 *     checker/cm/ce/holder must own or be delegated each definition's
 *     enforcer.
 *
 * @remarks For resident data reuse, set operation=reuse-data, artifact,
 *     subject/profile, session/generation and exact scope/purpose/recipient;
 *     the authenticated holder is checked against current provenance. This
 *     path does not acquire new data.
 *
 * @remarks Do not call while owning the callback context. A nonzero status
 *     blocks execution; retry uncertain work with the same IDs and identical
 *     payload.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
 * @param[in] wait_timeout_ms The local wait in milliseconds, from 1 through
 *     300,000. Timeout does not cancel remote work.
 * @param[out] result The owned result on success, or NULL on error. Release
 *     with consent_result_free().
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The authenticated caller lacks the
 *     required role or delegation.
 * @retval #CONSENT_ERROR_INVALID_OPERATION The handle is an offline
 *     registration handle.
 * @retval #CONSENT_ERROR_WOULD_DEADLOCK The caller owns the callback context.
 * @retval #CONSENT_ERROR_TIMEOUT The local wait expired; remote work may still
 *     exist.
 * @retval #CONSENT_ERROR_OUTCOME_UNKNOWN A sent operation has no confirmed
 *     outcome.
 * @retval #CONSENT_ERROR_CONFLICT A retry ID was reused with a different
 *     payload.
 * @see consent_result_get_decision()
 * @see consent_get_request_result()
 */
CONSENT_API int consent_check(consent_client_h client,
    const consent_params_t* params, unsigned int wait_timeout_ms,
    consent_result_t** result);
/**
 * @brief Queues an approval request on the client dispatcher.
 * @since 0.1.0
 *
 * @details Populate subject, profile, stable client_request_id and
 *     operation_id, and 1-16 requirements. Optional session requires its
 *     current generation. deadline_ms is a remote approval lifetime of
 *     100-300,000 ms (default 60,000); it is independent of the local wait.
 *     Only authenticated argo may request approval.
 *
 * @remarks Return 0 means local acceptance only. On a live registration the
 *     callback is queued after this call returns, including cache/immediate
 *     results, and is delivered at most once without library locks. The caller
 *     must iterate the captured context only on the creating thread and must
 *     not nest iteration inside this call. The local operation timeout is
 *     300,000 ms.
 *
 * @remarks Inputs are copied before return. Keep user_data valid until
 *     delivery or suppression. If this function returns an error, no callback
 *     is registered and operation is zero.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
 * @param[in] callback The non-NULL completion callback.
 * @param[in] user_data The caller-owned callback data; NULL is allowed.
 * @param[out] operation The nonzero local callback ID on acceptance, or zero
 *     on error.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Accepted locally; this does not imply approval.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_INVALID_OPERATION The handle is an offline
 *     registration handle.
 * @retval #CONSENT_ERROR_BUSY The bounded pending-operation queue is full.
 * @retval #CONSENT_ERROR_DISCONNECTED The client is closed or disconnected.
 * @see consent_result_cb
 * @see consent_async_detach()
 * @see consent_client_destroy()
 */
CONSENT_API int consent_request_async(consent_client_h client,
    const consent_params_t* params, consent_result_cb callback, void* user_data,
    consent_async_id_t* operation);
/**
 * @brief Queues an advisory or authoritative check without opening UI.
 * @since 0.1.0
 *
 * @details Populate subject, profile and 1-16 requirements. QUERY is the
 *     default and never consumes grants or opens UI. AUTHORIZE additionally
 *     requires stable operation_id and step_id; an ALLOWED response commits
 *     atomic ONCE consumption and supplies a receipt. The authenticated
 *     checker/cm/ce/holder must own or be delegated each definition's
 *     enforcer.
 *
 * @remarks Return 0 means local acceptance only. On a live registration the
 *     callback is queued after this call returns, including cache/immediate
 *     results, and is delivered at most once without library locks. The caller
 *     must iterate the captured context only on the creating thread and must
 *     not nest iteration inside this call. The local operation timeout is
 *     300,000 ms.
 *
 * @remarks Inputs are copied before return. Keep user_data valid until
 *     delivery or suppression. If this function returns an error, no callback
 *     is registered and operation is zero.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
 * @param[in] callback The non-NULL completion callback.
 * @param[in] user_data The caller-owned callback data; NULL is allowed.
 * @param[out] operation The nonzero local callback ID on acceptance, or zero
 *     on error.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Accepted locally; this does not imply approval.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_INVALID_OPERATION The handle is an offline
 *     registration handle.
 * @retval #CONSENT_ERROR_BUSY The bounded pending-operation queue is full.
 * @retval #CONSENT_ERROR_DISCONNECTED The client is closed or disconnected.
 * @see consent_result_cb
 * @see consent_async_detach()
 * @see consent_client_destroy()
 */
CONSENT_API int consent_check_async(consent_client_h client,
    const consent_params_t* params, consent_result_cb callback, void* user_data,
    consent_async_id_t* operation);
/**
 * @brief Suppresses a pending local asynchronous callback.
 * @since 0.1.0
 *
 * @details Call on the creating thread in the creating process. On success the
 *     callback will not subsequently start; caller-owned user_data may then be
 *     released on that thread. Detach does not cancel daemon work, consume a
 *     grant, or roll back an authorization. Use consent_cancel_request() for a
 *     pending remote approval request. An already delivered callback ID is not
 *     found.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] operation The local ID returned by an asynchronous API.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_INVALID_OPERATION The handle is an offline
 *     registration handle.
 * @retval #CONSENT_ERROR_NOT_FOUND No live asynchronous registration has this
 *     ID.
 */
CONSENT_API int consent_async_detach(consent_client_h client,
    consent_async_id_t operation);

/**
 * @brief Retrieves a request's stored decision.
 * @since 0.1.0
 *
 * @details Set request_id, or set subject, profile and the original
 *     client_request_id. The authenticated argo identity must own the request
 *     and be delegated its subject/profile. A lookup can return PENDING. A
 *     terminal result is historical and does not renew current authorization;
 *     use AUTHORIZE before execution.
 *
 * @remarks This is a synchronous operation with a 5,000 ms local wait. It does
 *     not wait for UI interaction or physical data deletion. The common
 *     threading, error and ownership rules in @ref CONSENT_MODULE apply.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
 * @param[out] result The owned result on success, or NULL on error. Release
 *     with consent_result_free().
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The authenticated caller lacks the
 *     required role or delegation.
 * @retval #CONSENT_ERROR_INVALID_OPERATION The handle is an offline
 *     registration handle.
 * @see consent_request()
 * @see consent_check()
 */
CONSENT_API int consent_get_request_result(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Cancels an owned approval request if it is still pending.
 * @since 0.1.0
 *
 * @details Set request_id, or set subject, profile and the original
 *     client_request_id. The authenticated argo identity must own the request
 *     and be delegated its subject/profile. A pending request becomes
 *     CANCELLED; an already terminal request retains its decision.
 *     Cancellation does not revoke existing grants or authorization receipts.
 *
 * @remarks This is a synchronous operation with a 5,000 ms local wait. It does
 *     not wait for UI interaction or physical data deletion. The common
 *     threading, error and ownership rules in @ref CONSENT_MODULE apply.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
 * @param[out] result The owned result on success, or NULL on error. Release
 *     with consent_result_free().
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The authenticated caller lacks the
 *     required role or delegation.
 * @retval #CONSENT_ERROR_INVALID_OPERATION The handle is an offline
 *     registration handle.
 * @see consent_async_detach()
 * @see consent_revoke()
 */
CONSENT_API int consent_cancel_request(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_REQUEST_H_
