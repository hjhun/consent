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

#ifndef CONSENT_TESTS_SMOKE_TOOL_JSON_H_
#define CONSENT_TESTS_SMOKE_TOOL_JSON_H_

#include <json-glib/json-glib.h>

#define TOOL_LIMIT 16384

const char* tool_string(JsonObject* object, const char* key);
JsonObject* tool_object(JsonObject* object, const char* key);
JsonParser* tool_parse(const char* data);
char* tool_request(const char* id, const char* name, const char* record);
char* tool_response(const char* id, const char* text, int error);
int tool_validate_response(const char* data, const char* id);

#endif  // CONSENT_TESTS_SMOKE_TOOL_JSON_H_
