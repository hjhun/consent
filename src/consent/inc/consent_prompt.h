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
#ifndef TIZEN_CONSENT_PROMPT_H_
#define TIZEN_CONSENT_PROMPT_H_

#include "consent_common.h"

/**
 * @defgroup CONSENT_PROMPT_MODULE Approval UI
 * @ingroup CONSENT_MODULE
 * @brief Bound localized prompts and responses.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Obtains a localized prompt bound to the approval UI instance.
 * @since 0.1.0
 *
 * @details Set request_id and locale. For typed definitions also set
 *     template_version=1. The result contains prompt_token and count, plus
 *     rN.title/body/locale, exact authorization fields, sensitivity and
 *     allowed modes. Fetching another prompt rotates its token. Display all
 *     required context together; a formatted sentence alone is insufficient.
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
 * @retval #CONSENT_ERROR_STALE The request, policy, text or session is no
 *     longer current.
 * @retval #CONSENT_ERROR_TOO_LARGE The combined prompt exceeds its bounded
 *     budget.
 * @pre The caller must have the ui role and delegated request context.
 * @see consent_prompt_format()
 * @see consent_respond()
 */
CONSENT_API int consent_get_prompt(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/**
 * @brief Commits the user's response to the displayed prompt.
 * @since 0.1.0
 *
 * @details Set request_id, the most recent prompt_token and decision=ALLOWED
 *     or DENIED. For ALLOWED select grant_mode=ONCE, SESSION, TIMED or
 *     PERSISTENT as allowed by every definition (default ONCE). TIMED
 *     duration_ms is 100-3,600,000, default 300,000. SESSION requires the
 *     active request session. For typed prompts echo the requested locale from
 *     get_prompt.
 *
 * @remarks The same authenticated UI process instance must respond. The daemon
 *     re-evaluates all conditions without consuming ONCE grants. A changed
 *     condition can make an ALLOWED UI response finish as INVALIDATED. Inspect
 *     the returned decision; even ALLOWED is advisory until AUTHORIZE.
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
 * @retval #CONSENT_ERROR_STALE The request or displayed policy is stale.
 * @pre The caller must have the ui role and own the displayed prompt token.
 * @see consent_check()
 */
CONSENT_API int consent_respond(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

/**
 * @brief Formats one returned prompt field as bounded plain UTF-8 text.
 * @since 0.1.0
 *
 * @details Formats title or body of a zero-based requirement, using only
 *     daemon-bound typed arguments. Literal prompts are also supported.
 *     Substituted values are not recursively expanded. No ICU plural/date
 *     rules or locale-specific number formatting are applied.
 *
 * @remarks The output is at most 8,192 bytes excluding NUL. Render it as plain
 *     text, never markup or a format string. On formatting failure the UI must
 *     not treat the prompt as successfully displayed.
 *
 * @param[in] prompt The borrowed get_prompt result; it remains caller-owned.
 * @param[in] requirement_index The zero-based index, less than the result
 *     count.
 * @param[in] field Exactly "title" or "body".
 * @param[out] formatted The allocated text, or NULL on error. Release with
 *     standard C free().
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @see consent_get_prompt()
 */
CONSENT_API int consent_prompt_format(const consent_result_t* prompt,
    unsigned int requirement_index, const char* field, char** formatted);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_PROMPT_H_
