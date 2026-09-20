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
#include "consent/consent.h"
#include "consent/endpoint.hh"
#include "common/message.hh"

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <new>
#include <cstdio>
#include <cstring>
#include <thread>

#define CHECK(expression) do { \
  if (!(expression)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
    abort(); \
  } \
} while (0)

// Fault only the API calling thread; the I/O fixture remains runnable.
thread_local int allocation_budget = -1;
thread_local size_t largest_allocation = 0;
__attribute__((noinline)) void* operator new(std::size_t size) {
  largest_allocation = std::max(largest_allocation, size);
  if (allocation_budget == 0)
    throw std::bad_alloc();
  if (allocation_budget > 0)
    --allocation_budget;
  if (void* value = malloc(size ? size : 1))
    return value;
  throw std::bad_alloc();
}
__attribute__((noinline)) void* operator new[](std::size_t size) { return ::operator new(size); }
__attribute__((noinline)) void operator delete(void* value) noexcept { free(value); }
__attribute__((noinline)) void operator delete[](void* value) noexcept { free(value); }
__attribute__((noinline)) void operator delete(void* value, std::size_t) noexcept { free(value); }
__attribute__((noinline)) void operator delete[](void* value, std::size_t) noexcept { free(value); }

namespace {
pid_t test_parent = 0;
constexpr const char* kEndpoint = "/tmp/consent-test/consent.sock";
std::atomic<unsigned> requests{0};
std::atomic<bool> stop{false};

bool ReadAll(int fd, void* data, size_t size) {
  auto* bytes = static_cast<uint8_t*>(data);
  while (size) {
    ssize_t count = recv(fd, bytes, size, 0);
    if (count <= 0)
      return false;
    bytes += count;
    size -= count;
  }
  return true;
}

bool Send(int fd, consent::Message message) {
  auto frame = consent::Encode(message);
  // Deliberately fragment header and body into tiny writes.
  for (size_t offset = 0; offset < frame.size();) {
    size_t size = std::min<size_t>(3, frame.size() - offset);
    ssize_t sent = send(fd, frame.data() + offset, size, MSG_NOSIGNAL);
    if (sent <= 0)
      return false;
    offset += sent;
  }
  return true;
}

void Serve(int listener) {
  int fd = accept(listener, nullptr, nullptr);
  if (fd < 0)
    return;
  uint64_t revision = 1;
  while (!stop) {
    uint8_t header[4];
    if (!ReadAll(fd, header, sizeof(header)))
      break;
    uint32_t size = consent::FrameSize(header);
    CHECK(size && size <= consent::kMaxFrameSize);
    std::vector<uint8_t> bytes(size);
    if (!ReadAll(fd, bytes.data(), size))
      break;
    consent::Message input;
    CHECK(consent::Decode(bytes.data(), bytes.size(), &input));
    std::string method = consent::Get(input, "method");
    std::string scenario = consent::Get(input, "scenario");
    if (scenario == "timeout")
      continue;
    if (scenario == "disconnect")
      break;
    if (scenario == "invalidate") {
      ++revision;
      if (!Send(fd, {{"v", "1"}, {"id", "0"}, {"method", "event"}, {"event", "invalidate"},
          {"epoch", "test-epoch"}, {"revision", std::to_string(revision)}}))
        break;
    }
    consent::Message reply{{"v", "1"}, {"method", "reply"}, {"id", consent::Get(input, "id")},
        {"status", "0"}, {"epoch", "test-epoch"},
        {"revision", std::to_string(revision)}, {"decision", "ALLOWED"}};
    if (method == "request") {
      ++requests;
      if (!consent::Get(input, "session").empty()) {
        reply["session"] = consent::Get(input, "session");
        reply["generation"] = consent::Get(input, "generation");
      }
      if (scenario == "pending") {
        reply["decision"] = "PENDING";
        reply["request_id"] = "pending-1";
      } else {
        reply["cacheable"] = "1";
        reply["cache_ttl_ms"] = "1000";
        reply["request_id"] = "request-1";
      }
    } else if (method == "result") {
      reply["request_id"] = "pending-1";
    } else if (scenario == "deny") {
      reply["decision"] = "DENIED";
    }
    if (!Send(fd, std::move(reply)))
      break;
  }
  close(fd);
}

struct Callback {
  bool returned = false;
  unsigned count = 0;
  int status = 0;
  consent_decision_e decision = CONSENT_DECISION_UNKNOWN;
  consent_client_h client = nullptr;
  bool destroy = false;
};

void Result(int status, const consent_result_t* result, void* data) {
  if (getpid() != test_parent)
    _exit(91);
  auto* callback = static_cast<Callback*>(data);
  CHECK(callback->returned);
  ++callback->count;
  callback->status = status;
  callback->decision = consent_result_get_decision(result);
  if (callback->destroy)
    CHECK(!consent_client_destroy(callback->client));
}

void DispatchUntil(Callback* callback) {
  gint64 deadline = g_get_monotonic_time() + 3000000;
  while (!callback->count && g_get_monotonic_time() < deadline) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
  CHECK(callback->count == 1);
}

void EndpointPolicy() {
  struct sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, "/run/.consentd.sock");
  socklen_t size = offsetof(struct sockaddr_un, sun_path) + strlen(address.sun_path) + 1;
  const char label[] = "System::Privileged";
  auto matches = [&](pid_t pid, uid_t uid, size_t label_size) {
    return consent::MatchActivatedPeer(pid, uid, address, size, label, label_size,
        "/run/.consentd.sock", "System::Privileged");
  };
  CHECK(matches(1, 0, sizeof(label)));
  CHECK(matches(1, 0, sizeof(label) - 1));
  CHECK(!matches(2, 0, sizeof(label)));  // Direct root server is not systemd.
  CHECK(!matches(1, 1000, sizeof(label)));
  CHECK(!matches(1, 0, 0));
  --size;
  CHECK(!matches(1, 0, sizeof(label)));  // Missing pathname terminal NUL.
  ++size;
  strcpy(address.sun_path, "/run/other.socket");
  size = offsetof(struct sockaddr_un, sun_path) + strlen(address.sun_path) + 1;
  CHECK(!matches(1, 0, sizeof(label)));  // Renamed unrelated systemd socket.
}

