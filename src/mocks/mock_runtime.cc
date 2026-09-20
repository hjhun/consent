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
#include "mock_runtime.h"

#include <consent.h>
#include <glib.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Fields = std::map<std::string, std::string>;
using Params = std::unique_ptr<consent_params_t, decltype(&consent_params_free)>;
using Result = std::unique_ptr<consent_result_t, decltype(&consent_result_free)>;
constexpr size_t kInputLimit = 65536;
constexpr size_t kBufferLimit = 128;
volatile sig_atomic_t stopped = 0;

void Stop(int) { stopped = 1; }

class Failure final : public std::runtime_error {
 public:
  Failure(int status, const std::string& message) : std::runtime_error(message), status_(status) {}
  int Status() const { return status_; }
 private:
  int status_;
};

void Require(bool condition, const char* message, int status = -EINVAL) {
  if (!condition)
    throw Failure(status, message);
}

std::string Get(const Fields& fields, const std::string& key, const std::string& fallback = "") {
  auto found = fields.find(key);
  return found == fields.end() ? fallback : found->second;
}

long Number(const std::string& value, long minimum, long maximum) {
  Require(!value.empty() && value.find_first_not_of("-0123456789") == std::string::npos,
      "invalid decimal option");
  errno = 0;
  char* end = nullptr;
  long result = std::strtol(value.c_str(), &end, 10);
  Require(!errno && end == value.c_str() + value.size() && result >= minimum && result <= maximum,
      "numeric option outside bounds");
  return result;
}

std::string Trim(const std::string& value) {
  auto start = value.find_first_not_of(" \t\r");
  if (start == std::string::npos)
    return "";
  return value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
}

std::string Json(const std::string& value) {
  static const char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char byte : value) {
    if (byte == '\\' || byte == '"') {
      result += '\\';
      result += static_cast<char>(byte);
    } else if (byte < 0x20) {
      result += "\\u00";
      result += hex[byte >> 4];
      result += hex[byte & 15];
    } else {
      result += static_cast<char>(byte);
    }
  }
  return result + '"';
}

Fields CopyResult(const consent_result_t* result) {
  Fields fields;
  if (!result)
    return fields;
  for (size_t index = 0; index < consent_result_size(result); ++index) {
    const char* key = nullptr;
    const char* value = nullptr;
    if (!consent_result_get_at(result, index, &key, &value) && key && value)
      fields.emplace(key, value);
  }
  return fields;
}

struct Command {
  std::string name;
  Fields options;
  Fields fields;
};

Command Load(const std::string& name, const std::string& path) {
  Require(!path.empty() && path.front() == '/' && path.size() <= 4096,
      "input path must be absolute and bounded");
  int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  Require(descriptor >= 0, "cannot open input file", -errno);
  struct File {
    int descriptor;
    ~File() { close(descriptor); }
  } file{descriptor};
  struct stat status = {};
  Require(fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode) &&
      status.st_size > 0 && status.st_size <= static_cast<off_t>(kInputLimit),
      "input must be a regular INI file of at most 64 KiB");
  std::string content;
  char buffer[4096];
  for (;;) {
    ssize_t count = read(descriptor, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR)
      continue;
    Require(count >= 0, "input read failed", -EIO);
    if (!count)
      break;
    content.append(buffer, count);
    Require(content.size() <= kInputLimit, "input grew beyond limit", -E2BIG);
  }
  Require(content.find('\0') == std::string::npos &&
      g_utf8_validate(content.data(), content.size(), nullptr), "input is not UTF-8 text");
  Command result{name, {}, {}};
  std::set<std::string> sections;
  std::string section;
  size_t start = 0;
  size_t count = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    if (end == std::string::npos)
      end = content.size();
    std::string line = Trim(content.substr(start, end - start));
    start = end + 1;
    if (line.empty() || line.front() == '#' || line.front() == ';')
      continue;
    Require(line.size() <= 8450, "INI line exceeds limit", -E2BIG);
    if (line.front() == '[') {
      Require(line == "[mock]" || line == "[params]", "only [mock] and [params] sections are supported");
      section = line;
      Require(sections.insert(section).second, "duplicate INI section");
      continue;
    }
    auto equal = line.find('=');
    Require(!section.empty() && equal != std::string::npos, "INI entry needs section and equals sign");
    std::string key = Trim(line.substr(0, equal));
    std::string value = Trim(line.substr(equal + 1));
    Require(!key.empty() && key.size() <= 128 && value.size() <= 8192 && ++count <= 240,
        "INI fields exceed bounds", -E2BIG);
    auto& fields = section == "[mock]" ? result.options : result.fields;
    Require(key != "role" && key != "roles", "mock input cannot claim a caller role");
    Require(fields.emplace(key, value).second, "duplicate INI field");
  }
  static const std::set<std::string> options = {"id", "package", "app", "timeout_ms",
      "expect_status", "expect_decision", "payload", "cleanup_failure"};
  for (const auto& field : result.options)
    Require(options.count(field.first), "unsupported mock option");
  Require(result.options.count("id") && Get(result.options, "id").size() <= 128 &&
      !Get(result.options, "id").empty(), "[mock] id is required and at most 128 bytes");
  Number(Get(result.options, "timeout_ms", "120000"), 100, 300000);
  Number(Get(result.options, "expect_status", "0"), INT_MIN, 0);
  Require(Get(result.options, "payload").size() <= 4096, "mock memory payload exceeds 4096 bytes", -E2BIG);
  Require(Get(result.options, "cleanup_failure", "0") == "0" ||
      Get(result.options, "cleanup_failure") == "1", "cleanup_failure must be 0 or 1");
  return result;
}

