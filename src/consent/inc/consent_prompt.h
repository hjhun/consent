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

#ifdef __cplusplus
extern "C" {
#endif

/* Management operations wait for a short commit (5 seconds), not UI/cleanup.
 * Required role is always authenticated by consentd; no role claim is sent. */
CONSENT_API int consent_get_prompt(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_respond(consent_client_h client,
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

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_PROMPT_H_
