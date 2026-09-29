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
#ifndef CONSENT_COMMON_DISPATCH_HH_
#define CONSENT_COMMON_DISPATCH_HH_

#include <glib.h>

#include <functional>
#include <memory>

namespace consent {

// Own posted sources. Stop/join context threads before destruction;
// cancellation then removes queued callbacks and their captures, including
// uniterated startup.
class Dispatcher final {
 public:
  Dispatcher();
  ~Dispatcher();
  Dispatcher(const Dispatcher&) = delete;
  Dispatcher& operator=(const Dispatcher&) = delete;
  // Always attaches a source. False publishes no callback and releases owners.
  bool Post(GMainContext* context, std::function<void()> work) noexcept;
  void Cancel() noexcept;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace consent
#endif  // CONSENT_COMMON_DISPATCH_HH_
