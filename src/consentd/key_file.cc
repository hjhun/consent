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
#include "key_file.hh"

#include <memory>

namespace consentd {
std::string KeyValue(GKeyFile* file, const char* group, const char* key) {
  std::unique_ptr<gchar, decltype(&g_free)> text(
      g_key_file_get_string(file, group, key, nullptr), g_free);
  return text ? text.get() : "";
}

std::set<std::string> KeyValues(GKeyFile* file, const char* group,
                                const char* key) {
  gsize count = 0;
  std::unique_ptr<gchar*, decltype(&g_strfreev)> list(
      g_key_file_get_string_list(file, group, key, &count, nullptr),
      g_strfreev);
  std::set<std::string> result;
  for (gsize i = 0; i < count; ++i) {
    if (list.get()[i][0])
      result.insert(list.get()[i]);
  }
  return result;
}

}  // namespace consentd
