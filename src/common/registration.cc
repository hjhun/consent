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
#include "registration.hh"

#include "localization.hh"

#include <cerrno>
#include <cstdlib>
#include <set>

namespace consent {
namespace registration {
namespace {
bool Error(std::string* error, const char* reason) {
  if (error)
    *error = reason;
  return false;
}

bool Identifier(const std::string& value) {
  if (value.empty() || value.size() > 256)
    return false;
  for (unsigned char character : value) {
    if (!g_ascii_isalnum(character) && character != '.' && character != '_' &&
        character != ':' && character != '-')
      return false;
  }
  return true;
}

bool Integer(const std::string& value, int64_t minimum, int64_t maximum,
    int64_t* output = nullptr) {
  if (value.empty() || value.size() > 20)
    return false;
  for (unsigned char character : value) {
    if (character < '0' || character > '9')
      return false;
  }
  errno = 0;
  char* end = nullptr;
  long long result = strtoll(value.c_str(), &end, 10);
  if (errno || !end || *end || result < minimum || result > maximum)
    return false;
  if (output)
    *output = result;
  return true;
}
}  // namespace

bool IsDefinitionField(const std::string& key) {
  return key == "definition" || key == "package" || key == "app" ||
      key == "enforcer" || key == "policy_version" || key == "text_revision" ||
      key == "level" || key == "modes" || key == "default_locale" ||
      key == "retention_ms" || key.compare(0, 8, "message.") == 0 ||
      localization::IsDefinitionField(key);
}

bool ValidateDefinition(const Message& definition, std::string* error) {
  for (const char* key : {"definition", "package", "app", "enforcer", "default_locale"}) {
    if (!Identifier(Get(definition, key)))
      return Error(error, "invalid definition identity");
  }
  if (!Integer(Get(definition, "policy_version"), 1, INT32_MAX) ||
      !Integer(Get(definition, "text_revision"), 1, INT32_MAX))
    return Error(error, "invalid definition revision");
  int64_t level = 0;
  if (!Integer(Get(definition, "level"), 0, 3, &level))
    return Error(error, "invalid consent level");
  auto modes = Get(definition, "modes");
  if (modes.empty())
    return Error(error, "allowed grant modes required");
  std::set<std::string> seen;
  size_t start = 0;
  do {
    size_t end = modes.find(',', start);
    auto mode = modes.substr(start, end == std::string::npos ? end : end - start);
    if ((mode != "ONCE" && mode != "SESSION" && mode != "TIMED" && mode != "PERSISTENT") ||
        !seen.insert(mode).second)
      return Error(error, "invalid allowed grant mode");
    if (level == 3 && mode != "ONCE")
      return Error(error, "level 3 permits ONCE only in initial policy");
    if (end == std::string::npos)
      break;
    start = end + 1;
  } while (true);
  auto retention = definition.find("retention_ms");
  if (retention != definition.end() && !Integer(retention->second, 0, 86400000))
    return Error(error, "retention outside policy bounds");
  size_t messages = 0;
  for (const auto& field : definition) {
    if (field.first.compare(0, 8, "message.") != 0)
      continue;
    if (field.second.size() > 4096 || field.second.empty() ||
        !g_utf8_validate(field.second.data(), field.second.size(), nullptr))
      return Error(error, "invalid localized message");
    ++messages;
  }
  auto prefix = "message." + Get(definition, "default_locale");
  if (messages < 2 || messages > 32 || Get(definition, prefix + ".title").empty() ||
      Get(definition, prefix + ".body").empty())
    return Error(error, "default locale title and body required");
  return localization::ValidateDefinition(definition, error);
}

}  // namespace registration
}  // namespace consent
