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
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

static std::string authority_path;
static bool fail_read;
static ssize_t FixtureRead(int fd, void* buffer, size_t size) {
  if (fail_read) {
    errno = EIO;
    return -1;
  }
  return syscall(SYS_read, fd, buffer, size);
}
#define CONSENT_TEST_BUILD
#define CONSENT_INSTALLATIONS authority_path.c_str()
#define read FixtureRead
#include "../consentd/identity.cc"
#undef read

#include <cassert>

namespace {
void Write(const std::string& contents) {
  int fd = open(authority_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  assert(fd >= 0);
  assert(write(fd, contents.data(), contents.size()) == static_cast<ssize_t>(contents.size()));
  assert(close(fd) == 0);
}
const char* valid = "[authority]\nschema=1\n[package demo]\nstate=active\ngeneration=g1\n"
    "[demo.app]\npackage=demo\nstate=active\ngeneration=g1\n";
}

int main() {
  if (getuid() != 0 || geteuid() != 0) {
    puts("SKIP offline protected authority fixture requires root");
    return 77;
  }
  char directory[] = "/tmp/consent-offline-identity-XXXXXX";
  assert(mkdtemp(directory));
  authority_path = std::string(directory) + "/installations.conf";
  assert(consentd::CheckOfflineAuthority() == -ESTALE);
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == -ESTALE);
  Write(valid);
  assert(consentd::CheckOfflineAuthority() == 0);
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == 0);
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g2") == -ESTALE);
  assert(consentd::ValidateOfflineInstallation("absent", "missing", "g1") == -ESTALE);
  std::string pending(valid);
  pending.replace(pending.find("state=active"), 12, "state=pending");
  Write(pending);
  assert(consentd::CheckOfflineAuthority() == 0);
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == -ESTALE);
  pending.replace(pending.find("state=pending"), 13, "state=invalid");
  Write(pending);
  assert(consentd::CheckOfflineAuthority() == -EINVAL);
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == -EINVAL);
  puts("PASS offline authority valid/missing/mismatch/pending/schema classification");
  for (const auto& body : {std::string("[broken"), std::string("[authority]\nschema=2\n"),
      std::string("[authority]\n"), std::string()}) {
    Write(body);
    assert(consentd::CheckOfflineAuthority() == -EINVAL);
    assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == -EINVAL);
  }
  Write(valid);
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == 0);
  Write("[broken");
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == -EINVAL);
  puts("PASS malformed/schema/empty and changed-source classified reads");
  Write(valid);
  fail_read = true;
  assert(consentd::CheckOfflineAuthority() == -EIO);
  assert(consentd::ValidateOfflineInstallation("demo", "demo.app", "g1") == -EIO);
  fail_read = false;
  assert(chmod(authority_path.c_str(), 0666) == 0);
  assert(consentd::CheckOfflineAuthority() == -EACCES);
  assert(unlink(authority_path.c_str()) == 0);
  assert(mkfifo(authority_path.c_str(), 0600) == 0);
  alarm(3);
  assert(consentd::CheckOfflineAuthority() == -EACCES);
  alarm(0);
  assert(unlink(authority_path.c_str()) == 0);
  assert(symlink("/dev/null", authority_path.c_str()) == 0);
  assert(consentd::CheckOfflineAuthority() != 0 && consentd::CheckOfflineAuthority() != -ESTALE);
  assert(unlink(authority_path.c_str()) == 0);
  assert(rmdir(directory) == 0);
  assert(consentd::CheckOfflineAuthority() != 0 && consentd::CheckOfflineAuthority() != -ESTALE);
  puts("PASS I/O/protection/FIFO/symlink/ancestor errors are not deferred");
  return 0;
}
