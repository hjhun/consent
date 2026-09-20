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

#ifdef __cplusplus
extern "C" {
#endif

/* Params are single-threaded builders. Strings are copied, UTF-8, <=8192 bytes.
 * Protocol metadata (v,id,method,status and underscore-prefixed keys) is reserved.
 * Fields and accepted values are documented in docs/developer-guide.*.md. */
CONSENT_API int consent_params_create(consent_params_t** params);
CONSENT_API void consent_params_free(consent_params_t* params);
CONSENT_API int consent_params_set(consent_params_t* params, const char* key,
    const char* value);
CONSENT_API int consent_params_set_int64(consent_params_t* params,
    const char* key, int64_t value);
CONSENT_API int consent_params_set_check_mode(consent_params_t* params,
    consent_check_mode_e mode);
/* Append rN.definition/operation/scope/purpose/recipient; at most 16. */
CONSENT_API int consent_params_add_requirement(consent_params_t* params,
    const char* definition, const char* operation, const char* scope,
    const char* purpose, const char* recipient);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_PARAMS_H_
