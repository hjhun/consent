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
#ifndef TIZEN_CONSENT_REGISTRATION_H_
#define TIZEN_CONSENT_REGISTRATION_H_

#include "consent_common.h"

/**
 * @defgroup CONSENT_REGISTRATION_MODULE Definition registration
 * @ingroup CONSENT_MODULE
 * @brief Package/app registration and grant revocation.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registers or updates a package-owned consent definition.
 * @since 0.1.0
 *
 * @details Set operation_id, expected_generation, definition, enforcer,
 *     positive policy_version/text_revision, level (0-3), modes
 *     (comma-separated ONCE/SESSION/TIMED/PERSISTENT), default_locale and
 *     message.<locale>.title/body. The default locale needs both strings.
 *     Level 3 permits ONCE only. Optional retention_ms is 0-86,400,000. Typed
 *     templates and locale aliases are documented in the C API guide.
 *
 * @remarks Online calls require authenticated Installer package delegation and
 *     trusted app/package membership plus the active installation generation.
 *     Package name and app ID are explicit, separate inputs; they are not
 *     proof of identity. Retry with the same operation_id and exact payload.
 *     Changed semantics require a new policy_version; changed text/locale maps
 *     require a higher text_revision.
 *
 * @remarks On an offline handle only a durable STAGED definition is written.
 *     Activation is deferred until daemon startup validation; no approval is
 *     created. Post-publication uncertainty returns OUTCOME_UNKNOWN and must
 *     be retried with the same IDs. Online registration waits locally for up
 *     to 5,000 ms.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] package_name The nonempty package name.
 * @param[in] app_id The nonempty app ID belonging to that package.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The authenticated caller lacks the
 *     required role or delegation.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_STALE The installation generation or definition
 *     revision is stale.
 * @retval #CONSENT_ERROR_CONFLICT The retry ID or definition ownership
 *     conflicts.
 * @retval #CONSENT_ERROR_OUTCOME_UNKNOWN Publication or the online outcome is
 *     uncertain.
 * @retval #CONSENT_ERROR_STORAGE Trusted storage is unavailable.
 * @see consent_client_create_offline_registration()
 * @see consent_update()
 * @see consent_unregister()
 */
CONSENT_API int consent_register(consent_client_h client,
    const char* package_name, const char* app_id, const consent_params_t* params);
/**
 * @brief Updates a definition through the online registration contract.
 * @since 0.1.0
 *
 * @details Uses the same schema, validation and retry rules as
 *     consent_register(). Supply the complete definition and stable
 *     operation_id, not a partial patch. Offline handles reject update; use
 *     consent_register() to stage a new offline record.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] package_name The nonempty package name.
 * @param[in] app_id The nonempty app ID belonging to that package.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
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
 * @retval #CONSENT_ERROR_CONFLICT A retry ID was reused with different
 *     content.
 * @retval #CONSENT_ERROR_STALE The installation or revision is stale.
 * @see consent_register()
 */
CONSENT_API int consent_update(consent_client_h client,
    const char* package_name, const char* app_id, const consent_params_t* params);
/**
 * @brief Removes all active definitions belonging to a package.
 * @since 0.1.0
 *
 * @details Set operation_id and the current expected_generation. No app ID is
 *     required; all apps in the package are affected atomically. Other
 *     packages are unchanged. Related grants and requests are invalidated and
 *     cache invalidation is published after commit. Cleanup/audit metadata may
 *     remain. Retry the same ID and payload after uncertainty.
 *
 * @remarks This is a synchronous operation with a 5,000 ms local wait. It does
 *     not wait for UI interaction or physical data deletion. The common
 *     threading, error and ownership rules in @ref CONSENT_MODULE apply.
 *
 * @param[in] client The live client handle. It remains owned by the caller.
 * @param[in] package_name The package whose definitions are removed.
 * @param[in] params The parameter builder. Its fields are copied before
 *     return; the caller retains ownership.
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
 * @retval #CONSENT_ERROR_CONFLICT Retry content conflicts.
 * @retval #CONSENT_ERROR_STALE The package generation is stale.
 * @see consent_register()
 */
CONSENT_API int consent_unregister(consent_client_h client,
    const char* package_name, const consent_params_t* params);
/**
 * @brief Revokes grants for a definition and subject/profile.
 * @since 0.1.0
 *
 * @details Set definition, subject and profile. Matching grants and dependent
 *     receipts become invalid; affected artifacts enter cleanup. Physical
 *     deletion still requires holder acknowledgement. This does not unregister
 *     the definition.
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
 * @pre The caller must have the admin role and delegated subject/profile.
 * @see consent_data_release()
 */
CONSENT_API int consent_revoke(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_REGISTRATION_H_
