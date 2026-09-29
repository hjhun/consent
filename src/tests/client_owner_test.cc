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
// Include the private implementation to exercise member-owner destruction.
// This executable has no production role/security test switch.
#define CONSENT_CLIENT_OWNER_TEST
#include "../consent/client.cc"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <thread>

namespace consent {
class ClientTestPeer {
 public:
  static bool Adopt(Client& client, int fd) {
    return client.state_->transport_.Adopt(fd);
  }
  static void Admit(Client& client, Message message) {
    std::shared_ptr<Client::State::Operation> operation;
    ASSERT_EQ(client.state_->Admit(std::move(message), 5000, false, nullptr,
                                   nullptr, &operation),
              0);
  }
  static void Pump(Client& client) { client.state_->Pump(); }
  static bool HasWriteSource(Client& client) {
    return client.state_->transport_.write_ != nullptr;
  }
  static bool PartialBlocked(Client& client) {
    const auto& transport = client.state_->transport_;
    return transport.would_block_ && !transport.output_.empty() &&
           transport.output_.front().offset > 0;
  }
  static bool WireEmpty(Client& client) {
    return client.state_->transport_.wire_.empty();
  }
  static bool OutputEmpty(Client& client) {
    return client.state_->transport_.output_.empty();
  }
  static GMainContext* Context(Client& client) {
    return client.state_->io_.Get();
  }
  static void Abandon(Client& client) { client.state_->transport_.Abandon(); }
  static void SeedCache(Client& client) {
    auto* state = client.state_.get();
    state->synced_ = true;
    state->cache_[CacheKey({{"method", "request"}})] = {
        {{"decision", "ALLOWED"}, {"source", "DAEMON"}},
        g_get_monotonic_time() + 5000000,
        g_get_monotonic_time()};
  }
  static void Observe(Client& client, void (*observer)(void*) noexcept,
                      void* data) {
    client.state_->admission_observer_ = observer;
    client.state_->admission_observer_data_ = data;
  }
  static void PumpAndWrite(Client& client) {
    client.state_->Pump();
    ASSERT_TRUE(client.state_->Write());
    EXPECT_TRUE(client.state_->transport_.wire_.empty());
  }
  static int Cached(Client& client, consent_result_cb callback, void* data) {
    std::shared_ptr<Client::State::Operation> operation;
    return client.state_->Admit({{"method", "request"}}, 5000, true, callback,
                                data, &operation);
  }
  static void Delivery(Client& client, consent_result_cb callback, void* data) {
    std::shared_ptr<Client::State::Operation> operation;
    ASSERT_EQ(client.state_->Admit({{"method", "check"}}, 5000, true, callback,
                                   data, &operation),
              0);
    client.state_->Complete(operation, 0, {{"decision", "ALLOWED"}});
  }
};
}  // namespace consent
namespace {
struct AdmissionBarrier {
  GMutex mutex;
  GCond condition;
  bool published = false;
  bool pumped = false;
  AdmissionBarrier() {
    g_mutex_init(&mutex);
    g_cond_init(&condition);
  }
  ~AdmissionBarrier() {
    g_cond_clear(&condition);
    g_mutex_clear(&mutex);
  }
  static void Published(void* data) noexcept {
    auto* gate = static_cast<AdmissionBarrier*>(data);
    g_mutex_lock(&gate->mutex);
    gate->published = true;
    g_cond_signal(&gate->condition);
    while (!gate->pumped) g_cond_wait(&gate->condition, &gate->mutex);
    g_mutex_unlock(&gate->mutex);
  }
};
struct CachedCallback {
  bool returned = false;
  testing::StrictMock<testing::MockFunction<void(const char*)>> sink;
  static void Deliver(int status, const consent_result_t* result, void* data) {
    auto* callback = static_cast<CachedCallback*>(data);
    EXPECT_TRUE(callback->returned);
    EXPECT_EQ(status, 0);
    // This private fixture borrows the real result object passed by Dispatch.
    callback->sink.Call(consent::Get(result->values, "source").c_str());
  }
};
TEST(ClientOwner, PublishedCacheHitCannotBecomeRemoteWork) {
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair), 0);
  consent::Client client(nullptr);
  ASSERT_TRUE(consent::ClientTestPeer::Adopt(client, pair[0]));
  consent::ClientTestPeer::SeedCache(client);
  AdmissionBarrier gate;
  consent::ClientTestPeer::Observe(client, AdmissionBarrier::Published, &gate);
  CachedCallback callback;
  EXPECT_CALL(callback.sink, Call(testing::StrEq("CACHE"))).Times(1);
  std::thread worker([&] {
    g_mutex_lock(&gate.mutex);
    while (!gate.published) g_cond_wait(&gate.condition, &gate.mutex);
    g_mutex_unlock(&gate.mutex);
    consent::ClientTestPeer::PumpAndWrite(client);
    g_mutex_lock(&gate.mutex);
    gate.pumped = true;
    g_cond_signal(&gate.condition);
    g_mutex_unlock(&gate.mutex);
  });
  EXPECT_EQ(consent::ClientTestPeer::Cached(client, CachedCallback::Deliver,
                                            &callback),
            0);
  callback.returned = true;
  worker.join();
  char byte;
  EXPECT_EQ(recv(pair[1], &byte, 1, MSG_DONTWAIT), -1);
  EXPECT_EQ(errno, EAGAIN);
  while (g_main_context_iteration(nullptr, FALSE)) {
  }
  consent::ClientTestPeer::Observe(client, nullptr, nullptr);
  close(pair[1]);
}