class Runtime final {
 public:
  explicit Runtime(std::string role) : role_(std::move(role)), context_(g_main_context_new()) {
    if (!context_)
      throw std::bad_alloc();
    int status = consent_client_create_with_context(context_, &client_);
    if (status) {
      g_main_context_unref(context_);
      context_ = nullptr;
      throw Failure(status, "cannot create consent client");
    }
    Emit("ready", "", 0, {{"pid", std::to_string(getpid())}});
  }
  ~Runtime() {
    if (client_)
      consent_client_destroy(client_);
    for (auto& buffer : buffers_)
      Clear(&buffer.second);
    if (context_)
      g_main_context_unref(context_);
  }
  int Run(int argc, char** argv);

 private:
  struct Job {
    Runtime* owner;
    Command command;
    consent_async_id_t operation = 0;
    gint64 deadline = 0;
    gint64 lookup_at = 0;
    bool returned = false;
    bool discovered = false;
    bool done = false;
    unsigned callbacks = 0;
  };
  static void Completed(int status, const consent_result_t* result, void* data) noexcept;
  static void Clear(std::vector<char>* buffer) {
    volatile char* bytes = buffer->data();
    for (size_t index = 0; index < buffer->size(); ++index)
      bytes[index] = 0;
    buffer->clear();
  }
  void Emit(const std::string& event, const std::string& id, int status, const Fields& fields);
  void Finish(const Command& command, int status, Fields fields);
  Params Parameters(const Fields& fields);
  void Execute(Command command);
  void Pump();
  void Input(const std::string& line);
  bool Supports(const std::string& command) const;
  int Manage(const Command& command, consent_params_t* params, consent_result_t** result);

  std::string role_;
  GMainContext* context_ = nullptr;
  consent_client_h client_ = nullptr;
  std::map<std::string, std::unique_ptr<Job>> jobs_;
  std::map<std::string, std::vector<char>> buffers_;
  std::set<std::string> erased_;
  std::set<std::string> command_ids_;
  bool failed_ = false;
  bool quitting_ = false;
};

void Runtime::Emit(const std::string& event, const std::string& id, int status, const Fields& fields) {
  std::string line = "{\"schema\":1,\"role\":" + Json(role_) + ",\"event\":" + Json(event) +
      ",\"id\":" + Json(id) + ",\"status\":" + std::to_string(status);
  for (const char* key : {"request_id", "client_request_id", "decision", "receipt", "artifact", "session", "generation"}) {
    auto field = fields.find(key);
    if (field != fields.end())
      line += ',' + Json(key) + ':' + Json(field->second);
  }
  line += ",\"fields\":{";
  bool first = true;
  for (const auto& field : fields) {
    if (!first)
      line += ',';
    first = false;
    line += Json(field.first) + ':' + Json(field.second);
  }
  line += "}}\n";
  Require(fwrite(line.data(), 1, line.size(), stdout) == line.size() && fflush(stdout) == 0,
      "output pipe failed", -EPIPE);
}

void Runtime::Finish(const Command& command, int status, Fields fields) {
  bool matches = status == Number(Get(command.options, "expect_status", "0"), INT_MIN, 0);
  if (command.options.count("expect_decision"))
    matches = matches && Get(fields, "decision") == Get(command.options, "expect_decision");
  failed_ = failed_ || !matches;
  fields["matches_expectation"] = matches ? "1" : "0";
  fields["command"] = command.name;
  Emit("result", Get(command.options, "id"), status, fields);
}

