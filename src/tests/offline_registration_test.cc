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
#include <consent.h>

#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static ino_t fail_sync_inode;
static unsigned matching_sync_calls;
static unsigned fail_sync_call;
static bool fail_ancestor_mode;
static int OfflineChmod(int fd, mode_t mode) {
  if (fail_ancestor_mode && mode == 0755) {
    fail_ancestor_mode = false;
    errno = EIO;
    return -1;
  }
  return syscall(SYS_fchmod, fd, mode);
}
static int OfflineSync(int fd) {
  struct stat info = {};
  if (fail_sync_inode && fstat(fd, &info) == 0 && info.st_ino == fail_sync_inode) {
    ++matching_sync_calls;
    if (matching_sync_calls == fail_sync_call) {
      errno = EIO;
      return -1;
    }
  }
  return syscall(SYS_fsync, fd);
}

// Only this executable substitutes fsync. The installed common/client library
// and public C calls below retain their ordinary filesystem implementation.
#define fsync OfflineSync
#define fchmod OfflineChmod
#include "../common/offline_registration.cc"
#undef fsync
#undef fchmod

namespace {
using consent::Message;
using consent::offline::ImageRoot;
using consent::offline::LoadRegistrations;
using consent::offline::RegistrationWriter;

#define CHECK(expression) do { if (!(expression)) { \
  std::fprintf(stderr, "FAIL offline registration line=%d expression=%s\n", __LINE__, #expression); \
  std::exit(1); \
} } while (0)

void Remove(const std::string& path) {
  struct stat info = {};
  if (lstat(path.c_str(), &info) < 0)
    return;
  if (!S_ISDIR(info.st_mode)) {
    CHECK(unlink(path.c_str()) == 0);
    return;
  }
  DIR* directory = opendir(path.c_str());
  CHECK(directory);
  while (auto* entry = readdir(directory)) {
    if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, ".."))
      Remove(path + "/" + entry->d_name);
  }
  closedir(directory);
  CHECK(rmdir(path.c_str()) == 0);
}

struct Fixture {
  Fixture() {
    char pattern[] = "/opt/var/lib/consent-offline-XXXXXX";
    char* path = mkdtemp(pattern);
    CHECK(path);
    root = path;
    authority = root + "/opt/var/lib/consent-authority";
    spool = authority + "/registrations";
  }
  ~Fixture() { Remove(root); }
  std::string root;
  std::string authority;
  std::string spool;
};

Message Definition(const std::string& operation = "install-1") {
  return {{"definition", "offline.read"}, {"package", "offline.package"},
      {"app", "offline.app"}, {"enforcer", "offline.enforcer"}, {"policy_version", "1"},
      {"text_revision", "1"}, {"level", "1"}, {"modes", "PERSISTENT"},
      {"default_locale", "en"}, {"message.en.title", "Read"},
      {"message.en.body", "Allow reading?"}, {"operation_id", operation},
      {"expected_generation", "generation-1"}};
}

std::string Name(const Message& value) {
  auto operation = consent::Get(value, "operation_id");
  gchar* checksum = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      reinterpret_cast<const guchar*>(operation.data()), operation.size());
  CHECK(checksum);
  std::string name = std::string(checksum) + ".parcel";
  g_free(checksum);
  return name;
}

void File(const std::string& path, const std::vector<uint8_t>& bytes) {
  int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
  CHECK(fd >= 0);
  CHECK(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()));
  CHECK(close(fd) == 0);
}

std::vector<uint8_t> Encode(Message value) {
  value["v"] = "1";
  value["id"] = "1";
  value["method"] = "register";
  auto bytes = consent::Encode(value);
  CHECK(!bytes.empty());
  gchar* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      bytes.data() + 4, bytes.size() - 4);
  CHECK(digest);
  value["offline_format"] = "1";
  value["payload_sha256"] = digest;
  g_free(digest);
  auto stored = consent::Encode(value);
  CHECK(!stored.empty());
  return stored;
}

