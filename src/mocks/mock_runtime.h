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
#ifndef CONSENT_MOCK_RUNTIME_H_
#define CONSENT_MOCK_RUNTIME_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Separate main executables select their local command set. This name is never
 * transmitted as an identity or role; consentd authenticates the executable. */
int consent_mock_main(const char* role, int argc, char** argv);
int consent_feature_argo_main(void);
int consent_feature_worker_main(const char* role);
int consent_feature_register_main(const char* generation);

#ifdef __cplusplus
}
#endif
#endif  // CONSENT_MOCK_RUNTIME_H_
