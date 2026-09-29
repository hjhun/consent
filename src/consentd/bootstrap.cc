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
#include "bootstrap.hh"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

#include "common/recovery_state.hh"
#include "common/resource.hh"
#include "identity.hh"

namespace consentd {
namespace {

bool FilePresent(int directory, const char* name, bool* present) {
  struct stat info = {};
  if (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) < 0) {
    if (errno != ENOENT)
      return false;
    *present = false;
    return true;
  }
  *present = true;
  return S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
         info.st_gid == getegid() && (info.st_mode & 07777) == 0600 &&
         info.st_nlink == 1;
}

std::string ProcessField(pid_t process, const char* name) {
  std::string path = "/proc/" + std::to_string(process) + "/" + name;
  consent::Descriptor fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
  if (fd.Get() < 0)
    return "unavailable";
  char bytes[64] = {};
  ssize_t size = read(fd.Get(), bytes, sizeof(bytes) - 1);
  if (size <= 0)
    return "unavailable";
  std::string value(bytes, static_cast<size_t>(size));
  if (!value.empty() && value.back() == '\n')
    value.pop_back();
  return value;
}

bool MainPid(const char* unit, std::string* error) {
  int fds[2];
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) < 0) {
    *error = "systemd MainPID pipe failed";
    return false;
  }
  consent::Descriptor reader(fds[0]);
  consent::Descriptor writer(fds[1]);
  int exec_fds[2];
  if (pipe2(exec_fds, O_CLOEXEC | O_NONBLOCK) < 0) {
    *error = "systemd MainPID exec pipe failed";
    return false;
  }
  consent::Descriptor exec_reader(exec_fds[0]);
  consent::Descriptor exec_writer(exec_fds[1]);
  auto started = std::chrono::steady_clock::now();
  pid_t child = fork();
  if (child < 0) {
    *error = "systemd MainPID query fork failed";
    return false;
  }
  if (child == 0) {
    if (dup2(writer.Get(), STDOUT_FILENO) < 0) {
      int failure = errno;
      ssize_t reported = write(exec_writer.Get(), &failure, sizeof(failure));
      _exit(reported == static_cast<ssize_t>(sizeof(failure)) ? 126 : 125);
    }
    execl("/usr/bin/systemctl", "systemctl", "show", "-p", "MainPID", "--value",
          unit, static_cast<char*>(nullptr));
    int failure = errno;
    ssize_t reported = write(exec_writer.Get(), &failure, sizeof(failure));
    _exit(reported == static_cast<ssize_t>(sizeof(failure)) ? 127 : 125);
  }
  writer.Reset();
  exec_writer.Reset();
  std::string output;
  bool exited = false;
  bool output_eof = false;
  bool output_limit = false;
  int status = 0;
  int poll_error = 0;
  int wait_error = 0;
  auto deadline = started + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                         deadline - std::chrono::steady_clock::now())
                         .count();
    int timeout = output_eof ? 10 : 50;
    if (remaining < timeout)
      timeout = remaining > 0 ? static_cast<int>(remaining) : 1;
    struct pollfd event = {reader.Get(), POLLIN, 0};
    if (poll(output_eof ? nullptr : &event, output_eof ? 0 : 1, timeout) < 0 &&
        errno != EINTR) {
      poll_error = errno;
      break;
    }
    if (!output_eof) {
      char bytes[32];
      ssize_t size = read(reader.Get(), bytes, sizeof(bytes));
      if (size > 0)
        output.append(bytes, static_cast<size_t>(size));
      else if (size == 0)
        output_eof = true;
      else if (errno != EAGAIN && errno != EINTR) {
        poll_error = errno;
        break;
      }
      if (output.size() > 32) {
        output_limit = true;
        break;
      }
    }
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child) {
      exited = true;
      break;
    }
    if (waited < 0 && errno != EINTR) {
      wait_error = errno;
      break;
    }
  }
  if (!exited) {
    int exec_failure = 0;
    ssize_t exec_result =
        read(exec_reader.Get(), &exec_failure, sizeof(exec_failure));
    std::string stage = exec_result == 0 ? "after-exec" : "before-exec";
    if (exec_result > 0)
      stage = "exec-failed";
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started)
                       .count();
    std::string comm = ProcessField(child, "comm");
    std::string wchan = ProcessField(child, "wchan");
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    *error = "systemd MainPID query incomplete stage=" + stage +
             " elapsed_ms=" + std::to_string(elapsed) + " comm=" + comm +
             " wchan=" + wchan +
             " output_bytes=" + std::to_string(output.size()) +
             " exec_errno=" + std::to_string(exec_failure) +
             " output_limit=" + std::to_string(output_limit) +
             " poll_errno=" + std::to_string(poll_error) +
             " wait_errno=" + std::to_string(wait_error);
    return false;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    *error = "systemd MainPID query exited with error";
    return false;
  }
  char bytes[32];
  ssize_t size;
  do {
    size = read(reader.Get(), bytes, sizeof(bytes));
    if (size > 0)
      output.append(bytes, static_cast<size_t>(size));
  } while (size > 0 && output.size() <= 32);
  if (output == std::to_string(getpid()) + "\n")
    return true;
  if (output == "0\n") {
    *error = "systemd MainPID is zero";
    return false;
  }
  if (!output.empty() && output.back() == '\n' &&
      output.find_first_not_of("0123456789", 0) == output.size() - 1)
    *error = "systemd MainPID is another process";
  else
    *error = "systemd MainPID response is malformed";
  return false;
}

