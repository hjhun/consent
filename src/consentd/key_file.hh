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
#ifndef CONSENTD_KEY_FILE_HH_
#define CONSENTD_KEY_FILE_HH_

#include <glib.h>

#include <set>
#include <string>

namespace consentd {
std::string KeyValue(GKeyFile* file, const char* group, const char* key);
std::set<std::string> KeyValues(GKeyFile* file, const char* group,
                                const char* key);
}  // namespace consentd
#endif  // CONSENTD_KEY_FILE_HH_