std::vector<uint8_t> DuplicateField(const Message& stored, const std::string& duplicate) {
  consent::wire::Envelope envelope;
  envelope.version = 1;
  envelope.kind = 1;
  envelope.correlation = 1;
  envelope.method = "register";
  envelope.status = 0;
  for (const auto& entry : stored) {
    if (entry.first == "v" || entry.first == "id" || entry.first == "method")
      continue;
    consent::wire::Field field;
    field.key = entry.first;
    field.value = entry.second;
    envelope.fields.push_back(field);
    if (entry.first == duplicate)
      envelope.fields.push_back(field);
  }
  std::vector<uint8_t> bytes(65536);
  tizen_base::Parcel parcel(bytes.data() + 4, bytes.size() - 4, false, false);
  parcel.Clear();
  parcel.SetByteOrder(true);
  parcel.WriteParcelable(envelope);
  uint32_t size = parcel.GetDataSize();
  bytes[0] = (size >> 24) & 0xff;
  bytes[1] = (size >> 16) & 0xff;
  bytes[2] = (size >> 8) & 0xff;
  bytes[3] = size & 0xff;
  bytes.resize(size + 4);
  return bytes;
}

void FormatMetadata() {
  // Exercise the actual producer/reader codec without requiring root or a
  // writable image. Root-only checks below also inspect a published file.
  const auto value = Definition();
  const auto name = Name(value);
  auto payload = consent::offline::RecordPayload(value);
  CHECK(payload == Encode(value));
  CHECK(consent::offline::DecodeRecord(payload, name) == value);
  Message stored;
  CHECK(consent::Decode(payload.data() + 4, payload.size() - 4, &stored));
  CHECK(consent::Get(stored, "offline_format") == "1");
  CHECK(consent::Get(stored, "payload_sha256").size() == 64);
  auto rejected = [&](const std::vector<uint8_t>& bytes) {
    try {
      consent::offline::DecodeRecord(bytes, name);
      return false;
    } catch (const consent::offline::Failure& error) {
      return error.Status() == -EINVAL;
    }
  };
  for (const char* field : {"offline_format", "payload_sha256"}) {
    auto missing = stored;
    missing.erase(field);
    CHECK(rejected(consent::Encode(missing)));
    CHECK(rejected(DuplicateField(stored, field)));
    auto caller = value;
    caller[field] = "forged";
    bool invalid = false;
    try {
      consent::offline::PublicRegistration(caller);
    } catch (const consent::offline::Failure& error) {
      invalid = error.Status() == -EINVAL;
    }
    CHECK(invalid);
  }
  for (const char* version : {"", "0", "2", "01"}) {
    auto wrong = stored;
    wrong["offline_format"] = version;
    CHECK(rejected(consent::Encode(wrong)));
  }
  for (const auto& digest : {std::string(), std::string(63, 'a'), std::string(65, 'a'),
      std::string(64, 'A'), std::string(64, 'g'), std::string(64, '0')}) {
    auto wrong = stored;
    wrong["payload_sha256"] = digest;
    CHECK(rejected(consent::Encode(wrong)));
  }
  auto tampered = stored;
  tampered["message.en.body"] = "Different consent text";
  CHECK(rejected(consent::Encode(tampered)));

  auto full = value;
  unsigned index = 0;
  while (full.size() < consent::kMaxFields - 3)
    full["locale_fallback.alias" + std::to_string(index++)] = "en";
  CHECK(consent::offline::PublicRegistration(full) == full);
  bool bounded = false;
  try {
    consent::offline::RecordPayload(full);
  } catch (const consent::offline::Failure& error) {
    bounded = error.Status() == -E2BIG;
  }
  CHECK(bounded);  // The two stored metadata fields count toward the limit.

  auto near_limit = value;
  near_limit["message.en.title"] = std::string(4096, 'a');
  near_limit["message.en.body"] = "b";
  for (unsigned locale = 0; locale < 7; ++locale) {
    auto prefix = "message.x" + std::to_string(locale);
    near_limit[prefix + ".title"] = std::string(4096, 'a');
    near_limit[prefix + ".body"] = std::string(4096, 'b');
  }
  auto canonical = near_limit;
  canonical["v"] = "1";
  canonical["id"] = "1";
  canonical["method"] = "register";
  auto bytes = consent::Encode(canonical);
  CHECK(!bytes.empty() && bytes.size() < 65500 && 65501 - bytes.size() <= 4096);
  near_limit["message.en.body"] = std::string(65501 - bytes.size(), 'b');
  CHECK(consent::offline::PublicRegistration(near_limit) == near_limit);
  canonical["message.en.body"] = near_limit["message.en.body"];
  CHECK(consent::Encode(canonical).size() == 65500);
  bounded = false;
  try {
    consent::offline::RecordPayload(near_limit);
  } catch (const consent::offline::Failure& error) {
    bounded = error.Status() == -E2BIG;
  }
  CHECK(bounded);  // The public envelope fits; stored metadata exceeds 64 KiB.
  std::puts("PASS offline format: actual producer, version/SHA256, metadata injection/duplicates/tampering and field budget");
}