void Protocol() {
  auto hello = consent::Encode({{"id", "1"}, {"method", "hello"}, {"v", "1"}});
  const uint8_t vector[] = {0, 0, 0, 34, 0, 0, 0, 1, 0, 0, 0, 1,
      0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 6, 'h', 'e', 'l', 'l', 'o', 0,
      0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(hello == std::vector<uint8_t>(vector, vector + sizeof(vector)));
  consent::Message message{{"v", "1"}, {"method", "hello"}, {"id", "1"},
      {"text", "동의"}};
  auto frame = consent::Encode(message);
  consent::Message decoded;
  CHECK(consent::FrameSize(frame.data()) == frame.size() - 4);
  CHECK(consent::Decode(frame.data() + 4, frame.size() - 4, &decoded));
  CHECK(decoded == message);
  CHECK(!consent::Decode(nullptr, 0, &decoded));
  message["bad key"] = "x";
  CHECK(consent::Encode(message).empty());
  int64_t number;
  CHECK(consent::ParseNumber("-9223372036854775808", &number) && number == INT64_MIN);
  CHECK(!consent::ParseNumber("9223372036854775808", &number));
  CHECK(!consent::ParseNumber("1junk", &number));
  CHECK(!consent::ParseNumber("+1", &number));
  std::vector<uint8_t> storage(256);
  tizen_base::Parcel parcel(storage.data(), storage.size(), false, false);
  parcel.Clear();
  parcel.SetByteOrder(true);
  consent::wire::Envelope envelope;
  envelope.version = 1;
  envelope.kind = 1;
  envelope.correlation = 1;
  envelope.method = "hello";
  envelope.status = 0;
  consent::wire::Field field;
  field.key = "duplicate";
  field.value = "value";
  envelope.fields.push_back(field);
  envelope.fields.push_back(field);
  parcel.WriteParcelable(envelope);
  CHECK(!consent::Decode(parcel.GetData(), parcel.GetDataSize(), &decoded));
  for (size_t length = 0; length + 4 < hello.size(); ++length)
    CHECK(!consent::Decode(hello.data() + 4, length, &decoded));
  auto excessive = hello;
  excessive.push_back(0);
  CHECK(!consent::Decode(excessive.data() + 4, excessive.size() - 4, &decoded));
  auto malformed = hello;
  malformed[24] = 0;  // Embedded NUL before declared end of method string.
  CHECK(!consent::Decode(malformed.data() + 4, malformed.size() - 4, &decoded));
  malformed = hello;
  malformed[23] = 0xff;  // Method length beyond remaining body.
  CHECK(!consent::Decode(malformed.data() + 4, malformed.size() - 4, &decoded));
  malformed = hello;
  malformed[37] = 0xff;  // Array count exceeds bounded remaining fields.
  CHECK(!consent::Decode(malformed.data() + 4, malformed.size() - 4, &decoded));
  malformed = hello;
  malformed[36] = 1;  // Legal maximum count=256 with zero remaining field bytes.
  malformed[37] = 0;
  largest_allocation = 0;
  CHECK(!consent::Decode(malformed.data() + 4, malformed.size() - 4, &decoded));
  CHECK(largest_allocation < 4096);  // Rejected before a Field[256] allocation.

}
}  // namespace

