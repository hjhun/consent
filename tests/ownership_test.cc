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
#include "common/dispatch.hh"

#include "common/logging.hh"
#include "common/resource.hh"
#include "consentd/key_file.hh"
#include "consentd/repository.hh"
#include "consentd/server.hh"
#include "consentd/server_connection.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cerrno>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>

namespace {
// This executable alone injects C++ allocation failure. No daemon flag exists.
thread_local bool fail_next = false;
thread_local size_t fail_size = 0;
const char* registry_path = nullptr;
bool registry_open_at_failure = false;

bool HasRegistryDescriptor() noexcept {
  if (!registry_path)
    return false;
  DIR* directory = opendir("/proc/self/fd");
  if (!directory)
    return false;
  bool found = false;
  while (auto* entry = readdir(directory)) {
    char link[512];
    char target[4096];
    snprintf(link, sizeof(link), "/proc/self/fd/%s", entry->d_name);
    ssize_t size = readlink(link, target, sizeof(target) - 1);
    if (size > 0) {
      target[size] = '\0';
      found = found || strcmp(target, registry_path) == 0;
    }
  }
  closedir(directory);
  return found;
}
}  // namespace

void* operator new(size_t size) {
  if (fail_next || (fail_size && size == fail_size)) {
    fail_next = false;
    fail_size = 0;
    registry_open_at_failure = HasRegistryDescriptor();
    throw std::bad_alloc();
  }
  void* pointer = malloc(size ? size : 1);
  if (!pointer)
    throw std::bad_alloc();
  return pointer;
}
__attribute__((noinline)) void operator delete(void* pointer) noexcept {
  free(pointer);
}
__attribute__((noinline)) void operator delete(void* pointer, size_t) noexcept {
  free(pointer);
}
void* operator new[](size_t size) {
  return ::operator new(size);
}
__attribute__((noinline)) void operator delete[](void* pointer) noexcept {
  free(pointer);
}
__attribute__((noinline)) void operator delete[](void* pointer,
                                                 size_t) noexcept {
  free(pointer);
}

