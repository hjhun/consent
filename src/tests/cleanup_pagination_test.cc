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
#include "consentd/repository.hh"

#include "common/cleanup_cursor.hh"
#include "common/cleanup_sweep.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>

namespace {
using consent::Get;
using consent::Message;
using testing::_;
using testing::Invoke;
using testing::Return;

class Pagination : public testing::Test {
 protected:
  void SetUp() override {
    char pattern[] = "/tmp/consent-pagination-XXXXXX";
    const auto* created = mkdtemp(pattern);
    ASSERT_NE(created, nullptr);
    directory_ = created;
    ASSERT_EQ(mkdir((directory_ + "/registry").c_str(), 0700), 0);
    peer_.identity = "holder";
    peer_.instance = "instance";
    peer_.roles = {"holder", "session"};
    peer_.subjects = {"subject", "other"};
    peer_.profiles = {"profile", "other"};
    Start();
    auto opened = Call({{"method", "session_open"},
        {"subject", "subject"}, {"profile", "profile"}});
    session_ = Get(opened, "session");
  }
  void TearDown() override {
    repository_.reset();
    std::filesystem::remove_all(directory_);
  }
  void Start() {
    repository_ = std::make_unique<consentd::Repository>(
        directory_ + "/consent.db", directory_ + "/registry");
    std::string error;
    ASSERT_TRUE(repository_->Open(&error)) << error;
  }
  Message Call(Message request, int status = 0) {
    auto result = repository_->Execute(peer_, request);
    EXPECT_EQ(Get(result, "status"), std::to_string(status))
        << Get(result, "reason");
    return result;
  }
  Message Page(const std::string& cursor = "", bool reconcile = false,
      int status = 0) {
    return Call({{"method", "cleanup_list"}, {"subject", "subject"},
        {"profile", "profile"}, {"cursor", cursor},
        {"reconcile", reconcile ? "1" : "0"}}, status);
  }
  std::string Key(unsigned index) {
    char key[49];
    snprintf(key, sizeof(key), "%048x", index);
    return key;
  }
  void Seed(unsigned count, unsigned start = 1) {
    sqlite3* database = nullptr;
    ASSERT_EQ(sqlite3_open((directory_ + "/consent.db").c_str(), &database),
        SQLITE_OK);
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> owner(
        database, sqlite3_close);
    ASSERT_EQ(sqlite3_exec(database, "BEGIN", nullptr, nullptr, nullptr),
        SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(database,
        "INSERT INTO artifacts VALUES(?,?,?,'holder','instance',"
        "'purpose','','scope',9223372036854775807,"
        "'CLEANUP_PENDING',1,'')", -1, &statement, nullptr), SQLITE_OK);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> prepared(
        statement, sqlite3_finalize);
    for (unsigned index = start; index < start + count; ++index) {
      const auto id = Key(index);
      ASSERT_EQ(sqlite3_bind_text(statement, 1, id.c_str(), -1,
          SQLITE_TRANSIENT), SQLITE_OK);
      ASSERT_EQ(sqlite3_bind_text(statement, 2, id.c_str(), -1,
          SQLITE_TRANSIENT), SQLITE_OK);
      ASSERT_EQ(sqlite3_bind_text(statement, 3, session_.c_str(), -1,
          SQLITE_TRANSIENT), SQLITE_OK);
      ASSERT_EQ(sqlite3_step(statement), SQLITE_DONE);
      ASSERT_EQ(sqlite3_reset(statement), SQLITE_OK);
    }
    ASSERT_EQ(sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr),
        SQLITE_OK);
  }
  void Ack(unsigned index, bool success) {
    Call({{"method", "cleanup_ack"}, {"artifact", Key(index)},
        {"success", success ? "1" : "0"}});
  }
  consentd::Peer peer_;
  std::string directory_;
  std::string session_;
  std::unique_ptr<consentd::Repository> repository_;
};

TEST_F(Pagination, EmptyExactPageAndNewSweep) {
  EXPECT_EQ(Get(Page(), "count"), "0");
  EXPECT_EQ(Get(Page(), "next_cursor"), "");
  Seed(48);
  auto page = Page();
  EXPECT_EQ(Get(page, "count"), "48");
  EXPECT_EQ(Get(page, "more"), "0");
  EXPECT_EQ(Get(page, "next_cursor"), "");
  EXPECT_EQ(Get(Page(Get(page, "next_cursor")), "count"), "48");
}

TEST_F(Pagination, FailedFirst48DoNotStarve49) {
  Seed(49);
  auto first = Page();
  EXPECT_EQ(Get(first, "count"), "48");
  for (unsigned index = 1; index <= 48; ++index)
    Ack(index, false);
  auto last = Page(Get(first, "next_cursor"));
  EXPECT_EQ(Get(last, "count"), "1");
  EXPECT_EQ(Get(last, "a0.artifact"), Key(49));
  EXPECT_EQ(Get(last, "more"), "0");
  EXPECT_EQ(Get(last, "next_cursor"), "");
  Ack(49, true);
  auto retry = Page();
  EXPECT_EQ(Get(retry, "count"), "48");
  EXPECT_EQ(Get(retry, "a0.state"), "CLEANUP_FAILED");
}

