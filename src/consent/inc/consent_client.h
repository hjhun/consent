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
#ifndef TIZEN_CONSENT_CLIENT_H_
#define TIZEN_CONSENT_CLIENT_H_

#include "consent_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create/destroy and asynchronous APIs belong to the creating thread. NULL
 * context captures its thread-default GLib context. Iterate that context only
 * from this thread; do not run nested iterations during an asynchronous API.
 * Inputs are copied before return. Synchronous APIs may run on other threads;
 * they return WOULD_DEADLOCK while the caller owns the callback context.
 * Destroy suppresses queued callbacks and joins I/O. It is safe in a callback,
 * but must not race another call using the same raw client handle. Fork/exec or
 * identity changes require a new handle; inherited handles are rejected. */
CONSENT_API int consent_client_create(consent_client_h* client);
CONSENT_API int consent_client_create_with_context(GMainContext* context,
    consent_client_h* client);

/* Explicit root-only image construction; never connects to consentd or falls
 * back from an online failure. image_root is an absolute, root-protected image
 * directory ("/" selects the current filesystem). Only consent_register() and
 * consent_client_destroy() accept this handle, on its creating thread/process.
 * A successful registration durably stages a definition, not an approval or an
 * active registration. On boot consentd must validate the actual installed
 * package/app and protected installation generation before importing it.
 * Supply the usual stable operation_id and expected_generation; this API does
 * not bootstrap installation authority. The handle owns an exclusive image
 * lifecycle lock until destroyed; an active daemon/installer causes BUSY.
 * On failure *client is NULL. No GLib context or I/O thread is created. */
CONSENT_API int consent_client_create_offline_registration(const char* image_root,
    consent_client_h* client);
CONSENT_API int consent_client_destroy(consent_client_h client);

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_CLIENT_H_
