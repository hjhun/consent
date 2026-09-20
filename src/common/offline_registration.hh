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
#ifndef CONSENT_OFFLINE_REGISTRATION_HH_
#define CONSENT_OFFLINE_REGISTRATION_HH_

#include "message.hh"
#include "offline_layout.hh"

#include <sys/types.h>

namespace consent {
namespace offline {

// Root-only image construction. DirectoryFd is borrowed and anchors subsequent
// relative operations. The exclusive nonblocking lifecycle lock stays held
// until destruction; existing ownership, permissions and labels are untouched.
// Existing canonical authority ancestors must permit other-read and execute.
// An interrupted scaffold left at 0700 is rejected until explicitly repaired.
class ImageRoot final {
 public:
  ImageRoot() = default;
  ~ImageRoot();
  ImageRoot(const ImageRoot&) = delete;
  ImageRoot& operator=(const ImageRoot&) = delete;
  int Open(const std::string& image_root,
      const std::string& authority_path = "/opt/var/lib/consent-authority");
  int DirectoryFd() const { return directory_; }
  bool IsCurrentProcess() const;
  bool IsOwner() const;

 private:
  int directory_ = -1;
  int lifecycle_ = -1;
  pid_t pid_ = 0;
  GThread* owner_ = nullptr;
};

class RegistrationWriter final {
 public:
  int Open(const std::string& image_root,
      const std::string& authority_path = "/opt/var/lib/consent-authority");
  // Accepts public definition fields, operation_id, expected_generation and
  // optional method=register only. -EEXIST means a stable operation conflict;
  // -EINPROGRESS means publication occurred but durability is uncertain.
  int Register(const Message& registration);
  bool IsCurrentProcess() const { return root_.IsCurrentProcess(); }
  bool IsOwner() const { return root_.IsOwner(); }

 private:
  ImageRoot root_;
};

// Read-only daemon import. Records contain public fields only, never trusted
// identity claims or transport metadata. An absent registrations directory is
// empty; all other uncertain/malformed state fails without partial output.
// Production callers hold their existing shared/exclusive lifecycle lock.
int LoadRegistrations(const std::string& authority_dir,
    std::vector<Message>* registrations, std::string* error = nullptr);

}  // namespace offline
}  // namespace consent
#endif  // CONSENT_OFFLINE_REGISTRATION_HH_
