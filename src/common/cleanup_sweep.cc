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
#include "cleanup_sweep.hh"

#include <cerrno>
#include <set>

namespace consent {

int CleanupSweep(Message input, CleanupProgress* progress,
                 const std::function<int(const Message&, Message*)>& fetch,
                 const std::function<int(const std::string&)>& cleanup) {
  if (!progress)
    return -EINVAL;
  input.erase("cursor");
  if (progress->finished)
    *progress = {input, "", 0, false};
  else if (progress->scope != input)
    return -EINVAL;
  input["cursor"] = progress->cursor;
  std::set<std::string> positions;
  positions.insert(progress->cursor);
  // Bound one invocation while preserving progress through failed entries.
  for (unsigned page = 0; page < 128; ++page) {
    Message result;
    int status = fetch(input, &result);
    if (status)
      return status;
    int64_t count = 0;
    if (!ParseNumber(Get(result, "count"), &count) || count < 0 || count > 48)
      return -EPROTO;
    const auto more = Get(result, "more");
    const auto cursor = Get(result, "next_cursor");
    if ((more != "0" && more != "1") ||
        (more == "1" && (count == 0 || cursor.empty())) ||
        (more == "0" && !cursor.empty()))
      return -EPROTO;
    for (int64_t index = 0; index < count; ++index) {
      const auto artifact =
          Get(result, "a" + std::to_string(index) + ".artifact");
      if (artifact.empty())
        return -EPROTO;
      status = cleanup(artifact);
      if (status && !progress->failure)
        progress->failure = status;
    }
    if (more == "0") {
      progress->cursor.clear();
      progress->finished = true;
      return progress->failure;
    }
    if (!positions.insert(cursor).second)
      return -EPROTO;
    progress->cursor = cursor;
    input["cursor"] = cursor;
  }
  return -EINPROGRESS;
}

}  // namespace consent