TEST_F(Pagination, PagesSurviveAckRestartAndInterleavedInsert) {
  Seed(97);
  auto first = Page();
  const auto cursor = Get(first, "next_cursor");
  const auto old_epoch = Get(first, "epoch");
  Ack(1, true);
  Ack(49, true);
  Seed(1, 98);
  repository_.reset();
  Start();
  auto second = Page(cursor);
  EXPECT_NE(Get(second, "epoch"), old_epoch);
  EXPECT_EQ(Get(second, "count"), "48");
  EXPECT_EQ(Get(second, "a0.artifact"), Key(50));
  EXPECT_EQ(Get(second, "more"), "0");
  // The range was bounded at 97. Addition 98 belongs to the next sweep.
  auto fresh = Page();
  auto tail = Page(Get(fresh, "next_cursor"));
  EXPECT_EQ(Get(tail, "a47.artifact"), Key(98));
}

TEST_F(Pagination, ThreePagesWithoutDuplicates) {
  Seed(97);
  std::set<std::string> seen;
  std::string cursor;
  for (unsigned page = 0; page < 3; ++page) {
    auto result = Page(cursor);
    const auto count = consent::Number(result, "count");
    EXPECT_EQ(count, page == 2 ? 1 : 48);
    for (int64_t i = 0; i < count; ++i)
      EXPECT_TRUE(seen.insert(Get(result,
          "a" + std::to_string(i) + ".artifact")).second);
    cursor = Get(result, "next_cursor");
  }
  EXPECT_TRUE(cursor.empty());
  EXPECT_EQ(seen.size(), 97u);
}

TEST_F(Pagination, BoundScopeIncludesInstanceEvenForReconciliation) {
  Seed(49);
  auto cursor = Get(Page(), "next_cursor");
  auto reconcile_cursor = Get(Page("", true), "next_cursor");
  peer_.identity = "other";
  Page(cursor, false, -EACCES);
  peer_.identity = "holder";
  peer_.instance = "new-instance";
  Page(cursor, false, -EACCES);
  Page(reconcile_cursor, true, -EACCES);
  EXPECT_EQ(Get(Page("", true), "count"), "48");
  EXPECT_EQ(Get(Page(), "count"), "0");
  peer_.instance = "instance";
  Page(cursor, true, -EACCES);
  for (const auto* key : {"subject", "profile"}) {
    Message request{{"method", "cleanup_list"}, {"subject", "subject"},
        {"profile", "profile"}, {"cursor", cursor}};
    request[key] = "other";
    Call(request, -EACCES);
    request.erase("cursor");
    EXPECT_EQ(Get(Call(request), "count"), "0");
  }
}

TEST_F(Pagination, FullTokenValidationAndDatabaseReset) {
  Seed(49);
  const auto token = Get(Page(), "next_cursor");
  for (const auto& invalid : {std::string(), std::string("1"), token + "x",
      token.substr(0, token.size() - 1), std::string(8192, 'x')}) {
    if (!invalid.empty())
      Page(invalid, false, -EINVAL);
  }
  auto wrong_version = token;
  wrong_version[0] = '2';
  Page(wrong_version, false, -EINVAL);
  auto bad_hex = token;
  bad_hex[2] = 'G';
  Page(bad_hex, false, -EINVAL);
  consent::CleanupCursor parsed;
  ASSERT_TRUE(consent::CleanupCursor::Parse(token, &parsed));
  parsed.last = parsed.upper;
  Page(parsed.Encode(), false, -EINVAL);
  parsed.last = std::string(48, 'f');
  Page(parsed.Encode(), false, -EINVAL);
  ASSERT_EQ(unlink((directory_ + "/consent.db").c_str()), 0);
  Page(token, false, -ESTALE);
}

TEST_F(Pagination, PartialIndexAndBoundedWireReply) {
  Seed(97);
  auto result = Page();
  result["v"] = "1";
  result["id"] = "1";
  result["method"] = "reply";
  auto frame = consent::Encode(result);
  EXPECT_FALSE(frame.empty());
  EXPECT_LE(frame.size(), consent::kMaxFrameSize + 4);
  sqlite3* database = nullptr;
  ASSERT_EQ(sqlite3_open((directory_ + "/consent.db").c_str(), &database), 0);
  std::unique_ptr<sqlite3, decltype(&sqlite3_close)> owner(
      database, sqlite3_close);
  sqlite3_stmt* query = nullptr;
  ASSERT_EQ(sqlite3_prepare_v2(database,
      "EXPLAIN QUERY PLAN SELECT id FROM artifacts WHERE holder='holder' "
      "AND state IN ('CLEANUP_PENDING','CLEANUP_FAILED') "
      "AND id>'0' AND id<='f' ORDER BY id LIMIT 49", -1, &query, nullptr), 0);
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> prepared(
      query, sqlite3_finalize);
  ASSERT_EQ(sqlite3_step(query), SQLITE_ROW);
  const auto* plan =
      reinterpret_cast<const char*>(sqlite3_column_text(query, 3));
  EXPECT_THAT(plan, testing::HasSubstr("artifact_cleanup_key"));
}

