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
#ifndef TIZEN_CONSENT_DATA_H_
#define TIZEN_CONSENT_DATA_H_

#include "consent_common.h"

/**
 * @defgroup CONSENT_DATA_MODULE Data lifecycle
 * @ingroup CONSENT_MODULE
 * @brief Receipt-bound retention, derived provenance and cleanup.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers retained in-memory data against an acquisition receipt.
 * @since 0.1.0
 *
 * @details Set receipt, subject/profile, active session/generation, exact
 *     scope/purpose/recipient and optional requirement index (default 0). The
 *     original AUTHORIZE requirement must set rN.holder to this authenticated
 *     holder. storage_class is MEMORY_ONLY; other classes are denied.
 *
 * @remarks The definition must allow retention_ms > 0. Expiry is acquisition
 *     time plus that retention, not registration time. Same
 *     receipt/holder/instance/context retries return the existing artifact
 *     without extending its lifetime. Store only control metadata here, never
 *     the data body.
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
 * @retval #CONSENT_ERROR_STALE Receipt, grant, session or artifact provenance
 *     is stale.
 * @retval #CONSENT_ERROR_CONFLICT Retry context differs from the original
 *     artifact.
 * @pre The caller must have the holder role and delegated subject/profile.
 */
CONSENT_API int consent_data_register(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Registers data derived from existing in-memory artifacts.
 * @since 0.1.0
 *
 * @details Set subject/profile, active session/generation,
 *     scope/purpose/recipient, count (1-16) and parent0 through parentN.
 *     Parents must be distinct, current, owned by the same holder process and
 *     match the exact session and use context. storage_class is MEMORY_ONLY.
 *
 * @remarks The result inherits the earliest parent expiry, strongest level and
 *     combined provenance. Each successful call creates an artifact; there is
 *     no operation-ID deduplication or lifetime extension.
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
 * @retval #CONSENT_ERROR_STALE A parent or its provenance is no longer valid.
 * @pre The caller must have the holder role and delegated subject/profile.
 */
CONSENT_API int consent_data_register_derived(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Acknowledges actual holder cleanup of an artifact.
 * @since 0.1.0
 *
 * @details Set artifact and success="1" only after deleting all corresponding
 *     data (default success is "1"); use success="0" to report failure. The
 *     result state is DELETED or CLEANUP_FAILED. A completed deletion cannot
 *     be changed back to failure.
 *
 * @remarks For a prior process instance of the same authenticated holder, also
 *     set reconcile="1" and the matching subject/profile. Reconciliation is
 *     limited to pending/failed/already-deleted cleanup; it cannot recover use
 *     rights. Acknowledgement records the holder's evidence; consentd does not
 *     delete application data itself.
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
 * @pre The caller must have the holder role and own the artifact identity.
 * @see consent_cleanup_get_pending()
 */
CONSENT_API int consent_data_release(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Reads session cleanup progress.
 * @since 0.1.0
 *
 * @details Set subject, profile and session. Returns session state, generation
 *     and cleanup_pending. A zero count reports recorded holder
 *     acknowledgements, not independent inspection of application storage.
 *     This read does not renew session deadlines.
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
 * @see consent_session_get_state()
 * @see consent_data_release()
 */
CONSENT_API int consent_cleanup_get_state(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Lists cleanup still owed by the authenticated holder.
 * @since 0.1.0
 *
 * @details Set subject/profile; reconcile="1" also includes earlier process
 *     instances of this holder. Returns count and
 *     aN.artifact/session/state/error for at most 48 entries. After actual
 *     deletion acknowledge each artifact with consent_data_release(); query
 *     again to drain further entries. Reconciliation grants cleanup access
 *     only.
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
 * @pre The caller must have the holder role and delegated subject/profile.
 * @see consent_data_release()
 */
CONSENT_API int consent_cleanup_get_pending(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_DATA_H_
