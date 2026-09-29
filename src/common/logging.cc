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
#include "logging.hh"

#include <dlog.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace consent {
namespace logging {
namespace {
log_priority Priority(Level level) noexcept {
  switch (level) {
    case Level::DEBUG:
      return DLOG_DEBUG;
    case Level::INFO:
      return DLOG_INFO;
    case Level::WARNING:
      return DLOG_WARN;
    case Level::ERROR:
      return DLOG_ERROR;
  }
  return DLOG_ERROR;
}
}  // namespace

void Write(Level level, const char* tag, const char* text) noexcept {
  const int saved = errno;
  // Constant format preserves literal percent text and source information.
  dlog_print(Priority(level), tag, "%s", text);
  // Preserve service journal and existing event parsers, independently of DLOG.
  std::fprintf(stderr, "%s\n", text);
  errno = saved;
}

Line::Line(Level level, const char* tag, const char* file, const char* function,
           unsigned line, Sink sink) noexcept
    : level_(level), tag_(tag), sink_(sink) {
  const auto* slash = std::strrchr(file, '/');
  *this << '[' << (slash ? slash + 1 : file) << ':' << line << ' ' << function
        << "] ";
}

Line::~Line() noexcept {
  const int saved = errno;
  try {
    sink_(level_, tag_, buffer_.data());
  } catch (...) {
    std::fputs("consent logging backend failed\n", stderr);
  }
  errno = saved;
}

Line& Line::operator<<(std::string_view value) noexcept {
  const auto size = std::min(value.size(), buffer_.size() - 1 - size_);
  std::memcpy(buffer_.data() + size_, value.data(), size);
  size_ += size;
  buffer_[size_] = '\0';
  return *this;
}

}  // namespace logging
}  // namespace consent
