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
#ifndef CONSENTD_IDENTITY_HH_
#define CONSENTD_IDENTITY_HH_

#include <sys/types.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace consentd {

struct Peer {
  pid_t pid = -1;
  uid_t uid = static_cast<uid_t>(-1);
  gid_t gid = static_cast<gid_t>(-1);
  std::string identity;
  std::string instance;
  std::string subject;
  std::string profile;
  std::set<std::string> roles;
  std::set<std::string> subjects;
  std::set<std::string> profiles;
  std::set<std::string> enforcers;
  std::set<std::string> packages;
};

struct ProcessIdentity {
  ~ProcessIdentity();
  ProcessIdentity() = default;
  ProcessIdentity(const ProcessIdentity&) = delete;
  ProcessIdentity& operator=(const ProcessIdentity&) = delete;
  int pid_fd = -1;
  unsigned long long start_time = 0;
  dev_t device = 0;
  ino_t inode = 0;
  std::string executable;
  std::string label;
};

// Configuration is loaded once, before accepting clients. Never trust a role
// supplied in a protocol message; the immutable Peer is derived from the socket.
class IdentityPolicy {
 public:
  bool Load(const std::string& path, std::string* error);
  bool Authenticate(int fd, Peer* peer, ProcessIdentity* process,
                    std::string* error) const;
  bool IsAlive(const Peer& peer, const ProcessIdentity& process) const;
  static bool Allows(const Peer& peer, const std::string& method);

 private:
  struct Rule {
    Peer peer;
    std::string executable;
    std::string label;
    dev_t device = 0;
    ino_t inode = 0;
  };
  std::vector<Rule> rules_;
};

// Opens every path component without following symlinks and checks protection.
int OpenProtected(const std::string& path, bool directory = false);
bool GetInstallationIdentity(const std::string& package, const std::string& app,
                             std::string* identity);
bool ValidateInstallation(const std::string& package, const std::string& app,
                          const std::string& identity);
bool ValidatePackageGeneration(const std::string& package,
                               const std::string& generation);

// Offline startup uses a classified read of one protected authority FD.
// Missing/pending/mismatched installations return -ESTALE; malformed sources,
// schema, protection and I/O failures remain errors, never deferred records.
int CheckOfflineAuthority();
int ValidateOfflineInstallation(const std::string& package,
    const std::string& app, const std::string& generation);

}  // namespace consentd
#endif  // CONSENTD_IDENTITY_HH_
