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
#ifndef TIZEN_CONSENT_RESULT_H_
#define TIZEN_CONSENT_RESULT_H_

#include "consent_common.h"

/**
 * @defgroup CONSENT_RESULT_MODULE Results
 * @ingroup CONSENT_MODULE
 * @brief Owned and borrowed result access.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Frees an owned result.
 * @since 0.1.0
 *
 * @details NULL is accepted. Use only for synchronous outputs or clones. Never
 *     free the borrowed result passed to a callback. Every borrowed field
 *     pointer obtained from this result becomes invalid.
 *
 * @param[in] result The owned result to release, or NULL.
 * @see consent_result_clone()
 */
CONSENT_API void consent_result_free(consent_result_t* result);
/**
 * @brief Copies a result into independent caller-owned storage.
 * @since 0.1.0
 *
 * @details Use within a callback to retain its borrowed result. The input
 *     remains owned by its original owner.
 *
 * @param[in] result The non-NULL result to copy.
 * @param[out] copy The owned clone on success, or NULL on error.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @see consent_result_free()
 * @see consent_result_cb
 */
CONSENT_API int consent_result_clone(const consent_result_t* result,
    consent_result_t** copy);
/**
 * @brief Reads a result's decision without transferring ownership.
 * @since 0.1.0
 *
 * @details Returns UNKNOWN if the result is NULL or its decision field is
 *     absent/unrecognized. Check the operation status first. ALLOWED from
 *     request, QUERY or historical lookup is advisory; only a successful
 *     current AUTHORIZE check permits the bound execution.
 *
 * @param[in] result The borrowed result, or NULL.
 *
 * @return The decision, or #CONSENT_DECISION_UNKNOWN.
 * @see consent_check()
 */
CONSENT_API consent_decision_e consent_result_get_decision(
    const consent_result_t* result);
/**
 * @brief Borrows a named string field from a result.
 * @since 0.1.0
 *
 * @details The pointer is valid only for the lifetime of the result (only
 *     until callback return for callback results). Copy or clone before
 *     retaining it. Missing and empty fields differ: an existing empty field
 *     returns a non-NULL empty string.
 *
 * @param[in] result The borrowed result.
 * @param[in] key The non-NULL field name.
 *
 * @return A borrowed NUL-terminated UTF-8 string, or NULL for invalid input, a
 *     missing key or lookup allocation failure.
 * @see consent_result_clone()
 */
CONSENT_API const char* consent_result_get(const consent_result_t* result,
    const char* key);
/**
 * @brief Returns the number of fields in a result.
 * @since 0.1.0
 *
 * @details No ownership is transferred. Protocol and snapshot metadata may be
 *     present alongside operation fields; do not assume a fixed count.
 *
 * @param[in] result The borrowed result, or NULL.
 *
 * @return The field count, or zero for NULL.
 */
CONSENT_API size_t consent_result_size(const consent_result_t* result);
/**
 * @brief Borrows the key and value of an indexed result field.
 * @since 0.1.0
 *
 * @details Index is zero-based and must be less than consent_result_size(). Do
 *     not depend on field order. Returned pointers share the result lifetime.
 *     On invalid input the output pointers are not modified; initialize them
 *     before calling.
 *
 * @param[in] result The borrowed result.
 * @param[in] index The zero-based field index.
 * @param[out] key The borrowed field name on success.
 * @param[out] value The borrowed field value on success.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 */
CONSENT_API int consent_result_get_at(const consent_result_t* result,
    size_t index, const char** key, const char** value);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_RESULT_H_