int main() {
  test_parent = getpid();
  EndpointPolicy();
  Protocol();
  // An exclusive lock makes this fixture refuse another running test/daemon.
  if (mkdir("/tmp/consent-test", 0700) && errno != EEXIST)
    return 1;
  int lock = open("/tmp/consent-test/client-test.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB))
    return 1;
  struct stat existing = {};
  if (!lstat(kEndpoint, &existing)) {
    fprintf(stderr, "Refusing to replace existing test socket\n");
    return 1;
  }
  int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  CHECK(listener >= 0);
  struct sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, kEndpoint);
  CHECK(!bind(listener, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)));
  CHECK(!listen(listener, 4));
  std::thread server(Serve, listener);
  consent_client_h client = nullptr;
  CHECK(!consent_client_create(&client));
  consent_params_t* params = nullptr;
  CHECK(!consent_params_create(&params));
  CHECK(consent_params_set(params, "_role", "argo") == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(consent_params_set(params, "role", "argo") == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(!consent_params_add_requirement(params, "calendar.read", "read", "today", "answer", ""));
  CHECK(!consent_params_set_check_mode(params, CONSENT_CHECK_QUERY));
  consent_result_t* result = nullptr;
  CHECK(!consent_check(client, params, 2000, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  consent_result_free(result);
  // Deferred immediate success and denial, inputs can be freed after submit.
  for (const char* scenario : {"allow", "deny", "pending"}) {
    CHECK(!consent_params_set(params, "scenario", scenario));
    Callback callback;
    consent_async_id_t operation;
    int status = !strcmp(scenario, "pending") ?
        consent_request_async(client, params, Result, &callback, &operation) :
        consent_check_async(client, params, Result, &callback, &operation);
    CHECK(!status && !callback.count);
    callback.returned = true;
    DispatchUntil(&callback);
    CHECK(!callback.status);
    CHECK(callback.decision == (!strcmp(scenario, "deny") ?
        CONSENT_DECISION_DENIED : CONSENT_DECISION_ALLOWED));
  }
  // Repeated persistent request is local until a revision invalidates it.
  CHECK(!consent_params_set(params, "scenario", "cache"));
  CHECK(!consent_request(client, params, 2000, &result));
  consent_result_free(result);
  unsigned before = requests;
  CHECK(!consent_request(client, params, 2000, &result));
  CHECK(!strcmp(consent_result_get(result, "source"), "CACHE"));
  CHECK(!consent_result_get(result, "request_id"));
  CHECK(requests == before);
  consent_result_free(result);
  Callback cached_callback;
  consent_async_id_t cached_operation;
  CHECK(!consent_request_async(client, params, Result, &cached_callback, &cached_operation));
  CHECK(!cached_callback.count);
  cached_callback.returned = true;
  DispatchUntil(&cached_callback);
  CHECK(requests == before);
  CHECK(!consent_params_set(params, "scenario", "invalidate"));
  CHECK(!consent_check(client, params, 2000, &result));
  consent_result_free(result);
  CHECK(!consent_params_set(params, "scenario", "cache"));
  CHECK(!consent_request(client, params, 2000, &result));
  CHECK(requests == before + 1);
  consent_result_free(result);
  // Session cache requires server-confirmed session/generation; changing
  // generation never hits an older entry, and event invalidation clears both.
  CHECK(!consent_params_set(params, "session", "logical-session"));
  CHECK(!consent_params_set(params, "generation", "1"));
  CHECK(!consent_request(client, params, 2000, &result));
  consent_result_free(result);
  before = requests;
  CHECK(!consent_request(client, params, 2000, &result));
  CHECK(!strcmp(consent_result_get(result, "source"), "CACHE"));
  CHECK(requests == before);
  consent_result_free(result);
  CHECK(!consent_params_set(params, "generation", "2"));
  CHECK(!consent_request(client, params, 2000, &result));
  CHECK(requests == before + 1);
  CHECK(!strcmp(consent_result_get(result, "source"), "DAEMON"));
  consent_result_free(result);
  CHECK(!consent_params_set(params, "scenario", "invalidate"));
  CHECK(!consent_check(client, params, 2000, &result));
  consent_result_free(result);
  CHECK(!consent_params_set(params, "scenario", "cache"));
  CHECK(!consent_request(client, params, 2000, &result));
  CHECK(requests == before + 2);
  consent_result_free(result);
  // Local wait is finite and is not interpreted as remote denial.
  CHECK(!consent_params_set(params, "scenario", "timeout"));
  int status = consent_check(client, params, 60, &result);
  CHECK(status == CONSENT_ERROR_OUTCOME_UNKNOWN && !result);
  Callback detached;
  consent_async_id_t operation;
  CHECK(!consent_check_async(client, params, Result, &detached, &operation));
  CHECK(!consent_async_detach(client, operation));
  detached.returned = true;
  while (g_main_context_iteration(nullptr, FALSE)) {}
  CHECK(!detached.count);
  // Bound accepted jobs even if the dispatcher is never iterated.
  consent_async_id_t operations[64];
  Callback backlog;
  for (size_t i = 0; i < 64; ++i)
    CHECK(!consent_check_async(client, params, Result, &backlog, &operations[i]));
  CHECK(consent_check_async(client, params, Result, &backlog, &operation) == CONSENT_ERROR_BUSY);
  for (auto id : operations)
    CHECK(!consent_async_detach(client, id));
  // Failed admission never leaves a callback or a published operation.
  unsigned failed_allocations = 0;
  for (int budget = 0; budget < 100; ++budget) {
    Callback faulted;
    operation = 99;
    allocation_budget = budget;
    status = consent_check_async(client, params, Result, &faulted, &operation);
    allocation_budget = -1;
    faulted.returned = true;
    if (status == CONSENT_ERROR_OUT_OF_MEMORY) {
      ++failed_allocations;
      CHECK(operation == 0);
    } else {
      CHECK(!status);
      CHECK(!consent_async_detach(client, operation));
    }
    while (g_main_context_iteration(nullptr, FALSE)) {}
    CHECK(!faulted.count);
  }
  CHECK(failed_allocations > 5);
  // Saturate the parent's handle limit. A child must reset the inherited count
  // and discard callbacks from a parent's context before touching its mutexes.
  consent_client_h extra[15] = {};
  std::vector<std::thread> extra_servers;
  for (auto& handle : extra) {
    extra_servers.emplace_back(Serve, listener);
    CHECK(!consent_client_create(&handle));
  }
  CHECK(!consent_params_set(params, "scenario", "allow"));
  Callback inherited;
  CHECK(!consent_check_async(client, params, Result, &inherited, &operation));
  inherited.returned = true;
  g_usleep(100000);
  extra_servers.emplace_back(Serve, listener);
  pid_t child = fork();
  CHECK(child >= 0);
  if (!child) {
    alarm(10);
    while (g_main_context_iteration(nullptr, FALSE)) {}
    CHECK(!inherited.count);
    CHECK(consent_client_destroy(client) == CONSENT_ERROR_INVALID_PARAMETER);
    consent_client_h fresh = nullptr;
    CHECK(!consent_client_create(&fresh));
    CHECK(!consent_client_destroy(fresh));
    _exit(0);
  }
  int child_status = 0;
  CHECK(waitpid(child, &child_status, 0) == child);
  CHECK(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
  DispatchUntil(&inherited);
  for (auto handle : extra)
    CHECK(!consent_client_destroy(handle));
  for (auto& worker : extra_servers)
    worker.join();
  // Destroy from callback suppresses the remaining callbacks and joins I/O.
  CHECK(!consent_params_set(params, "scenario", "allow"));
  Callback closing;
  closing.client = client;
  closing.destroy = true;
  CHECK(!consent_check_async(client, params, Result, &closing, &operation));
  closing.returned = true;
  DispatchUntil(&closing);
  CHECK(!closing.status);
  consent_params_free(params);
  stop = true;
  server.join();
  close(listener);
  unlink(kEndpoint);
  close(lock);
  puts("PASS client: framing, UTF-8, duplicate rejection, async ordering, pending polling, cache invalidation, timeout, detach, bounds, callback shutdown, allocation failure, fork isolation");
  return 0;
}
