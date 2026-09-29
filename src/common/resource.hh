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

#ifndef CONSENT_COMMON_RESOURCE_HH_
#define CONSENT_COMMON_RESOURCE_HH_

#include <glib.h>
#include <unistd.h>

#include <atomic>
#include <memory>

namespace consent {

class Descriptor final {
 public:
  explicit Descriptor(int value = -1) noexcept : value_(value) {}
  ~Descriptor() {
    if (value_ >= 0)
      close(value_);
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int Get() const noexcept { return value_; }
  void Reset(int value = -1) noexcept {
    if (value_ >= 0)
      close(value_);
    value_ = value;
  }
  int Release() noexcept {
    int value = value_;
    value_ = -1;
    return value;
  }

 private:
  int value_;
};

struct SourceDeleter {
  void operator()(GSource* source) const noexcept {
    if (source) {
      g_source_destroy(source);
      g_source_unref(source);
    }
  }
};
using Source = std::unique_ptr<GSource, SourceDeleter>;

class MutexLock final {
 public:
  explicit MutexLock(GMutex& mutex) noexcept : mutex_(mutex) {
    g_mutex_lock(&mutex_);
  }
  ~MutexLock() { g_mutex_unlock(&mutex_); }
  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;

 private:
  GMutex& mutex_;
};

// Roll back a reservation unless ownership has transferred to a live job.
class Admission final {
 public:
  Admission(std::atomic<unsigned>& count, unsigned limit) noexcept
      : count_(count) {
    if (count_.fetch_add(1) < limit)
      held_ = true;
    else
      --count_;
  }
  ~Admission() {
    if (held_)
      --count_;
  }
  Admission(const Admission&) = delete;
  Admission& operator=(const Admission&) = delete;
  bool Accepted() const noexcept { return held_; }
  void Commit() noexcept { held_ = false; }

 private:
  std::atomic<unsigned>& count_;
  bool held_ = false;
};

}  // namespace consent
#endif  // CONSENT_COMMON_RESOURCE_HH_