class CleanupBackend {
 public:
  MOCK_METHOD(int, Fetch, (const Message&, Message*));
  MOCK_METHOD(int, DeleteAndAck, (const std::string&));
};

TEST(CleanupSweep, CollaboratorFailureDoesNotStopLaterPageAndReleasesCaptures) {
  testing::StrictMock<CleanupBackend> backend;
  consent::CleanupProgress progress;
  auto lifetime = std::make_shared<int>(1);
  std::weak_ptr<int> weak = lifetime;
  testing::InSequence sequence;
  EXPECT_CALL(backend, Fetch(_, _)).WillOnce(Invoke(
      [](const Message& input, Message* result) {
        EXPECT_EQ(Get(input, "cursor"), "");
        *result = {{"count", "1"}, {"a0.artifact", "failed"},
            {"more", "1"}, {"next_cursor", "next"}};
        return 0;
      }));
  EXPECT_CALL(backend, DeleteAndAck("failed")).WillOnce(Return(-EIO));
  EXPECT_CALL(backend, Fetch(_, _)).WillOnce(Invoke(
      [](const Message& input, Message* result) {
        EXPECT_EQ(Get(input, "cursor"), "next");
        *result = {{"count", "1"}, {"a0.artifact", "later"},
            {"more", "0"}, {"next_cursor", ""}};
        return 0;
      }));
  EXPECT_CALL(backend, DeleteAndAck("later")).WillOnce(Return(0));
  EXPECT_EQ(consent::CleanupSweep({}, &progress,
      [&backend, lifetime](const Message& input, Message* output) {
        return backend.Fetch(input, output);
      }, [&backend, lifetime](const std::string& artifact) {
        return backend.DeleteAndAck(artifact);
      }), -EIO);
  lifetime.reset();
  EXPECT_TRUE(weak.expired());
}

TEST(CleanupSweep, PageFailureStopsWithoutDeletingUnseenData) {
  testing::StrictMock<CleanupBackend> backend;
  consent::CleanupProgress progress;
  EXPECT_CALL(backend, Fetch(_, _)).WillOnce(Return(-EACCES));
  EXPECT_EQ(consent::CleanupSweep({}, &progress,
      [&backend](const Message& input, Message* output) {
        return backend.Fetch(input, output);
      }, [&backend](const std::string& artifact) {
        return backend.DeleteAndAck(artifact);
      }), -EACCES);
}

TEST(CleanupSweep, MoreThan128FailedPagesRetainContinuationAndFirstError) {
  testing::StrictMock<CleanupBackend> backend;
  consent::CleanupProgress progress;
  unsigned page = 0;
  EXPECT_CALL(backend, Fetch(_, _)).Times(129).WillRepeatedly(Invoke(
      [&page](const Message& input, Message* result) {
        EXPECT_EQ(Get(input, "cursor"), page ? std::to_string(page) : "");
        ++page;
        *result = {{"count", "48"},
            {"more", page == 129 ? "0" : "1"},
            {"next_cursor", page == 129 ? "" : std::to_string(page)}};
        for (unsigned index = 0; index < 48; ++index)
          (*result)["a" + std::to_string(index) + ".artifact"] =
              std::to_string((page - 1) * 48 + index);
        return 0;
      }));
  EXPECT_CALL(backend, DeleteAndAck(_)).Times(128 * 48)
      .WillRepeatedly(Return(-EIO));
  for (unsigned index = 6144; index < 6192; ++index)
    EXPECT_CALL(backend, DeleteAndAck(std::to_string(index)))
        .WillOnce(Return(0));
  auto fetch = [&backend](const Message& input, Message* output) {
    return backend.Fetch(input, output);
  };
  auto cleanup = [&backend](const std::string& artifact) {
    return backend.DeleteAndAck(artifact);
  };
  EXPECT_EQ(consent::CleanupSweep({}, &progress, fetch, cleanup), -EINPROGRESS);
  EXPECT_FALSE(progress.finished);
  EXPECT_EQ(progress.cursor, "128");
  EXPECT_EQ(progress.failure, -EIO);
  EXPECT_EQ(consent::CleanupSweep({}, &progress, fetch, cleanup), -EIO);
  EXPECT_TRUE(progress.finished);
  EXPECT_TRUE(progress.cursor.empty());
  EXPECT_EQ(page, 129u);
}
}  // namespace
