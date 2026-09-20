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
#include "offline_registration.hh"

#include "registration.hh"

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace consent {
namespace offline {
namespace {
class Failure final : public std::runtime_error {
 public:
  Failure(int status, const char* message) : std::runtime_error(message), status_(status) {}
  int Status() const { return status_; }
 private:
  int status_;
};

class Descriptor final {
 public:
  explicit Descriptor(int fd = -1) : fd_(fd) {}
  ~Descriptor() { if (fd_ >= 0) close(fd_); }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  int Get() const { return fd_; }
  int Release() { int result = fd_; fd_ = -1; return result; }
  void Reset(int fd) { if (fd_ >= 0) close(fd_); fd_ = fd; }
 private:
  int fd_;
};

struct DirectoryCloser {
  void operator()(DIR* directory) const { closedir(directory); }
};

void Require(bool condition, int status, const char* message) {
  if (!condition)
    throw Failure(status, message);
}

int SystemError() {
  switch (errno) {
    case ENOENT: return -ENOENT;
    case EACCES: case EPERM: case ELOOP: return -EACCES;
    case ENOSPC: case EDQUOT: return -ENOSPC;
    case ENOMEM: return -ENOMEM;
    case EINVAL: case ENAMETOOLONG: case ENOTDIR: return -EINVAL;
    default: return -EIO;
  }
}

void RequireSystem(bool condition, const char* message) {
  if (!condition)
    throw Failure(SystemError(), message);
}

void Sync(int fd) {
  Require(fsync(fd) == 0, -EIO, "cannot synchronize offline registration state");
}

void ProtectedDirectory(int fd, bool private_leaf = false) {
  struct stat info = {};
  Require(fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == 0 &&
      !(info.st_mode & (private_leaf ? 0027 : 0022)), -EACCES,
      "offline directory is not root protected");
}

void ProtectedFile(int fd, struct stat* info) {
  Require(fstat(fd, info) == 0 && S_ISREG(info->st_mode) && info->st_uid == 0 &&
      info->st_nlink == 1 && !(info->st_mode & 07137), -EACCES,
      "offline file ownership, type, links or permissions rejected");
}

std::vector<std::string> Components(const std::string& path) {
  Require(!path.empty() && path[0] == '/' && path.size() <= 4096 &&
      path.find('\0') == std::string::npos,
      -EINVAL, "absolute image and authority paths required");
  std::vector<std::string> result;
  size_t offset = 1;
  while (offset < path.size()) {
    auto end = path.find('/', offset);
    auto part = path.substr(offset, end == std::string::npos ? end : end - offset);
    Require(!part.empty() && part != "." && part != ".." && part.size() <= 255,
        -EINVAL, "noncanonical image or authority path");
    result.push_back(part);
    Require(result.size() <= 128, -EINVAL, "image path depth exceeded");
    if (end == std::string::npos)
      break;
    Require(end + 1 < path.size(), -EINVAL, "trailing image path separator rejected");
    offset = end + 1;
  }
  return result;
}

int Descend(int parent, const std::vector<std::string>& components, bool create) {
  Descriptor current(fcntl(parent, F_DUPFD_CLOEXEC, 0));
  RequireSystem(current.Get() >= 0, "cannot duplicate image directory");
  ProtectedDirectory(current.Get());
  for (size_t index = 0; index < components.size(); ++index) {
    const auto& component = components[index];
    bool created = false;
    int fd = openat(current.Get(), component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT && create) {
      int status = mkdirat(current.Get(), component.c_str(), 0700);
      created = status == 0;
      RequireSystem(created || errno == EEXIST, "cannot create image authority directory");
      fd = openat(current.Get(), component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    }
    RequireSystem(fd >= 0, "cannot open protected image directory");
    Descriptor child(fd);
    ProtectedDirectory(child.Get());
    if (created) {
      // Fresh image ancestors must be traversable after the authority leaf is
      // prepared for the non-root daemon, regardless of the builder's umask.
      // Never adjust an existing ancestor owned by the surrounding image.
      mode_t mode = index + 1 == components.size() ? 0700 : 0755;
      RequireSystem(fchown(child.Get(), 0, 0) == 0 && fchmod(child.Get(), mode) == 0,
          "cannot set new image directory metadata");
      Sync(child.Get());
    }
    if (create && index + 1 < components.size()) {
      struct stat info = {};
      Require(fstat(child.Get(), &info) == 0 && (info.st_mode & 0005) == 0005, -EACCES,
          "image authority ancestor is not readable and traversable by the target daemon");
    }
    // Repeat this sync on retry too: a prior mkdir may have succeeded before
    // its parent synchronization failed.
    if (create)
      Sync(current.Get());
    current.Reset(child.Release());
  }
  return current.Release();
}

bool Identifier(const std::string& value) {
  if (value.empty() || value.size() > 255)
    return false;
  for (unsigned char ch : value) {
    if (!g_ascii_isalnum(ch) && ch != '.' && ch != '_' && ch != '-')
      return false;
  }
  return true;
}

Message PublicRegistration(const Message& input) {
  Message result;
  for (const auto& field : input) {
    if (field.first == "method") {
      Require(field.second == "register", -EINVAL, "offline handle permits registration only");
      continue;
    }
    Require(registration::IsDefinitionField(field.first) || field.first == "operation_id" ||
        field.first == "expected_generation", -EINVAL, "unsupported offline registration field");
    Require(ValidField(field.first, field.second), -EINVAL, "invalid offline registration field");
    result.insert(field);
  }
  Require(Identifier(Get(result, "operation_id")) && Identifier(Get(result, "expected_generation")),
      -EINVAL, "stable operation and expected installation generation required");
  std::string error;
  Require(registration::ValidateDefinition(result, &error), -EINVAL,
      "offline registration definition rejected");
  return result;
}

std::string RecordName(const Message& value) {
  auto operation = Get(value, "operation_id");
  gchar* checksum = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      reinterpret_cast<const guchar*>(operation.data()), operation.size());
  Require(checksum != nullptr, -ENOMEM, "offline operation fingerprint allocation failed");
  std::unique_ptr<gchar, decltype(&g_free)> owned(checksum, g_free);
  return std::string(checksum) + ".parcel";
}

std::string PayloadDigest(const std::vector<uint8_t>& frame) {
  Require(frame.size() > 4, -EINVAL, "missing canonical offline registration envelope");
  // Hash the canonical native Parcel envelope, excluding its outer BE length.
  // offline_format and payload_sha256 are absent from this canonical envelope.
  gchar* checksum = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      frame.data() + 4, frame.size() - 4);
  Require(checksum != nullptr, -ENOMEM, "offline payload fingerprint allocation failed");
  std::unique_ptr<gchar, decltype(&g_free)> owned(checksum, g_free);
  return checksum;
}

std::vector<uint8_t> RecordPayload(Message value) {
  value["v"] = "1";
  value["id"] = "1";
  value["method"] = "register";
  auto canonical = Encode(value);
  Require(!canonical.empty(), -E2BIG, "offline registration exceeds canonical envelope limits");
  value["payload_sha256"] = PayloadDigest(canonical);
  value["offline_format"] = "1";
  auto payload = Encode(value);
  Require(!payload.empty() && payload.size() <= kMaxRegistrationFileBytes, -E2BIG,
      "offline registration including format metadata exceeds record limits");
  return payload;
}

Message DecodeRecord(const std::vector<uint8_t>& payload, const std::string& name) {
  Message value;
  Require(payload.size() >= 4 && payload.size() <= kMaxRegistrationFileBytes &&
      FrameSize(payload.data()) == payload.size() - 4 &&
      Decode(payload.data() + 4, payload.size() - 4, &value) && Get(value, "method") == "register" &&
      Get(value, "id") == "1", -EINVAL, "malformed offline registration record");
  Require(Get(value, "offline_format") == "1", -EINVAL, "unsupported offline registration format");
  std::string digest = Get(value, "payload_sha256");
  Require(digest.size() == 64 && RegistrationName(digest + ".parcel"), -EINVAL,
      "invalid offline payload SHA-256 encoding");
  Message canonical = value;
  canonical.erase("offline_format");
  canonical.erase("payload_sha256");
  auto canonical_payload = Encode(canonical);
  Require(!canonical_payload.empty() && PayloadDigest(canonical_payload) == digest, -EINVAL,
      "offline payload fingerprint mismatch");
  canonical.erase("v");
  canonical.erase("id");
  canonical.erase("method");
  auto registration = PublicRegistration(canonical);
  Require(RecordName(registration) == name, -EINVAL, "offline record operation fingerprint mismatch");
  return registration;
}

std::vector<uint8_t> ReadFile(int directory, const std::string& name) {
  Descriptor file(openat(directory, name.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
  RequireSystem(file.Get() >= 0, "cannot read offline registration record");
  struct stat info = {};
  ProtectedFile(file.Get(), &info);
  Require(info.st_size >= 0 && info.st_size <= static_cast<off_t>(kMaxRegistrationFileBytes),
      -E2BIG, "offline registration record exceeds 64 KiB");
  std::vector<uint8_t> bytes(static_cast<size_t>(info.st_size));
  size_t offset = 0;
  while (offset < bytes.size()) {
    ssize_t count = read(file.Get(), bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    Require(count > 0, -EIO, "offline registration record is truncated");
    offset += static_cast<size_t>(count);
  }
  uint8_t trailing;
  Require(read(file.Get(), &trailing, 1) == 0, -EIO, "offline registration record changed during read");
  return bytes;
}

struct Record {
  std::string name;
  std::vector<uint8_t> bytes;
  Message registration;
};

std::vector<Record> Scan(int directory, bool remove_temporary) {
  int duplicate = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  RequireSystem(duplicate >= 0, "cannot scan offline registrations");
  DIR* raw = fdopendir(duplicate);
  if (!raw)
    close(duplicate);
  Require(raw != nullptr, -EIO, "cannot iterate offline registrations");
  std::unique_ptr<DIR, DirectoryCloser> entries(raw);
  size_t entry_count = 0;
  size_t bytes = 0;
  std::vector<Record> records;
  bool removed = false;
  while (true) {
    errno = 0;
    auto* entry = readdir(entries.get());
    if (!entry) {
      Require(errno == 0, -EIO, "offline registration scan failed");
      break;
    }
    std::string name(entry->d_name);
    if (name == "." || name == "..")
      continue;
    Require(++entry_count <= kMaxRegistrationEntries, -ENOSPC, "offline directory entry limit exceeded");
    bool temporary = PendingRegistrationName(name);
    Require(temporary || RegistrationName(name), -EINVAL, "unexpected offline registration file");
    auto payload = ReadFile(directory, name);
    bytes += payload.size();
    Require(bytes <= kMaxRegistrationBytes, -ENOSPC, "offline registration storage limit exceeded");
    if (temporary) {
      if (remove_temporary) {
        RequireSystem(unlinkat(directory, name.c_str(), 0) == 0, "cannot retire interrupted offline write");
        removed = true;
      }
      continue;
    }
    Require(records.size() < kMaxRegistrations, -ENOSPC, "offline registration count limit exceeded");
    Message value = DecodeRecord(payload, name);
    records.push_back({name, std::move(payload), std::move(value)});
  }
  if (removed)
    Sync(directory);
  return records;
}

int RegistrationDirectory(int authority, bool create) {
  bool created = false;
  if (create) {
    created = mkdirat(authority, kRegistrationDirectory, 0700) == 0;
    RequireSystem(created || errno == EEXIST, "cannot create offline registration spool");
  }
  Descriptor directory(openat(authority, kRegistrationDirectory,
      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  if (directory.Get() < 0 && errno == ENOENT && !create)
    return -1;
  RequireSystem(directory.Get() >= 0, "cannot open offline registration spool");
  ProtectedDirectory(directory.Get(), true);
  if (created) {
    RequireSystem(fchown(directory.Get(), 0, 0) == 0 && fchmod(directory.Get(), 0700) == 0,
        "cannot set new spool metadata");
    Sync(directory.Get());
  }
  if (create)
    Sync(authority);
  return directory.Release();
}
}  // namespace

ImageRoot::~ImageRoot() {
  // Closing inherited descriptors does not unlock the parent's flock; never
  // issue LOCK_UN in a fork child against the shared open file description.
  if (lifecycle_ >= 0)
    close(lifecycle_);
  if (directory_ >= 0)
    close(directory_);
}

bool ImageRoot::IsCurrentProcess() const { return pid_ == getpid(); }
bool ImageRoot::IsOwner() const { return owner_ == g_thread_self(); }

int ImageRoot::Open(const std::string& image_root, const std::string& authority_path) {
  try {
    Require(directory_ < 0 && lifecycle_ < 0, -EINVAL, "image root is already open");
    Require(getuid() == 0 && geteuid() == 0, -EACCES, "offline image construction requires root");
    auto image_components = Components(image_root);
    auto authority_components = Components(authority_path);
    Require(!authority_components.empty(), -EINVAL, "authority directory cannot be image root");
    Descriptor host(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    RequireSystem(host.Get() >= 0, "cannot open host root");
    Descriptor image(Descend(host.Get(), image_components, false));
    Descriptor directory(Descend(image.Get(), authority_components, true));
    ProtectedDirectory(directory.Get(), true);
    int lifecycle_fd = openat(directory.Get(), "lifecycle.lock",
        O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0600);
    bool created = lifecycle_fd >= 0;
    if (lifecycle_fd < 0 && errno == EEXIST)
      lifecycle_fd = openat(directory.Get(), "lifecycle.lock",
          O_RDWR | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    Descriptor lifecycle(lifecycle_fd);
    RequireSystem(lifecycle.Get() >= 0, "cannot open image lifecycle lock");
    struct stat info = {};
    ProtectedFile(lifecycle.Get(), &info);
    Require(flock(lifecycle.Get(), LOCK_EX | LOCK_NB) == 0, -EBUSY,
        "daemon, installer or another image writer is active");
    if (created)
      RequireSystem(fchown(lifecycle.Get(), 0, 0) == 0 && fchmod(lifecycle.Get(), 0600) == 0,
          "cannot set new image lifecycle lock metadata");
    // Do not change existing target UID/GID, labels or file modes here.
    Sync(lifecycle.Get());
    Sync(directory.Get());
    directory_ = directory.Release();
    lifecycle_ = lifecycle.Release();
    pid_ = getpid();
    owner_ = g_thread_self();
    return 0;
  } catch (const Failure& failure) {
    return failure.Status();
  }
}

int RegistrationWriter::Open(const std::string& image_root, const std::string& authority_path) {
  return root_.Open(image_root, authority_path);
}

int RegistrationWriter::Register(const Message& registration) {
  std::string temporary;
  Descriptor directory;
  bool published = false;
  try {
    Require(IsCurrentProcess() && IsOwner(), -EINVAL, "offline handle belongs to another process or thread");
    Require(getuid() == 0 && geteuid() == 0, -EACCES, "offline registration requires root");
    Message value = PublicRegistration(registration);
    std::string name = RecordName(value);
    auto payload = RecordPayload(std::move(value));
    directory.Reset(RegistrationDirectory(root_.DirectoryFd(), true));
    auto records = Scan(directory.Get(), true);
    size_t bytes = 0;
    for (const auto& record : records) {
      bytes += record.bytes.size();
      if (record.name == name) {
        Require(record.bytes == payload, -EEXIST, "offline operation payload changed");
        published = true;
        Descriptor existing(openat(directory.Get(), name.c_str(),
            O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
        RequireSystem(existing.Get() >= 0, "cannot synchronize existing offline record");
        struct stat info = {};
        ProtectedFile(existing.Get(), &info);
        Sync(existing.Get());
        Sync(directory.Get());
        Sync(root_.DirectoryFd());
        return 0;
      }
    }
    Require(records.size() < kMaxRegistrations && bytes + payload.size() <= kMaxRegistrationBytes,
        -ENOSPC, "offline registration spool is full");
    gchar* uuid = g_uuid_string_random();
    Require(uuid != nullptr, -ENOMEM, "offline temporary name allocation failed");
    std::unique_ptr<gchar, decltype(&g_free)> owned(uuid, g_free);
    temporary = std::string(".pending-") + uuid;
    Descriptor file(openat(directory.Get(), temporary.c_str(),
        O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600));
    RequireSystem(file.Get() >= 0, "cannot create offline registration record");
    RequireSystem(fchown(file.Get(), 0, 0) == 0 && fchmod(file.Get(), 0600) == 0,
        "cannot set new offline record metadata");
    size_t offset = 0;
    while (offset < payload.size()) {
      ssize_t count = write(file.Get(), payload.data() + offset, payload.size() - offset);
      if (count < 0 && errno == EINTR)
        continue;
      RequireSystem(count > 0, "offline registration write failed");
      offset += static_cast<size_t>(count);
    }
    Sync(file.Get());
    RequireSystem(close(file.Release()) == 0, "cannot finish offline registration record");
    RequireSystem(renameat(directory.Get(), temporary.c_str(), directory.Get(), name.c_str()) == 0,
        "cannot publish offline registration record");
    published = true;
    temporary.clear();
    Sync(directory.Get());
    Sync(root_.DirectoryFd());
    return 0;
  } catch (const Failure& failure) {
    if (!temporary.empty() && directory.Get() >= 0)
      unlinkat(directory.Get(), temporary.c_str(), 0);
    return published ? -EINPROGRESS : failure.Status();
  } catch (...) {
    if (!temporary.empty() && directory.Get() >= 0)
      unlinkat(directory.Get(), temporary.c_str(), 0);
    if (published)
      return -EINPROGRESS;
    throw;
  }
}

int LoadRegistrations(const std::string& authority_dir,
    std::vector<Message>* registrations, std::string* error) {
  if (!registrations)
    return -EINVAL;
  registrations->clear();
  try {
    Descriptor host(open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    RequireSystem(host.Get() >= 0, "cannot open filesystem root");
    Descriptor authority(Descend(host.Get(), Components(authority_dir), false));
    ProtectedDirectory(authority.Get(), true);
    Descriptor directory(RegistrationDirectory(authority.Get(), false));
    if (directory.Get() < 0)
      return 0;
    auto records = Scan(directory.Get(), false);
    std::vector<Message> result;
    for (auto& record : records)
      result.push_back(std::move(record.registration));
    std::sort(result.begin(), result.end(), [](const Message& left, const Message& right) {
      auto key = [](const Message& value) {
        return std::make_tuple(Get(value, "package"), Get(value, "app"),
            Get(value, "definition"), Get(value, "expected_generation"),
            Number(value, "policy_version"), Number(value, "text_revision"),
            Get(value, "operation_id"));
      };
      return key(left) < key(right);
    });
    registrations->swap(result);
    return 0;
  } catch (const Failure& failure) {
    if (error)
      *error = failure.what();
    return failure.Status();
  }
}

}  // namespace offline
}  // namespace consent
