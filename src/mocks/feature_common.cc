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
#include "feature_common.hh"

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <sstream>

namespace consent_mock {
namespace {
std::string Proc(pid_t pid, const char* field) {
  auto path = "/proc/" + std::to_string(pid) + "/" + field;
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  std::string output;
  char buffer[4096];
  ssize_t count;
  while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
    output.append(buffer, count);
    if (output.size() > 65536) { output.clear(); break; }
  }
  close(fd);
  return count < 0 ? std::string() : output;
}
unsigned long long Start(pid_t pid) {
  auto text = Proc(pid, "stat");
  auto end = text.rfind(')');
  if (end == std::string::npos) return 0;
  std::istringstream fields(text.substr(end + 2));
  std::string value;
  for (int i = 3; i <= 22; ++i) if (!(fields >> value)) return 0;
  int64_t number = 0;
  return consent::ParseNumber(value, &number) && number > 0 ? number : 0;
}
bool Uid(pid_t pid, uid_t expected) {
  std::istringstream lines(Proc(pid, "status"));
  std::string line;
  while (std::getline(lines, line)) {
    if (line.compare(0, 4, "Uid:") != 0) continue;
    std::istringstream fields(line.substr(4));
    unsigned long a, b, c, d;
    return (fields >> a >> b >> c >> d) && a == expected && b == expected &&
        c == expected && d == expected;
  }
  return false;
}
int Transfer(int fd, void* data, size_t size, bool write, int64_t deadline) {
  auto* bytes = static_cast<unsigned char*>(data);
  while (size) {
    int64_t left = (deadline - g_get_monotonic_time() + 999) / 1000;
    if (left <= 0) return -ETIMEDOUT;
    pollfd wait{fd, static_cast<short>(write ? POLLOUT : POLLIN), 0};
    int ready = poll(&wait, 1, static_cast<int>(left));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) return ready ? -errno : -ETIMEDOUT;
    ssize_t count = write ? send(fd, bytes, size, MSG_NOSIGNAL | MSG_DONTWAIT) :
        recv(fd, bytes, size, MSG_DONTWAIT);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (count <= 0) return count ? -errno : -ECONNRESET;
    bytes += count;
    size -= static_cast<size_t>(count);
  }
  return 0;
}
}  // namespace

