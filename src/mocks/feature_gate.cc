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
#include "feature_gate.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdio>

namespace consent_mock {
namespace {
constexpr int64_t kGateMilliseconds = 30000;

int64_t MonotonicMilliseconds() {
  timespec now{};
  if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
  return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

bool Identifier(const std::string& value) {
  if (value.empty() || value.size() > 128) return false;
  for (unsigned char byte : value)
    if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '-')) return false;
  return true;
}

bool Generation(const std::string& value) {
  if (value.empty() || value.front() < '1' || value.front() > '9') return false;
  int64_t parsed = 0;
  auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size() && parsed > 0;
}

bool Digest(const std::string& value) {
  if (value.size() != 64) return false;
  for (char byte : value)
    if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) return false;
  return true;
}

int SelectedStage(int directory, const char* stage) {
  int fd = openat(directory, "kind", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return -errno;
  struct stat info{};
  if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_uid != 0 || info.st_gid != 0 ||
      info.st_nlink != 1 || (info.st_mode & 07777) != 0600 || info.st_size < 1 || info.st_size > 16) {
    close(fd); return -EACCES;
  }
  char buffer[17]{};
  ssize_t count = read(fd, buffer, sizeof(buffer));
  close(fd);
  if (count != info.st_size) return -EIO;
  std::string value(buffer, static_cast<size_t>(count));
  if (value != "acquisition\n" && value != "reuse\n") return -EINVAL;
  return value == std::string(stage) + '\n' ? 0 : 1;
}

int OpenDirectory() {
  int current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (current < 0) return -errno;
  for (const char* part : {"opt", "var", "lib", "consent-feature-gate"}) {
    struct stat parent{};
    if (fstat(current, &parent) || parent.st_uid != 0 || (parent.st_mode & 0022)) {
      close(current); return -EACCES;
    }
    int next = openat(current, part, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int saved = errno;
    close(current);
    if (next < 0) return -saved;
    current = next;
  }
  struct stat info{};
  if (fstat(current, &info) || info.st_uid != 0 || info.st_gid != 0 ||
      (info.st_mode & 07777) != 0700) { close(current); return -EACCES; }
  return current;
}
}  // namespace

FeatureGate::~FeatureGate() { Cancel(); Close(); }

void FeatureGate::Close() noexcept {
  if (release_ >= 0) close(release_);
  if (directory_ >= 0) close(directory_);
  release_ = -1;
  directory_ = -1;
}

int FeatureGate::Publish(const char* name, const char* state) noexcept {
  int fd = -1;
  try {
    const std::string body = "{\"schema\":1,\"state\":\"" + std::string(state) + "\"," + fields_ + "}\n";
    if (body.size() > 2048) return -E2BIG;
    const std::string temporary = "." + std::string(name) + ".tmp";
    fd = openat(directory_, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -errno;
    size_t offset = 0;
    while (offset < body.size()) {
      ssize_t count = write(fd, body.data() + offset, body.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) { int saved = count < 0 ? errno : EIO; close(fd); return -saved; }
      offset += static_cast<size_t>(count);
    }
    if (fsync(fd)) { int saved = errno; close(fd); return -saved; }
    if (close(fd)) return -errno;
    fd = -1;
    // Expose only fully written, fsynced bytes and never replace old evidence.
    if (linkat(directory_, temporary.c_str(), directory_, name, 0)) return -errno;
    if (unlinkat(directory_, temporary.c_str(), 0)) return -errno;
    if (fsync(directory_)) return -errno;
    return 0;
  } catch (...) {
    if (fd >= 0) close(fd);
    return -ENOMEM;
  }
}

int FeatureGate::Arm(const std::string& receipt, const std::string& operation_id,
    const std::string& feature_id, const std::string& job_id) {
  if (geteuid() != 0) return -EACCES;
  if (!Identifier(receipt)) return -EINVAL;
  return Begin("acquisition", "acquisition-receipt", receipt, operation_id, feature_id,
      job_id, "\"receipt\":\"" + receipt + "\",");
}

int FeatureGate::ArmReuse(const std::string& artifact, const std::string& session,
    const std::string& generation, const std::string& context_digest,
    const std::string& operation_id, const std::string& feature_id,
    const std::string& job_id) {
  if (geteuid() != 0) return -EACCES;
  if (!Identifier(artifact) || !Identifier(session) || !Generation(generation) ||
      !Digest(context_digest)) return -EINVAL;
  return Begin("reuse", "artifact-permit", artifact, operation_id, feature_id, job_id,
      "\"artifact\":\"" + artifact + "\",\"session\":\"" + session +
      "\",\"generation\":\"" + generation + "\",\"context_digest\":\"" + context_digest + "\",");
}

int FeatureGate::Begin(const char* stage, const char* proof_kind, const std::string& proof_id,
    const std::string& operation_id, const std::string& feature_id,
    const std::string& job_id, const std::string& proof_fields) {
  if (armed_) return -EBUSY;
  if (!Identifier(operation_id) ||
      !Identifier(feature_id) || !Identifier(job_id)) return -EINVAL;
  Close();
  directory_ = OpenDirectory();
  if (directory_ < 0) { int status = directory_; directory_ = -1; return status; }
  int selected = SelectedStage(directory_, stage);
  if (selected) { Close(); return selected; }
  release_ = openat(directory_, "release.fifo", O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
  if (release_ < 0) { int status = -errno; Close(); return status; }
  struct stat info{};
  if (fstat(release_, &info) || !S_ISFIFO(info.st_mode) || info.st_uid != 0 ||
      info.st_gid != 0 || info.st_nlink != 1 || (info.st_mode & 07777) != 0600) {
    Close(); return -EACCES;
  }
  unsigned char stale = 0;
  ssize_t count = read(release_, &stale, 1);
  if (count >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { Close(); return -EINVAL; }
  auto now = MonotonicMilliseconds();
  if (now < 0) { Close(); return -EIO; }
  deadline_ = now + kGateMilliseconds;
  fields_ = "\"proof_kind\":\"" + std::string(proof_kind) + "\",\"proof_id\":\"" + proof_id +
      "\"," + proof_fields + "\"operation_id\":\"" + operation_id +
      "\",\"feature_id\":\"" + feature_id + "\",\"job_id\":\"" + job_id +
      "\",\"pid\":" + std::to_string(getpid()) +
      ",\"deadline_monotonic_ms\":" + std::to_string(deadline_);
  int status = Publish("ready.json", "armed");
  if (status) { Close(); return status; }
  armed_ = true;
  return 0;
}

int FeatureGate::Poll() {
  if (!armed_) return -EINVAL;
  auto now = MonotonicMilliseconds();
  if (now < 0 || now >= deadline_) {
    Publish("terminal.json", "expired"); armed_ = false; Close(); return -ETIMEDOUT;
  }
  unsigned char command[2]{};
  ssize_t count = read(release_, command, sizeof(command));
  if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
  if (count != 1 || command[0] != 'R') {
    Publish("terminal.json", "invalid-release"); armed_ = false; Close(); return -EINVAL;
  }
  int status = Publish("terminal.json", "released");
  armed_ = false;
  Close();
  return status ? status : 1;
}

void FeatureGate::Cancel() noexcept {
  if (!armed_) return;
  const auto now = MonotonicMilliseconds();
  Publish("terminal.json", now >= 0 && now < deadline_ ? "cancelled" : "expired");
  armed_ = false;
  Close();
}

}  // namespace consent_mock
