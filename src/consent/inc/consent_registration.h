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

#ifdef __cplusplus
extern "C" {
#endif

/* Online handles wait for a short consentd commit (5 seconds), not UI/cleanup.
 * consentd authenticates the required online role; no role claim is sent.
 * With an explicit offline registration handle, only consent_register() is
 * accepted: root durably stages a definition without contacting consentd.
 * Success then means STAGED, not active registration or approval. See
 * consent_client.h for offline root, ownership and boot-validation rules. */

CONSENT_API int consent_register(consent_client_h client,
    const char* package_name, const char* app_id, const consent_params_t* params);
CONSENT_API int consent_update(consent_client_h client,
    const char* package_name, const char* app_id, const consent_params_t* params);
CONSENT_API int consent_unregister(consent_client_h client,
    const char* package_name, const consent_params_t* params);
CONSENT_API int consent_revoke(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_REGISTRATION_H_
