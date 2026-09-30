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
#ifndef CONSENT_TESTS_SMOKE_COMMON_H_
#define CONSENT_TESTS_SMOKE_COMMON_H_

#include <consent.h>
#include <glib.h>

void smoke_call(int status, const char* operation);
void smoke_set(consent_params_t* params, const char* key, const char* value);
consent_params_t* smoke_params(void);
consent_params_t* smoke_requirement(const char* definition);
const char* smoke_tool_record(const char* definition);
void smoke_expect(int condition, const char* description);
#endif  // CONSENT_TESTS_SMOKE_COMMON_H_
