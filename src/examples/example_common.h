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
#ifndef CONSENT_EXAMPLE_COMMON_H_
#define CONSENT_EXAMPLE_COMMON_H_

#include <consent.h>

/* Helpers for these executable examples, not additions to the public ABI. */
int example_requirement(const char* subject, const char* profile,
    const char* definition, const char* revision, const char* scope,
    const char* purpose, const char* recipient, const char* session,
    const char* generation, consent_params_t** params);
int example_definition(const char* definition, const char* enforcer,
    const char* generation, const char* operation_id, const char* policy_version,
    const char* text_revision, consent_params_t** params);
void example_error(const char* operation, int status);
const char* example_decision(const consent_result_t* result);

#endif  // CONSENT_EXAMPLE_COMMON_H_