const std::vector<Feature>& Catalog() {
  static const std::vector<Feature> catalog = {
    {"calendar.read", "Read calendar and use in this conversation", "달력 읽기 및 현재 대화에서 사용",
      "Read the next 7 days for a local summary; keep mock data in this conversation only.",
      "향후 7일 달력을 로컬 요약에 사용하고 현재 대화에서만 보관합니다.", "Mock calendar provider", "ce",
      {{"definition", "mock.calendar.read"}, {"policy_version", "1"}, {"feature_id", "calendar.read"},
       {"feature_revision", "1"}, {"scope", "calendar.default.next7days"}, {"operation", "read"},
       {"purpose", "conversation-summary"}, {"recipient", "local-conversation"}, {"holder", "mock-holder"}}},
    {"device.control", "Turn off the study lamp", "서재 조명 끄기",
      "Turn off only mock device study-lamp; the impact is loss of light in the study.",
      "모의 기기 study-lamp만 끕니다. 영향은 서재 조명이 꺼지는 것입니다.", "Mock device provider", "cm",
      {{"definition", "mock.device.control"}, {"policy_version", "1"}, {"feature_id", "device.control"},
       {"feature_revision", "1"}, {"scope", "device.study-lamp.action.off.impact.study-dark"},
       {"operation", "control"}, {"purpose", "requested-device-control"}, {"recipient", "local-device"},
       {"holder", ""}}}
  };
  return catalog;
}
Message ExpandedCalendarRow() {
  auto row = Catalog().front().row;
  row["scope"] = "calendar.default.next30days";
  return row;
}
std::string CatalogRevision() {
  // Hash the exact immutable catalog JSON with only the three instance/CAS
  // fields empty. Every published mapping, label, preset and task contributes.
  const auto canonical = std::string("consent-feature-catalog-v1\n") + CatalogJson("", "");
  gchar* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      reinterpret_cast<const guchar*>(canonical.data()), canonical.size());
  std::string result(digest); g_free(digest); return result;
}
const Feature* FindFeature(const std::string& id) {
  for (const auto& feature : Catalog()) if (feature.id == id) return &feature;
  return nullptr;
}
bool Identifier(const std::string& input) {
  if (input.empty() || input.size() > 128) return false;
  for (unsigned char c : input)
    if (!g_ascii_isalnum(c) && c != '.' && c != '_' && c != '-') return false;
  return true;
}
bool ParseSelection(const std::string& input, std::set<std::string>* output) {
  output->clear();
  if (input.size() > 1024) return false;
  if (input.empty()) return true;
  size_t start = 0;
  for (;;) {
    auto end = input.find(',', start);
    auto id = input.substr(start, end == std::string::npos ? end : end - start);
    if (!FindFeature(id) || !output->insert(id).second || output->size() > 8) return false;
    if (end == std::string::npos) return true;
    start = end + 1;
  }
}
std::string Json(const std::string& value) {
  static const char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '\\' || c == '"') { result += '\\'; result += c; }
    else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
    else result += c;
  }
  return result + '"';
}
std::string JsonFields(const Message& values) {
  std::string out = "{";
  for (const auto& item : values) {
    if (out.size() > 1) out += ',';
    out += Json(item.first) + ':' + Json(item.second);
  }
  return out + '}';
}
std::string ContextDigest(const Message& values) {
  const auto canonical = std::string("consent-feature-use-v1\n") + JsonFields(values);
  gchar* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      reinterpret_cast<const guchar*>(canonical.data()), canonical.size());
  std::string result(digest); g_free(digest); return result;
}
std::string CatalogJson(const std::string& revision, const std::string& epoch) {
  std::string out = "{\"schema\":1,\"catalog_revision\":" + Json(revision.empty() && epoch.empty() ? "" : CatalogRevision()) + ",\"selection_revision\":" + Json(revision) + ",\"coordinator_epoch\":" + Json(epoch) + ",\"features\":[";
  for (const auto& feature : Catalog()) {
    if (out.back() != '[') out += ',';
    out += JsonFields({{"id", feature.id}, {"revision", "1"}, {"title_en", feature.title_en},
        {"title_ko", feature.title_ko}, {"description_en", feature.description_en},
        {"description_ko", feature.description_ko}, {"provider_label", feature.provider},
        {"provider_package", "org.tizen.consentui"}, {"provider_app", "org.tizen.consentui"}});
    out.pop_back();
    Message mapping = feature.row;
    mapping.erase("feature_id"); mapping.erase("feature_revision");
    mapping["retention_ms"] = feature.id == "calendar.read" ? "1800000" : "0";
    auto base = JsonFields(mapping); base.pop_back();
    out += ",\"mappings\":[" + base + ",\"task_only\":false}";
    if (feature.id == "calendar.read") {
      mapping["scope"] = "calendar.default.next30days";
      auto expanded = JsonFields(mapping); expanded.pop_back();
      out += ',' + expanded + ",\"task_only\":true}";
    }
    out += "]}";
  }
  return out + "],\"tasks\":[\"calendar-summary\",\"device-off\",\"calendar-and-device\",\"calendar-alternative\",\"calendar-expanded\",\"conversation-close\"],\"presets\":[\"SESSION\",\"TIMED\"]}";
}
int Protected(const std::string& path, bool directory) {
  if (path.empty() || path.front() != '/' || path.back() == '/') return -1;
  int fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  size_t start = 1;
  while (fd >= 0 && start < path.size()) {
    auto end = path.find('/', start);
    auto part = path.substr(start, end == std::string::npos ? end : end - start);
    bool last = end == std::string::npos;
    if (part.empty() || part == "." || part == "..") { close(fd); return -1; }
    int next = openat(fd, part.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC |
        ((!last || directory) ? O_DIRECTORY : 0));
    close(fd);
    struct stat info{};
    if (next < 0 || fstat(next, &info) || info.st_uid != 0 || (info.st_mode & 0022) ||
        ((!last || directory) ? !S_ISDIR(info.st_mode) : (!S_ISREG(info.st_mode) || info.st_nlink != 1))) {
      if (next >= 0) close(next);
      return -1;
    }
    fd = next;
    if (last) break;
    start = end + 1;
  }
  return fd;
}
int ConnectSocket(int fd, const sockaddr* address, socklen_t size, int timeout_ms) {
  const int64_t deadline = g_get_monotonic_time() + timeout_ms * 1000LL;
  for (;;) {
    if (!connect(fd, address, size)) return 0;
    int error = errno;
    if (error == EINTR) continue;
    int64_t left = (deadline - g_get_monotonic_time() + 999) / 1000;
    if (left <= 0) return -ETIMEDOUT;
    if (error == EAGAIN) {
      // AF_UNIX uses EAGAIN for a saturated listener queue. It does not imply
      // an in-progress connection; SO_ERROR==0 alone would be insufficient.
      poll(nullptr, 0, static_cast<int>(left < 10 ? left : 10));
      continue;
    }
    if (error != EINPROGRESS) return -error;
    pollfd wait{fd, POLLOUT, 0};
    int ready = poll(&wait, 1, static_cast<int>(left));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) return ready ? -errno : -ETIMEDOUT;
    socklen_t error_size = sizeof(error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size)) return -errno;
    return error ? -error : 0;
  }
}
int VerifyWorkerParent(int fd, pid_t expected_pid) {
  ucred peer{};
  socklen_t size = sizeof(peer);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size)) return -errno;
  if (size != sizeof(peer)) return -EPROTO;
  if (expected_pid <= 0 || peer.pid != expected_pid || peer.uid || peer.gid) return -EACCES;
  char label[64]{};
  size = sizeof(label);
  if (getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &size)) return -errno;
  // Kernels may include the terminator. An absent label is never a fallback.
  if ((size != 6 && size != 7) || memcmp(label, "System", 6) ||
      (size == 7 && label[6])) return -EACCES;
  return 0;
}
int CreateWorkerChannel(int sockets[2]) try {
  if (!sockets) return -EINVAL;
  sockets[0] = sockets[1] = -1;
  if (getuid() || geteuid() || getgid() || getegid()) return -EACCES;
  struct Channel final {
    int directory = -1;
    int listener = -1;
    int outgoing = -1;
    int incoming = -1;
    std::string leaf;
    std::string path;
    struct stat node{};
    int node_error = -EIO;
    bool created = false;
    bool bound = false;
    int Remove() {
      if (!bound) return 0;
      struct stat current{};
      if (fstatat(directory, leaf.c_str(), &current, AT_SYMLINK_NOFOLLOW)) return -errno;
      if (!S_ISSOCK(current.st_mode) || current.st_dev != node.st_dev ||
          current.st_ino != node.st_ino || current.st_uid || current.st_gid) return -ESTALE;
      if (unlinkat(directory, leaf.c_str(), 0)) return -errno;
      bound = false;
      created = false;
      return 0;
    }
    ~Channel() {
      int status = Remove();
      if (status || (created && !bound))
        WorkerDiagnostic("channel-node-unremoved", status ? status : node_error, 0, path.c_str());
      if (incoming >= 0) close(incoming);
      if (outgoing >= 0) close(outgoing);
      if (listener >= 0) close(listener);
      if (directory >= 0) close(directory);
    }
  } channel;
  channel.directory = Protected(kFeatureDirectory, true);
  if (channel.directory < 0) return -EACCES;
  std::unique_ptr<gchar, decltype(&g_free)> unique(g_uuid_string_random(), g_free);
  channel.leaf = ".worker-" + std::string(unique.get());
  channel.path = std::string(kFeatureDirectory) + '/' + channel.leaf;
  const auto& path = channel.path;
  sockaddr_un address{};
  if (path.size() >= sizeof(address.sun_path)) return -ENAMETOOLONG;
  address.sun_family = AF_UNIX;
  memcpy(address.sun_path, path.c_str(), path.size() + 1);
  const auto size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
  channel.listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (channel.listener < 0) return -errno;
  if (bind(channel.listener, reinterpret_cast<sockaddr*>(&address), size)) return -errno;
  channel.created = true;
  int inspected = -1;
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    inspected = fstatat(channel.directory, channel.leaf.c_str(), &channel.node, AT_SYMLINK_NOFOLLOW);
    if (!inspected || errno != EINTR) break;
  }
  if (inspected) { channel.node_error = -errno; return channel.node_error; }
  channel.bound = true;
  if (!S_ISSOCK(channel.node.st_mode) || channel.node.st_uid || channel.node.st_gid ||
      channel.node.st_nlink != 1) return -EACCES;
  if (fchmodat(channel.directory, channel.leaf.c_str(), 0600, 0)) return -errno;
  struct stat node{};
  if (fstatat(channel.directory, channel.leaf.c_str(), &node, AT_SYMLINK_NOFOLLOW)) return -errno;
  if (node.st_dev != channel.node.st_dev || node.st_ino != channel.node.st_ino ||
      (node.st_mode & 0777) != 0600 || node.st_uid || node.st_gid) return -EACCES;
  if (listen(channel.listener, 1)) return -errno;
  channel.outgoing = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (channel.outgoing < 0) return -errno;
  int status = ConnectSocket(channel.outgoing, reinterpret_cast<sockaddr*>(&address), size, 1000);
  if (status) return status;
  pollfd wait{channel.listener, POLLIN, 0};
  int ready = poll(&wait, 1, 1000);
  if (ready <= 0) return ready ? -errno : -ETIMEDOUT;
  channel.incoming = accept4(channel.listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
  if (channel.incoming < 0) return -errno;
  status = VerifyWorkerParent(channel.outgoing, getpid());
  if (!status) status = VerifyWorkerParent(channel.incoming, getpid());
  if (status) return status;
  status = channel.Remove();
  if (status) return status;
  // Keep connection credentials captured from this root/System process. The
  // connected child end is inherited; no socketpair-label fallback is used.
  sockets[0] = channel.outgoing; channel.outgoing = -1;
  sockets[1] = channel.incoming; channel.incoming = -1;
  return 0;
} catch (const std::bad_alloc&) {
  return -ENOMEM;
} catch (...) {
  return -EIO;
}
void WorkerDiagnostic(const char* stage, int status, pid_t child, const char* generated_path) noexcept {
  // Also used between fork and fexecve: only stack writes and write(2), with
  // fixed internal stage strings, no allocator or stdio locks after fork.
  char buffer[384];
  size_t count = 0;
  auto append = [&](const char* text) { while (*text && count < sizeof(buffer)) buffer[count++] = *text++; };
  auto number = [&](long long value) {
    if (value < 0) { append("-"); value = -value; }
    char digits[24]; size_t length = 0;
    do { digits[length++] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    while (length && count < sizeof(buffer)) buffer[count++] = digits[--length];
  };
  append("{\"event\":\"feature-worker-stage\",\"pid\":"); number(getpid());
  append(",\"child_pid\":"); number(child);
  append(",\"stage\":\""); append(stage);
  append("\",\"status\":"); number(status);
  if (generated_path) { append(",\"path\":\""); append(generated_path); append("\""); }
  append("}\n");
  size_t sent = 0;
  while (sent < count) {
    ssize_t written = write(STDERR_FILENO, buffer + sent, count - sent);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) break;
    sent += static_cast<size_t>(written);
  }
}
int SendFrame(int fd, const Message& message, int timeout_ms) {
  auto frame = consent::Encode(message);
  if (frame.empty()) return -EINVAL;
  return Transfer(fd, frame.data(), frame.size(), true, g_get_monotonic_time() + timeout_ms * 1000LL);
}
int ReceiveFrame(int fd, Message* message, int timeout_ms) {
  unsigned char header[4];
  int64_t deadline = g_get_monotonic_time() + timeout_ms * 1000LL;
  int status = Transfer(fd, header, sizeof(header), false, deadline);
  if (status) return status;
  auto size = consent::FrameSize(header);
  if (!size || size > consent::kMaxFrameSize) return -E2BIG;
  std::vector<unsigned char> body(size);
  status = Transfer(fd, body.data(), body.size(), false, deadline);
  return status ? status : consent::Decode(body.data(), body.size(), message) ? 0 : -EPROTO;
}
Peer::~Peer() { if (pid_fd_ >= 0) close(pid_fd_); }
bool Peer::Authenticate(int socket, bool ui, int* error) {
  if (error) *error = -EACCES;
  ucred credentials{};
  socklen_t size = sizeof(credentials);
  if (getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &credentials, &size)) {
    if (error) *error = -errno;
    return false;
  }
  if (size != sizeof(credentials)) { if (error) *error = -EPROTO; return false; }
  passwd* owner = getpwnam("owner");
  if ((ui && !owner) || credentials.uid != (ui ? owner->pw_uid : 0)) return false;
  char label[1024]{};
  size = sizeof(label) - 1;
  if (getsockopt(socket, SOL_SOCKET, SO_PEERSEC, label, &size)) {
    if (error) *error = -errno;
    return false;
  }
  if (!size || size >= sizeof(label)) return false;
  std::string actual(label, strnlen(label, size));
  if (actual != (ui ? "User::Pkg::org.tizen.consentui" : "System")) return false;
  pid_ = credentials.pid;
  uid_ = credentials.uid;
  start_ = Start(pid_);
  if (!start_ || !Uid(pid_, uid_)) return false;
