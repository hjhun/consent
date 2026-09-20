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
#ifndef CONSENT_COMMON_MESSAGE_HH_
#define CONSENT_COMMON_MESSAGE_HH_

#include "consent_wire.hh"

#include <glib.h>
#include <cstdint>
#include <climits>
#include <limits>
#include <stdexcept>
#include <map>
#include <string>
#include <vector>

namespace consent {

using Message = std::map<std::string, std::string>;
constexpr size_t kMaxFrameSize = 65536;
constexpr size_t kMaxFields = 256;
constexpr size_t kMaxValueSize = 8192;
constexpr size_t kMaxRequirements = 16;

inline std::string Get(const Message& message, const std::string& key,
    const std::string& fallback = "") {
  auto it = message.find(key);
  return it == message.end() ? fallback : it->second;
}

inline bool ParseNumber(const std::string& value, int64_t* output) {
  if (value.empty() || output == nullptr)
    return false;
  bool negative = value[0] == '-';
  size_t pos = negative ? 1 : 0;
  if (pos == value.size())
    return false;
  uint64_t number = 0;
  uint64_t maximum = static_cast<uint64_t>(INT64_MAX) + (negative ? 1 : 0);
  for (; pos < value.size(); ++pos) {
    if (value[pos] < '0' || value[pos] > '9')
      return false;
    unsigned digit = value[pos] - '0';
    if (number > (maximum - digit) / 10)
      return false;
    number = number * 10 + digit;
  }
  *output = negative ? (number == maximum ? INT64_MIN : -static_cast<int64_t>(number)) :
      static_cast<int64_t>(number);
  return true;
}

inline int64_t Number(const Message& message, const std::string& key,
    int64_t fallback = 0) {
  int64_t value;
  return ParseNumber(Get(message, key), &value) ? value : fallback;
}

inline bool ValidField(const std::string& key, const std::string& value) {
  if (key.empty() || key.size() > 128 || value.size() > kMaxValueSize ||
      value.find('\0') != std::string::npos ||
      !g_utf8_validate(value.c_str(), value.size(), nullptr))
    return false;
  for (unsigned char ch : key) {
    if (!g_ascii_isalnum(ch) && ch != '_' && ch != '.' && ch != '-')
      return false;
  }
  return true;
}

inline uint32_t FrameSize(const uint8_t* header) {
  return (static_cast<uint32_t>(header[0]) << 24) |
      (static_cast<uint32_t>(header[1]) << 16) |
      (static_cast<uint32_t>(header[2]) << 8) | header[3];
}

/* Encode uses the IDL-generated native Parcelable envelope. The owned result
 * includes the four-byte network-order length. Empty means invalid input. */
inline std::vector<uint8_t> Encode(const Message& message) {
  if (message.empty() || message.size() > kMaxFields || Get(message, "v") != "1")
    return {};
  for (const auto& field : message) {
    if (!ValidField(field.first, field.second))
      return {};
  }
  wire::Envelope envelope;
  envelope.version = 1;
  envelope.method = Get(message, "method");
  if (envelope.method.empty() || envelope.method.size() > 128)
    return {};
  envelope.kind = envelope.method == "reply" ? 2 : envelope.method == "event" ? 3 : 1;
  int64_t correlation = -1;
  if (!ParseNumber(Get(message, "id"), &correlation) || correlation < 0 ||
      (envelope.kind != 3 && correlation == 0) ||
      (envelope.kind == 3 && correlation != 0))
    return {};
  envelope.correlation = static_cast<uint64_t>(correlation);
  int64_t status = 0;
  if (envelope.kind == 2) {
    if (!ParseNumber(Get(message, "status"), &status) || status > 0 || status < INT32_MIN)
      return {};
  } else if (message.count("status")) {
    return {};
  }
  envelope.status = static_cast<int32_t>(status);
  // u32 version, u32 kind, u64 correlation, string method, i32 status, u32 count.
  size_t size = 29 + envelope.method.size();
  for (const auto& field : message) {
    if (field.first == "v" || field.first == "id" || field.first == "method" ||
        field.first == "status")
      continue;
    size += 10 + field.first.size() + field.second.size();
    if (size > kMaxFrameSize)
      return {};
    wire::Field value;
    value.key = field.first;
    value.value = field.second;
    envelope.fields.push_back(std::move(value));
  }
  std::vector<uint8_t> frame(size + 4);
  frame[0] = (size >> 24) & 0xff;
  frame[1] = (size >> 16) & 0xff;
  frame[2] = (size >> 8) & 0xff;
  frame[3] = size & 0xff;
  // Borrow already allocated bounded storage. Avoid an unbounded Parcel growth
  // path, and do not require the capacity constructor added in Tizen 11.
  tizen_base::Parcel parcel(frame.data() + 4, static_cast<uint32_t>(size), false, false);
  parcel.Clear();
  parcel.SetByteOrder(true);
  try {
    parcel.WriteParcelable(envelope);
  } catch (const std::invalid_argument&) {
    return {};
  }
  if (parcel.GetDataSize() != size)
    return {};
  return frame;
}

/* Decode only the body. Every native Parcel read is guarded by the generated
 * bounded readers before allocation. Neither ReadString nor ReadCString is
 * used for untrusted data. */
inline bool Decode(const uint8_t* data, size_t size, Message* output) {
  if (!data || !output || size < 29 || size > kMaxFrameSize)
    return false;
  tizen_base::Parcel parcel(data, static_cast<uint32_t>(size), false, false);
  parcel.SetByteOrder(true);
  wire::Envelope envelope;
  try {
    if (parcel.ReadParcelable(&envelope) || !envelope.Valid() ||
        parcel.GetReader() != size)
      return false;
  } catch (const std::invalid_argument&) {
    return false;
  }
  if (envelope.version != 1 || envelope.method.empty() ||
      envelope.correlation > static_cast<uint64_t>(INT64_MAX) ||
      envelope.fields.size() > kMaxFields - (envelope.kind == 2 ? 4 : 3))
    return false;
  if (envelope.kind == 1) {
    if (!envelope.correlation || envelope.status || envelope.method == "reply" ||
        envelope.method == "event")
      return false;
  } else if (envelope.kind == 2) {
    if (!envelope.correlation || envelope.method != "reply" || envelope.status > 0)
      return false;
  } else if (envelope.kind == 3) {
    if (envelope.correlation || envelope.method != "event" || envelope.status)
      return false;
  } else {
    return false;
  }
  Message message{{"v", "1"}, {"id", std::to_string(envelope.correlation)},
      {"method", envelope.method}};
  if (envelope.kind == 2)
    message["status"] = std::to_string(envelope.status);
  for (auto& field : envelope.fields) {
    if (!ValidField(field.key, field.value) || field.key == "status" ||
        !message.emplace(std::move(field.key), std::move(field.value)).second)
      return false;
  }
  *output = std::move(message);
  return true;
}

}  // namespace consent
#endif  // CONSENT_COMMON_MESSAGE_HH_
