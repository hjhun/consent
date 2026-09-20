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

/**
 * @defgroup CONSENT_CLIENT_MODULE Client lifecycle
 * @ingroup CONSENT_MODULE
 * @brief Connections, dispatcher ownership and explicit offline handles.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates and connects an online client.
 * @since 0.1.0
 *
 * @details Equivalent to consent_client_create_with_context(NULL, client). The
 *     creating thread's thread-default GLib context is captured, or the global
 *     default context if none is set. Connection and hello may each wait up to
 *     2,000 ms. No offline fallback is attempted.
 *
 * @param[out] client The new owned handle on success, or NULL on error.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The systemd socket or peer could
 *     not be authenticated.
 * @retval #CONSENT_ERROR_DISCONNECTED Connection failed.
 * @retval #CONSENT_ERROR_TIMEOUT Connection or hello timed out.
 * @see consent_client_destroy()
 * @see consent_client_create_with_context()
 */
CONSENT_API int consent_client_create(consent_client_h* client);
/**
 * @brief Creates an online client with an explicit callback dispatcher.
 * @since 0.1.0
 *
 * @details The library takes its own reference to @a context. Create, destroy
 *     and asynchronous operations must run on the creating thread; only that
 *     thread may iterate this context for the handle. Do not nest context
 *     iteration inside an asynchronous API call.
 *
 * @remarks Synchronous operations may run on another thread, but return
 *     WOULD_DEADLOCK if their caller owns the callback context. Destroy must
 *     not race any call using the raw handle. After fork, exec or an identity
 *     change, create a new handle. The library creates an internal I/O thread;
 *     it does not run the callback context for you.
 *
 * @param[in] context The GLib context, or NULL to capture the thread-default
 *     context.
 * @param[out] client The new owned handle, or NULL on error.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The fixed systemd endpoint failed
 *     authentication.
 * @retval #CONSENT_ERROR_BUSY The client resource limit was reached.
 * @retval #CONSENT_ERROR_DISCONNECTED Connection failed.
 * @retval #CONSENT_ERROR_TIMEOUT Connection or hello timed out.
 * @see consent_result_cb
 * @see consent_client_destroy()
 */
CONSENT_API int consent_client_create_with_context(GMainContext* context,
    consent_client_h* client);

/**
 * @brief Creates a root-only handle for image-time definition staging.
 * @since 0.1.0
 *
 * @details This explicit constructor never connects to consentd and never
 *     opens consent.db. Only consent_register() and consent_client_destroy()
 *     accept the resulting handle, on its creating thread and in its creating
 *     process. Real and effective UID must both be zero at creation and
 *     registration. No callback context or I/O thread is created.
 *
 * @remarks The handle holds an exclusive nonblocking image lifecycle lock
 *     until destruction. Existing image paths must be root-protected, absolute
 *     and free of symlinks or dot components. The canonical opt/var/lib
 *     ancestors must allow other-read and other-execute. The writer does not
 *     silently repair existing permissions.
 *
 * @remarks A successful registration means a durable STAGED definition, not an
 *     active registration or an approval. Supply a stable operation_id and
 *     Installer-issued expected_generation; this API does not create
 *     installation authority. At startup consentd verifies installed
 *     package/app identity and active generation before import.
 *
 * @param[in] image_root The absolute image root; "/" explicitly selects the
 *     current filesystem.
 * @param[out] client The owned offline handle, or NULL on error.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER A required argument or field is
 *     invalid.
 * @retval #CONSENT_ERROR_OUT_OF_MEMORY Allocation failed.
 * @retval #CONSENT_ERROR_PERMISSION_DENIED The caller is not root or a path is
 *     not protected.
 * @retval #CONSENT_ERROR_BUSY A daemon, Installer or image writer holds the
 *     lifecycle lock.
 * @retval #CONSENT_ERROR_IO Image storage could not be opened or synchronized.
 * @see consent_register()
 * @see consent_client_destroy()
 */
CONSENT_API int consent_client_create_offline_registration(const char* image_root,
    consent_client_h* client);
/**
 * @brief Destroys a client and suppresses its queued callbacks.
 * @since 0.1.0
 *
 * @details Call on the creating thread in the creating process. Online
 *     destruction joins I/O and suppresses queued callbacks without remotely
 *     cancelling approval requests. It is safe inside a callback; the current
 *     borrowed callback result remains valid until the callback returns.
 *     Offline destruction releases the image lock and may also be used after
 *     dropping root privileges.
 *
 * @remarks Do not race destruction with any API call using this raw handle. No
 *     caller may use the handle after successful destruction. Free callback
 *     user_data only after its callback has finished or delivery has been
 *     suppressed.
 *
 * @param[in] client The handle to destroy. It becomes invalid on successful destruction.
 *
 * @return @c 0 on success, otherwise a negative error value from
 *     #consent_error_e.
 * @retval #CONSENT_ERROR_NONE Operation succeeded.
 * @retval #CONSENT_ERROR_INVALID_PARAMETER The handle is NULL or belongs to
 *     another thread/process.
 * @see consent_async_detach()
 * @see consent_cancel_request()
 */
CONSENT_API int consent_client_destroy(consent_client_h client);

/** @} */

#ifdef __cplusplus
}
#endif
#endif  // TIZEN_CONSENT_CLIENT_H_
