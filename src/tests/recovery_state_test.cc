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
#include "common/recovery_state.hh"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>

#include "common/resource.hh"

namespace {
using consent::recovery::Assess;
using consent::recovery::Boot;
using consent::recovery::Phase;
using consent::recovery::Receipt;

TEST(RecoveryState, SingleUseAndDatabaseOnlyRecovery) {
  EXPECT_EQ(Assess(Phase::kFresh, false, false, false), Boot::kReject);
  EXPECT_EQ(Assess(Phase::kClaimed, false, false, false), Boot::kReject);
  EXPECT_EQ(Assess(Phase::kClaimed, false, false, true), Boot::kFirst);
  EXPECT_EQ(Assess(Phase::kClaimed, true, false, true), Boot::kReject);
  EXPECT_EQ(Assess(Phase::kClaimed, true, true, true), Boot::kReject);
  EXPECT_EQ(Assess(Phase::kInitialized, false, false, false), Boot::kReject);
  EXPECT_EQ(Assess(Phase::kInitialized, false, true, false), Boot::kReject);
  EXPECT_EQ(Assess(Phase::kInitialized, true, false, false),
            Boot::kRecoverDatabase);
  EXPECT_EQ(Assess(Phase::kInitialized, true, true, false), Boot::kExisting);
  EXPECT_EQ(Assess(Phase::kInvalid, true, true, true), Boot::kReject);
  EXPECT_EQ(Assess(Phase::kRecoveryRequired, true, true, true), Boot::kReject);
}

TEST(RecoveryState, TicketFieldsRequireExactLowercaseHex) {
  EXPECT_TRUE(consent::recovery::HexId(std::string(32, 'a'), 32));
  EXPECT_FALSE(consent::recovery::HexId(std::string(31, 'a'), 32));
  EXPECT_FALSE(consent::recovery::HexId(std::string(33, 'a'), 32));
  EXPECT_FALSE(consent::recovery::HexId(std::string(32, 'A'), 32));
  EXPECT_FALSE(consent::recovery::HexId(std::string(32, 'g'), 32));
}

TEST(RecoveryState, ProtectedReceiptRoundTripAndMalformedFile) {
  if (geteuid() != 0)
    GTEST_SKIP() << "root-owned receipt requires root";
  char pattern[] = "/tmp/consent-receipt-XXXXXX";
  char* path = mkdtemp(pattern);
  ASSERT_NE(path, nullptr);
  consent::Descriptor owner(
      open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  ASSERT_GE(owner.Get(), 0);
  Receipt receipt;
  receipt.phase = Phase::kFresh;
  receipt.unit = "consentd-isolated.service";
  ASSERT_TRUE(consent::recovery::WriteReceipt(owner.Get(), getegid(), receipt));
  Receipt read;
  EXPECT_TRUE(consent::recovery::ReadReceipt(owner.Get(), getegid(), &read));
  EXPECT_EQ(read.phase, Phase::kFresh);
  EXPECT_EQ(read.unit, receipt.unit);
  receipt.phase = Phase::kClaimed;
  receipt.invocation = std::string(32, 'a');
  receipt.nonce = std::string(32, 'b');
  ASSERT_TRUE(consent::recovery::WriteReceipt(owner.Get(), getegid(), receipt));
  EXPECT_TRUE(consent::recovery::ReadReceipt(owner.Get(), getegid(), &read));
  EXPECT_EQ(read.phase, Phase::kClaimed);
  EXPECT_EQ(read.invocation, receipt.invocation);
  EXPECT_EQ(read.nonce, receipt.nonce);
  consent::Descriptor corrupt(
      openat(owner.Get(), "bootstrap.receipt",
             O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC));
  ASSERT_GE(corrupt.Get(), 0);
  const char invalid[] = "schema=1\nphase=INITIALIZED\n";
  ASSERT_EQ(write(corrupt.Get(), invalid, sizeof(invalid) - 1),
            static_cast<ssize_t>(sizeof(invalid) - 1));
  corrupt.Reset();
  EXPECT_FALSE(consent::recovery::ReadReceipt(owner.Get(), getegid(), &read));
  EXPECT_EQ(unlinkat(owner.Get(), "bootstrap.receipt", 0), 0);
  owner.Reset();
  EXPECT_EQ(rmdir(path), 0);
}
}  // namespace