namespace consentd {
class ServerTestPeer {
 public:
  static void FailSubmit(Server& server) {
    fail_next = true;
    server.Submit([] {});
  }
  static void FailParse(Server& server) {
    fail_next = true;
    server.Parse({}, {});
  }
  static bool FailPost(Server& server) {
    fail_next = true;
    return server.Post(server.main_context_, [] {});
  }
  static void StartDelayedIo(Server& server, std::atomic<bool>& started) {
    server.InitializeWakeSources();
    struct Start {
      Server* server;
      std::atomic<bool>* started;
    };
    auto* start = new Start{&server, &started};
    server.io_thread_ = g_thread_new(
        "delayed-io",
        [](gpointer data) -> gpointer {
          std::unique_ptr<Start> start(static_cast<Start*>(data));
          start->started->store(true);
          // Force destructor to queue quit before this thread enters Run.
          auto* self = start->server;
          while (g_source_get_ready_time(self->io_quit_source_) != 0)
            g_usleep(1000);
          g_main_context_push_thread_default(self->io_context_);
          g_main_loop_run(self->io_loop_);
          g_main_context_pop_thread_default(self->io_context_);
          return nullptr;
        },
        start);
  }
  static void StopAndDrain(Server& server, std::atomic<unsigned>& drained) {
    server.InitializeWakeSources();
    server.StartThreads();
    for (unsigned i = 0; i < 8; ++i) {
      ASSERT_TRUE(server.Submit([&drained, i] {
        EXPECT_EQ(drained.load(), i);
        ++drained;
      }));
    }
    server.Stop();
    g_main_loop_run(server.main_loop_);
    g_async_queue_push(server.db_queue_, &server.db_stop_);
    g_thread_join(server.db_thread_);
    server.db_thread_ = nullptr;
    EXPECT_EQ(server.db_jobs_.load(), 0u);
  }
  static void ProfileOutput(Server& server, bool partial) {
    ASSERT_TRUE(server.profiles_->Configure({{"agent", "A", "profile.A"}}));
    int descriptors[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                         0, descriptors), 0);
    int capacity = 4096;
    ASSERT_EQ(setsockopt(descriptors[0], SOL_SOCKET, SO_SNDBUF,
                         &capacity, sizeof(capacity)), 0);
    auto connection = std::make_shared<Server::Connection>();
    connection->server = &server;
    auto* socket = g_socket_new_from_fd(descriptors[0], nullptr);
    ASSERT_NE(socket, nullptr);
    g_socket_set_blocking(socket, FALSE);
    connection->stream = g_socket_connection_factory_create_connection(socket);
    connection->socket = socket;
    g_object_unref(socket);
    ++server.connections_;
    consent::Message reply{{"v", "1"}, {"id", "1"},
        {"method", "reply"}, {"status", "0"}, {"decision", "ALLOWED"}};
    for (unsigned i = 0; i < 4; ++i)
      reply["payload" + std::to_string(i)] = std::string(8192, 'a');
    auto bytes = consent::Encode(reply);
    ASSERT_FALSE(bytes.empty());
    connection->output_bytes = bytes.size();
    connection->output.push_back({bytes,
        std::to_string(server.profiles_->Generation())});
    if (!partial) {
      char fill[4096] = {};
      while (send(descriptors[0], fill, sizeof(fill), MSG_NOSIGNAL) > 0) {}
      ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
    }
    server.Write(connection);
    ASSERT_FALSE(connection->closed);
    ASSERT_FALSE(connection->output.empty());
    if (partial) {
      ASSERT_GT(connection->offset, 0u);
      ASSERT_LT(connection->offset, bytes.size());
    } else {
      ASSERT_EQ(connection->offset, 0u);
    }
    const auto sent = connection->offset;
    server.profiles_->Fence();
    server.Write(connection);
    EXPECT_TRUE(connection->closed);
    size_t received = 0;
    char buffer[4096];
    ssize_t count;
    while ((count = recv(descriptors[1], buffer, sizeof(buffer), 0)) > 0)
      received += static_cast<size_t>(count);
    if (partial) {
      EXPECT_EQ(received, sent);  // The sensitive frame never completes.
    }
    close(descriptors[1]);
  }
  static unsigned Jobs(const Server& server) { return server.db_jobs_; }
  static unsigned Parsers(const Server& server) { return server.parse_jobs_; }
};
}  // namespace consentd

