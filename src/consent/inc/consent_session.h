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

#ifdef __cplusplus
extern "C" {
#endif

/* Management operations wait for a short commit (5 seconds), not UI/cleanup.
 * Required role is always authenticated by consentd; no role claim is sent. */
CONSENT_API int consent_session_open(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_suspend(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_resume(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_close(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_session_get_state(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_SESSION_H_
