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
#ifndef CONSENT_IO_CONTEXT_HH_
#define CONSENT_IO_CONTEXT_HH_

#include <gio/gio.h>
#include <sys/types.h>

#include <atomic>
#include <functional>

namespace consent {
class IoContext final {
 public:
  IoContext(std::function<void()> work, std::function<void()> retire);
  ~IoContext();
  IoContext(const IoContext&) = delete;
  IoContext& operator=(const IoContext&) = delete;
  GMainContext* Get() const noexcept { return context_; }
  bool Start() noexcept;
  void Wake() noexcept;
  void Stop() noexcept;
  void Join() noexcept;
  void Deadline(gint64 when) noexcept;
  bool Wait(GSocket* socket, unsigned timeout_ms);

 private:
  static gboolean Dispatch(gpointer data) noexcept;
  void Abandon() noexcept;
  pid_t pid_;
  GMainContext* context_ = nullptr;
  GMainLoop* loop_ = nullptr;
  GSource* wake_ = nullptr;
  GSource* deadline_ = nullptr;
  GThread* thread_ = nullptr;
  std::atomic<bool> stopping_{false};
  std::function<void()> work_;
  std::function<void()> retire_;
};
}  // namespace consent
#endif  // CONSENT_IO_CONTEXT_HH_