namespace {
TEST(Ownership, ProfileFenceRejectsBlockedAndPartialSensitiveOutput) {
  for (bool partial : {false, true}) {
    consentd::Server server;
    consentd::ServerTestPeer::ProfileOutput(server, partial);
  }
}

TEST(Logging, ActualBackendPreservesLiteralPercent) {
  LOG(INFO) << "event=build31-logging-probe actor_pid=" << getpid()
            << " percent=100%";
}

TEST(Ownership, RealServerAllocationFailureDoesNotReserveAdmission) {
  consentd::Server server;
  EXPECT_THROW(consentd::ServerTestPeer::FailSubmit(server), std::bad_alloc);
  EXPECT_EQ(consentd::ServerTestPeer::Jobs(server), 0u);
  EXPECT_THROW(consentd::ServerTestPeer::FailParse(server), std::bad_alloc);
  EXPECT_EQ(consentd::ServerTestPeer::Parsers(server), 0u);
  EXPECT_FALSE(consentd::ServerTestPeer::FailPost(server));
}

size_t DescriptorCount() {
  DIR* directory = opendir("/proc/self/fd");
  if (!directory)
    return 0;
  size_t count = 0;
  while (auto* entry = readdir(directory)) {
    if (entry->d_name[0] != '.')
      ++count;
  }
  closedir(directory);
  return count;
}

TEST(Ownership, ProtectedOpenAllocationFailureClosesCurrentDirectory) {
  // The long leaf allocates after / and /usr have been opened and validated.
  const std::string path = "/usr/" + std::string(200, 'a');
  const auto before = DescriptorCount();
  fail_size = 201;
  EXPECT_THROW(consentd::OpenProtected(path), std::bad_alloc);
  fail_size = 0;
  EXPECT_EQ(DescriptorCount(), before);
  EXPECT_LT(consentd::OpenProtected(path), 0);
  EXPECT_EQ(DescriptorCount(), before);
}

TEST(Ownership, ActualStopDrainsAcceptedDatabaseJobsInOrder) {
  consentd::Server server;
  std::atomic<unsigned> drained{0};
  consentd::ServerTestPeer::StopAndDrain(server, drained);
  EXPECT_EQ(drained.load(), 8u);
}

TEST(Ownership, DestructorQuitQueuedBeforeIoRunIsNotLost) {
  std::atomic<bool> started{false};
  auto server = std::make_unique<consentd::Server>();
  consentd::ServerTestPeer::StartDelayedIo(*server, started);
  while (!started.load())
    std::this_thread::yield();
  const auto before = std::chrono::steady_clock::now();
  server.reset();
  const auto elapsed = std::chrono::steady_clock::now() - before;
  EXPECT_LT(elapsed, std::chrono::seconds(1));
}

__attribute__((noinline)) std::unique_ptr<int> AllocateJob() {
  return std::make_unique<int>(1);
}

TEST(Ownership, ReservationRollbackAndTransferAreDistinct) {
  std::atomic<unsigned> count{0};
  try {
    consent::Admission reservation(count, 1);
    EXPECT_TRUE(reservation.Accepted());
    fail_next = true;
    auto job = AllocateJob();
    reservation.Commit();
  } catch (const std::bad_alloc&) {
  }
  EXPECT_EQ(count, 0u);
  {
    consent::Admission accepted(count, 1);
    EXPECT_TRUE(accepted.Accepted());
    consent::Admission rejected(count, 1);
    EXPECT_FALSE(rejected.Accepted());
    accepted.Commit();
  }
  EXPECT_EQ(count, 1u);
  --count;
}

TEST(Ownership, FailedPostAndContextCancellationReleaseCallable) {
  std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> context(
      g_main_context_new(), g_main_context_unref);
  auto lifetime = std::make_shared<int>(1);
  std::weak_ptr<int> weak = lifetime;
  consent::Dispatcher dispatcher;
  std::function<void()> work = [lifetime] {};
  lifetime.reset();
  fail_next = true;
  EXPECT_FALSE(dispatcher.Post(context.get(), std::move(work)));
  EXPECT_TRUE(weak.expired());
  lifetime = std::make_shared<int>(1);
  weak = lifetime;
  EXPECT_TRUE(dispatcher.Post(context.get(), [lifetime] {}));
  lifetime.reset();
  EXPECT_FALSE(weak.expired());
  dispatcher.Cancel();
  EXPECT_TRUE(weak.expired());
  context.reset();
}

TEST(Ownership, DispatchIsDeferredAndContainsCallableException) {
  std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> context(
      g_main_context_new(), g_main_context_unref);
  consent::Dispatcher dispatcher;
  bool called = false;
  ASSERT_TRUE(dispatcher.Post(context.get(), [&called] {
    called = true;
    throw std::bad_alloc();
  }));
  EXPECT_FALSE(called);
  EXPECT_TRUE(g_main_context_iteration(context.get(), FALSE));
  EXPECT_TRUE(called);
  EXPECT_FALSE(g_main_context_pending(context.get()));
}

TEST(Ownership, KeyFileCopiesReleaseGLibMemoryWhenCppAllocationFails) {
  std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(g_key_file_new(),
                                                              g_key_file_unref);
  const std::string text(1024, 'x');
  g_key_file_set_string(file.get(), "group", "value", text.c_str());
  const char* values[] = {text.c_str(), "second"};
  g_key_file_set_string_list(file.get(), "group", "values", values, 2);
  fail_next = true;
  EXPECT_THROW(consentd::KeyValue(file.get(), "group", "value"),
               std::bad_alloc);
  fail_next = true;
  EXPECT_THROW(consentd::KeyValues(file.get(), "group", "values"),
               std::bad_alloc);
  EXPECT_EQ(consentd::KeyValue(file.get(), "group", "value"), text);
  EXPECT_EQ(consentd::KeyValues(file.get(), "group", "values").size(), 2u);
}

TEST(Ownership, RegistryDescriptorClosesAfterActualPostOpenAllocationFailure) {
  char pattern[] = "/tmp/consent-ownership-XXXXXX";
  const auto* created = mkdtemp(pattern);
  ASSERT_NE(created, nullptr);
  const std::string directory = created;
  ASSERT_EQ(mkdir((directory + "/registry").c_str(), 0700), 0);
  const auto database = directory + "/consent.db";
  const auto registry = directory + "/registry/definitions.registry";
  {
    consentd::Repository repository(database, directory + "/registry");
    std::string error;
    ASSERT_TRUE(repository.Open(&error)) << error;
  }
  struct stat info{};
  ASSERT_EQ(stat(registry.c_str(), &info), 0);
  consentd::Repository repository(database, directory + "/registry");
  std::string error;
  registry_path = registry.c_str();
  registry_open_at_failure = false;
  fail_size = info.st_size + 1;
  EXPECT_FALSE(repository.Open(&error));
  fail_size = 0;
  EXPECT_TRUE(registry_open_at_failure);
  EXPECT_FALSE(HasRegistryDescriptor());
  registry_path = nullptr;
  EXPECT_TRUE(repository.Open(&error)) << error;
  std::filesystem::remove_all(directory);
}

class LogBackend {
 public:
  MOCK_METHOD(void, Write, (consent::logging::Level, const char*, const char*));
};
LogBackend* log_backend = nullptr;
void LogSink(consent::logging::Level level, const char* tag, const char* text) {
  log_backend->Write(level, tag, text);
}

TEST(Logging, LevelTagSourceAndLiteralPercentReachBackendUnaltered) {
  testing::StrictMock<LogBackend> backend;
  log_backend = &backend;
  EXPECT_CALL(
      backend,
      Write(
          consent::logging::Level::WARNING, testing::StrEq("CONSENTD"),
          testing::StrEq("[source.cc:42 Function] event=probe percent=100%")));
  {
    consent::logging::Line line(consent::logging::Level::WARNING, "CONSENTD",
                                "/path/source.cc", "Function", 42, LogSink);
    line << "event=probe percent=100%";
  }
  log_backend = nullptr;
}

TEST(Logging, BackendExceptionAndDisabledDebugDoNotEscapeOrEvaluate) {
  testing::StrictMock<LogBackend> backend;
  log_backend = &backend;
  EXPECT_CALL(backend, Write(testing::_, testing::_, testing::_))
      .WillOnce([](auto, auto, auto) { throw std::bad_alloc(); });
  EXPECT_NO_THROW({
    consent::logging::Line line(consent::logging::Level::ERROR, "CONSENTD",
                                "test.cc", "Function", 1, LogSink);
    line << "allocation failure";
  });
  unsigned evaluated = 0;
  LOG(DEBUG) << ++evaluated;
  EXPECT_EQ(evaluated, 0u);
  log_backend = nullptr;
}

TEST(Logging, FixedStorageTruncatesAndPreservesErrnoWithoutAllocation) {
  testing::StrictMock<LogBackend> backend;
  log_backend = &backend;
  const std::string text(4096, 'x');
  EXPECT_CALL(backend, Write(testing::_, testing::_, testing::_))
      .WillOnce([](auto, auto, const char* value) {
        EXPECT_EQ(strlen(value), 2047u);
      });
  {
    consent::logging::Line line(consent::logging::Level::INFO, "CONSENTD",
                                "test.cc", "Function", 1, LogSink);
    fail_next = true;
    line << text << 42 << '%';
    EXPECT_TRUE(fail_next);
    fail_next = false;
    errno = EIO;
  }
  EXPECT_EQ(errno, EIO);
  log_backend = nullptr;
}
}  // namespace
