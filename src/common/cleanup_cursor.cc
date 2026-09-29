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
#include "cleanup_cursor.hh"

#include <glib.h>

#include <array>
#include <memory>

namespace consent {
namespace {
constexpr size_t kIdentitySize = 48;
constexpr size_t kScopeSize = 64;
constexpr size_t kTokenSize = 2 + 3 * kIdentitySize + kScopeSize + 3;

bool Hex(const std::string& value, size_t size) {
  if (value.size() != size)
    return false;
  for (char byte : value) {
    if ((byte < '0' || byte > '9') && (byte < 'a' || byte > 'f'))
      return false;
  }
  return true;
}
}  // namespace

bool CleanupCursor::Parse(const std::string& token, CleanupCursor* cursor) {
  // Version and four fixed-width lowercase hexadecimal fields, fully consumed.
  if (!cursor || token.size() != kTokenSize || token.compare(0, 2, "1:") != 0)
    return false;
  std::array<std::string, 4> fields;
  size_t offset = 2;
  for (size_t i = 0; i < fields.size(); ++i) {
    size_t length = i == 1 ? kScopeSize : kIdentitySize;
    fields[i] = token.substr(offset, length);
    if (!Hex(fields[i], length))
      return false;
    offset += length;
    if (i + 1 < fields.size() && token[offset++] != ':')
      return false;
  }
  if (offset != token.size() || fields[2] >= fields[3])
    return false;
  *cursor = {fields[0], fields[1], fields[2], fields[3]};
  return true;
}

std::string CleanupCursor::Encode() const {
  return "1:" + incarnation + ":" + scope + ":" + last + ":" + upper;
}

std::string CleanupScope(const std::string& holder, const std::string& instance,
    const std::string& subject, const std::string& profile, bool reconcile) {
  std::string input;
  for (const auto* field : {&holder, &instance, &subject, &profile})
    input += std::to_string(field->size()) + ":" + *field;
  input += reconcile ? "1" : "0";
  std::unique_ptr<gchar, decltype(&g_free)> digest(
      g_compute_checksum_for_data(G_CHECKSUM_SHA256,
          reinterpret_cast<const guchar*>(input.data()), input.size()), g_free);
  if (!digest)
    throw std::bad_alloc();
  return digest.get();
}

}  // namespace consent
