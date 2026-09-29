/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights reserved.
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
#include "recovery_state.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <string>

#include "resource.hh"

namespace consent {
namespace recovery {
namespace {
constexpr char kName[] = "bootstrap.receipt";
constexpr size_t kLimit = 4096;

const char* Name(Phase phase) noexcept {
  switch (phase) {
    case Phase::kFresh:
      return "FRESH_UNCLAIMED";
    case Phase::kClaimed:
      return "FRESH_CLAIMED";
    case Phase::kInitialized:
      return "INITIALIZED";
    case Phase::kRecoveryRequired:
      return "RECOVERY_REQUIRED";
    default:
      return nullptr;
  }
}

bool Parse(const std::string& data, Receipt* receipt) {
  const std::string prefix = "schema=1\nphase=";
  if (data.compare(0, prefix.size(), prefix) != 0)
    return false;
  auto next = data.find('\n', prefix.size());
  if (next == std::string::npos)
    return false;
  std::string phase = data.substr(prefix.size(), next - prefix.size());
  for (auto value : {Phase::kFresh, Phase::kClaimed, Phase::kInitialized,
                     Phase::kRecoveryRequired}) {
    if (phase == Name(value))
      receipt->phase = value;
  }
  if (receipt->phase == Phase::kAbsent)
    return false;
  auto unit = data.find("unit=", next + 1);
  if (unit != next + 1)
    return false;
  auto unit_end = data.find('\n', unit);
  if (unit_end == std::string::npos)
    return false;
  receipt->unit = data.substr(unit + 5, unit_end - unit - 5);
  auto invocation = data.find("invocation=", unit_end + 1);
  if (invocation != unit_end + 1)
    return false;
  auto invocation_end = data.find('\n', invocation);
  if (invocation_end == std::string::npos)
    return false;
  receipt->invocation =
      data.substr(invocation + 11, invocation_end - invocation - 11);
  auto nonce = data.find("nonce=", invocation_end + 1);
  if (nonce != invocation_end + 1 || data.back() != '\n')
    return false;
  receipt->nonce = data.substr(nonce + 6, data.size() - nonce - 7);
  if (receipt->unit.empty() || receipt->unit.size() > 128 ||
      receipt->unit.find('\n') != std::string::npos ||
      receipt->unit.find('/') != std::string::npos ||
      receipt->unit.find('=') != std::string::npos)
    return false;
  if (receipt->phase == Phase::kClaimed)
    return HexId(receipt->invocation, 32) && HexId(receipt->nonce, 32);
  return receipt->invocation.empty() && receipt->nonce.empty();
}
}  // namespace

bool HexId(const std::string& value, size_t width) noexcept {
  if (value.size() != width)
    return false;
  for (char ch : value) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
      return false;
  }
  return true;
}

Boot Assess(Phase phase, bool registry, bool database, bool ticket) noexcept {
  if (phase == Phase::kClaimed && ticket && !registry && !database)
    return Boot::kFirst;
  if (phase != Phase::kInitialized)
    return Boot::kReject;
  if (!registry)
    return Boot::kReject;
  return database ? Boot::kExisting : Boot::kRecoverDatabase;
}

bool ReadReceipt(int authority, gid_t group, Receipt* receipt) noexcept {
  if (!receipt || authority < 0)
    return false;
  try {
    *receipt = {};
    Descriptor file(
        openat(authority, kName, O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (file.Get() < 0)
      return errno == ENOENT;
    struct stat info = {};
    if (fstat(file.Get(), &info) < 0 || !S_ISREG(info.st_mode) ||
        info.st_uid != 0 || info.st_gid != group ||
        (info.st_mode & 07777) != 0640 || info.st_nlink != 1 ||
        info.st_size <= 0 || info.st_size > static_cast<off_t>(kLimit))
      return false;
    std::string data(static_cast<size_t>(info.st_size), '\0');
    size_t offset = 0;
    while (offset < data.size()) {
      ssize_t count = read(file.Get(), &data[offset], data.size() - offset);
      if (count < 0 && errno == EINTR)
        continue;
      if (count <= 0)
        return false;
      offset += static_cast<size_t>(count);
    }
    char extra;
    ssize_t count;
    do {
      count = read(file.Get(), &extra, 1);
    } while (count < 0 && errno == EINTR);
    if (count != 0 || !Parse(data, receipt))
      return false;
    return true;
  } catch (...) {
    return false;
  }
}

bool WriteReceipt(int authority, gid_t group, const Receipt& receipt) noexcept {
  try {
    const char* phase = Name(receipt.phase);
    if (authority < 0 || !phase || receipt.unit.empty() ||
        receipt.unit.size() > 128 ||
        receipt.unit.find_first_of("\n/=") != std::string::npos ||
        (receipt.phase == Phase::kClaimed &&
         (!HexId(receipt.invocation, 32) || !HexId(receipt.nonce, 32))) ||
        (receipt.phase != Phase::kClaimed &&
         (!receipt.invocation.empty() || !receipt.nonce.empty())))
      return false;
    std::string data = "schema=1\nphase=" + std::string(phase) +
                       "\nunit=" + receipt.unit +
                       "\ninvocation=" + receipt.invocation +
                       "\nnonce=" + receipt.nonce + "\n";
    char temporary[] = ".bootstrap-XXXXXXXX";
    Descriptor random(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    unsigned char bytes[4];
    if (random.Get() < 0 || read(random.Get(), bytes, sizeof(bytes)) !=
                                static_cast<ssize_t>(sizeof(bytes)))
      return false;
    std::snprintf(temporary + 11, 9, "%02x%02x%02x%02x", bytes[0], bytes[1],
                  bytes[2], bytes[3]);
    Descriptor file(openat(authority, temporary,
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                           0600));
    if (file.Get() < 0)
      return false;
    bool ok =
        fchown(file.Get(), 0, group) == 0 && fchmod(file.Get(), 0640) == 0;
    size_t offset = 0;
    while (ok && offset < data.size()) {
      ssize_t count =
          write(file.Get(), data.data() + offset, data.size() - offset);
      if (count < 0 && errno == EINTR)
        continue;
      ok = count > 0;
      if (ok)
        offset += static_cast<size_t>(count);
    }
    ok = ok && fsync(file.Get()) == 0;
    file.Reset();
    if (ok)
      ok = renameat(authority, temporary, authority, kName) == 0;
    if (ok)
      ok = fsync(authority) == 0;
    if (!ok)
      unlinkat(authority, temporary, 0);
    return ok;
  } catch (...) {
    return false;
  }
}

}  // namespace recovery
}  // namespace consent
