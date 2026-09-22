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
#ifndef CONSENT_MOCK_FEATURE_COMMON_HH_
#define CONSENT_MOCK_FEATURE_COMMON_HH_

#include "common/message.hh"

#include <sys/types.h>
#include <sys/socket.h>
#include <set>
#include <string>
#include <vector>

namespace consent_mock {
using consent::Message;
constexpr const char* kFeatureDirectory = "/opt/var/lib/consent-feature-runtime";
constexpr const char* kFeatureSocket = "/opt/var/lib/consent-feature-runtime/argo.sock";
#ifndef CONSENT_FEATURE_ARGO_PATH
#define CONSENT_FEATURE_ARGO_PATH "/usr/libexec/consent/poc/consent-mock-argo"
#endif
#ifndef CONSENT_FEATURE_WORKER_PREFIX
#define CONSENT_FEATURE_WORKER_PREFIX "/usr/libexec/consent/poc/consent-mock-"
#endif
constexpr const char* kArgoPath = CONSENT_FEATURE_ARGO_PATH;
constexpr const char* kWorkerPrefix = CONSENT_FEATURE_WORKER_PREFIX;

struct Feature {
  std::string id;
  std::string title_en;
  std::string title_ko;
  std::string description_en;
  std::string description_ko;
  std::string provider;
  std::string worker;
  Message row;
};
const std::vector<Feature>& Catalog();
std::string CatalogRevision();
Message ExpandedCalendarRow();
const Feature* FindFeature(const std::string& id);
bool ParseSelection(const std::string& input, std::set<std::string>* result);
bool Identifier(const std::string& input);
std::string Json(const std::string& value);
std::string JsonFields(const Message& values);
std::string ContextDigest(const Message& values);
std::string CatalogJson(const std::string& revision, const std::string& epoch = "");
int Protected(const std::string& path, bool directory = false);
int ConnectSocket(int fd, const sockaddr* address, socklen_t size, int timeout_ms);
int VerifyWorkerParent(int fd, pid_t expected_pid);
int CreateWorkerChannel(int sockets[2]);
void WorkerDiagnostic(const char* stage, int status, pid_t child = 0,
    const char* generated_path = nullptr) noexcept;
int SendFrame(int fd, const Message& message, int timeout_ms = 3000);
int ReceiveFrame(int fd, Message* message, int timeout_ms = 3000);

class Peer final {
 public:
  Peer() = default;
  ~Peer();
  bool Authenticate(int socket, bool ui, int* error = nullptr);
  bool Alive() const;
  std::string Instance() const;
 private:
  pid_t pid_ = -1;
  uid_t uid_ = 0;
  unsigned long long start_ = 0;
  int pid_fd_ = -1;
  dev_t device_ = 0;
  ino_t inode_ = 0;
  std::string executable_;
};
}  // namespace consent_mock

extern "C" int consent_feature_argo_main();
extern "C" int consent_feature_worker_main(const char* role);
#endif  // CONSENT_MOCK_FEATURE_COMMON_HH_