TEST(ClientOwner, CacheOnlyAdmissionWakesCallerButNotIoContext) {
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair), 0);
  consent::Descriptor peer(pair[1]);
  std::unique_ptr<GMainContext, decltype(&g_main_context_unref)> context(
      g_main_context_new(), g_main_context_unref);
  {
    consent::Client client(context.get());
    ASSERT_TRUE(consent::ClientTestPeer::Adopt(client, pair[0]));
    consent::ClientTestPeer::SeedCache(client);
    auto* io = consent::ClientTestPeer::Context(client);
    ASSERT_FALSE(g_main_context_pending(io));
    ASSERT_FALSE(g_main_context_pending(context.get()));
    CachedCallback callback;
    EXPECT_CALL(callback.sink, Call(testing::StrEq("CACHE"))).Times(1);
    ASSERT_EQ(consent::ClientTestPeer::Cached(client, CachedCallback::Deliver,
                                              &callback),
              0);
    callback.returned = true;
    // The real owner context exists but its thread has not been started.
    // No callback/thread-start race can explain a missing I/O wake here.
    EXPECT_FALSE(g_main_context_pending(io));
    EXPECT_TRUE(g_main_context_pending(context.get()));
    EXPECT_TRUE(consent::ClientTestPeer::WireEmpty(client));
    EXPECT_TRUE(consent::ClientTestPeer::OutputEmpty(client));
    char byte;
    EXPECT_EQ(recv(peer.Get(), &byte, 1, MSG_DONTWAIT), -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_TRUE(g_main_context_iteration(context.get(), FALSE));
    EXPECT_FALSE(g_main_context_iteration(context.get(), FALSE));
    EXPECT_FALSE(g_main_context_pending(io));
    // A noncache admission must still signal the same preattached wake source.
    consent::ClientTestPeer::Admit(client, {{"method", "check"}});
    EXPECT_TRUE(g_main_context_pending(io));
  }
}

TEST(ClientOwner, WritableFrameNeedsNoWriteReadinessSource) {
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair), 0);
  consent::Descriptor peer(pair[1]);
  consent::Client client(nullptr);
  ASSERT_TRUE(consent::ClientTestPeer::Adopt(client, pair[0]));
  consent::Message message{{"method", "check"}, {"payload", "first"}};
  consent::ClientTestPeer::Admit(client, message);
  consent::ClientTestPeer::Pump(client);
  EXPECT_FALSE(consent::ClientTestPeer::HasWriteSource(client));
  EXPECT_TRUE(consent::ClientTestPeer::OutputEmpty(client));
  message["id"] = "1";
  message["v"] = "1";
  auto expected = consent::Encode(message);
  std::vector<uint8_t> received(expected.size());
  ASSERT_EQ(recv(peer.Get(), received.data(), received.size(), MSG_DONTWAIT),
            static_cast<ssize_t>(expected.size()));
  EXPECT_EQ(received, expected);
}

