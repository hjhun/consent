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
#include "approval.hh"

#include <array>

namespace consent {
namespace approval {
namespace {
constexpr std::array<const char*, 7> kContext = {{"approval_version", "request_kind",
    "selection_id", "selection_revision", "selection_digest", "grant_mode", "duration_ms"}};
constexpr std::array<const char*, 9> kRow = {{"definition", "policy_version", "scope",
    "operation", "purpose", "recipient", "holder", "feature_id", "feature_revision"}};

bool Identifier(const std::string& value) {
  if (value.empty() || value.size() > 128)
    return false;
  for (unsigned char byte : value) {
    if (!g_ascii_isalnum(byte) && byte != '_' && byte != '-' && byte != '.')
      return false;
  }
  return true;
}

bool Integer(const std::string& value, int64_t minimum, int64_t maximum) {
  int64_t number = 0;
  return ParseNumber(value, &number) && number >= minimum && number <= maximum &&
      value == std::to_string(number);
}

bool Fail(std::string* error, const char* text) {
  if (error)
    *error = text;
  return false;
}
}  // namespace

void CopyContext(const Message& source, Message* destination) {
  for (const auto* key : kContext) {
    auto found = source.find(key);
    if (found != source.end())
      (*destination)[key] = found->second;
  }
}

bool SameContext(const Message& left, const Message& right) {
  for (const auto* key : kContext) {
    if (left.count(key) != right.count(key) || Get(left, key) != Get(right, key))
      return false;
  }
  return true;
}

Message SelectionFields(const Message& request) {
  Message result;
  CopyContext(request, &result);
  result.erase("selection_digest");
  for (const auto* key : {"subject", "profile", "session", "generation", "count"})
    result[key] = Get(request, key);
  int64_t count = Number(request, "count", 0);
  if (count < 1 || count > 16)
    return {};
  for (int i = 0; i < count; ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    for (const auto* key : kRow)
      result[prefix + key] = Get(request, prefix + key);
  }
  return result;
}

std::string SelectionDigest(const Message& request) {
  auto fields = SelectionFields(request);
  if (fields.empty())
    return {};
  std::string canonical = "consent-selection-v1\n";
  for (const auto& field : fields) {
    if (!ValidField(field.first, field.second))
      return {};
    canonical += std::to_string(field.first.size()) + ":" + field.first +
        std::to_string(field.second.size()) + ":" + field.second;
  }
  gchar* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
      reinterpret_cast<const guchar*>(canonical.data()), canonical.size());
  std::string result(digest);
  g_free(digest);
  return result;
}

bool Validate(const Message& request, std::string* error) {
  if (!request.count("approval_version")) {
    for (const auto* field : kContext) {
      if (request.count(field))
        return Fail(error, "approval context requires version 1");
    }
    for (int i = 0; i < 16; ++i) {
      std::string prefix = "r" + std::to_string(i) + ".";
      if (request.count(prefix + "feature_id") || request.count(prefix + "feature_revision") ||
          request.count(prefix + "grant_mode") || request.count(prefix + "duration_ms"))
        return Fail(error, "feature context requires version 1");
    }
    return true;
  }
  if (Get(request, "approval_version") != "1" ||
      (Get(request, "request_kind") != "PREAPPROVAL" && Get(request, "request_kind") != "TASK") ||
      !Identifier(Get(request, "selection_id")) ||
      !Integer(Get(request, "selection_revision"), 1, INT64_MAX) ||
      !Integer(Get(request, "count"), 1, 16))
    return Fail(error, "invalid approval selection context");
  for (const auto& field : request) {
    if (field.first.size() > 2 && field.first.front() == 'r' &&
        (field.first.find(".grant_mode") != std::string::npos ||
         field.first.find(".duration_ms") != std::string::npos))
      return Fail(error, "version 1 selects one common period for the batch");
  }
  const auto mode = Get(request, "grant_mode");
  if (mode != "ONCE" && mode != "SESSION" && mode != "TIMED")
    return Fail(error, "invalid selected approval period");
  if (mode == "TIMED" ? !Integer(Get(request, "duration_ms"), 100, 3600000) :
      request.count("duration_ms") != 0)
    return Fail(error, "invalid selected approval duration");
  if (mode == "SESSION" && (Get(request, "session").empty() ||
      !Integer(Get(request, "generation"), 1, INT64_MAX)))
    return Fail(error, "session approval requires active session context");
  for (int i = 0; i < Number(request, "count"); ++i) {
    std::string prefix = "r" + std::to_string(i) + ".";
    if (!Identifier(Get(request, prefix + "feature_id")) ||
        !Integer(Get(request, prefix + "feature_revision"), 1, INT64_MAX) ||
        !Integer(Get(request, prefix + "policy_version"), 1, INT64_MAX))
      return Fail(error, "feature selection requires explicit policy and feature revisions");
  }
  const auto digest = Get(request, "selection_digest");
  if (digest.size() != 64 || digest != SelectionDigest(request))
    return Fail(error, "selection digest mismatch");
  return true;
}

bool Covers(const Message& request, const std::string& mode, int64_t expires,
    int64_t now) {
  if (expires != 0 && expires <= now)
    return false;
  if (Get(request, "approval_version") != "1" || Get(request, "request_kind") == "TASK")
    return true;
  const auto selected = Get(request, "grant_mode");
  if (selected == "ONCE" || mode == "PERSISTENT")
    return true;
  if (selected == "SESSION")
    return mode == "SESSION";
  const auto target = Number(request, "_approval_deadline", INT64_MAX);
  return selected == "TIMED" && mode == "TIMED" && expires >= target;
}

}  // namespace approval
}  // namespace consent
