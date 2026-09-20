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
#include "repository.hh"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <sqlite3.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace consentd {
namespace {

using consent::Message;
constexpr int kStorage = -2006;
constexpr int kConflict = -2005;
constexpr int kInactive = -2003;
constexpr int kClosed = -2004;
constexpr size_t kRegistryLimit = 4 * 1024 * 1024;
constexpr size_t kMaxDefinitions = 2048;
constexpr int64_t kDayMs = 24 * 60 * 60 * 1000;

class Failure final : public std::runtime_error {
 public:
  Failure(int status, const std::string& message, int sqlite_code = SQLITE_OK)
      : std::runtime_error(message), status_(status), sqlite_code_(sqlite_code) {}
  int Status() const { return status_; }
  int SqliteCode() const { return sqlite_code_; }
 private:
  int status_;
  int sqlite_code_;
};

void Require(bool condition, int status, const char* reason) {
  if (!condition)
    throw Failure(status, reason);
}

std::string Get(const Message& message, const std::string& key,
    const std::string& fallback = "") {
  auto it = message.find(key);
  return it == message.end() ? fallback : it->second;
}

int64_t Integer(const std::string& value, int64_t minimum, int64_t maximum) {
  Require(!value.empty() && value.size() <= 20, -EINVAL, "invalid integer");
  for (char ch : value)
    Require(ch >= '0' && ch <= '9', -EINVAL, "invalid integer");
  errno = 0;
  char* end = nullptr;
  long long number = strtoll(value.c_str(), &end, 10);
  Require(errno == 0 && end && *end == '\0' && number >= minimum &&
      number <= maximum, -EINVAL, "integer outside policy bounds");
  return number;
}

int64_t Number(const Message& message, const std::string& key,
    int64_t fallback, int64_t minimum, int64_t maximum) {
  auto it = message.find(key);
  return it == message.end() ? fallback : Integer(it->second, minimum, maximum);
}

int64_t Now() { return g_get_monotonic_time() / 1000; }

std::string Id() {
  unsigned char bytes[24];
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  Require(fd >= 0, kStorage, "random source unavailable");
  size_t done = 0;
  while (done < sizeof(bytes)) {
    ssize_t count = read(fd, bytes + done, sizeof(bytes) - done);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      close(fd);
      throw Failure(kStorage, "random source failed");
    }
    done += count;
  }
  close(fd);
  static const char hex[] = "0123456789abcdef";
  std::string result;
  for (unsigned char byte : bytes) {
    result += hex[byte >> 4];
    result += hex[byte & 15];
  }
  return result;
}

std::string Hash(const std::string& value) {
  gchar* raw = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      reinterpret_cast<const guchar*>(value.data()), value.size());
  Require(raw != nullptr, -ENOMEM, "checksum allocation failed");
  std::string result(raw);
  g_free(raw);
  return result;
}

void Append(std::string* output, const std::string& value) {
  *output += std::to_string(value.size()) + ":" + value;
}

std::string Pack(const Message& message) {
  std::string result;
  for (const auto& item : message) {
    Append(&result, item.first);
    Append(&result, item.second);
  }
  return result;
}

Message Unpack(const std::string& value) {
  Message message;
  size_t offset = 0;
  auto next = [&]() {
    size_t end = value.find(':', offset);
    Require(end != std::string::npos && end - offset <= 10,
        kStorage, "malformed stored metadata");
    auto size = static_cast<size_t>(Integer(value.substr(offset, end - offset),
        0, kRegistryLimit));
    offset = end + 1;
    Require(size <= value.size() - offset, kStorage, "truncated stored metadata");
    std::string part = value.substr(offset, size);
    offset += size;
    return part;
  };
  while (offset < value.size()) {
    std::string key = next();
    std::string item = next();
    Require(message.emplace(key, item).second, kStorage, "duplicate stored key");
  }
  return message;
}

bool Identifier(const std::string& value) {
  if (value.empty() || value.size() > 256)
    return false;
  for (unsigned char ch : value) {
    if (!(g_ascii_isalnum(ch) || ch == '.' || ch == '_' || ch == ':' || ch == '-'))
      return false;
  }
  return true;
}

bool Member(const std::set<std::string>& values, const std::string& value) {
  return values.count(value) || values.count("*");
}

void Context(const Peer& peer, const Message& request) {
  Require(Identifier(Get(request, "subject")) && Identifier(Get(request, "profile")),
      -EINVAL, "subject and profile required");
  Require(Member(peer.subjects, Get(request, "subject")) &&
      Member(peer.profiles, Get(request, "profile")), -EACCES,
      "subject or profile delegation denied");
}

bool Role(const Peer& peer, const std::string& role) {
  return peer.roles.count(role) != 0;
}

std::string Parent(const std::string& path) {
  auto slash = path.rfind('/');
  Require(slash != std::string::npos && slash > 0, kStorage,
      "absolute storage path required");
  return path.substr(0, slash);
}

void SecureDirectory(const std::string& path) {
  struct stat st = {};
  Require(lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
      st.st_uid == geteuid() && !(st.st_mode & 0022), kStorage,
      "storage directory ownership or mode rejected");
  // All ancestors must be real directories; the final protected directory
  // may live under sticky /tmp only for a directly instantiated private test.
  size_t pos = 1;
  while ((pos = path.find('/', pos)) != std::string::npos) {
    std::string prefix = path.substr(0, pos++);
    Require(lstat(prefix.c_str(), &st) == 0 && S_ISDIR(st.st_mode),
        kStorage, "storage ancestor is not a directory");
    Require(!(st.st_mode & 0022) || (st.st_mode & S_ISVTX), kStorage,
        "storage ancestor is writable");
  }
}

void SecureFile(const struct stat& st) {
  Require(S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
      !(st.st_mode & 0077) && st.st_nlink == 1, kStorage,
      "storage file ownership or mode rejected");
}

class Statement final {
 public:
  Statement(sqlite3* db, const char* sql) : db_(db) {
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (rc != SQLITE_OK)
      throw Failure(kStorage, sqlite3_errmsg(db_), rc);
  }
  ~Statement() { sqlite3_finalize(stmt_); }
  Statement& Bind(int index, const std::string& value) {
    Require(sqlite3_bind_text(stmt_, index, value.data(), value.size(),
        SQLITE_TRANSIENT) == SQLITE_OK, kStorage, "SQL bind failed");
    return *this;
  }
  Statement& Bind(int index, int64_t value) {
    Require(sqlite3_bind_int64(stmt_, index, value) == SQLITE_OK,
        kStorage, "SQL bind failed");
    return *this;
  }
  bool Row() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW)
      return true;
    if (rc == SQLITE_DONE)
      return false;
    throw Failure((rc & 0xff) == SQLITE_BUSY || (rc & 0xff) == SQLITE_LOCKED ? -EBUSY : kStorage,
        sqlite3_errmsg(db_), rc);
  }
  void Run() { Require(!Row(), kStorage, "unexpected SQL row"); }
  std::string Text(int index) const {
    const auto* text = sqlite3_column_text(stmt_, index);
    return text ? std::string(reinterpret_cast<const char*>(text),
        sqlite3_column_bytes(stmt_, index)) : "";
  }
  int64_t Int(int index) const { return sqlite3_column_int64(stmt_, index); }
 private:
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};

void Sql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  std::string reason = error ? error : "SQL failed";
  sqlite3_free(error);
  if (rc != SQLITE_OK)
    throw Failure((rc & 0xff) == SQLITE_BUSY || (rc & 0xff) == SQLITE_LOCKED ? -EBUSY : kStorage,
        reason, rc);
}

class Transaction final {
 public:
  explicit Transaction(sqlite3* db) : db_(db) { Sql(db_, "BEGIN IMMEDIATE"); }
  ~Transaction() {
    if (!done_)
      sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
  }
  void Commit() { Sql(db_, "COMMIT"); done_ = true; }
 private:
  sqlite3* db_;
  bool done_ = false;
};

