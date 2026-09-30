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
#include "profile_state.hh"

#include <cerrno>
#include <set>
#include <utility>

#include "common/resource.hh"

namespace consentd {
namespace {

bool Identifier(const std::string& value) {
  if (value.empty() || value.size() > 256)
    return false;
  for (unsigned char ch : value) {
    if (!g_ascii_isalnum(ch) && ch != '.' && ch != '_' && ch != ':' &&
        ch != '-')
      return false;
  }
  return true;
}

}  // namespace

ProfileState::ProfileState() {
  g_mutex_init(&mutex_);
}

ProfileState::~ProfileState() {
  g_mutex_clear(&mutex_);
}

bool ProfileState::Configure(std::vector<ProfileBinding> bindings) {
  if (enabled_ || bindings.empty() || bindings.size() > 64)
    return false;
  std::set<std::pair<std::string, std::string>> profiles;
  std::set<std::pair<std::string, std::string>> users;
  for (const auto& binding : bindings) {
    if (!Identifier(binding.subject) || !Identifier(binding.profile) ||
        binding.user.size() >= 20 ||
        (!binding.user.empty() && !Identifier(binding.user)) ||
        !profiles.emplace(binding.subject, binding.profile).second ||
        !users.emplace(binding.subject, binding.user).second)
      return false;
  }
  consent::MutexLock lock(mutex_);
  bindings_ = std::move(bindings);
  enabled_ = true;
  ++generation_;
  return true;
}

uint64_t ProfileState::Fence() {
  consent::MutexLock lock(mutex_);
  ready_ = false;
  return ++generation_;
}

bool ProfileState::Activate(uint64_t generation, const std::string& user) {
  consent::MutexLock lock(mutex_);
  if (!enabled_ || generation != generation_)
    return false;
  bool known = false;
  for (const auto& binding : bindings_)
    known = known || binding.user == user;
  if (!known)
    return false;
  active_user_ = user;
  ready_ = true;
  return true;
}

int ProfileState::Check(const std::string& subject,
                        const std::string& profile) const {
  if (!enabled_)
    return 0;
  consent::MutexLock lock(mutex_);
  for (const auto& binding : bindings_) {
    if (binding.subject == subject && binding.profile == profile) {
      if (!ready_)
        return -EBUSY;
      return binding.user == active_user_ ? 0 : -EACCES;
    }
  }
  return -EACCES;
}

}  // namespace consentd
