/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
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
#include "profile_config.hh"

#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include "common/message.hh"

namespace consentd {
namespace {

bool String(GKeyFile* file, const char* group, const char* key,
            std::string* value) {
  GError* error = nullptr;
  std::unique_ptr<gchar, decltype(&g_free)> text(
      g_key_file_get_string(file, group, key, &error), g_free);
  bool valid = !error && text;
  g_clear_error(&error);
  if (!valid)
    return false;
  *value = text.get();
  return true;
}

}  // namespace

bool ReadProfileConfig(int fd, ProfileConfig* config) {
  ProfileConfig parsed;
  std::string data;
  char buffer[1024];
  for (;;) {
    ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0 || data.size() + std::max<ssize_t>(count, 0) > 16384)
      return false;
    if (!count)
      break;
    data.append(buffer, count);
  }
  if (data.find('\0') != std::string::npos)
    return false;
  std::istringstream lines(data);
  std::set<std::string> sections;
  std::set<std::pair<std::string, std::string>> names;
  std::string group;
  std::string line;
  while (std::getline(lines, line)) {
    gchar* trimmed = g_strstrip(line.data());
    if (!*trimmed || *trimmed == '#' || *trimmed == ';')
      continue;
    if (*trimmed == '[') {
      const size_t size = strlen(trimmed);
      if (size < 3 || trimmed[size - 1] != ']')
        return false;
      group.assign(trimmed + 1, size - 2);
      if (!sections.insert(group).second)
        return false;
    } else {
      char* equal = strchr(trimmed, '=');
      if (!equal || group.empty())
        return false;
      *equal = 0;
      if (!names.emplace(group, g_strstrip(trimmed)).second)
        return false;
    }
  }

  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(
      g_key_file_new(), g_key_file_unref);
  if (!g_key_file_load_from_data(file.get(), data.data(), data.size(),
                                 G_KEY_FILE_NONE, nullptr))
    return false;
  auto exact_keys = [&](const char* group,
                        const std::set<std::string>& expected) {
    gsize count = 0;
    std::unique_ptr<gchar*, decltype(&g_strfreev)> keys(
        g_key_file_get_keys(file.get(), group, &count, nullptr), g_strfreev);
    if (count != expected.size())
      return false;
    std::set<std::string> actual;
    for (gsize i = 0; i < count; ++i)
      actual.insert(keys.get()[i]);
    return actual == expected;
  };
  if (!exact_keys("authority", {"mode", "session_uid"}))
    return false;
  std::string mode;
  if (!String(file.get(), "authority", "mode", &mode))
    return false;
  if (mode != "sessiond" && mode != "fixture")
    return false;
  parsed.fixture = mode == "fixture";
  std::string uid;
  if (!String(file.get(), "authority", "session_uid", &uid))
    return false;
  int64_t number = 0;
  if (!consent::ParseNumber(uid, &number) || number <= 0 ||
      number > INT32_MAX || uid != std::to_string(number))
    return false;
  parsed.session_uid = static_cast<int>(number);
  gsize count = 0;
  std::unique_ptr<gchar*, decltype(&g_strfreev)> groups(
      g_key_file_get_groups(file.get(), &count), g_strfreev);
  if (count < 2 || count > 65)
    return false;
  std::vector<ProfileBinding> bindings;
  for (gsize i = 0; i < count; ++i) {
    const std::string group(groups.get()[i]);
    if (group == "authority")
      continue;
    if (group.compare(0, 8, "binding ") != 0 ||
        !exact_keys(group.c_str(), {"subject", "subsession", "profile"}))
      return false;
    ProfileBinding binding;
    if (!String(file.get(), group.c_str(), "subject", &binding.subject) ||
        !String(file.get(), group.c_str(), "subsession", &binding.user) ||
        !String(file.get(), group.c_str(), "profile", &binding.profile))
      return false;
    bindings.push_back(std::move(binding));
  }
  parsed.bindings = std::move(bindings);
  *config = std::move(parsed);
  return true;
}

}  // namespace consentd
