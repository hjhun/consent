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

#ifdef __cplusplus
extern "C" {
#endif

CONSENT_API void consent_result_free(consent_result_t* result);
CONSENT_API int consent_result_clone(const consent_result_t* result,
    consent_result_t** copy);
CONSENT_API consent_decision_e consent_result_get_decision(
    const consent_result_t* result);
/* Borrowed pointers remain valid until the result is freed. */
CONSENT_API const char* consent_result_get(const consent_result_t* result,
    const char* key);
CONSENT_API size_t consent_result_size(const consent_result_t* result);
CONSENT_API int consent_result_get_at(const consent_result_t* result,
    size_t index, const char** key, const char** value);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_RESULT_H_
