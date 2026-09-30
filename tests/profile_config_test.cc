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
#include "consentd/profile_config.hh"

#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <string>

namespace {

bool Parse(const std::string& text, consentd::ProfileConfig* config) {
  FILE* file = tmpfile();
  assert(file);
  assert(fwrite(text.data(), 1, text.size(), file) == text.size());
  assert(fflush(file) == 0);
  rewind(file);
  bool valid = consentd::ReadProfileConfig(fileno(file), config);
  fclose(file);
  return valid;
}

}  // namespace

int main() {
  const std::string valid =
      "[authority]\nmode=sessiond\nsession_uid=5001\n"
      "[binding A]\nsubject=subject\nsubsession=\nprofile=A\n";
  consentd::ProfileConfig config;
  assert(Parse(valid, &config));
  assert(config.session_uid == 5001 && !config.fixture);
  assert(config.bindings.size() == 1 && config.bindings[0].user.empty());
  for (const auto* key : {"mode", "session_uid", "subject", "subsession",
                          "profile"}) {
    std::string malformed = valid;
    const auto start = malformed.find(std::string(key) + "=");
    const auto end = malformed.find('\n', start);
    malformed.replace(start, end - start, std::string(key) + "=\\x");
    assert(!Parse(malformed, &config));
    // Rejection must not overwrite the previously valid output.
    assert(config.session_uid == 5001 && config.bindings[0].user.empty());
    std::string missing = valid;
    missing.erase(start, end - start + 1);
    assert(!Parse(missing, &config));
  }
  assert(!Parse(valid + "unknown=value\n", &config));
  assert(!Parse(valid + "profile=B\n", &config));
  assert(!Parse(valid + "[binding A]\n", &config));
  assert(!Parse(valid + std::string(1, '\0'), &config));
  assert(!Parse(std::string(16385, '#'), &config));
  puts("PASS checked profile strings and bounded config");
}
