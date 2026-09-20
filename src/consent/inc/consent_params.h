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
#ifndef TIZEN_CONSENT_PARAMS_H_
#define TIZEN_CONSENT_PARAMS_H_

#include "consent_common.h"

/**
 * @defgroup CONSENT_PARAMS_MODULE Parameters
 * @ingroup CONSENT_MODULE
 * @brief Copied fields and atomic requirement builders.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates an empty parameter builder.
 * @since 0.1.0
 *
 * @details A builder owns copied strings and must not be mutated or freed
 *     concurrently with another use. An empty builder alone is not a valid
 *     request; populate the required operation schema.
 *
 * @param[out] params The owned builder, or NULL on error.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @see consent_params_free()
 * @see consent_params_set()
 */
CONSENT_API int consent_params_create(consent_params_t** params);
/**
 * @brief Releases a parameter builder.
 * @since 0.1.0
 *
 * @details NULL is accepted. Previously submitted calls retain their own
 *     copies of the input fields.
 *
 * @param[in] params The owned builder to free, or NULL.
 */
CONSENT_API void consent_params_free(consent_params_t* params);
/**
 * @brief Copies or replaces a named UTF-8 parameter.
 * @since 0.1.0
 *
 * @details Keys contain ASCII letters, digits, underscore, dot or hyphen and
 *     are at most 128 bytes. Empty keys, underscore-prefixed keys and
 *     v/id/method/status/role/pid/uid/gid are reserved. Values are
 *     NUL-terminated UTF-8 of at most 8,192 bytes excluding the terminator.
 *     Empty values are allowed where the operation schema allows them.
 *
 * @remarks The builder permits at most 253 distinct fields; transport and
 *     operation-specific limits may reject a smaller payload. This function
 *     validates the generic field encoding, not the complete daemon schema.
 *     Caller-provided identity strings never establish a role.
 *
 * @param[in] params The builder to modify.
 * @param[in] key The non-NULL field name.
 * @param[in] value The non-NULL string to copy.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_BUSY Adding a new field would exceed the builder
 *     limit.
 */
CONSENT_API int consent_params_set(consent_params_t* params, const char* key,
    const char* value);
/**
 * @brief Sets a parameter to a canonical signed decimal integer.
 * @since 0.1.0
 *
 * @details Converts the full int64_t range to decimal and applies
 *     consent_params_set() validation. The daemon separately validates the
 *     allowed range for this field.
 *
 * @param[in] params The builder to modify.
 * @param[in] key The field name.
 * @param[in] value The integer value to encode.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_BUSY The builder field limit was reached.
 * @see consent_params_set()
 */
CONSENT_API int consent_params_set_int64(consent_params_t* params,
    const char* key, int64_t value);
/**
 * @brief Selects advisory QUERY or authoritative AUTHORIZE checking.
 * @since 0.1.0
 *
 * @details Sets the mode field. AUTHORIZE requires stable operation_id and
 *     step_id fields and may consume ONCE grants. Request APIs remain advisory
 *     regardless of this field.
 *
 * @param[in] params The builder to modify.
 * @param[in] mode The check mode.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @see consent_check()
 * @see consent_check_mode_e
 */
CONSENT_API int consent_params_set_check_mode(consent_params_t* params,
    consent_check_mode_e mode);
/**
 * @brief Atomically appends one required authorization condition.
 * @since 0.1.0
 *
 * @details Appends rN.definition, rN.operation, rN.scope, rN.purpose and
 *     rN.recipient and increments count. At most 16 conditions are allowed. On
 *     failure the builder is unchanged. Every condition is combined by AND.
 *
 * @remarks The daemon requires nonempty identifier values for definition,
 *     operation and purpose; scope is an exact string of at most 4,096 bytes.
 *     Scope and recipient may be explicitly empty. Set rN.policy_version
 *     separately, especially for typed definitions, and rN.holder when an
 *     acquisition receipt must authorize a data holder.
 *
 * @param[in] params The builder to modify.
 * @param[in] definition The registered definition ID.
 * @param[in] operation The protected operation, for example "read".
 * @param[in] scope The non-NULL exact scope string.
 * @param[in] purpose The non-NULL purpose identifier.
 * @param[in] recipient The non-NULL recipient string; use "" when absent.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_BUSY The builder field limit was reached.
 */
CONSENT_API int consent_params_add_requirement(consent_params_t* params,
    const char* definition, const char* operation, const char* scope,
    const char* purpose, const char* recipient);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_PARAMS_H_