bool ManagerIdentity(const char* unit, std::string* error) {
  char* raw_unit = nullptr;
  char* raw_cgroup = nullptr;
  int unit_status = sd_pid_get_unit(0, &raw_unit);
  int group_status = sd_pid_get_cgroup(0, &raw_cgroup);
  std::unique_ptr<char, decltype(&free)> actual_unit(raw_unit, free);
  std::unique_ptr<char, decltype(&free)> cgroup(raw_cgroup, free);
  if (unit_status < 0 || !actual_unit) {
    *error = "systemd unit lookup failed";
    return false;
  }
  if (unit != std::string(actual_unit.get())) {
    *error = "systemd unit does not match";
    return false;
  }
  if (group_status < 0 || !cgroup) {
    *error = "systemd cgroup lookup failed";
    return false;
  }
  std::string path(cgroup.get());
  std::string suffix = std::string("/") + unit;
  if (path.size() < suffix.size() ||
      path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
    *error = "systemd cgroup does not match";
    return false;
  }
  if (!MainPid(unit, error)) {
    return false;
  }
  return true;
}

}  // namespace

bool CheckBootstrap(const char* authority_path, int state, const char* unit,
                    std::string* error) {
  try {
    if (!ManagerIdentity(unit, error)) {
      return false;
    }
    consent::Descriptor authority(OpenProtected(authority_path, true));
    if (authority.Get() < 0 || state < 0) {
      *error = "bootstrap authority or state is unprotected";
      return false;
    }
    consent::recovery::Receipt receipt;
    if (!consent::recovery::ReadReceipt(authority.Get(), getegid(), &receipt) ||
        receipt.unit != unit) {
      *error = "bootstrap receipt is absent or invalid";
      return false;
    }
    bool registry = false;
    bool database = false;
    if (!FilePresent(state, "definitions.registry", &registry) ||
        !FilePresent(state, "consent.db", &database)) {
      *error = "bootstrap pair protection failed";
      return false;
    }
    const char* invocation = getenv("INVOCATION_ID");
    bool ticket = receipt.phase == consent::recovery::Phase::kClaimed &&
                  invocation && receipt.invocation == invocation &&
                  consent::recovery::HexId(receipt.nonce, 32);
    auto result =
        consent::recovery::Assess(receipt.phase, registry, database, ticket);
    if (result == consent::recovery::Boot::kReject) {
      *error = "bootstrap pair or invocation requires recovery";
      return false;
    }
    return true;
  } catch (...) {
    *error = "bootstrap preflight failed";
    return false;
  }
}

}  // namespace consentd