const char kSchema[] = R"SQL(
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
INSERT OR IGNORE INTO meta VALUES('revision','0');
INSERT OR IGNORE INTO meta VALUES('registry_revision','0');
INSERT OR IGNORE INTO meta VALUES('cleanup_unknown','0');
CREATE TABLE IF NOT EXISTS definitions(id TEXT PRIMARY KEY,package TEXT NOT NULL,
 app TEXT NOT NULL,active INTEGER NOT NULL,config TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS definition_package ON definitions(package);
CREATE TABLE IF NOT EXISTS sessions(id TEXT PRIMARY KEY,subject TEXT NOT NULL,
 profile TEXT NOT NULL,owner TEXT NOT NULL,instance TEXT NOT NULL,
 state TEXT NOT NULL,generation INTEGER NOT NULL,policy TEXT NOT NULL,
 created INTEGER NOT NULL,idle_deadline INTEGER NOT NULL,
 absolute_deadline INTEGER NOT NULL,lease_deadline INTEGER NOT NULL,
 grace INTEGER NOT NULL,resume_deadline INTEGER NOT NULL,resume_hash TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS requests(id TEXT PRIMARY KEY,owner TEXT NOT NULL,
 subject TEXT NOT NULL,profile TEXT NOT NULL,client_id TEXT NOT NULL,
 fingerprint TEXT NOT NULL,payload TEXT NOT NULL,state TEXT NOT NULL,
 deadline INTEGER NOT NULL,token TEXT NOT NULL DEFAULT '',
 ui_owner TEXT NOT NULL DEFAULT '',ui_instance TEXT NOT NULL DEFAULT '',
 UNIQUE(owner,subject,profile,client_id));
CREATE INDEX IF NOT EXISTS request_state ON requests(state,deadline);
CREATE TABLE IF NOT EXISTS grants(id TEXT PRIMARY KEY,key TEXT NOT NULL,
 definition TEXT NOT NULL REFERENCES definitions(id),version TEXT NOT NULL,
 subject TEXT NOT NULL,profile TEXT NOT NULL,session TEXT NOT NULL,
 mode TEXT NOT NULL,expires INTEGER NOT NULL,remaining INTEGER NOT NULL,
 revoked INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS grant_key ON grants(key,revoked);
CREATE TABLE IF NOT EXISTS authorizations(id TEXT PRIMARY KEY,enforcer TEXT NOT NULL,
 operation TEXT NOT NULL,step TEXT NOT NULL,fingerprint TEXT NOT NULL,
 payload TEXT NOT NULL,created INTEGER NOT NULL,valid INTEGER NOT NULL DEFAULT 1,
 UNIQUE(enforcer,operation,step));
CREATE TABLE IF NOT EXISTS authorization_grants(receipt TEXT NOT NULL
 REFERENCES authorizations(id),grant_id TEXT NOT NULL REFERENCES grants(id),
 PRIMARY KEY(receipt,grant_id));
CREATE TABLE IF NOT EXISTS artifacts(id TEXT PRIMARY KEY,receipt TEXT NOT NULL,
 session TEXT NOT NULL REFERENCES sessions(id),holder TEXT NOT NULL,
 instance TEXT NOT NULL,purpose TEXT NOT NULL,recipient TEXT NOT NULL,
 scope TEXT NOT NULL,expires INTEGER NOT NULL,state TEXT NOT NULL,
 level INTEGER NOT NULL,cleanup_error TEXT NOT NULL DEFAULT '',
 UNIQUE(receipt,holder,instance));
CREATE TABLE IF NOT EXISTS artifact_grants(artifact TEXT NOT NULL REFERENCES artifacts(id),
 grant_id TEXT NOT NULL REFERENCES grants(id),PRIMARY KEY(artifact,grant_id));
CREATE TABLE IF NOT EXISTS artifact_parents(child TEXT NOT NULL REFERENCES artifacts(id),
 parent TEXT NOT NULL REFERENCES artifacts(id),PRIMARY KEY(child,parent));
CREATE TABLE IF NOT EXISTS cleanup_acknowledgements(artifact TEXT PRIMARY KEY
 REFERENCES artifacts(id),holder TEXT NOT NULL,instance TEXT NOT NULL,
 success INTEGER NOT NULL,acknowledged INTEGER NOT NULL);
PRAGMA user_version=2;
)SQL";

}  // namespace

class Repository::Impl final {
 public:
  Impl(std::string path, std::string recovery_dir)
      : path_(std::move(path)), recovery_dir_(std::move(recovery_dir)),
        registry_path_(recovery_dir_ + "/definitions.registry") {}
  ~Impl() {
    if (db_)
      sqlite3_close(db_);
  }

  bool Open(std::string* error);
  Message Execute(const Peer& peer, const Message& request);
  Message Snapshot();
  void Tick();
  void Shutdown();
  InstallationValidator validator_;
  std::function<bool(const std::string&, const std::string&)> package_validator_;

 private:
  void OpenDatabase(bool recovering);
  void RetireDatabase(bool main_file);
  void Ensure();
  void ReadRegistry(bool initial);
  void WriteRegistry(const std::map<std::string, Message>& definitions,
      const Message& operations, int64_t revision);
  void Replay();
  void Bump();
  void Expire();
  void Invalidate(const std::string& definition);
  void CloseSession(const std::string& session);
  void SuspendSession(const std::string& session);
  void FinishCleanup();
  Message Register(const Peer& peer, const Message& request, bool remove);
  Message Evaluate(const Peer& peer, const Message& request, bool create);
  Message Prompt(const Peer& peer, const Message& request, bool respond);
  Message Result(const Peer& peer, const Message& request, bool cancel);
  Message Session(const Peer& peer, const Message& request);
  Message Data(const Peer& peer, const Message& request);
  Message Revoke(const Peer& peer, const Message& request);
  Message Definition(const std::string& id);
  Message SessionState(const Peer& peer, const Message& request,
      bool active, bool owner = false);
  Message RequestRow(const Peer& peer, const Message& request, bool ui);
  Message CheckConditions(const Peer& peer, const Message& request,
      std::vector<std::string>* grants, bool enforce);
  std::string GrantKey(const Message& request, int index,
      const Message& definition);
  bool ReceiptValid(const std::string& receipt);
  void ValidateArtifactProvenance(const Peer& peer, const Message& request,
      const std::string& artifact);
  void ValidateDefinition(const Message& definition);
  void ResetRuntime();

  std::string path_;
  std::string recovery_dir_;
  std::string registry_path_;
  sqlite3* db_ = nullptr;
  struct stat db_identity_ = {};
  struct stat registry_identity_ = {};
  std::string epoch_;
  int64_t revision_ = 0;
  int64_t registry_revision_ = 0;
  bool fenced_ = false;
  bool cleanup_unknown_ = false;
  bool corrupt_ = false;
  int64_t expected_device_ = 0;
  int64_t expected_inode_ = 0;
  std::string expected_incarnation_;
  std::map<std::string, Message> definitions_;
  Message operations_;
};

void Repository::Impl::ReadRegistry(bool initial) {
  struct stat st = {};
  if (lstat(registry_path_.c_str(), &st) < 0) {
    Require(errno == ENOENT && initial, kStorage,
        "definition registry missing: installer re-registration required");
    struct stat db_stat = {};
    Require(lstat(path_.c_str(), &db_stat) < 0 && errno == ENOENT,
        kStorage, "existing database has no trusted definition registry");
    WriteRegistry({}, {}, 0);
    return;
  }
  SecureFile(st);
  Require(st.st_size > 0 && st.st_size <= static_cast<off_t>(kRegistryLimit),
      kStorage, "definition registry size rejected");
  int fd = open(registry_path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  Require(fd >= 0, kStorage, "cannot open definition registry");
  struct stat opened = {};
  if (fstat(fd, &opened) < 0 || opened.st_dev != st.st_dev ||
      opened.st_ino != st.st_ino) {
    close(fd);
    throw Failure(kStorage, "registry changed while opening");
  }
  std::string content(st.st_size, '\0');
  size_t offset = 0;
  while (offset < content.size()) {
    ssize_t count = read(fd, &content[offset], content.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      close(fd);
      throw Failure(kStorage, "cannot read definition registry");
    }
    offset += count;
  }
  close(fd);
  Message root = Unpack(content);
  Require(Get(root, "schema") == "1", kStorage, "unsupported registry schema");
  auto revision = Integer(Get(root, "revision"), 0, INT64_MAX);
  expected_device_ = Integer(Get(root, "db_device", "0"), 0, INT64_MAX);
  expected_inode_ = Integer(Get(root, "db_inode", "0"), 0, INT64_MAX);
  expected_incarnation_ = Get(root, "db_incarnation");
  auto expected = Get(root, "checksum");
  root.erase("checksum");
  Require(Hash(Pack(root)) == expected, kStorage, "registry checksum mismatch");
  std::map<std::string, Message> definitions;
  Message operations;
  for (const auto& item : root) {
    if (item.first.compare(0, 2, "d.") == 0) {
      Message definition = Unpack(item.second);
      ValidateDefinition(definition);
      Require(item.first.substr(2) == Get(definition, "definition"),
          kStorage, "registry definition key mismatch");
      definitions.emplace(item.first.substr(2), std::move(definition));
    } else if (item.first.compare(0, 2, "o.") == 0) {
      operations.emplace(item.first.substr(2), item.second);
    } else {
      Require(item.first == "schema" || item.first == "revision" ||
          item.first == "db_device" || item.first == "db_inode" ||
          item.first == "db_incarnation", kStorage,
          "unknown registry field");
    }
  }
  Require(definitions.size() <= kMaxDefinitions && operations.size() <= 8192,
      kStorage, "registry capacity exceeded");
  definitions_ = std::move(definitions);
  operations_ = std::move(operations);
  registry_revision_ = revision;
  registry_identity_ = st;
}

void Repository::Impl::WriteRegistry(
    const std::map<std::string, Message>& definitions,
    const Message& operations, int64_t revision) {
  Message root = {{"schema", "1"}, {"revision", std::to_string(revision)},
      {"db_device", std::to_string(expected_device_)},
      {"db_inode", std::to_string(expected_inode_)},
      {"db_incarnation", expected_incarnation_}};
  for (const auto& item : definitions)
    root["d." + item.first] = Pack(item.second);
  for (const auto& item : operations)
    root["o." + item.first] = item.second;
  root["checksum"] = Hash(Pack(root));
  std::string content = Pack(root);
  Require(content.size() <= kRegistryLimit && operations.size() <= 8192,
      -ENOSPC, "definition registry capacity exceeded");
  std::string temporary = recovery_dir_ + "/.registry-" + Id();
  int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
      O_NOFOLLOW, 0600);
  Require(fd >= 0, kStorage, "cannot create registry temporary file");
  size_t offset = 0;
  bool ok = true;
  while (offset < content.size()) {
    ssize_t count = write(fd, content.data() + offset, content.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) { ok = false; break; }
    offset += count;
  }
  if (ok && fsync(fd) < 0)
    ok = false;
  if (close(fd) < 0)
    ok = false;
  if (!ok || rename(temporary.c_str(), registry_path_.c_str()) < 0) {
    unlink(temporary.c_str());
    throw Failure(kStorage, "definition registry write failed");
  }
  // A rename failure and a directory fsync failure differ: after rename the
  // authority may have advanced. Fence even when durability is uncertain.
  fenced_ = true;
  int dir = open(recovery_dir_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC |
      O_NOFOLLOW);
  ok = dir >= 0 && fsync(dir) == 0;
  if (dir >= 0)
    close(dir);
  Require(ok, kStorage, "definition registry directory sync failed");
  definitions_ = definitions;
  operations_ = operations;
  registry_revision_ = revision;
  Require(lstat(registry_path_.c_str(), &registry_identity_) == 0,
      kStorage, "cannot stat committed registry");
}

void Repository::Impl::RetireDatabase(bool main_file) {
  if (db_) {
    Require(sqlite3_close(db_) == SQLITE_OK, kStorage, "cannot retire live database handle");
    db_ = nullptr;
  }
  std::string suffix = ".retired-" + Id();
  for (const char* sidecar : {"", "-journal", "-wal", "-shm"}) {
    if (!*sidecar && !main_file)
      continue;
    std::string source = path_ + sidecar;
    struct stat st = {};
    if (lstat(source.c_str(), &st) < 0) {
      Require(errno == ENOENT, kStorage, "cannot inspect database artifacts");
      continue;
    }
    Require(rename(source.c_str(), (source + suffix).c_str()) == 0,
        kStorage, "cannot quarantine database artifact");
  }
  int dir = open(Parent(path_).c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  bool synced = dir >= 0 && fsync(dir) == 0;
  if (dir >= 0)
    close(dir);
  Require(synced, kStorage, "cannot sync database quarantine");
}

void Repository::Impl::OpenDatabase(bool recovering) {
  SecureDirectory(Parent(path_));
  struct stat st = {};
  bool existed = lstat(path_.c_str(), &st) == 0;
  if (existed)
    SecureFile(st);
  else
    Require(errno == ENOENT, kStorage, "cannot stat database");
  if (existed && expected_inode_ != 0 &&
      (static_cast<int64_t>(st.st_dev) != expected_device_ ||
       static_cast<int64_t>(st.st_ino) != expected_inode_)) {
    RetireDatabase(true);
    existed = false;
    recovering = true;
  }
  if (!existed) {
    // A main file lost independently of its hot journal must never let that
    // journal replay old pages into a new database.
    RetireDatabase(false);
    recovering = recovering || expected_inode_ != 0;
    int fd = open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
        O_NOFOLLOW, 0600);
    Require(fd >= 0, kStorage, "cannot create database");
    close(fd);
  }
  for (const char* sidecar : {"-journal", "-wal", "-shm"}) {
    struct stat artifact = {};
    if (lstat((path_ + sidecar).c_str(), &artifact) == 0)
      SecureFile(artifact);
    else
      Require(errno == ENOENT, kStorage, "cannot inspect SQLite sidecar");
  }
  corrupt_ = false;
  int rc = sqlite3_open_v2(path_.c_str(), &db_, SQLITE_OPEN_READWRITE |
      SQLITE_OPEN_NOMUTEX, nullptr);
  Require(rc == SQLITE_OK, kStorage, "cannot open database");
  sqlite3_extended_result_codes(db_, 1);
  sqlite3_busy_timeout(db_, 100);
  // Unknown journal artifacts cannot safely be discarded. SQLite owns hot
  // rollback recovery; all file permissions remain confined to this directory.
  Sql(db_, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=EXTRA;"
      "PRAGMA foreign_keys=ON; PRAGMA secure_delete=ON;");
  {
    Statement mode(db_, "PRAGMA journal_mode");
    Require(mode.Row() && mode.Text(0) == "delete", kStorage, "journal mode rejected");
    Statement sync(db_, "PRAGMA synchronous");
    Require(sync.Row() && sync.Int(0) == 3, kStorage, "EXTRA synchronization unavailable");
    Statement keys(db_, "PRAGMA foreign_keys");
    Require(keys.Row() && keys.Int(0) == 1, kStorage, "foreign keys unavailable");
    Statement integrity(db_, "PRAGMA quick_check");
    if (!integrity.Row() || integrity.Text(0) != "ok") {
      corrupt_ = true;
      throw Failure(kStorage, "database corrupt");
    }
    Statement version(db_, "PRAGMA user_version");
    Require(version.Row() && version.Int(0) <= 2, kStorage, "unsupported database schema");
  }
  {
    Transaction transaction(db_);
    Sql(db_, kSchema);
    transaction.Commit();
  }
  std::string incarnation;
  {
    Statement query(db_, "SELECT value FROM meta WHERE key='incarnation'");
    if (query.Row())
      incarnation = query.Text(0);
  }
  if (existed && !expected_incarnation_.empty() && incarnation != expected_incarnation_) {
    RetireDatabase(true);
    OpenDatabase(true);
    return;
  }
  if (incarnation.empty()) {
    incarnation = Id();
    Statement store(db_, "INSERT OR REPLACE INTO meta VALUES('incarnation',?)");
    store.Bind(1, incarnation).Run();
  }
  Require(lstat(path_.c_str(), &db_identity_) == 0, kStorage, "database disappeared");
  SecureFile(db_identity_);
  epoch_ = Id();
  if (recovering)
    Sql(db_, "UPDATE meta SET value='1' WHERE key='cleanup_unknown'");
  Statement cleanup(db_, "SELECT value FROM meta WHERE key='cleanup_unknown'");
  Require(cleanup.Row(), kStorage, "cleanup metadata missing");
  cleanup_unknown_ = cleanup.Int(0) != 0;
  if (expected_device_ != static_cast<int64_t>(db_identity_.st_dev) ||
      expected_inode_ != static_cast<int64_t>(db_identity_.st_ino) ||
      expected_incarnation_ != incarnation) {
    expected_incarnation_ = incarnation;
    expected_device_ = db_identity_.st_dev;
    expected_inode_ = db_identity_.st_ino;
    WriteRegistry(definitions_, operations_, registry_revision_);
  }
  Replay();
  ResetRuntime();
}

bool Repository::Impl::Open(std::string* error) {
  try {
    SecureDirectory(recovery_dir_);
    ReadRegistry(true);
    try {
      OpenDatabase(false);
    } catch (const Failure& failure) {
      int rc = failure.SqliteCode();
      // Only SQLite's explicit corruption classifications authorize replacing
      // the database. Permissions, ENOSPC, IOERR, and migrations remain fenced.
      if ((rc & 0xff) != SQLITE_CORRUPT && (rc & 0xff) != SQLITE_NOTADB && !corrupt_)
        throw;
      RetireDatabase(true);
      OpenDatabase(true);
    }
    return true;
  } catch (const std::exception& failure) {
    fenced_ = true;
    if (error)
      *error = failure.what();
    return false;
  }
}

void Repository::Impl::Bump() {
  Statement update(db_, "UPDATE meta SET value=CAST(value AS INTEGER)+1 WHERE key='revision'");
  update.Run();
}

void Repository::Impl::Invalidate(const std::string& definition) {
  Statement grants(db_, "UPDATE grants SET revoked=1 WHERE definition=?");
  grants.Bind(1, definition).Run();
  Statement auth(db_, "UPDATE authorizations SET valid=0 WHERE id IN "
      "(SELECT receipt FROM authorization_grants JOIN grants ON grant_id=grants.id "
      "WHERE definition=?)");
  auth.Bind(1, definition).Run();
  Statement artifacts(db_, "UPDATE artifacts SET state='CLEANUP_PENDING' WHERE state='ACTIVE' "
      "AND id IN (SELECT artifact FROM artifact_grants JOIN grants ON grant_id=grants.id "
      "WHERE definition=?)");
  artifacts.Bind(1, definition).Run();
  Statement pending(db_, "SELECT id,payload FROM requests WHERE state='PENDING'");
  std::vector<std::string> ids;
  while (pending.Row()) {
    auto payload = Unpack(pending.Text(1));
    for (int i = 0; i < Number(payload, "count", 0, 0, 16); ++i) {
      if (Get(payload, "r" + std::to_string(i) + ".definition") == definition) {
        ids.push_back(pending.Text(0));
        break;
      }
    }
  }
  for (const auto& id : ids) {
    Statement update(db_, "UPDATE requests SET state='INVALIDATED',token='' WHERE id=?");
    update.Bind(1, id).Run();
  }
}

void Repository::Impl::Replay() {
  // Establish a successful persistence barrier on every reconciliation path,
  // including startup. A previous rename whose fsync failed must not become
  // trusted merely because its new inode can be read after a retry/restart.
  int directory = open(recovery_dir_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  bool durable = directory >= 0 && fsync(directory) == 0;
  if (directory >= 0)
    close(directory);
  Require(durable, kStorage, "registry authority durability remains uncertain");
  Transaction transaction(db_);
  bool changed = false;
  for (const auto& item : definitions_) {
    const auto& config = item.second;
    bool active = Get(config, "active", "1") == "1";
    if (active)
      active = validator_ && validator_(Get(config, "package"), Get(config, "app"),
          Get(config, "_install_identity"));
    Message projected = config;
    projected["active"] = active ? "1" : "0";
    std::string packed = Pack(projected);
    Statement current(db_, "SELECT config FROM definitions WHERE id=?");
    current.Bind(1, item.first);
    if (current.Row() && current.Text(0) == packed)
      continue;
    changed = true;
    Invalidate(item.first);
    Statement update(db_, "INSERT INTO definitions VALUES(?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET package=excluded.package,app=excluded.app,"
        "active=excluded.active,config=excluded.config");
    update.Bind(1, item.first).Bind(2, Get(config, "package"))
        .Bind(3, Get(config, "app")).Bind(4, active ? 1 : 0).Bind(5, packed).Run();
  }
  Statement current_revision(db_, "SELECT value FROM meta WHERE key='registry_revision'");
  Require(current_revision.Row(), kStorage, "registry projection revision missing");
  changed = changed || current_revision.Int(0) != registry_revision_;
  Statement mark(db_, "UPDATE meta SET value=? WHERE key='registry_revision'");
  mark.Bind(1, registry_revision_).Run();
  if (changed)
    Bump();
  transaction.Commit();
  fenced_ = false;
}

void Repository::Impl::ResetRuntime() {
  Transaction transaction(db_);
  Sql(db_, "UPDATE requests SET state='INVALIDATED',token='' WHERE state='PENDING';"
      "UPDATE sessions SET state='CLOSING',generation=generation+1 WHERE state IN ('ACTIVE','SUSPENDED');"
      "UPDATE grants SET revoked=1 WHERE mode<>'PERSISTENT';"
      "UPDATE authorizations SET valid=0;"
      "UPDATE artifacts SET state='CLEANUP_PENDING' WHERE state='ACTIVE';");
  FinishCleanup();
  Bump();
  transaction.Commit();
}

void Repository::Impl::Ensure() {
  Require(db_ != nullptr, kStorage, "database unavailable");
  SecureDirectory(Parent(path_));
  SecureDirectory(recovery_dir_);
  struct stat registry = {};
  Require(lstat(registry_path_.c_str(), &registry) == 0, kStorage,
      "definition registry missing: re-registration required");
  SecureFile(registry);
  if (registry.st_dev != registry_identity_.st_dev ||
      registry.st_ino != registry_identity_.st_ino ||
      registry.st_size != registry_identity_.st_size ||
      registry.st_mtim.tv_sec != registry_identity_.st_mtim.tv_sec ||
      registry.st_mtim.tv_nsec != registry_identity_.st_mtim.tv_nsec) {
    fenced_ = true;
    ReadRegistry(false);
  }
  struct stat st = {};
  int rc = lstat(path_.c_str(), &st);
  if (rc < 0)
    Require(errno == ENOENT, kStorage, "database stat failed");
  if (rc == 0) {
    SecureFile(st);
    if (st.st_size < 512)
      corrupt_ = true;
  }
  if (corrupt_) {
    fenced_ = true;
    RetireDatabase(rc == 0);
    OpenDatabase(true);
    return;
  }
  bool incarnation_matches = true;
  if (rc == 0 && st.st_dev == db_identity_.st_dev && st.st_ino == db_identity_.st_ino) {
    Statement incarnation(db_, "SELECT value FROM meta WHERE key='incarnation'");
    incarnation_matches = incarnation.Row() && incarnation.Text(0) == expected_incarnation_;
  }
  if (rc < 0 || st.st_dev != db_identity_.st_dev || st.st_ino != db_identity_.st_ino ||
      !incarnation_matches) {
    fenced_ = true;
    RetireDatabase(rc == 0);
    OpenDatabase(true);
  } else if (fenced_) {
    if (corrupt_) {
      RetireDatabase(true);
      OpenDatabase(true);
    } else {
      Replay();
    }
  }
  Require(!fenced_, kStorage, "repository is fenced");
}

Message Repository::Impl::Snapshot() {
  Ensure();
  Statement revision(db_, "SELECT value FROM meta WHERE key='revision'");
  Require(revision.Row(), kStorage, "revision metadata missing");
  revision_ = revision.Int(0);
  return {{"status", "0"}, {"epoch", epoch_},
      {"revision", std::to_string(revision_)}, {"source", "DAEMON"},
      {"cleanup_reconciliation_required", cleanup_unknown_ ? "1" : "0"}};
}

Message Repository::Impl::Definition(const std::string& id) {
  Statement statement(db_, "SELECT config FROM definitions WHERE id=? AND active=1");
  statement.Bind(1, id);
  Require(statement.Row(), -ENOENT, "definition unavailable");
  return Unpack(statement.Text(0));
}

void Repository::Impl::ValidateDefinition(const Message& definition) {
  for (const char* key : {"definition", "package", "app", "enforcer", "default_locale"})
    Require(Identifier(Get(definition, key)), -EINVAL, "invalid definition identity");
  Require(!Get(definition, "_install_identity").empty(), -EINVAL,
      "trusted installation identity required");
  Integer(Get(definition, "policy_version"), 1, INT32_MAX);
  Integer(Get(definition, "text_revision"), 1, INT32_MAX);
  int level = Integer(Get(definition, "level"), 0, 3);
  std::string modes = Get(definition, "modes");
  Require(!modes.empty(), -EINVAL, "allowed grant modes required");
  std::set<std::string> seen;
  size_t start = 0;
  do {
    size_t end = modes.find(',', start);
    std::string mode = modes.substr(start, end == std::string::npos ? end : end - start);
    Require((mode == "ONCE" || mode == "SESSION" || mode == "TIMED" ||
        mode == "PERSISTENT") && seen.insert(mode).second, -EINVAL,
        "invalid allowed grant mode");
    Require(level < 3 || mode == "ONCE", -EINVAL,
        "level 3 permits ONCE only in initial policy");
    if (end == std::string::npos)
      break;
    start = end + 1;
  } while (true);
  Number(definition, "retention_ms", 0, 0, kDayMs);
  size_t messages = 0;
  for (const auto& field : definition) {
    if (field.first.compare(0, 8, "message.") != 0)
      continue;
    Require(field.second.size() <= 4096 && !field.second.empty() &&
        g_utf8_validate(field.second.data(), field.second.size(), nullptr),
        -EINVAL, "invalid localized message");
    // This first format deliberately has no template evaluator. Parameters
    // remain separately displayed exact-scope fields, never untyped expansion.
    Require(field.second.find('{') == std::string::npos &&
        field.second.find('}') == std::string::npos, -EINVAL,
        "message placeholders require a future typed schema");
    ++messages;
  }
  std::string prefix = "message." + Get(definition, "default_locale");
  Require(messages >= 2 && messages <= 32 &&
      !Get(definition, prefix + ".title").empty() &&
      !Get(definition, prefix + ".body").empty(), -EINVAL,
      "default locale title and body required");
}

Message Repository::Impl::Register(const Peer& peer, const Message& request,
    bool remove) {
  Require(Role(peer, "installer"), -EACCES, "installer role required");
  std::string package = Get(request, "package");
  Require(Identifier(package) && Identifier(Get(request, "operation_id")),
      -EINVAL, "package and installation operation required");
  Require(Member(peer.packages, package), -EACCES, "package delegation denied");
  std::string key = Hash(peer.identity + ":" + Get(request, "operation_id"));
  Message input = request;
  input.erase("id");
  input.erase("protocol_request_id");
  std::string fingerprint = Hash(Pack(input));
  if (remove) {
    std::string expected = Get(request, "expected_generation");
    Require(!expected.empty(), -EINVAL, "expected installation generation required");
    Require(package_validator_ && package_validator_(package, expected), -ESTALE,
        "package generation authority rejected uninstall");
    for (const auto& item : definitions_) {
      if (Get(item.second, "package") == package && Get(item.second, "active") == "1")
        Require(Get(item.second, "_install_identity") == expected, -ESTALE,
            "stale package uninstall operation");
    }
  }
  auto prior = operations_.find(key);
  if (prior != operations_.end()) {
    Require(prior->second == fingerprint, kConflict, "installation operation payload conflict");
    return {{"status", "0"}};
  }
  auto definitions = definitions_;
  if (remove) {
    std::string expected = Get(request, "expected_generation");
    Require(!expected.empty(), -EINVAL, "expected installation generation required");
    for (const auto& item : definitions) {
      if (Get(item.second, "package") != package || Get(item.second, "active") != "1")
        continue;
      Require(Get(item.second, "_install_identity") == expected, -ESTALE,
          "stale package uninstall operation");
    }
    for (auto& item : definitions) {
      if (Get(item.second, "package") == package)
        item.second["active"] = "0";
    }
  } else {
    Message definition;
    for (const auto& item : request) {
      if (item.first == "definition" || item.first == "package" || item.first == "app" ||
          item.first == "enforcer" || item.first == "policy_version" ||
          item.first == "text_revision" || item.first == "level" || item.first == "modes" ||
          item.first == "default_locale" || item.first == "retention_ms" ||
          item.first == "_install_identity" || item.first.compare(0, 8, "message.") == 0)
        definition.insert(item);
    }
    definition["active"] = "1";
    ValidateDefinition(definition);
    Require(validator_ && validator_(package, Get(definition, "app"),
        Get(definition, "_install_identity")), -EACCES, "installed package identity mismatch");
    std::string id = Get(definition, "definition");
    auto found = definitions.find(id);
    if (found != definitions.end()) {
      Require(Get(found->second, "package") == package &&
          Get(found->second, "app") == Get(definition, "app"), -EACCES,
          "definition namespace belongs to another app");
      auto old_policy = Integer(Get(found->second, "policy_version"), 1, INT32_MAX);
      auto new_policy = Integer(Get(definition, "policy_version"), 1, INT32_MAX);
      Require(new_policy >= old_policy, kConflict, "policy rollback rejected");
      if (new_policy == old_policy && Get(found->second, "active") == "1" &&
          Get(found->second, "_install_identity") == Get(definition, "_install_identity")) {
        for (const char* field : {"enforcer", "level", "modes", "retention_ms"})
          Require(Get(found->second, field) == Get(definition, field), kConflict,
              "policy meaning changed without version increase");
        auto old_text = Integer(Get(found->second, "text_revision"), 1, INT32_MAX);
        auto new_text = Integer(Get(definition, "text_revision"), 1, INT32_MAX);
        Require(new_text >= old_text, kConflict, "text revision rollback rejected");
        if (new_text == old_text)
          Require(found->second == definition, kConflict, "same revision differs");
      }
    }
    definitions[id] = std::move(definition);
  }
  Require(definitions.size() <= kMaxDefinitions, -ENOSPC, "definition limit exceeded");
  Message operations = operations_;
  operations[key] = fingerprint;
  WriteRegistry(definitions, operations, registry_revision_ + 1);
  Replay();
  return {{"status", "0"}};
}

Message Repository::Impl::SessionState(const Peer& peer, const Message& request,
    bool active, bool owner) {
  Context(peer, request);
  std::string id = Get(request, "session");
  if (id.empty()) {
    Require(!owner, -EINVAL, "session required");
    return {};
  }
  Statement query(db_, "SELECT subject,profile,owner,instance,state,generation,policy,"
      "idle_deadline,absolute_deadline,lease_deadline,resume_deadline,resume_hash "
      "FROM sessions WHERE id=?");
  query.Bind(1, id);
  Require(query.Row(), -ENOENT, "session not found");
  Require(query.Text(0) == Get(request, "subject") &&
      query.Text(1) == Get(request, "profile"), -EACCES, "session context mismatch");
  if (owner)
    Require(query.Text(2) == peer.identity && query.Text(3) == peer.instance,
        -EACCES, "session owner instance mismatch");
  if (active) {
    Require(query.Text(4) != "CLOSING" && query.Text(4) != "CLOSED",
        kClosed, "session closed");
    Require(query.Text(4) == "ACTIVE", kInactive, "session suspended");
    Require(query.Int(5) == Number(request, "generation", 0, 1, INT64_MAX),
        -ESTALE, "session generation mismatch");
    Require(query.Int(7) > Now() && query.Int(8) > Now() && query.Int(9) > Now(),
        kInactive, "session deadline expired");
  }
  return {{"session", id}, {"state", query.Text(4)},
      {"generation", std::to_string(query.Int(5))}, {"policy", query.Text(6)},
      {"idle_deadline", std::to_string(query.Int(7))},
      {"absolute_deadline", std::to_string(query.Int(8))},
      {"lease_deadline", std::to_string(query.Int(9))},
      {"resume_deadline", std::to_string(query.Int(10))},
      {"resume_hash", query.Text(11)}};
}

void Repository::Impl::CloseSession(const std::string& session) {
  Statement close(db_, "UPDATE sessions SET state='CLOSING',generation=generation+1 "
      "WHERE id=? AND state IN ('ACTIVE','SUSPENDED')");
  close.Bind(1, session).Run();
  Statement grants(db_, "UPDATE grants SET revoked=1 WHERE session=? AND mode='SESSION'");
  grants.Bind(1, session).Run();
  Statement artifacts(db_, "UPDATE artifacts SET state='CLEANUP_PENDING' WHERE session=? AND state='ACTIVE'");
  artifacts.Bind(1, session).Run();
  Statement pending(db_, "SELECT id,payload FROM requests WHERE state='PENDING'");
  std::vector<std::string> ids;
  while (pending.Row()) {
    if (Get(Unpack(pending.Text(1)), "session") == session)
      ids.push_back(pending.Text(0));
  }
  for (const auto& id : ids) {
    Statement update(db_, "UPDATE requests SET state='INVALIDATED',token='' WHERE id=?");
    update.Bind(1, id).Run();
  }
}

void Repository::Impl::SuspendSession(const std::string& session) {
  Statement suspend(db_, "UPDATE sessions SET state='SUSPENDED',generation=generation+1,"
      "resume_deadline=?+grace WHERE id=? AND state='ACTIVE'");
  suspend.Bind(1, Now()).Bind(2, session).Run();
  Statement pending(db_, "SELECT id,payload FROM requests WHERE state='PENDING'");
  std::vector<std::string> ids;
  while (pending.Row()) {
    if (Get(Unpack(pending.Text(1)), "session") == session)
      ids.push_back(pending.Text(0));
  }
  for (const auto& id : ids) {
    Statement invalidate(db_, "UPDATE requests SET state='INVALIDATED',token='' WHERE id=?");
    invalidate.Bind(1, id).Run();
  }
}

void Repository::Impl::FinishCleanup() {
  Sql(db_, "UPDATE sessions SET state='CLOSED' WHERE state='CLOSING' AND NOT EXISTS "
      "(SELECT 1 FROM artifacts WHERE session=sessions.id AND state<>'DELETED')");
}

void Repository::Impl::Expire() {
  Transaction transaction(db_);
  Statement expired(db_, "UPDATE requests SET state='EXPIRED',token='' WHERE state='PENDING' AND deadline<=?");
  expired.Bind(1, Now()).Run();
  bool changed = sqlite3_changes(db_) != 0;
  Statement sessions(db_, "SELECT id,policy,state,idle_deadline,absolute_deadline FROM sessions WHERE "
      "(state='ACTIVE' AND (idle_deadline<=? OR absolute_deadline<=? OR lease_deadline<=?)) "
      "OR (state='SUSPENDED' AND (resume_deadline<=? OR absolute_deadline<=? OR idle_deadline<=?))");
  for (int i = 1; i <= 6; ++i)
    sessions.Bind(i, Now());
  std::vector<std::string> close;
  std::vector<std::string> suspend;
  while (sessions.Row()) {
    if (sessions.Text(1) == "RESUMABLE_CONVERSATION" && sessions.Text(2) == "ACTIVE" &&
        sessions.Int(3) > Now() && sessions.Int(4) > Now())
      suspend.push_back(sessions.Text(0));
    else
      close.push_back(sessions.Text(0));
  }
  for (const auto& id : close)
    CloseSession(id);
  for (const auto& id : suspend)
    SuspendSession(id);
  changed = changed || !close.empty() || !suspend.empty();
  Statement artifacts(db_, "UPDATE artifacts SET state='CLEANUP_PENDING' WHERE state='ACTIVE' AND expires<=?");
  artifacts.Bind(1, Now()).Run();
  changed = changed || sqlite3_changes(db_) != 0;
  FinishCleanup();
  if (changed)
    Bump();
  transaction.Commit();
}

Message Repository::Impl::Session(const Peer& peer, const Message& request) {
  std::string method = Get(request, "method");
  Context(peer, request);
  if (method == "session_get_state" || method == "cleanup_get_state") {
    auto state = SessionState(peer, request, false);
    state.erase("resume_hash");
    Statement count(db_, "SELECT count(*) FROM artifacts WHERE session=? AND state<>'DELETED'");
    count.Bind(1, Get(request, "session"));
    count.Row();
    state["cleanup_pending"] = std::to_string(count.Int(0));
    return state;
  }
  Require(Role(peer, "session"), -EACCES, "session controller role required");
  Transaction transaction(db_);
  Message result;
  if (method == "session_open") {
    Statement count(db_, "SELECT count(*) FROM sessions WHERE state IN ('ACTIVE','SUSPENDED')");
    count.Row();
    Require(count.Int(0) < 128, -EBUSY, "active session capacity exceeded");
    std::string policy = Get(request, "lifecycle", "CONNECTION_BOUND");
    Require(policy == "CONNECTION_BOUND" || policy == "RESUMABLE_CONVERSATION",
        -EINVAL, "invalid session lifecycle");
    auto idle = Number(request, "idle_timeout_ms", 600000, 100, kDayMs);
    auto maximum = Number(request, "max_lifetime_ms", 3600000, 100, kDayMs);
    auto lease = Number(request, "lease_ms", 30000, 100, 60000);
    auto grace = Number(request, "reconnect_grace_ms", 30000, 100, 300000);
    std::string id = Id();
    std::string token = Id();
    Statement insert(db_, "INSERT INTO sessions VALUES(?,?,?,?,?,'ACTIVE',1,?,?,?,?,?,?,0,?)");
    insert.Bind(1, id).Bind(2, Get(request, "subject")).Bind(3, Get(request, "profile"))
        .Bind(4, peer.identity).Bind(5, peer.instance).Bind(6, policy).Bind(7, Now())
        .Bind(8, Now() + idle).Bind(9, Now() + maximum).Bind(10, Now() + lease)
        .Bind(11, grace).Bind(12, Hash(token)).Run();
    result = {{"session", id}, {"generation", "1"}, {"state", "ACTIVE"},
        {"resume_token", token}};
  } else {
    auto state = SessionState(peer, request, false, true);
    auto generation = Integer(Get(state, "generation"), 1, INT64_MAX);
    Require(generation == Number(request, "generation", 0, 1, INT64_MAX),
        -ESTALE, "session generation mismatch");
    std::string id = Get(state, "session");
    if (method == "session_close") {
      CloseSession(id);
      FinishCleanup();
    } else if (method == "session_suspend") {
      Require(Get(state, "state") == "ACTIVE", kInactive, "session is not active");
      if (Get(state, "policy") == "CONNECTION_BOUND") {
        CloseSession(id);
        FinishCleanup();
      } else {
        SuspendSession(id);
      }
    } else if (method == "session_resume") {
      Require(Get(state, "state") == "SUSPENDED" &&
          Integer(Get(state, "resume_deadline"), 0, INT64_MAX) > Now(),
          kInactive, "session resume window closed");
      Require(!Get(request, "resume_token").empty() &&
          Hash(Get(request, "resume_token")) == Get(state, "resume_hash"),
          -EACCES, "resume proof mismatch");
      std::string token = Id();
      Statement resume(db_, "UPDATE sessions SET state='ACTIVE',generation=generation+1,"
          "resume_hash=?,lease_deadline=? WHERE id=?");
      resume.Bind(1, Hash(token)).Bind(2, Now() + 30000).Bind(3, id).Run();
      result["resume_token"] = token;
    } else if (method == "session_heartbeat") {
      Require(Get(state, "state") == "ACTIVE", kInactive, "session is not active");
      Statement heartbeat(db_, "UPDATE sessions SET lease_deadline=? WHERE id=?");
      heartbeat.Bind(1, Now() + 30000).Bind(2, id).Run();
    } else {
      throw Failure(-ENOSYS, "session method unsupported");
    }
    auto current = SessionState(peer, request, false, true);
    current.erase("resume_hash");
    result.insert(current.begin(), current.end());
  }
  Bump();
  transaction.Commit();
  return result;
}

std::string Repository::Impl::GrantKey(const Message& request, int index,
    const Message& definition) {
  Message fields = {{"subject", Get(request, "subject")},
      {"profile", Get(request, "profile")}, {"definition", Get(definition, "definition")},
      {"version", Get(definition, "policy_version")}};
  std::string prefix = "r" + std::to_string(index) + ".";
  for (const char* field : {"scope", "operation", "purpose", "recipient", "holder"})
    fields[field] = Get(request, prefix + field);
  return Pack(fields);  // Exact canonical values are compared, not only a hash.
}

Message Repository::Impl::CheckConditions(const Peer& peer, const Message& request,
    std::vector<std::string>* grants, bool enforce) {
  auto count = Number(request, "count", 0, 1, 16);
  Require(count >= 1, -EINVAL, "requirements required");
  Message result = {{"decision", "ALLOWED"}, {"count", std::to_string(count)}};
  std::set<std::string> unique;
  bool required = false;
  bool denied = false;
  bool cacheable = true;
  for (int i = 0; i < count; ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    for (const char* field : {"definition", "operation", "purpose"})
      Require(Identifier(Get(request, prefix + field)), -EINVAL, "invalid requirement field");
    Require(Get(request, prefix + "scope").size() <= 4096, -EINVAL, "scope too large");
    Message definition;
    try {
      definition = Definition(Get(request, prefix + "definition"));
    } catch (const Failure& failure) {
      if (failure.Status() != -ENOENT)
        throw;
      result[prefix + "decision"] = "DENIED";
      denied = true;
      continue;
    }
    Require(validator_ && validator_(Get(definition, "package"), Get(definition, "app"),
        Get(definition, "_install_identity")), -ESTALE, "installation generation changed");
    if (!Get(request, prefix + "policy_version").empty())
      Require(Get(request, prefix + "policy_version") == Get(definition, "policy_version"),
          -ESTALE, "policy version mismatch");
    if (enforce || Get(request, "method") == "check")
      Require(peer.identity == Get(definition, "enforcer") ||
          Member(peer.enforcers, Get(definition, "enforcer")), -EACCES,
          "enforcement owner mismatch");
    std::string key = GrantKey(request, i, definition);
    Require(unique.insert(key).second, -EINVAL, "duplicate requirement");
    Statement grant(db_, "SELECT id,mode FROM grants WHERE key=? AND revoked=0 "
        "AND (expires=0 OR expires>?) AND remaining<>0 AND "
        "(mode<>'SESSION' OR session=?) ORDER BY rowid DESC LIMIT 1");
    grant.Bind(1, key).Bind(2, Now()).Bind(3, Get(request, "session"));
    if (grant.Row()) {
      grants->push_back(grant.Text(0));
      result[prefix + "decision"] = "ALLOWED";
      cacheable = cacheable && (grant.Text(1) == "PERSISTENT" ||
          grant.Text(1) == "SESSION") && Get(definition, "level") != "3";
    } else {
      result[prefix + "decision"] = "CONSENT_REQUIRED";
      required = true;
      cacheable = false;
    }
    result[prefix + "policy_version"] = Get(definition, "policy_version");
    result[prefix + "text_revision"] = Get(definition, "text_revision");
  }
  result["decision"] = denied ? "DENIED" : required ? "CONSENT_REQUIRED" : "ALLOWED";
  int64_t cache_ttl = 500;
  if (!Get(request, "session").empty()) {
    auto state = SessionState(peer, request, true);
    for (const char* key : {"idle_deadline", "absolute_deadline", "lease_deadline"})
      cache_ttl = std::min(cache_ttl, Integer(Get(state, key), 0, INT64_MAX) - Now());
    result["session"] = Get(request, "session");
    result["generation"] = Get(request, "generation");
  }
  result["cacheable"] = cacheable && !enforce && !required && !denied && cache_ttl > 0 ? "1" : "0";
  result["cache_ttl_ms"] = std::to_string(std::max<int64_t>(cache_ttl, 0));
  return result;
}

bool Repository::Impl::ReceiptValid(const std::string& receipt) {
  Statement query(db_, "SELECT valid,payload FROM authorizations WHERE id=?");
  query.Bind(1, receipt);
  if (!query.Row() || !query.Int(0))
    return false;
  Message payload = Unpack(query.Text(1));
  Statement revoked(db_, "SELECT count(*) FROM authorization_grants JOIN grants ON grant_id=grants.id "
      "WHERE receipt=? AND (revoked<>0 OR (expires<>0 AND expires<=?))");
  revoked.Bind(1, receipt).Bind(2, Now());
  revoked.Row();
  if (revoked.Int(0))
    return false;
  for (int i = 0; i < Number(payload, "count", 0, 1, 16); ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    Message definition = Definition(Get(payload, prefix + "definition"));
    if (!Get(payload, prefix + "policy_version").empty() &&
        Get(payload, prefix + "policy_version") != Get(definition, "policy_version"))
      return false;
    if (!validator_ || !validator_(Get(definition, "package"), Get(definition, "app"),
        Get(definition, "_install_identity")))
      return false;
  }
  return true;
}

Message Repository::Impl::Evaluate(const Peer& peer, const Message& request,
    bool create) {
  Require(create ? Role(peer, "argo") : (Role(peer, "checker") || Role(peer, "cm") ||
      Role(peer, "ce") || Role(peer, "holder")), -EACCES, "caller role denied");
  Context(peer, request);
  Transaction transaction(db_);
  SessionState(peer, request, true);
  std::string mode = Get(request, "mode", "QUERY");
  Require(mode == "QUERY" || mode == "AUTHORIZE", -EINVAL, "invalid check mode");
  bool authorize = !create && mode == "AUTHORIZE";
  Message payload;
  for (const char* key : {"subject", "profile", "session", "generation", "operation_id",
      "step_id", "client_request_id", "deadline_ms", "count"}) {
    auto field = request.find(key);
    if (field != request.end())
      payload.insert(*field);
  }
  for (int i = 0; i < Number(request, "count", 0, 0, 16); ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    for (const char* key : {"definition", "scope", "operation", "purpose", "recipient",
        "holder", "policy_version"}) {
      auto field = request.find(prefix + key);
      if (field != request.end())
        payload.insert(*field);
    }
  }
  if (authorize) {
    payload.erase("client_request_id");
    payload.erase("deadline_ms");
  }
  std::string fingerprint = Hash(Pack(payload));
  if (authorize) {
    Require(Identifier(Get(request, "operation_id")) && Identifier(Get(request, "step_id")),
        -EINVAL, "operation and execution step required");
    Statement prior(db_, "SELECT id,fingerprint FROM authorizations WHERE enforcer=? AND operation=? AND step=?");
    prior.Bind(1, peer.identity).Bind(2, Get(request, "operation_id")).Bind(3, Get(request, "step_id"));
    if (prior.Row()) {
      Require(prior.Text(1) == fingerprint, kConflict, "operation payload conflict");
      Require(ReceiptValid(prior.Text(0)), -ESTALE, "authorization was invalidated");
      transaction.Commit();
      return {{"decision", "ALLOWED"}, {"receipt", prior.Text(0)}, {"retry", "1"}};
    }
  }
  if (create) {
    Require(Identifier(Get(request, "client_request_id")) && Identifier(Get(request, "operation_id")),
        -EINVAL, "stable client request and operation required");
    Statement prior(db_, "SELECT id,state,fingerprint FROM requests WHERE owner=? AND subject=? AND profile=? AND client_id=?");
    prior.Bind(1, peer.identity).Bind(2, Get(request, "subject")).Bind(3, Get(request, "profile"))
        .Bind(4, Get(request, "client_request_id"));
    if (prior.Row()) {
      Require(prior.Text(2) == fingerprint, kConflict, "request payload conflict");
      transaction.Commit();
      return {{"request_id", prior.Text(0)}, {"decision", prior.Text(1)}};
    }
  }
  std::vector<std::string> grants;
  Message result = CheckConditions(peer, request, &grants, authorize);
  if (authorize && Get(result, "decision") == "ALLOWED") {
    std::string receipt = Id();
    Statement insert(db_, "INSERT INTO authorizations VALUES(?,?,?,?,?,?,?,1)");
    insert.Bind(1, receipt).Bind(2, peer.identity).Bind(3, Get(request, "operation_id"))
        .Bind(4, Get(request, "step_id")).Bind(5, fingerprint).Bind(6, Pack(payload))
        .Bind(7, Now()).Run();
    for (const auto& grant : grants) {
      Statement use(db_, "UPDATE grants SET remaining=remaining-1 WHERE id=? AND mode='ONCE' AND remaining>0");
      use.Bind(1, grant).Run();
      Statement source(db_, "INSERT INTO authorization_grants VALUES(?,?)");
      source.Bind(1, receipt).Bind(2, grant).Run();
    }
    result["receipt"] = receipt;
    Bump();
  }
  if (create) {
    bool pending = Get(result, "decision") == "CONSENT_REQUIRED";
    Statement capacity(db_, "SELECT count(*) FROM requests WHERE state='PENDING'");
    capacity.Row();
    Require(!pending || capacity.Int(0) < 256, -EBUSY, "pending request capacity exceeded");
    auto deadline = Number(request, "deadline_ms", 60000, 100, 300000);
    auto id = Id();
    for (const auto& item : result) {
      if (item.first.compare(0, 1, "r") == 0)
        payload[item.first] = item.second;
    }
    Statement insert(db_, "INSERT INTO requests(id,owner,subject,profile,client_id,"
        "fingerprint,payload,state,deadline) VALUES(?,?,?,?,?,?,?,?,?)");
    insert.Bind(1, id).Bind(2, peer.identity).Bind(3, Get(request, "subject"))
        .Bind(4, Get(request, "profile")).Bind(5, Get(request, "client_request_id"))
        .Bind(6, fingerprint).Bind(7, Pack(payload))
        .Bind(8, pending ? "PENDING" : Get(result, "decision")).Bind(9, Now() + deadline).Run();
    result["decision"] = pending ? "PENDING" : Get(result, "decision");
    result["request_id"] = id;
    Bump();
  }
  transaction.Commit();
  return result;
}

Message Repository::Impl::RequestRow(const Peer& peer, const Message& request,
    bool ui) {
  std::string id = Get(request, "request_id");
  if (id.empty())
    Context(peer, request);
  Statement query(db_, "SELECT id,owner,payload,state,deadline,token,ui_owner,ui_instance "
      "FROM requests WHERE id=? OR (?='' AND owner=? AND client_id=? AND subject=? AND profile=?)");
  query.Bind(1, id).Bind(2, id).Bind(3, peer.identity).Bind(4, Get(request, "client_request_id"))
      .Bind(5, Get(request, "subject")).Bind(6, Get(request, "profile"));
  Require(query.Row(), -ENOENT, "request not found");
  Message result = Unpack(query.Text(2));
  Context(peer, result);
  Require(ui || query.Text(1) == peer.identity, -EACCES, "request owner mismatch");
  result["request_id"] = query.Text(0);
  result["decision"] = query.Text(3);
  result["deadline"] = query.Text(4);
  result["prompt_token"] = query.Text(5);
  result["ui_owner"] = query.Text(6);
  result["ui_instance"] = query.Text(7);
  return result;
}

Message Repository::Impl::Result(const Peer& peer, const Message& request,
    bool cancel) {
  Require(Role(peer, "argo"), -EACCES, "argo role required");
  Transaction transaction(db_);
  Message result = RequestRow(peer, request, false);
  if (cancel && Get(result, "decision") == "PENDING") {
    Statement update(db_, "UPDATE requests SET state='CANCELLED',token='' WHERE id=? AND state='PENDING'");
    update.Bind(1, Get(result, "request_id")).Run();
    result["decision"] = "CANCELLED";
    Bump();
  }
  transaction.Commit();
  result.erase("prompt_token");
  result.erase("ui_owner");
  result.erase("ui_instance");
  return result;
}

Message Repository::Impl::Prompt(const Peer& peer, const Message& request,
    bool respond) {
  Require(Role(peer, "ui"), -EACCES, "approval UI role required");
  Transaction transaction(db_);
  auto pending = RequestRow(peer, request, true);
  Require(Get(pending, "decision") == "PENDING", -ESTALE, "request is already final");
  Require(Integer(Get(pending, "deadline"), 0, INT64_MAX) > Now(),
      -ETIMEDOUT, "request deadline expired");
  SessionState(peer, pending, true);
  auto count = Number(pending, "count", 0, 1, 16);
  std::vector<Message> definitions;
  for (int i = 0; i < count; ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    Message definition = Definition(Get(pending, prefix + "definition"));
    Require(validator_ && validator_(Get(definition, "package"), Get(definition, "app"),
        Get(definition, "_install_identity")), -ESTALE, "installation changed");
    Require(Get(definition, "policy_version") == Get(pending, prefix + "policy_version") &&
        Get(definition, "text_revision") == Get(pending, prefix + "text_revision"),
        -ESTALE, "displayed policy or text changed");
    definitions.push_back(std::move(definition));
  }
  Message result;
  if (!respond) {
    std::string locale = Get(request, "locale");
    Require(Identifier(locale), -EINVAL, "locale required");
    for (const char* key : {"request_id", "subject", "profile", "session", "generation", "count"})
      result[key] = Get(pending, key);
    for (int i = 0; i < count; ++i) {
      std::string row = "r" + std::to_string(i) + ".";
      for (const char* key : {"definition", "scope", "operation", "purpose", "recipient",
          "holder", "policy_version", "text_revision"})
        result[row + key] = Get(pending, row + key);
      auto& definition = definitions[i];
      std::string selected = locale;
      // Only the explicitly supported language-region fallbacks are reduced.
      // Unknown scripts/regions use the registered default as a whole.
      if (Get(definition, "message." + selected + ".title").empty()) {
        if (locale == "ko-KR") selected = "ko";
        else if (locale == "en-US" || locale == "en-GB") selected = "en";
      }
      if (Get(definition, "message." + selected + ".title").empty() ||
          Get(definition, "message." + selected + ".body").empty())
        selected = Get(definition, "default_locale");
      std::string prefix = "r" + std::to_string(i) + ".";
      result[prefix + "locale"] = selected;
      result[prefix + "title"] = Get(definition, "message." + selected + ".title");
      result[prefix + "body"] = Get(definition, "message." + selected + ".body");
      result[prefix + "modes"] = Get(definition, "modes");
      result[prefix + "level"] = Get(definition, "level");
      result[prefix + "retention_ms"] = Get(definition, "retention_ms", "0");
    }
    size_t response_bytes = 256;
    for (const auto& field : result)
      response_bytes += 10 + field.first.size() + field.second.size();
    Require(result.size() <= 240 && response_bytes <= consent::kMaxFrameSize,
        -E2BIG, "combined localized prompt exceeds frame budget");
    std::string token = Id();
    Statement update(db_, "UPDATE requests SET token=?,ui_owner=?,ui_instance=? WHERE id=?");
    update.Bind(1, Hash(token)).Bind(2, peer.identity).Bind(3, peer.instance)
        .Bind(4, Get(pending, "request_id")).Run();
    result["prompt_token"] = token;
    result.erase("ui_owner");
    result.erase("ui_instance");
  } else {
    Require(!Get(request, "prompt_token").empty() &&
        Hash(Get(request, "prompt_token")) == Get(pending, "prompt_token") &&
        Get(pending, "ui_owner") == peer.identity && Get(pending, "ui_instance") == peer.instance,
        -EACCES, "prompt token or UI instance mismatch");
    std::string decision = Get(request, "decision");
    Require(decision == "ALLOWED" || decision == "DENIED", -EINVAL,
        "invalid UI decision");
    Message reevaluated;
    std::string mode = Get(request, "grant_mode", "ONCE");
    if (decision == "ALLOWED") {
      Require(mode == "ONCE" || mode == "SESSION" || mode == "TIMED" || mode == "PERSISTENT",
          -EINVAL, "invalid grant mode");
      Require(mode != "SESSION" || !Get(pending, "session").empty(),
          -EINVAL, "SESSION grant requires active session");
      for (int i = 0; i < count; ++i) {
        auto& definition = definitions[i];
        std::string allowed = "," + Get(definition, "modes") + ",";
        Require(allowed.find("," + mode + ",") != std::string::npos,
            -EACCES, "grant mode is not permitted by policy");
        // Existing conditions are not granted a second ONCE allowance.
        if (Get(pending, "r" + std::to_string(i) + ".decision") == "ALLOWED")
          continue;
        int64_t expires = mode == "TIMED" ? Now() +
            Number(request, "duration_ms", 300000, 100, 3600000) : 0;
        Statement grant(db_, "INSERT INTO grants VALUES(?,?,?,?,?,?,?,?,?,?,0)");
        grant.Bind(1, Id()).Bind(2, GrantKey(pending, i, definition))
            .Bind(3, Get(definition, "definition")).Bind(4, Get(definition, "policy_version"))
            .Bind(5, Get(pending, "subject")).Bind(6, Get(pending, "profile"))
            .Bind(7, mode == "SESSION" ? Get(pending, "session") : "")
            .Bind(8, mode).Bind(9, expires).Bind(10, mode == "ONCE" ? 1 : -1).Run();
      }
    }
    // Conditions that were satisfied when the prompt opened can expire,
    // be revoked or be consumed while UI waits. Recheck the entire AND in
    // this transaction without consuming ONCE grants or adopting UI roles.
    std::vector<std::string> grants;
    reevaluated = CheckConditions(peer, pending, &grants, false);
    if (decision == "ALLOWED" && Get(reevaluated, "decision") != "ALLOWED")
      decision = "INVALIDATED";
    if (decision == "DENIED") {
      for (int i = 0; i < count; ++i) {
        std::string key = "r" + std::to_string(i) + ".decision";
        if (Get(pending, key) != "ALLOWED")
          reevaluated[key] = "DENIED";
      }
    }
    Message final_payload = pending;
    for (const char* field : {"request_id", "decision", "deadline", "prompt_token", "ui_owner", "ui_instance"})
      final_payload.erase(field);
    for (int i = 0; i < count; ++i) {
      std::string key = "r" + std::to_string(i) + ".decision";
      final_payload[key] = Get(reevaluated, key);
    }
    if (decision == "INVALIDATED") {
      final_payload["reason"] = "authorization conditions changed while awaiting approval";
      reevaluated["reason"] = final_payload["reason"];
    }
    Statement update(db_, "UPDATE requests SET state=?,token='',payload=? WHERE id=? AND state='PENDING'");
    update.Bind(1, decision).Bind(2, Pack(final_payload)).Bind(3, Get(pending, "request_id")).Run();
    result = std::move(reevaluated);
    result["request_id"] = Get(pending, "request_id");
    result["decision"] = decision;
    result["cacheable"] = "0";
  }
  Bump();
  transaction.Commit();
  return result;
}

Message Repository::Impl::Revoke(const Peer& peer, const Message& request) {
  Require(Role(peer, "admin"), -EACCES, "approval administration role required");
  Context(peer, request);
  Require(Identifier(Get(request, "definition")), -EINVAL, "definition required");
  Transaction transaction(db_);
  Statement grants(db_, "UPDATE grants SET revoked=1 WHERE definition=? AND subject=? AND profile=?");
  grants.Bind(1, Get(request, "definition")).Bind(2, Get(request, "subject"))
      .Bind(3, Get(request, "profile")).Run();
  Sql(db_, "UPDATE authorizations SET valid=0 WHERE id IN (SELECT receipt FROM authorization_grants "
      "JOIN grants ON grants.id=grant_id WHERE revoked<>0);"
      "UPDATE artifacts SET state='CLEANUP_PENDING' WHERE state='ACTIVE' AND id IN "
      "(SELECT artifact FROM artifact_grants JOIN grants ON grants.id=grant_id WHERE revoked<>0)");
  Bump();
  transaction.Commit();
  return {};
}

void Repository::Impl::ValidateArtifactProvenance(const Peer& peer, const Message& request,
    const std::string& artifact) {
  Context(peer, request);
  SessionState(peer, request, true);
  Statement metadata(db_, "SELECT session,holder,instance,purpose,recipient,scope,expires,state "
      "FROM artifacts WHERE id=?");
  metadata.Bind(1, artifact);
  Require(metadata.Row(), -ENOENT, "artifact not found");
  Require(metadata.Text(0) == Get(request, "session") && metadata.Text(1) == peer.identity &&
      metadata.Text(2) == peer.instance && metadata.Text(3) == Get(request, "purpose") &&
      metadata.Text(4) == Get(request, "recipient") && metadata.Text(5) == Get(request, "scope"),
      -EACCES, "artifact ownership or use mismatch");
  Require(metadata.Text(7) == "ACTIVE" && metadata.Int(6) > Now(), -ESTALE,
      "artifact blocked or expired");
  // Derived registration materializes the union of all parents' source grants;
  // this is the transitive policy provenance, independent of parent residency.
  Statement sources(db_, "SELECT grants.revoked,grants.version,definitions.active,definitions.config "
      "FROM artifact_grants "
      "JOIN grants ON grants.id=artifact_grants.grant_id "
      "JOIN definitions ON definitions.id=grants.definition "
      "WHERE artifact_grants.artifact=?");
  sources.Bind(1, artifact);
  bool found = false;
  while (sources.Row()) {
    found = true;
    Message definition = Unpack(sources.Text(3));
    Require(sources.Int(0) == 0 && sources.Int(2) == 1 &&
        sources.Text(1) == Get(definition, "policy_version"), -ESTALE,
        "artifact provenance revoked or policy changed");
    Require(validator_ && validator_(Get(definition, "package"), Get(definition, "app"),
        Get(definition, "_install_identity")), -ESTALE,
        "artifact installation identity changed");
    // Access expiry/ONCE consumption does not shorten the separately granted
    // retention interval. Artifact expiry, revocation and policy still apply.
  }
  Require(found, -ESTALE, "artifact provenance missing");
}

Message Repository::Impl::Data(const Peer& peer, const Message& request) {
  Require(Role(peer, "holder"), -EACCES, "holder role required");
  std::string method = Get(request, "method");
  Transaction transaction(db_);
  if (method == "cleanup_list") {
    Context(peer, request);
    bool reconcile = Get(request, "reconcile") == "1";
    Message result;
    Statement rows(db_, "SELECT artifacts.id,session,artifacts.state,cleanup_error FROM artifacts "
        "JOIN sessions ON sessions.id=artifacts.session WHERE holder=? AND (artifacts.instance=? OR ?=1) "
        "AND sessions.subject=? AND sessions.profile=? "
        "AND artifacts.state IN ('CLEANUP_PENDING','CLEANUP_FAILED') LIMIT 48");
    rows.Bind(1, peer.identity).Bind(2, peer.instance).Bind(3, reconcile ? 1 : 0)
        .Bind(4, Get(request, "subject")).Bind(5, Get(request, "profile"));
    int count = 0;
    while (rows.Row()) {
      std::string prefix = "a" + std::to_string(count++) + ".";
      result[prefix + "artifact"] = rows.Text(0);
      result[prefix + "session"] = rows.Text(1);
      result[prefix + "state"] = rows.Text(2);
      result[prefix + "error"] = rows.Text(3);
    }
    result["count"] = std::to_string(count);
    transaction.Commit();
    return result;
  }
  if (method == "data_release" || method == "cleanup_ack") {
    Statement artifact(db_, "SELECT holder,artifacts.instance,artifacts.state,sessions.subject,sessions.profile "
        "FROM artifacts JOIN sessions ON sessions.id=artifacts.session WHERE artifacts.id=?");
    artifact.Bind(1, Get(request, "artifact"));
    Require(artifact.Row(), -ENOENT, "artifact not found");
    Require(artifact.Text(0) == peer.identity, -EACCES, "holder identity mismatch");
    if (artifact.Text(1) != peer.instance) {
      Require(Get(request, "reconcile") == "1", -EACCES, "holder instance mismatch");
      Context(peer, request);
      Require(artifact.Text(3) == Get(request, "subject") &&
          artifact.Text(4) == Get(request, "profile"), -EACCES,
          "cleanup reconciliation context mismatch");
      Require(artifact.Text(2) == "CLEANUP_PENDING" || artifact.Text(2) == "CLEANUP_FAILED" ||
          artifact.Text(2) == "DELETED", -EACCES, "reconciliation is cleanup-only");
    }
    bool success = Get(request, "success", "1") == "1";
    Require(success || artifact.Text(2) != "DELETED", -ESTALE,
        "completed deletion cannot become failed");
    Statement update(db_, "UPDATE artifacts SET state=?,cleanup_error=? WHERE id=?");
    update.Bind(1, success ? "DELETED" : "CLEANUP_FAILED")
        .Bind(2, success ? "" : "holder reported cleanup failure")
        .Bind(3, Get(request, "artifact")).Run();
    Statement evidence(db_, "INSERT INTO cleanup_acknowledgements VALUES(?,?,?,?,?) "
        "ON CONFLICT(artifact) DO UPDATE SET holder=excluded.holder,instance=excluded.instance,"
        "success=excluded.success,acknowledged=excluded.acknowledged");
    evidence.Bind(1, Get(request, "artifact")).Bind(2, peer.identity).Bind(3, peer.instance)
        .Bind(4, success ? 1 : 0).Bind(5, Now()).Run();
    FinishCleanup();
    Bump();
    transaction.Commit();
    return {{"state", success ? "DELETED" : "CLEANUP_FAILED"}};
  }
  Require(Get(request, "reconcile") != "1", -EACCES,
      "cleanup reconciliation does not transfer data-use rights");
  Context(peer, request);
  Require(Get(request, "storage_class", "MEMORY_ONLY") == "MEMORY_ONLY", -EACCES,
      "persistent data storage is disabled");
  Require(!Get(request, "session").empty(), -EINVAL, "data requires session");
  SessionState(peer, request, true);
  if (method == "data_register") {
    std::string receipt = Get(request, "receipt");
    Require(ReceiptValid(receipt), -ESTALE, "acquisition receipt invalid");
    Statement auth(db_, "SELECT payload,created FROM authorizations WHERE id=?");
    auth.Bind(1, receipt);
    Require(auth.Row(), -ENOENT, "acquisition receipt missing");
    Message payload = Unpack(auth.Text(0));
    Require(Get(payload, "session") == Get(request, "session") &&
        Get(payload, "generation") == Get(request, "generation") &&
        Get(payload, "subject") == Get(request, "subject") &&
        Get(payload, "profile") == Get(request, "profile"), -EACCES,
        "acquisition context mismatch");
    // One original artifact per receipt and holder. Repeating registration
    // returns the old identity and deadline rather than extending retention.
    Statement prior(db_, "SELECT id,state,expires,purpose,recipient,scope,instance FROM artifacts WHERE receipt=? AND holder=?");
    prior.Bind(1, receipt).Bind(2, peer.identity);
    if (prior.Row()) {
      Require(prior.Text(6) == peer.instance, -EACCES,
          "acquisition receipt belongs to a prior holder instance");
      Require(prior.Text(3) == Get(request, "purpose") &&
          prior.Text(4) == Get(request, "recipient") && prior.Text(5) == Get(request, "scope"),
          kConflict, "artifact retry payload conflict");
      Require(prior.Text(1) == "ACTIVE" && prior.Int(2) > Now(), -ESTALE,
          "artifact is no longer resident");
      transaction.Commit();
      return {{"artifact", prior.Text(0)}, {"expires", prior.Text(2)}};
    }
    int index = Number(request, "requirement", 0, 0, 15);
    Require(index < Number(payload, "count", 0, 1, 16), -EINVAL, "requirement index out of range");
    std::string prefix = "r" + std::to_string(index) + ".";
    Require(Get(payload, prefix + "holder") == peer.identity, -EACCES,
        "receipt is bound to another holder");
    Require(Get(payload, prefix + "scope") == Get(request, "scope") &&
        Get(payload, prefix + "purpose") == Get(request, "purpose") &&
        Get(payload, prefix + "recipient") == Get(request, "recipient"),
        -EACCES, "returned data exceeds receipt context");
    Require(Get(request, "storage_class", "MEMORY_ONLY") == "MEMORY_ONLY", -EACCES,
        "persistent data storage is disabled");
    Message definition = Definition(Get(payload, prefix + "definition"));
    auto retention = Number(definition, "retention_ms", 0, 0, kDayMs);
    Require(retention > 0, -EACCES, "definition does not permit result retention");
    auto expires = auth.Int(1) + retention;
    Require(expires > Now(), -ESTALE, "acquisition data already expired");
    Statement count(db_, "SELECT count(*) FROM artifacts WHERE session=? AND state<>'DELETED'");
    count.Bind(1, Get(request, "session"));
    count.Row();
    Require(count.Int(0) < 256, -EBUSY, "session artifact capacity exceeded");
    auto id = Id();
    Statement insert(db_, "INSERT INTO artifacts VALUES(?,?,?,?,?,?,?,?,?,'ACTIVE',?,'')");
    insert.Bind(1, id).Bind(2, receipt).Bind(3, Get(request, "session"))
        .Bind(4, peer.identity).Bind(5, peer.instance).Bind(6, Get(request, "purpose"))
        .Bind(7, Get(request, "recipient")).Bind(8, Get(request, "scope"))
        .Bind(9, expires).Bind(10, Integer(Get(definition, "level"), 0, 3)).Run();
    Statement sources(db_, "INSERT INTO artifact_grants SELECT ?,grant_id FROM authorization_grants WHERE receipt=?");
    sources.Bind(1, id).Bind(2, receipt).Run();
    Bump();
    transaction.Commit();
    return {{"artifact", id}, {"permit", id}, {"expires", std::to_string(expires)},
        {"storage_class", "MEMORY_ONLY"}};
  }
  if (method == "data_register_derived") {
    auto count = Number(request, "count", 0, 1, 16);
    Require(count >= 1, -EINVAL, "parent artifacts required");
    int64_t expires = INT64_MAX;
    int64_t level = 0;
    std::set<std::string> parents;
    for (int i = 0; i < count; ++i) {
      std::string parent = Get(request, "parent" + std::to_string(i));
      Require(parents.insert(parent).second, -EINVAL, "duplicate parent artifact");
      Statement row(db_, "SELECT session,holder,instance,purpose,recipient,scope,expires,state,level "
          "FROM artifacts WHERE id=?");
      row.Bind(1, parent);
      Require(row.Row(), -ENOENT, "parent artifact missing");
      Require(row.Text(0) == Get(request, "session") && row.Text(1) == peer.identity &&
          row.Text(2) == peer.instance && row.Text(3) == Get(request, "purpose") &&
          row.Text(4) == Get(request, "recipient") && row.Text(5) == Get(request, "scope"),
          -EACCES, "parent artifact ownership or use mismatch");
      Require(row.Text(7) == "ACTIVE" && row.Int(6) > Now(), -ESTALE,
          "parent artifact unavailable");
      ValidateArtifactProvenance(peer, request, parent);
      expires = std::min(expires, row.Int(6));
      level = std::max(level, row.Int(8));
    }
    Statement capacity(db_, "SELECT count(*) FROM artifacts WHERE session=? AND state<>'DELETED'");
    capacity.Bind(1, Get(request, "session"));
    capacity.Row();
    Require(capacity.Int(0) < 256, -EBUSY, "session artifact capacity exceeded");
    std::string id = Id();
    Statement insert(db_, "INSERT INTO artifacts VALUES(?,?,?,?,?,?,?,?,?,'ACTIVE',?,'')");
    insert.Bind(1, id).Bind(2, "derived:" + id).Bind(3, Get(request, "session"))
        .Bind(4, peer.identity).Bind(5, peer.instance).Bind(6, Get(request, "purpose"))
        .Bind(7, Get(request, "recipient")).Bind(8, Get(request, "scope"))
        .Bind(9, expires).Bind(10, level).Run();
    for (const auto& parent : parents) {
      Statement source(db_, "INSERT OR IGNORE INTO artifact_grants SELECT ?,grant_id "
          "FROM artifact_grants WHERE artifact=?");
      source.Bind(1, id).Bind(2, parent).Run();
      Statement link(db_, "INSERT INTO artifact_parents VALUES(?,?)");
      link.Bind(1, id).Bind(2, parent).Run();
    }
    Bump();
    transaction.Commit();
    return {{"artifact", id}, {"permit", id}, {"expires", std::to_string(expires)}};
  }
  if (method == "data_check") {
    ValidateArtifactProvenance(peer, request, Get(request, "artifact"));
    transaction.Commit();
    return {{"decision", "ALLOWED"}};
  }
  throw Failure(-ENOSYS, "data method unsupported");
}

Message Repository::Impl::Execute(const Peer& peer, const Message& request) {
  try {
    Ensure();
    Expire();
    Require(!peer.identity.empty() && !peer.instance.empty(), -EACCES,
        "authenticated caller identity required");
    std::string method = Get(request, "method");
    Message result;
    if (method == "register" || method == "update" || method == "unregister")
      result = Register(peer, request, method == "unregister");
    else if (method == "request" || method == "check") {
      if (method == "check" && Get(request, "operation") == "reuse-data") {
        Message data = request;
        data["method"] = "data_check";
        result = Data(peer, data);
      } else {
        result = Evaluate(peer, request, method == "request");
      }
    } else if (method == "get_prompt" || method == "respond")
      result = Prompt(peer, request, method == "respond");
    else if (method == "get_request_result" || method == "result" ||
        method == "cancel_request" || method == "cancel")
      result = Result(peer, request, method == "cancel" || method == "cancel_request");
    else if (method.compare(0, 8, "session_") == 0 || method == "cleanup_get_state")
      result = Session(peer, request);
    else if (method.compare(0, 5, "data_") == 0 || method == "cleanup_ack" || method == "cleanup_list")
      result = Data(peer, request);
    else if (method == "revoke")
      result = Revoke(peer, request);
    else if (method != "snapshot" && method != "subscribe" && method != "hello")
      throw Failure(-ENOSYS, "unsupported method");
    // No successful reply can be published against a deleted/replaced handle.
    // Recovery changes epoch; this operation's old result becomes uncertain.
    std::string before = epoch_;
    Ensure();
    Require(before == epoch_, -ESTALE, "database generation changed before reply");
    // The external installation-generation authority is checked again after
    // committing and before publishing any approval-sensitive success.
    Message validated = request;
    if (method == "respond" || method == "get_prompt") {
      Statement stored(db_, "SELECT payload FROM requests WHERE id=?");
      stored.Bind(1, Get(request, "request_id"));
      if (stored.Row())
        validated = Unpack(stored.Text(0));
    } else if (method == "data_register") {
      Statement stored(db_, "SELECT payload FROM authorizations WHERE id=?");
      stored.Bind(1, Get(request, "receipt"));
      if (stored.Row())
        validated = Unpack(stored.Text(0));
    }
    std::set<std::string> checked_definitions;
    if (method == "register" || method == "update")
      checked_definitions.insert(Get(request, "definition"));
    if (method == "request" || method == "check" || method == "respond" ||
        method == "get_prompt" || method == "data_register") {
      for (int i = 0; i < Number(validated, "count", 0, 0, 16); ++i)
        checked_definitions.insert(Get(validated, "r" + std::to_string(i) + ".definition"));
    }
    for (const auto& id : checked_definitions) {
      auto item = definitions_.find(id);
      if (item == definitions_.end() || Get(item->second, "active") != "1")
        continue;
      Require(validator_ && validator_(Get(item->second, "package"), Get(item->second, "app"),
          Get(item->second, "_install_identity")), -ESTALE,
          "installation changed before reply publication");
    }
    bool data_check = method == "data_check" ||
        (method == "check" && Get(request, "operation") == "reuse-data");
    if (data_check || method == "data_register" || method == "data_register_derived") {
      ValidateArtifactProvenance(peer, request,
          data_check ? Get(request, "artifact") : Get(result, "artifact"));
    }
    auto metadata = Snapshot();
    result.insert(metadata.begin(), metadata.end());
    result["status"] = "0";
    return result;
  } catch (const Failure& failure) {
    if ((failure.SqliteCode() & 0xff) == SQLITE_CORRUPT ||
        (failure.SqliteCode() & 0xff) == SQLITE_NOTADB)
      corrupt_ = true;
    if (failure.Status() == kStorage)
      fenced_ = true;
    return {{"status", std::to_string(failure.Status())}, {"reason", failure.what()},
        {"epoch", epoch_}, {"source", "DAEMON"}};
  } catch (const std::bad_alloc&) {
    return {{"status", std::to_string(-ENOMEM)}};
  } catch (const std::exception&) {
    fenced_ = true;
    return {{"status", std::to_string(kStorage)}, {"reason", "repository failure"}};
  }
}

void Repository::Impl::Tick() {
  try {
    Ensure();
    {
      Statement integrity(db_, "PRAGMA quick_check(1)");
      if (!integrity.Row() || integrity.Text(0) != "ok") {
        corrupt_ = true;
        throw Failure(kStorage, "runtime database corruption detected");
      }
    }
    // The trusted Installer authority may change independently of the DB.
    // Reconciliation invalidates affected grants before advertising a revision.
    Replay();
    Expire();
  } catch (const Failure& failure) {
    if ((failure.SqliteCode() & 0xff) == SQLITE_CORRUPT ||
        (failure.SqliteCode() & 0xff) == SQLITE_NOTADB)
      corrupt_ = true;
    fenced_ = true;
    g_warning("consentd storage timer fenced: %s", failure.what());
  } catch (const std::exception& failure) {
    fenced_ = true;
    g_warning("consentd storage timer fenced: %s", failure.what());
  }
}

void Repository::Impl::Shutdown() {
  if (db_ && !fenced_)
    ResetRuntime();
}

Repository::Repository(std::string path, std::string recovery_dir)
    : impl_(new Impl(std::move(path), std::move(recovery_dir))) {}
Repository::~Repository() = default;
void Repository::SetInstallationValidator(InstallationValidator validator) {
  impl_->validator_ = std::move(validator);
}
void Repository::SetPackageGenerationValidator(
    std::function<bool(const std::string&, const std::string&)> validator) {
  impl_->package_validator_ = std::move(validator);
}
bool Repository::Open(std::string* error) { return impl_->Open(error); }
Message Repository::Execute(const Peer& peer, const Message& request) {
  return impl_->Execute(peer, request);
}
Message Repository::Snapshot() { return impl_->Snapshot(); }
void Repository::Tick() { impl_->Tick(); }
void Repository::Shutdown() { impl_->Shutdown(); }

}  // namespace consentd
