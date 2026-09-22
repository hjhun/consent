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
#ifndef CONSENT_FEATURE_BRIDGE_H_
#define CONSENT_FEATURE_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif
#define FEATURE_API __attribute__((visibility("default")))

/* Private PoC UI bridge. UTF-8 outputs are owned and freed with this library.
 * Inputs never carry arbitrary provider, scope, purpose, recipient or role.
 * Every call authenticates the fixed argo peer; errors leave *json NULL. */
FEATURE_API int consent_feature_catalog(char** json);
FEATURE_API int consent_feature_status(char** json);
FEATURE_API int consent_feature_select(const char* feature_ids, const char* mode,
    unsigned int duration_ms, const char* coordinator_epoch, const char* catalog_revision, const char* expected_revision,
    const char* command_id, char** json);
FEATURE_API int consent_feature_preapprove(const char* coordinator_epoch, const char* catalog_revision, const char* expected_revision,
    const char* command_id, char** json);
FEATURE_API int consent_feature_task(const char* task_id, const char* task_mode,
    const char* coordinator_epoch, const char* catalog_revision, const char* expected_revision, const char* command_id, char** json);
FEATURE_API void consent_feature_free(char* json);

#ifdef __cplusplus
}
#endif
#endif  // CONSENT_FEATURE_BRIDGE_H_
