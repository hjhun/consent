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
#include "consent/io_context.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <memory>
#include <stdexcept>

namespace {
TEST(ClientIo, AdmissionAfterSnapshotRetainsItsWake) {
  GMutex mutex;
  GCond condition;
  g_mutex_init(&mutex);
  g_cond_init(&condition);
  bool entered = false;
  bool release = false;
  std::atomic<unsigned> calls{0};
  testing::StrictMock<testing::MockFunction<void()>> retire;
  EXPECT_CALL(retire, Call()).Times(1);
  consent::IoContext* owner = nullptr;
  consent::IoContext io(
      [&] {
        if (++calls == 1) {
          g_mutex_lock(&mutex);
          entered = true;
          g_cond_signal(&condition);
          while (!release)
            g_cond_wait(&condition, &mutex);
          g_mutex_unlock(&mutex);
        } else {
          owner->Stop();
        }
      },
      [&] { retire.Call(); });
  owner = &io;
  io.Wake();
  ASSERT_TRUE(io.Start());
  g_mutex_lock(&mutex);
  const auto deadline = g_get_monotonic_time() + 5000000;
  while (!entered && g_cond_wait_until(&condition, &mutex, deadline)) {
  }
  EXPECT_TRUE(entered);
  // The first work snapshot is already taken and its source disarmed.
  io.Wake();
  release = true;
  g_cond_signal(&condition);
  g_mutex_unlock(&mutex);
  io.Join();
  EXPECT_EQ(calls, 2u);
  g_cond_clear(&condition);
  g_mutex_clear(&mutex);
}

TEST(ClientIo, StopBeforeRunSurvivesRetireException) {
  testing::StrictMock<testing::MockFunction<void()>> work;
  EXPECT_CALL(work, Call()).Times(0);
  consent::IoContext io([&] { work.Call(); },
                        [] { throw std::runtime_error("retire failure"); });
  io.Stop();
  ASSERT_TRUE(io.Start());
  io.Join();
}

TEST(ClientIo, SingleDeadlineDeliversWorkWithoutIdlePolling) {
  std::atomic<unsigned> calls{0};
  consent::IoContext* owner = nullptr;
  consent::IoContext io(
      [&] {
        if (++calls == 1)
          owner->Deadline(g_get_monotonic_time() + 20000);
        else
          owner->Stop();
      },
      [] {});
  owner = &io;
  io.Wake();
  ASSERT_TRUE(io.Start());
  io.Join();
  EXPECT_EQ(calls, 2u);
}

TEST(ClientIo, ChildAbandonsInheritedOwnersAndRejectsWait) {
  auto io = std::make_unique<consent::IoContext>([] {}, [] {});
  ASSERT_TRUE(io->Start());
  auto child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    bool accepted = io->Start() || io->Wait(nullptr, 1);
    io->Wake();
    io->Deadline(0);
    io->Stop();
    io->Join();
    io.reset();
    _exit(accepted ? 1 : 0);
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  io->Stop();
  io->Join();
}
}  // namespace
