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

#ifndef CONSENT_TESTS_SMOKE_TOOL_GATE_H_
#define CONSENT_TESTS_SMOKE_TOOL_GATE_H_

#include "tool-json.h"

JsonParser* tool_load_metadata(void);
int tool_validate_catalog(int verbose);
void tool_validate_binding(JsonObject* binding, const char* definition,
                           const char* record, int verbose);
char* tool_context_read(const char* record, const char* id);

#endif  // CONSENT_TESTS_SMOKE_TOOL_GATE_H_
