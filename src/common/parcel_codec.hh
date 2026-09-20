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
#ifndef CONSENT_COMMON_PARCEL_CODEC_HH_
#define CONSENT_COMMON_PARCEL_CODEC_HH_

#include <parcel/parcel.hh>
#include <glib.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace consent {
namespace wire {

inline bool ValidString(const std::string& value, size_t maximum) {
  return value.size() <= maximum && value.find('\0') == std::string::npos &&
      g_utf8_validate(value.c_str(), value.size(), nullptr);
}

class Writer final {
 public:
  explicit Writer(tizen_base::Parcel* parcel) : parcel_(parcel) {}
  bool U32(uint32_t value) {
    if (!Room(sizeof(value)))
      return false;
    parcel_->WriteUInt32(value);
    return true;
  }
  bool I32(int32_t value) {
    if (!Room(sizeof(value)))
      return false;
    parcel_->WriteInt32(value);
    return true;
  }
  bool U64(uint64_t value) {
    if (!Room(sizeof(value)))
      return false;
    parcel_->WriteUInt64(value);
    return true;
  }
  bool String(const std::string& value, size_t maximum) {
    if (!ValidString(value, maximum) || !Room(sizeof(uint32_t) + value.size() + 1))
      return false;
    parcel_->WriteString(value);
    return true;
  }
 private:
  bool Room(size_t size) const {
    return parcel_ && parcel_->GetDataSize() <= 65536 &&
        size <= 65536 - parcel_->GetDataSize();
  }
  tizen_base::Parcel* parcel_;
};

class Reader final {
 public:
  explicit Reader(tizen_base::Parcel* parcel) : parcel_(parcel) {}
  bool U32(uint32_t* value) {
    return value && Remaining(sizeof(*value)) && !parcel_->ReadUInt32(value);
  }
  bool I32(int32_t* value) {
    return value && Remaining(sizeof(*value)) && !parcel_->ReadInt32(value);
  }
  bool U64(uint64_t* value) {
    return value && Remaining(sizeof(*value)) && !parcel_->ReadUInt64(value);
  }
  bool String(std::string* value, size_t maximum) {
    uint32_t length = 0;
    if (!value || !U32(&length) || length == 0 || length - 1 > maximum ||
        !Remaining(length))
      return false;
    // Parcel::ReadString allocates from an untrusted length and assumes NUL.
    // Read the checked span without allocation, then validate it explicitly.
    const char* bytes = static_cast<const char*>(parcel_->ReadPtr(length));
    if (!bytes || bytes[length - 1] != '\0' ||
        memchr(bytes, '\0', length - 1) ||
        !g_utf8_validate(bytes, length - 1, nullptr))
      return false;
    value->assign(bytes, length - 1);
    return true;
  }
 private:
  bool Remaining(size_t size) const {
    if (!parcel_ || parcel_->GetReader() > parcel_->GetDataSize())
      return false;
    return size <= parcel_->GetDataSize() - parcel_->GetReader();
  }
  tizen_base::Parcel* parcel_;
};

}  // namespace wire
}  // namespace consent
#endif  // CONSENT_COMMON_PARCEL_CODEC_HH_