Params Runtime::Parameters(const Fields& fields) {
  consent_params_t* raw = nullptr;
  int status = consent_params_create(&raw);
  if (status)
    throw Failure(status, "cannot create API parameters");
  Params params(raw, consent_params_free);
  for (const auto& field : fields) {
    status = consent_params_set(params.get(), field.first.c_str(), field.second.c_str());
    if (status)
      throw Failure(status, "invalid public API parameter");
  }
  return params;
}

bool Runtime::Supports(const std::string& command) const {
  if (command == "probe-request" || command == "probe-register" || command == "probe-authorize")
    return true;
  if (role_ == "argo")
    return command == "request" || command == "result" || command == "cancel" ||
        command == "session-open" || command == "session-state" || command == "session-suspend" ||
        command == "session-resume" || command == "session-close" || command == "cleanup-state";
  if (role_ == "cm" || role_ == "ce")
    return command == "query" || command == "authorize";
  if (role_ == "installer")
    return command == "register" || command == "update" || command == "unregister";
  if (role_ == "holder")
    return command == "data-register" || command == "derive" || command == "reuse" ||
        command == "release" || command == "cleanup-list" || command == "cleanup-state";
  return false;
}

int Runtime::Manage(const Command& command, consent_params_t* params, consent_result_t** result) {
  const auto& name = command.name;
  if (name == "register" || name == "update" || name == "probe-register" || name == "unregister") {
    std::string package = Get(command.options, "package");
    std::string app = Get(command.options, "app");
    Require(!package.empty() && (name == "unregister" || !app.empty()),
        "registration requires explicit [mock] package and app");
    Require(!Get(command.fields, "expected_generation").empty() &&
        !Get(command.fields, "operation_id").empty(), "registration requires generation and operation ID");
    if (name == "unregister")
      return consent_unregister(client_, package.c_str(), params);
    if (name == "update")
      return consent_update(client_, package.c_str(), app.c_str(), params);
    return consent_register(client_, package.c_str(), app.c_str(), params);
  }
  if (name == "query" || name == "authorize" || name == "probe-authorize" || name == "reuse") {
    int status = consent_params_set_check_mode(params,
        name == "query" ? CONSENT_CHECK_QUERY : CONSENT_CHECK_AUTHORIZE);
    if (status)
      return status;
    if (name == "reuse") {
      Require(buffers_.count(Get(command.fields, "artifact")), "mock has no resident artifact", -ENOENT);
      status = consent_params_set(params, "operation", "reuse-data");
      if (status)
        return status;
    }
    return consent_check(client_, params, 5000, result);
  }
  if (name == "result") return consent_get_request_result(client_, params, result);
  if (name == "cancel") return consent_cancel_request(client_, params, result);
  if (name == "session-open") return consent_session_open(client_, params, result);
  if (name == "session-state") return consent_session_get_state(client_, params, result);
  if (name == "session-suspend") return consent_session_suspend(client_, params, result);
  if (name == "session-resume") return consent_session_resume(client_, params, result);
  if (name == "session-close") return consent_session_close(client_, params, result);
  if (name == "cleanup-state") return consent_cleanup_get_state(client_, params, result);
  if (name == "cleanup-list") return consent_cleanup_get_pending(client_, params, result);
  if (name == "data-register" || name == "derive") {
    Require(buffers_.size() < kBufferLimit, "mock resident artifact limit reached", -EBUSY);
    if (name == "derive") {
      long count = Number(Get(command.fields, "count"), 1, 16);
      for (long index = 0; index < count; ++index)
        Require(buffers_.count(Get(command.fields, "parent" + std::to_string(index))),
            "derived mock data needs each resident parent", -ENOENT);
    }
    std::string payload = Get(command.options, "payload", "consent mock MEMORY_ONLY fixture");
    std::vector<char> buffer(payload.begin(), payload.end());
    int status = name == "derive" ? consent_data_register_derived(client_, params, result) :
        consent_data_register(client_, params, result);
    const char* artifact = status ? nullptr : consent_result_get(*result, "artifact");
    if (!status && artifact && *artifact && !buffers_.count(artifact))
      buffers_.emplace(artifact, std::move(buffer));
    Clear(&buffer);
    return status;
  }
  if (name == "release") {
    const std::string artifact = Get(command.fields, "artifact");
    bool failure = Get(command.options, "cleanup_failure") == "1";
    auto found = buffers_.find(artifact);
    Require(found != buffers_.end() || erased_.count(artifact),
        "mock cannot claim deletion of data it did not hold", -ENOENT);
    if (!failure && found != buffers_.end()) {
      Clear(&found->second);
      buffers_.erase(found);
      erased_.insert(artifact);
    }
    int status = consent_params_set(params, "success", failure ? "0" : "1");
    return status ? status : consent_data_release(client_, params, result);
  }
  return -ENOSYS;
}

