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
#ifndef CONSENTD_PROFILE_STATE_HH_
#define CONSENTD_PROFILE_STATE_HH_

#include <glib.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace consentd {

struct ProfileBinding {
  std::string subject;
  std::string user;
  std::string profile;
};

// Main-context authority writes; DB/I/O threads validate generation and use.
// The disabled state preserves legacy explicit static delegation.
class ProfileState final {
 public:
  ProfileState();
  ~ProfileState();
  ProfileState(const ProfileState&) = delete;
  ProfileState& operator=(const ProfileState&) = delete;

  bool Configure(std::vector<ProfileBinding> bindings);
  uint64_t Fence();
  bool Activate(uint64_t generation, const std::string& user);
  int Check(const std::string& subject, const std::string& profile) const;
  uint64_t Generation() const noexcept { return generation_.load(); }
  bool Enabled() const noexcept { return enabled_.load(); }
  const std::vector<ProfileBinding>& Bindings() const { return bindings_; }

 private:
  mutable GMutex mutex_;
  std::vector<ProfileBinding> bindings_;
  std::string active_user_;
  bool ready_ = false;
  std::atomic<bool> enabled_{false};
  std::atomic<uint64_t> generation_{0};
};

}  // namespace consentd
#endif  // CONSENTD_PROFILE_STATE_HH_