TEST(ClientOwner, WouldBlockRetainsSourceAndDrainsOrderedPartialFrames) {
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair), 0);
  consent::Descriptor peer(pair[1]);
  int size = 4096;
  ASSERT_EQ(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)), 0);
  consent::Client client(nullptr);
  ASSERT_TRUE(consent::ClientTestPeer::Adopt(client, pair[0]));
  std::vector<uint8_t> expected;
  for (unsigned i = 1; i <= 2; ++i) {
    consent::Message message{{"method", "check"}};
    for (unsigned field = 0; field < 8; ++field)
      message["payload" + std::to_string(field)] = std::string(4000, 'a' + i);
    consent::ClientTestPeer::Admit(client, message);
    message["id"] = std::to_string(i);
    message["v"] = "1";
    auto frame = consent::Encode(message);
    ASSERT_FALSE(frame.empty());
    expected.insert(expected.end(), frame.begin(), frame.end());
  }
  consent::ClientTestPeer::Pump(client);
  ASSERT_TRUE(consent::ClientTestPeer::PartialBlocked(client));
  ASSERT_TRUE(consent::ClientTestPeer::HasWriteSource(client));
  std::vector<uint8_t> received;
  // Each peer read makes the socket writable. Iterate the real attached source
  // without sleeps; a fixed bound fails instead of hiding a missed drain.
  for (unsigned pass = 0; pass < 128 && received.size() < expected.size();
       ++pass) {
    uint8_t bytes[8192];
    for (;;) {
      auto count = recv(peer.Get(), bytes, sizeof(bytes), MSG_DONTWAIT);
      if (count < 0) {
        ASSERT_EQ(errno, EAGAIN);
        break;
      }
      ASSERT_GT(count, 0);
      received.insert(received.end(), bytes, bytes + count);
    }
    g_main_context_iteration(consent::ClientTestPeer::Context(client), FALSE);
  }
  EXPECT_EQ(received, expected);
  EXPECT_TRUE(consent::ClientTestPeer::OutputEmpty(client));
  EXPECT_FALSE(consent::ClientTestPeer::HasWriteSource(client));
}

TEST(ClientOwner, ChildAbandonDoesNotCloseReusedDescriptor) {
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair), 0);
  auto client = std::make_unique<consent::Client>(nullptr);
  ASSERT_TRUE(consent::ClientTestPeer::Adopt(*client, pair[0]));
  auto child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    alarm(5);
    consent::ClientTestPeer::Abandon(*client);
    int other = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (other != pair[0]) {
      if (dup2(other, pair[0]) != pair[0]) _exit(2);
      close(other);
    }
    client.reset();
    bool alive = fcntl(pair[0], F_GETFD) >= 0;
    close(pair[0]);
    _exit(alive ? 0 : 1);
  }
  int status;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  char value = 'a';
  ASSERT_EQ(send(pair[0], &value, 1, MSG_NOSIGNAL), 1);
  ASSERT_EQ(recv(pair[1], &value, 1, 0), 1);
  close(pair[1]);
}

void Callback(int, const consent_result_t*, void* data) {
  static_cast<testing::MockFunction<void()>*>(data)->Call();
}
TEST(ClientOwner, ChildNotifyDropsLastOwnerWithoutInheritedCallbacks) {
  testing::StrictMock<testing::MockFunction<void()>> callback;
  EXPECT_CALL(callback, Call()).Times(0);
  int pair[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair), 0);
  auto client = std::make_unique<consent::Client>(nullptr);
  ASSERT_TRUE(consent::ClientTestPeer::Adopt(*client, pair[0]));
  consent::ClientTestPeer::Delivery(*client, Callback, &callback);
  auto child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    alarm(5);
    client.reset();
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    // Source notify has destroyed the last inherited State and its owners.
    bool closed = fcntl(pair[0], F_GETFD) < 0 && errno == EBADF;
    _exit(closed ? 0 : 1);
  }
  int status;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  client.reset();
  close(pair[1]);
  while (g_main_context_iteration(nullptr, FALSE)) {
  }
}
}  // namespace
