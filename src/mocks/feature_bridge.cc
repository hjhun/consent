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
#include "feature_bridge.h"
#include "feature_common.hh"
#include "consent/endpoint.hh"

#include <sys/socket.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <grp.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace {
int Call(consent::Message request, char** output) noexcept {
  if (!output) return -EINVAL;
  *output = nullptr;
  try {
    int parent = consent_mock::Protected(consent_mock::kFeatureDirectory, true);
    if (parent < 0) return -EACCES;
    close(parent);
    struct stat info{};
    group* users = getgrnam("users");
    if (!users || lstat(consent_mock::kFeatureSocket, &info) || !S_ISSOCK(info.st_mode) ||
        info.st_gid != users->gr_gid ||
        info.st_uid != 0 || (info.st_mode & 0777) != 0660) return -EACCES;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -errno;
    struct Socket { int fd; ~Socket() { close(fd); } } socket{fd};
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, consent_mock::kFeatureSocket);
    int connection = consent_mock::ConnectSocket(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address), 3000);
    if (connection) return connection;
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &length) || length != sizeof(credentials)) return -EACCES;
    char label[1024]{};
    length = sizeof(label);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &length) || !length || length > sizeof(label)) return -EACCES;
    sockaddr_un peer_address{};
    socklen_t address_size = sizeof(peer_address);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer_address), &address_size) ||
        !consent::MatchActivatedPeer(credentials.pid, credentials.uid, peer_address, address_size,
            label, length, consent_mock::kFeatureSocket, "System::Privileged")) return -EACCES;
    struct stat current{};
    if (lstat(consent_mock::kFeatureSocket, &current) || current.st_dev != info.st_dev ||
        current.st_ino != info.st_ino || current.st_uid != 0 || current.st_gid != info.st_gid || !S_ISSOCK(current.st_mode) ||
        (current.st_mode & 0777) != 0660) return -EACCES;
    request["v"] = "1";
    request["id"] = "1";
    int status = consent_mock::SendFrame(fd, request);
    consent::Message response;
    if (!status) status = consent_mock::ReceiveFrame(fd, &response, 8000);
    if (status) return status;
    if (lstat(consent_mock::kFeatureSocket, &current) || current.st_dev != info.st_dev ||
        current.st_ino != info.st_ino || consent::Get(response, "method") != "reply" ||
        consent::Get(response, "id") != "1") return -EACCES;
    status = static_cast<int>(consent::Number(response, "status", -EPROTO));
    if (status) return status;
    const auto json = consent::Get(response, "json");
    if (json.empty() || json.size() > 8192) return -EPROTO;
    *output = strdup(json.c_str());
    return *output ? 0 : -ENOMEM;
  } catch (const std::bad_alloc&) { return -ENOMEM; }
  catch (...) { return -EIO; }
}
int Mutation(const char* method, const char* epoch, const char* catalog, const char* revision, const char* command,
    consent::Message request, char** output) {
  if (output) *output = nullptr;
  if (!epoch || strnlen(epoch, 129) > 128 || !consent_mock::Identifier(epoch) || !catalog || strnlen(catalog, 65) != 64 || !revision || !command || strnlen(revision, 32) >= 32 ||
      strnlen(command, 129) > 128 || !consent_mock::Identifier(command)) return -EINVAL;
  int64_t value;
  if (!consent::ParseNumber(revision, &value) || value < 1 || revision != std::to_string(value)) return -EINVAL;
  request["method"] = method;
  request["coordinator_epoch"] = epoch;
  request["catalog_revision"] = catalog;
  request["expected_revision"] = revision;
  request["command_id"] = command;
  return Call(std::move(request), output);
}
}  // namespace
extern "C" {
int consent_feature_catalog(char** json) {
  try { return Call({{"method", "catalog"}}, json); }
  catch (...) { if (json) *json = nullptr; return -ENOMEM; }
}
int consent_feature_status(char** json) {
  try { return Call({{"method", "status"}}, json); }
  catch (...) { if (json) *json = nullptr; return -ENOMEM; }
}
int consent_feature_select(const char* ids, const char* mode, unsigned duration,
    const char* epoch, const char* catalog, const char* revision, const char* command, char** json) {
  if (json) *json = nullptr;
  if (!ids || !mode || strnlen(ids, 1025) > 1024 || strnlen(mode, 16) >= 16) return -EINVAL;
  try { return Mutation("select", epoch, catalog, revision, command,
      {{"selected", ids}, {"grant_mode", mode}, {"duration_ms", std::to_string(duration)}}, json); }
  catch (...) { return -ENOMEM; }
}
int consent_feature_preapprove(const char* epoch, const char* catalog, const char* revision, const char* command, char** json) {
  try { return Mutation("preapprove", epoch, catalog, revision, command, {}, json); }
  catch (...) { if (json) *json = nullptr; return -ENOMEM; }
}
int consent_feature_task(const char* task, const char* mode, const char* epoch, const char* catalog, const char* revision, const char* command, char** json) {
  if (json) *json = nullptr;
  if (!task || !mode || strnlen(mode, 16) >= 16 || strnlen(task, 129) > 128) return -EINVAL;
  try { return Mutation("task", epoch, catalog, revision, command, {{"task", task}, {"task_mode", mode}}, json); }
  catch (...) { return -ENOMEM; }
}
void consent_feature_free(char* json) { free(json); }
}
