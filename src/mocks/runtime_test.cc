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
// Keep the private CLI parser private. This fixture tests input handling only;
// daemon/API behavior is exercised separately by the five actual executables.
#include "mock_runtime.cc"

namespace {
class InputFixture final {
 public:
  InputFixture() {
    char pattern[] = "/tmp/consent-mock-input-XXXXXX";
    auto* result = mkdtemp(pattern);
    Require(result != nullptr, "create isolated CLI fixture");
    directory = result;
    path = directory + "/input.ini";
  }
  ~InputFixture() {
    unlink(path.c_str());
    unlink((directory + "/link.ini").c_str());
    unlink((directory + "/pipe.ini").c_str());
    rmdir(directory.c_str());
  }
  void Write(const std::string& data) {
    FILE* stream = fopen(path.c_str(), "wb");
    Require(stream != nullptr, "create CLI input");
    size_t written = fwrite(data.data(), 1, data.size(), stream);
    int status = fclose(stream);
    Require(written == data.size() && status == 0, "write CLI input");
  }
  void Reject(const std::string& data) {
    Write(data);
    RejectPath(path);
  }
  static void RejectPath(const std::string& path) {
    bool failed = false;
    try {
      Load("request", path);
    } catch (const Failure&) {
      failed = true;
    }
    Require(failed, "malformed or unsafe CLI input was accepted");
  }
  std::string directory;
  std::string path;
};
}  // namespace

int main() {
  try {
    InputFixture input;
    const std::string valid = "[mock]\nid=fixture\nexpect_status=-13\ntimeout_ms=1000\n"
        "[params]\nsubject=사용자\nr0.scope=30\nr0.recipient=sink{name}%<tag>\n";
    input.Write(valid);
    auto command = Load("probe-request", input.path);
    Require(command.name == "probe-request" && Get(command.options, "expect_status") == "-13" &&
        Get(command.fields, "subject") == "사용자" && Get(command.fields, "r0.recipient") == "sink{name}%<tag>",
        "parser must preserve UTF-8 and literal template-looking values");
    Require(Json("a\n\"\\\t") == "\"a\\u000a\\\"\\\\\\u0009\"",
        "JSONL output must escape control characters, quotes and backslashes");
    input.Reject(valid + "r0.scope=90\n");
    input.Reject(valid + "[params]\nr1.scope=1\n");
    input.Reject("[mock]\nid=fixture\nunexpected=1\n");
    input.Reject("[identity]\nrole=argo\n");
    input.Reject(valid + "role=argo\n");
    input.Reject("[mock]\nid=fixture\ntimeout_ms=300001\n");
    input.Reject("[mock]\nid=fixture\nexpect_status=1\n");
    input.Reject("[mock]\nid=fixture\ncleanup_failure=true\n");
    input.Reject("[params]\nsubject=missing-command-id\n");
    input.Reject(valid + std::string(1, '\0'));
    input.Reject(valid + std::string(1, static_cast<char>(0xff)));
    input.Reject(std::string(kInputLimit + 1, 'x'));
    input.Reject("[mock]\nid=fixture\npayload=" + std::string(4097, 'x') + '\n');
    std::string many = "[mock]\nid=fixture\n[params]\n";
    for (unsigned index = 0; index < 241; ++index)
      many += "key" + std::to_string(index) + "=value\n";
    input.Reject(many);
    input.Write(valid);
    const std::string link = input.directory + "/link.ini";
    Require(symlink(input.path.c_str(), link.c_str()) == 0, "create CLI symlink fixture");
    InputFixture::RejectPath(link);
    const std::string pipe = input.directory + "/pipe.ini";
    Require(mkfifo(pipe.c_str(), 0600) == 0, "create nonblocking FIFO fixture");
    InputFixture::RejectPath(pipe);
    InputFixture::RejectPath("relative.ini");
    std::puts("PASS bounded mock INI parser, duplicate/unknown fields, UTF-8, JSONL escaping, symlink and FIFO rejection");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL mock input fixture: %s\n", error.what());
    return 1;
  }
}