void DurableRecords() {
  Fixture fixture;
  RegistrationWriter writer;
  CHECK(writer.Open(fixture.root) == 0);
  CHECK(writer.Register(Definition()) == 0);
  int stored_directory = open(fixture.spool.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  CHECK(stored_directory >= 0);
  CHECK(consent::offline::ReadFile(stored_directory, Name(Definition())) == Encode(Definition()));
  CHECK(close(stored_directory) == 0);
  ImageRoot competing;
  CHECK(competing.Open(fixture.root) == -EBUSY);
  CHECK(writer.Register(Definition()) == 0);
  auto changed = Definition();
  changed["message.en.body"] = "Changed meaning";
  CHECK(writer.Register(changed) == -EEXIST);
  std::vector<Message> records;
  CHECK(LoadRegistrations(fixture.authority, &records) == 0 && records.size() == 1);
  CHECK(records[0] == Definition());
  auto private_field = Definition("private");
  private_field["_install_identity"] = "forged";
  CHECK(writer.Register(private_field) == -EINVAL);
  auto wrong_level = Definition("wrong-level");
  wrong_level["level"] = "3";
  CHECK(writer.Register(wrong_level) == -EINVAL);

  // A committed name survives failure of the final directory synchronization.
  // Retrying the exact operation repeats synchronization before success.
  struct stat info = {};
  CHECK(stat(fixture.spool.c_str(), &info) == 0);
  fail_sync_inode = info.st_ino;
  matching_sync_calls = 0;
  fail_sync_call = 1;
  CHECK(writer.Register(Definition("uncertain")) == -EINPROGRESS);
  CHECK(matching_sync_calls == 1);
  matching_sync_calls = 0;
  fail_sync_call = 0;
  CHECK(writer.Register(Definition("uncertain")) == 0);
  CHECK(matching_sync_calls >= 1);
  fail_sync_inode = 0;

  const auto orphan = fixture.spool + "/.pending-00000000-0000-4000-8000-000000000001";
  File(orphan, {0, 0});
  CHECK(LoadRegistrations(fixture.authority, &records) == 0 && records.size() == 2);
  CHECK(writer.Register(Definition("after-interruption")) == 0);
  CHECK(access(orphan.c_str(), F_OK) < 0 && errno == ENOENT);
  std::puts("PASS offline: durable native Parcel, retry/conflict, post-publication sync failure and orphan recovery");
}

void FilesystemRejection() {
  Fixture fixture;
  RegistrationWriter writer;
  CHECK(writer.Open(fixture.root) == 0);
  CHECK(writer.Register(Definition()) == 0);
  std::vector<Message> records;
  const auto record = fixture.spool + "/" + Name(Definition());
  CHECK(chmod(record.c_str(), 0666) == 0);
  CHECK(LoadRegistrations(fixture.authority, &records) == -EACCES && records.empty());
  CHECK(chmod(record.c_str(), 0600) == 0);
  const auto extra = fixture.spool + "/" + Name(Definition("extra"));
  CHECK(link(record.c_str(), extra.c_str()) == 0);
  CHECK(LoadRegistrations(fixture.authority, &records) == -EACCES);
  CHECK(unlink(extra.c_str()) == 0);
  CHECK(symlink(record.c_str(), extra.c_str()) == 0);
  CHECK(LoadRegistrations(fixture.authority, &records) == -EACCES);
  CHECK(unlink(extra.c_str()) == 0);

  CHECK(mkfifo(extra.c_str(), 0600) == 0);
  // A FIFO with an otherwise valid record name must be rejected immediately,
  // even when no writer has opened the other end.
  gint64 start = g_get_monotonic_time();
  CHECK(LoadRegistrations(fixture.authority, &records) == -EACCES);
  CHECK(writer.Register(Definition("next")) == -EACCES);
  CHECK(g_get_monotonic_time() - start < G_USEC_PER_SEC);
  CHECK(unlink(extra.c_str()) == 0);
  File(extra, std::vector<uint8_t>(65537, 0));
  CHECK(LoadRegistrations(fixture.authority, &records) == -E2BIG);
  CHECK(unlink(extra.c_str()) == 0);
  File(record, {0, 0, 0, 1, 0});
  CHECK(LoadRegistrations(fixture.authority, &records) == -EINVAL && records.empty());

  ImageRoot escape;
  CHECK(escape.Open(fixture.root + "/../escape") == -EINVAL);
  std::string symlink_root = fixture.root + "-link";
  CHECK(symlink(fixture.root.c_str(), symlink_root.c_str()) == 0);
  CHECK(escape.Open(symlink_root) != 0);
  CHECK(unlink(symlink_root.c_str()) == 0);
  CHECK(chmod(fixture.root.c_str(), 0777) == 0);
  CHECK(escape.Open(fixture.root) == -EACCES);
  CHECK(chmod(fixture.root.c_str(), 0700) == 0);
  std::puts("PASS offline: protected paths, symlink/hardlink/FIFO, mode, malformed and oversized record rejection");
}

void Bounds() {
  Fixture fixture;
  RegistrationWriter writer;
  CHECK(writer.Open(fixture.root) == 0 && writer.Register(Definition()) == 0);
  auto large = Definition("oversize");
  for (unsigned i = 0; i < 15; ++i) {
    auto prefix = "message.x" + std::to_string(i);
    large[prefix + ".title"] = std::string(4096, 'a');
    large[prefix + ".body"] = std::string(4096, 'b');
  }
  CHECK(writer.Register(large) == -E2BIG);
  for (unsigned i = 1; i < 128; ++i) {
    auto value = Definition("op-" + std::to_string(i));
    File(fixture.spool + "/" + Name(value), Encode(value));
  }
  std::vector<Message> records;
  CHECK(LoadRegistrations(fixture.authority, &records) == 0 && records.size() == 128);
  CHECK(writer.Register(Definition("full")) == -ENOSPC);
  auto excess = Definition("129th");
  File(fixture.spool + "/" + Name(excess), Encode(excess));
  CHECK(LoadRegistrations(fixture.authority, &records) == -ENOSPC && records.empty());
  std::puts("PASS offline: exact128 record capacity and registration/reader size limits");
}

void AggregateAndOrder() {
  Fixture fixture;
  RegistrationWriter writer;
  CHECK(writer.Open(fixture.root) == 0);
  auto newer = Definition("newer-first");
  newer["policy_version"] = "2";
  newer["text_revision"] = "2";
  CHECK(writer.Register(newer) == 0);
  CHECK(writer.Register(Definition("older-second")) == 0);
  std::vector<Message> records;
  CHECK(LoadRegistrations(fixture.authority, &records) == 0 && records.size() == 2);
  CHECK(consent::Get(records[0], "policy_version") == "1" &&
      consent::Get(records[1], "policy_version") == "2");
  CHECK(unlink((fixture.spool + "/" + Name(newer)).c_str()) == 0);
  CHECK(unlink((fixture.spool + "/" + Name(Definition("older-second"))).c_str()) == 0);
  size_t total = 0;
  unsigned count = 0;
  while (true) {
    auto value = Definition("large-" + std::to_string(count));
    for (unsigned locale = 0; locale < 7; ++locale) {
      auto prefix = "message.x" + std::to_string(locale);
      value[prefix + ".title"] = std::string(4096, 'a');
      value[prefix + ".body"] = std::string(4096, 'b');
    }
    auto bytes = Encode(value);
    CHECK(bytes.size() <= 65536 && ++count < 128);
    File(fixture.spool + "/" + Name(value), bytes);
    total += bytes.size();
    if (total > consent::offline::kMaxRegistrationBytes)
      break;
  }
  CHECK(LoadRegistrations(fixture.authority, &records) == -ENOSPC && records.empty());
  CHECK(writer.Register(Definition("over-budget")) == -ENOSPC);
  std::puts("PASS offline: revision ordering and aggregate4MiB bound independent of record count/size");
}

void InterruptedScaffold() {
  Fixture fixture;
  RegistrationWriter writer;
  fail_ancestor_mode = true;
  CHECK(writer.Open(fixture.root) == -EIO);
  struct stat info = {};
  CHECK(stat((fixture.root + "/opt").c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
  CHECK(writer.Open(fixture.root) == -EACCES);
  CHECK(stat((fixture.root + "/opt").c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
  CHECK(access(fixture.authority.c_str(), F_OK) < 0);
  CHECK(chmod((fixture.root + "/opt").c_str(), 0711) == 0);
  CHECK(writer.Open(fixture.root) == -EACCES);
  CHECK(stat((fixture.root + "/opt").c_str(), &info) == 0 && (info.st_mode & 0777) == 0711);
  CHECK(access(fixture.authority.c_str(), F_OK) < 0);
  // Existing image metadata is never silently repaired. An explicit image
  // builder correction permits a later retry without overstating completion.
  CHECK(chmod((fixture.root + "/opt").c_str(), 0755) == 0);
  CHECK(writer.Open(fixture.root) == 0);
  CHECK(writer.Register(Definition()) == 0);
  std::puts("PASS offline: interrupted ancestor initialization rejects retry until explicit repair");
}

void PublicHandle() {
  Fixture fixture;
  consent_client_h client = nullptr;
  CHECK(consent_client_create_offline_registration(fixture.root.c_str(), &client) == 0 && client);
  consent_params_t* params = nullptr;
  CHECK(consent_params_create(&params) == 0);
  for (const auto& field : Definition()) {
    if (field.first != "package" && field.first != "app")
      CHECK(consent_params_set(params, field.first.c_str(), field.second.c_str()) == 0);
  }
  CHECK(consent_register(client, "offline.package", "offline.app", params) == 0);
  CHECK(consent_update(client, "offline.package", "offline.app", params) == CONSENT_ERROR_INVALID_OPERATION);
  CHECK(consent_unregister(client, "offline.package", params) == CONSENT_ERROR_INVALID_OPERATION);
  consent_result_t* result = reinterpret_cast<consent_result_t*>(1);
  CHECK(consent_request(client, params, 1, &result) == CONSENT_ERROR_INVALID_OPERATION && !result);
  result = reinterpret_cast<consent_result_t*>(1);
  CHECK(consent_check(client, params, 1, &result) == CONSENT_ERROR_INVALID_OPERATION && !result);
  consent_async_id_t operation = 1;
  CHECK(consent_request_async(client, params, nullptr, nullptr, &operation) == CONSENT_ERROR_INVALID_OPERATION && !operation);
  operation = 1;
  CHECK(consent_check_async(client, params, nullptr, nullptr, &operation) == CONSENT_ERROR_INVALID_OPERATION && !operation);
  CHECK(consent_async_detach(client, 1) == CONSENT_ERROR_INVALID_OPERATION);
  using Function = int (*)(consent_client_h, const consent_params_t*, consent_result_t**);
  const Function functions[] = {consent_get_prompt, consent_respond, consent_get_request_result,
      consent_cancel_request, consent_revoke, consent_session_open, consent_session_suspend,
      consent_session_resume, consent_session_close, consent_session_get_state, consent_data_register,
      consent_data_register_derived, consent_data_release, consent_cleanup_get_state, consent_cleanup_get_pending};
  for (auto function : functions) {
    result = reinterpret_cast<consent_result_t*>(1);
    CHECK(function(client, params, &result) == CONSENT_ERROR_INVALID_OPERATION && !result);
  }
  std::thread other([&]() {
    CHECK(consent_register(client, "offline.package", "offline.app", params) == CONSENT_ERROR_INVALID_PARAMETER);
    CHECK(consent_client_destroy(client) == CONSENT_ERROR_INVALID_PARAMETER);
  });
  other.join();
  pid_t child = fork();
  CHECK(child >= 0);
  if (child == 0) {
    CHECK(consent_register(client, "offline.package", "offline.app", params) == CONSENT_ERROR_INVALID_PARAMETER);
    CHECK(consent_client_destroy(client) == CONSENT_ERROR_INVALID_PARAMETER);
    CHECK(setgid(65534) == 0 && setuid(65534) == 0);
    consent_client_h denied = reinterpret_cast<consent_client_h>(1);
    CHECK(consent_client_create_offline_registration(fixture.root.c_str(), &denied) == CONSENT_ERROR_PERMISSION_DENIED && !denied);
    _exit(0);
  }
  int status = 0;
  CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  ImageRoot competing;
  CHECK(competing.Open(fixture.root) == -EBUSY);
  CHECK(consent_client_destroy(client) == 0);
  CHECK(competing.Open(fixture.root) == 0);
  consent_params_free(params);
  std::puts("PASS offline public C API: registration only, output resets, thread/fork/root guards and lifecycle lock");
}
}  // namespace

int main() {
  FormatMetadata();
  if (getuid() != 0 || geteuid() != 0) {
    consent_client_h client = reinterpret_cast<consent_client_h>(1);
    CHECK(consent_client_create_offline_registration("/", &client) == CONSENT_ERROR_PERMISSION_DENIED && !client);
    std::puts("SKIP root-only offline fixture; non-root public constructor denial PASS");
    return 77;
  }
  DurableRecords();
  FilesystemRejection();
  Bounds();
  AggregateAndOrder();
  InterruptedScaffold();
  PublicHandle();
  return 0;
}
