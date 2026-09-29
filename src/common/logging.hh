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
#ifndef CONSENT_COMMON_LOGGING_HH_
#define CONSENT_COMMON_LOGGING_HH_

#include <array>
#include <charconv>
#include <string_view>
#include <type_traits>

#ifndef CONSENT_LOG_TAG
#define CONSENT_LOG_TAG "CONSENT"
#endif

namespace consent {
namespace logging {
enum class Level { DEBUG, INFO, WARNING, ERROR };
using Sink = void (*)(Level, const char*, const char*);
void Write(Level level, const char* tag, const char* text) noexcept;

constexpr bool Enabled(Level level) noexcept {
#ifdef CONSENT_DEBUG_LOGGING
  (void)level;
  return true;
#else
  return level != Level::DEBUG;
#endif
}

// Fixed storage keeps error logging independent of C++ allocation success.
class Line final {
 public:
  Line(Level level, const char* tag, const char* file, const char* function,
       unsigned line, Sink sink = Write) noexcept;
  ~Line() noexcept;
  Line(const Line&) = delete;
  Line& operator=(const Line&) = delete;
  Line& operator<<(std::string_view value) noexcept;
  Line& operator<<(const char* value) noexcept {
    return *this << std::string_view(value ? value : "(null)");
  }
  Line& operator<<(char value) noexcept {
    return *this << std::string_view(&value, 1);
  }
  template <typename T, typename = std::enable_if_t<std::is_integral<T>::value>>
  Line& operator<<(T value) noexcept {
    std::array<char, 32> number{};
    if constexpr (std::is_same<T, bool>::value) {
      return *this << (value ? "1" : "0");
    } else {
      auto end =
          std::to_chars(number.data(), number.data() + number.size(), value);
      if (end.ec == std::errc())
        *this << std::string_view(number.data(), end.ptr - number.data());
      return *this;
    }
  }

 private:
  Level level_;
  const char* tag_;
  Sink sink_;
  std::array<char, 2048> buffer_{};
  size_t size_ = 0;
};
}  // namespace logging
}  // namespace consent

#define LOG(level)                                                        \
  for (bool consent_log_once =                                            \
           consent::logging::Enabled(consent::logging::Level::level);     \
       consent_log_once; consent_log_once = false)                        \
  consent::logging::Line(consent::logging::Level::level, CONSENT_LOG_TAG, \
                         __FILE__, __func__, __LINE__)

#endif  // CONSENT_COMMON_LOGGING_HH_