void Runtime::Completed(int status, const consent_result_t* result, void* data) noexcept {
  auto* job = static_cast<Job*>(data);
  try {
    ++job->callbacks;
    Fields fields = CopyResult(result);
    fields["callback_after_return"] = job->returned ? "1" : "0";
    fields["callbacks"] = std::to_string(job->callbacks);
    if (!job->returned || job->callbacks != 1)
      status = CONSENT_ERROR_PROTOCOL;
    job->owner->Finish(job->command, status, std::move(fields));
  } catch (...) {
    job->owner->failed_ = true;
    std::fputs("mock callback output failed\n", stderr);
  }
  job->done = true;
}

void Runtime::Execute(Command command) {
  try {
    Require(Supports(command.name), "command is not supported by this mock executable", -ENOSYS);
    Require(command_ids_.size() < 1024 && command_ids_.insert(Get(command.options, "id")).second,
        "duplicate command ID or process command limit");
    if (command.name == "request" || command.name == "probe-request") {
      Require(jobs_.size() < 16, "mock pending request limit reached", -EBUSY);
      Require(!Get(command.fields, "client_request_id").empty() &&
          !Get(command.fields, "subject").empty() && !Get(command.fields, "profile").empty(),
          "async request requires stable client_request_id and explicit subject/profile");
      if (!command.fields.count("deadline_ms"))
        command.fields["deadline_ms"] = Get(command.options, "timeout_ms", "120000");
      auto params = Parameters(command.fields);
      auto job = std::make_unique<Job>();
      job->owner = this;
      job->command = command;
      job->deadline = g_get_monotonic_time() +
          Number(Get(command.options, "timeout_ms", "120000"), 100, 300000) * 1000;
      std::string id = Get(command.options, "id");
      Job* pending = job.get();
      jobs_.emplace(id, std::move(job));
      int status = consent_request_async(client_, params.get(), Completed, pending, &pending->operation);
      pending->returned = true;
      if (status) {
        jobs_.erase(id);
        Finish(command, status, {});
      } else {
        Emit("accepted", id, 0, {{"client_request_id", Get(command.fields, "client_request_id")},
            {"local_operation", std::to_string(pending->operation)}, {"approval", "not-decided"}});
      }
      return;
    }
    auto params = Parameters(command.fields);
    consent_result_t* raw = nullptr;
    int status = Manage(command, params.get(), &raw);
    Result result(raw, consent_result_free);
    Fields fields = CopyResult(result.get());
    if (role_ == "holder")
      fields["resident_artifacts"] = std::to_string(buffers_.size());
    Finish(command, status, std::move(fields));
  } catch (const Failure& failure) {
    Finish(command, failure.Status(), {{"reason", failure.what()}});
  }
}

void Runtime::Pump() {
  // API calls happen between context iterations. A synchronous management call
  // inside a GLib callback would violate the public WOULD_DEADLOCK contract.
  for (unsigned index = 0; index < 64 && g_main_context_iteration(context_, FALSE); ++index) {}
  bool lookup_started = false;
  for (auto item = jobs_.begin(); item != jobs_.end();) {
    auto& job = *item->second;
    if (!job.done && (stopped || g_get_monotonic_time() >= job.deadline)) {
      consent_async_detach(client_, job.operation);
      Finish(job.command, stopped ? -ECANCELED : CONSENT_ERROR_TIMEOUT,
          {{"local_callback_detached", "1"}, {"remote_cancelled", "0"},
           {"client_request_id", Get(job.command.fields, "client_request_id")}});
      job.done = true;
    }
    if (job.done) {
      item = jobs_.erase(item);
      continue;
    }
    if (!lookup_started && !job.discovered && role_ == "argo" && g_get_monotonic_time() >= job.lookup_at) {
      lookup_started = true;
      Fields lookup;
      for (const char* key : {"subject", "profile", "client_request_id"})
        lookup[key] = Get(job.command.fields, key);
      auto params = Parameters(lookup);
      consent_result_t* raw = nullptr;
      int status = consent_get_request_result(client_, params.get(), &raw);
      Result result(raw, consent_result_free);
      Fields fields = CopyResult(result.get());
      if (!status && !Get(fields, "request_id").empty()) {
        job.discovered = true;
        if (Get(fields, "decision") == "PENDING")
          Emit("pending", Get(job.command.options, "id"), 0, fields);
      }
      job.lookup_at = g_get_monotonic_time() + 50000;
    }
    ++item;
  }
}

