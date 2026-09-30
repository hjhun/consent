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
#ifndef CONSENTD_PROFILE_AUTHORITY_HH_
#define CONSENTD_PROFILE_AUTHORITY_HH_

#include <gio/gio.h>

#include <functional>
#include <memory>
#include <string>

#include "profile_state.hh"

namespace consentd {

// Native production always creates a private system-bus connection. The
// injected connection constructor exists only for private adapter tests.
class ProfileAuthority final {
 public:
  using Done = std::function<void(bool)>;
  using Barrier = std::function<void(bool, const std::string&, Done)>;

  ProfileAuthority(std::shared_ptr<ProfileState> state, int session_uid,
                   Barrier barrier);
  ProfileAuthority(std::shared_ptr<ProfileState> state, int session_uid,
                   Barrier barrier, GDBusConnection* test_connection,
                   unsigned timeout_ms);
  ProfileAuthority(std::shared_ptr<ProfileState> state, int session_uid,
                   Barrier barrier, std::string test_address);
  ~ProfileAuthority();
  void Start();
  void Stop() noexcept;

 private:
  class Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace consentd
#endif  // CONSENTD_PROFILE_AUTHORITY_HH_