#ifdef SYS_pidfd_open
  pid_fd_ = static_cast<int>(syscall(SYS_pidfd_open, pid_, 0));
#endif
  executable_ = ui ? "/usr/bin/dotnet-hydra-loader" : std::string(kArgoPath);
  char target[4096];
  auto proc = "/proc/" + std::to_string(pid_) + "/exe";
  ssize_t count = readlink(proc.c_str(), target, sizeof(target));
  if (count <= 0 || static_cast<size_t>(count) >= sizeof(target) || std::string(target, count) != executable_) return false;
  int fd = Protected(executable_);
  struct stat expected{}, running{};
  bool valid = fd >= 0 && !fstat(fd, &expected) && !stat(proc.c_str(), &running) &&
      expected.st_dev == running.st_dev && expected.st_ino == running.st_ino;
  if (fd >= 0) close(fd);
  if (!valid) return false;
  device_ = running.st_dev;
  inode_ = running.st_ino;
  const bool alive = Alive();
  if (alive && error) *error = 0;
  return alive;
}
bool Peer::Alive() const {
  if (pid_fd_ >= 0) { pollfd wait{pid_fd_, POLLIN, 0}; if (poll(&wait, 1, 0) != 0) return false; }
  if (!start_ || Start(pid_) != start_ || !Uid(pid_, uid_)) return false;
  auto proc = "/proc/" + std::to_string(pid_) + "/exe";
  struct stat running{}, expected{};
  int fd = Protected(executable_);
  bool valid = fd >= 0 && !fstat(fd, &expected) && !stat(proc.c_str(), &running) &&
      running.st_dev == device_ && running.st_ino == inode_ &&
      expected.st_dev == device_ && expected.st_ino == inode_;
  if (fd >= 0) close(fd);
  return valid && Start(pid_) == start_;
}
std::string Peer::Instance() const { return std::to_string(pid_) + ':' + std::to_string(start_); }
}  // namespace consent_mock