void Runtime::Input(const std::string& line) {
  if (line == "quit") {
    quitting_ = true;
    return;
  }
  try {
    auto space = line.find(' ');
    Require(space != std::string::npos && space > 0, "serve input is COMMAND /absolute/file.ini or quit");
    Execute(Load(line.substr(0, space), Trim(line.substr(space + 1))));
  } catch (const Failure& failure) {
    failed_ = true;
    Emit("input-error", "", failure.Status(), {{"reason", failure.what()}});
  }
}

int Runtime::Run(int argc, char** argv) {
  if (argc == 3) {
    Execute(Load(argv[1], argv[2]));
    while (!jobs_.empty()) {
      Pump();
      if (!jobs_.empty())
        poll(nullptr, 0, 10);
    }
    return failed_ ? 1 : 0;
  }
  Require(argc == 2 && !std::strcmp(argv[1], "serve"), "use COMMAND /absolute/input.ini or serve");
  std::string input;
  bool eof = false;
  while ((!eof && !quitting_ && !stopped) || !jobs_.empty()) {
    Pump();
    if (stopped || quitting_ || eof) {
      if (!jobs_.empty())
        poll(nullptr, 0, 10);
      continue;
    }
    struct pollfd descriptor = {STDIN_FILENO, POLLIN, 0};
    int status = poll(&descriptor, 1, 10);
    if (status < 0 && errno == EINTR)
      continue;
    Require(status >= 0 && !(descriptor.revents & (POLLERR | POLLNVAL)), "stdin poll failed", -EIO);
    if (status && (descriptor.revents & (POLLIN | POLLHUP))) {
      char buffer[1024];
      ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
      if (count < 0 && errno == EINTR)
        continue;
      Require(count >= 0, "stdin read failed", -EIO);
      eof = count == 0;
      input.append(buffer, count);
      for (;;) {
        auto newline = input.find('\n');
        if (newline == std::string::npos)
          break;
        std::string line = Trim(input.substr(0, newline));
        input.erase(0, newline + 1);
        Require(line.size() <= 4352, "serve command exceeds limit", -E2BIG);
        if (!line.empty())
          Input(line);
        if (quitting_)
          break;
      }
      Require(input.size() <= 4352, "unterminated serve command exceeds limit", -E2BIG);
      if (eof && !input.empty()) {
        Input(Trim(input));
        input.clear();
      }
    }
  }
  return failed_ || stopped ? 1 : 0;
}
}  // namespace

extern "C" int consent_mock_main(const char* role, int argc, char** argv) {
  if (argc == 2 && !std::strcmp(argv[1], "--help")) {
    std::printf("Usage: %s COMMAND /absolute/input.ini | serve\n"
        "INI: [mock] id/package/app/timeout_ms/expect_status/expect_decision/payload/cleanup_failure;\n"
        "     [params] exact consent API fields, literal UTF-8 values (no escape expansion).\n"
        "serve reads COMMAND /absolute/input.ini lines; quit drains pending callbacks.\n"
        "Outputs JSON lines: schema/role/event/id/status plus fields and common result IDs.\n"
        "Argo: request/result/cancel/session-open/session-state/session-suspend/\n"
        "      session-resume/session-close/cleanup-state.\n"
        "CM/CE: query/authorize. Installer: register/update/unregister.\n"
        "Holder: data-register/derive/reuse/release/cleanup-list/cleanup-state.\n"
        "All: probe-request/probe-register/probe-authorize (real daemon rejection probes).\n", argv[0]);
    return 0;
  }
  struct sigaction action = {};
  action.sa_handler = Stop;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &action, nullptr);
  try {
    Runtime runtime(role);
    return runtime.Run(argc, argv);
  } catch (const Failure& failure) {
    std::fprintf(stderr, "mock %s failed: status=%d reason=%s\n", role, failure.Status(), failure.what());
    return 2;
  } catch (const std::exception& failure) {
    std::fprintf(stderr, "mock %s failed: %s\n", role, failure.what());
    return 2;
  }
}
