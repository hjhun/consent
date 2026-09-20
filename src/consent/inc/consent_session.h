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
#ifndef TIZEN_CONSENT_SESSION_H_
#define TIZEN_CONSENT_SESSION_H_

#include "consent_common.h"

/**
 * @defgroup CONSENT_SESSION_MODULE Logical sessions
 * @ingroup CONSENT_MODULE
 * @brief Session transitions and deadline-aware state.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opens a logical consent session.
 * @since 0.1.0
 *
 * @details Set subject/profile; optional lifecycle is CONNECTION_BOUND
 *     (default) or RESUMABLE_CONVERSATION. idle_timeout_ms defaults to 600,000
 *     and max_lifetime_ms to 3,600,000, each bounded to 100-86,400,000.
 *     lease_ms defaults to 30,000 (100-60,000); reconnect_grace_ms defaults to
 *     30,000 (100-300,000).
 *
 * @remarks Retain returned session, generation and resume_token. This creates
 *     a new session on each successful call; there is no operation-ID
 *     deduplication. Sessions are distinct from sockets and are not
 *     reactivated after daemon restart. The current C API has no heartbeat
 *     method; callers must respect the lease.
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
 * @pre The caller must have the session role and delegated subject/profile.
 */
CONSENT_API int consent_session_open(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Suspends or closes an owned active session.
 * @since 0.1.0
 *
 * @details Set subject, profile, session and current generation. A
 *     RESUMABLE_CONVERSATION becomes SUSPENDED and increments generation;
 *     CONNECTION_BOUND closes instead. Use the returned state/generation.
 *     Pending approvals are invalidated and protected use stops.
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
 * @retval #CONSENT_ERROR_STALE The supplied generation is stale.
 * @retval #CONSENT_ERROR_SESSION_INACTIVE The requested transition is not
 *     available.
 * @pre The caller must have the session role and own the session process
 *     instance.
 * @see consent_session_get_state()
 */
CONSENT_API int consent_session_suspend(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Resumes an owned suspended conversation session.
 * @since 0.1.0
 *
 * @details Set subject, profile, session, current generation and resume_token.
 *     Resume is allowed only within the grace, idle and maximum lifetime
 *     bounds and for the same authenticated owner process instance. It
 *     increments generation, rotates resume_token and sets a 30,000 ms lease.
 *     Replace stored generation/token with the returned values; old values are
 *     stale.
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
 * @retval #CONSENT_ERROR_STALE The supplied generation is stale.
 * @retval #CONSENT_ERROR_SESSION_INACTIVE The requested transition is not
 *     available.
 * @pre The caller must have the session role and own the session process
 *     instance.
 * @see consent_session_get_state()
 */
CONSENT_API int consent_session_resume(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Closes a session and schedules holder cleanup.
 * @since 0.1.0
 *
 * @details Set subject, profile, session and current generation. Access and
 *     SESSION grants are invalidated; artifacts enter cleanup. The result
 *     state is CLOSING until all artifacts are acknowledged DELETED, then
 *     CLOSED. A successful close is not proof of physical deletion. A stale
 *     generation must be resolved by state lookup, not blindly retried.
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
 * @retval #CONSENT_ERROR_STALE The supplied generation is stale.
 * @pre The caller must have the session role and own the session process
 *     instance.
 * @see consent_session_get_state()
 */
CONSENT_API int consent_session_close(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Reads current session state and cleanup count.
 * @since 0.1.0
 *
 * @details Set subject, profile and session. Returns state, generation,
 *     policy, monotonic deadline fields and cleanup_pending. No generation is
 *     required for this read. It does not renew the lease or idle deadline and
 *     never returns a resume token.
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
 * @pre The caller must be authenticated and delegated the session
 *     subject/profile.
 */
CONSENT_API int consent_session_get_state(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_SESSION_H_
