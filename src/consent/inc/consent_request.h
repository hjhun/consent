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
#ifndef TIZEN_CONSENT_REQUEST_H_
#define TIZEN_CONSENT_REQUEST_H_

#include "consent_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SYNC result ownership transfers to caller. wait_timeout_ms is a local wait,
 * not a remote cancellation. After OUTCOME_UNKNOWN use the same stable
 * client_request_id to retrieve/retry; never start protected work from an error.
 * A successful check QUERY is advisory; only AUTHORIZE permits access. */
CONSENT_API int consent_request(consent_client_h client,
    const consent_params_t* params, unsigned int wait_timeout_ms,
    consent_result_t** result);
CONSENT_API int consent_check(consent_client_h client,
    const consent_params_t* params, unsigned int wait_timeout_ms,
    consent_result_t** result);
/* 0 means local acceptance, never an approval. Every accepted live registration
 * gets at most one queued callback, including immediate ALLOWED/DENIED results.
 * Callback result is borrowed for that callback; clone it to retain it. */
CONSENT_API int consent_request_async(consent_client_h client,
    const consent_params_t* params, consent_result_cb callback, void* user_data,
    consent_async_id_t* operation);
CONSENT_API int consent_check_async(consent_client_h client,
    const consent_params_t* params, consent_result_cb callback, void* user_data,
    consent_async_id_t* operation);
/* Detaches local callback only. Remote work can be cancelled with
 * consent_cancel_request; request lookup by client_request_id remains valid. */
CONSENT_API int consent_async_detach(consent_client_h client,
    consent_async_id_t operation);

/* Management operations wait for a short commit (5 seconds), not UI/cleanup.
 * Required role is always authenticated by consentd; no role claim is sent. */
CONSENT_API int consent_get_request_result(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);
CONSENT_API int consent_cancel_request(consent_client_h client,
    const consent_params_t* params, consent_result_t** result);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_REQUEST_H_
