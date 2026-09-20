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

#ifdef __cplusplus
extern "C" {
#endif

/* Management operations wait for a short commit (5 seconds), not UI/cleanup.
 * Required role is always authenticated by consentd; no role claim is sent. */
CONSENT_API int consent_data_register(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_data_register_derived(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_data_release(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_cleanup_get_state(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
/* Discover pending/failed cleanup for the authenticated holder and explicit
 * subject/profile. reconcile=1 includes earlier instances of the same holder;
 * this grants cleanup access only, never artifact-use or registration rights.
 * Results contain count and aN.artifact/session/state/error (at most 48).
 * Acknowledge actual deletion with consent_data_release(), using reconcile=1
 * and the same subject/profile when reconciling an earlier process instance. */
CONSENT_API int consent_cleanup_get_pending(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_DATA_H_
