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
#ifndef CONSENT_CLIENT_HH_
#define CONSENT_CLIENT_HH_

#include "consent.h"
#include "common/message.hh"

#include <memory>

namespace consent {
class Client final {
 public:
  explicit Client(GMainContext* context);
  ~Client();
  int Connect();
  int Close();
  bool IsOwner() const;
  bool IsCurrentProcess() const;
  int Call(Message message, unsigned timeout_ms, Message* result);
  int Submit(Message message, consent_result_cb callback, void* data,
      uint64_t* operation);
  int Detach(uint64_t operation);
 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace consent

struct consent_params {
  consent::Message values;
};
struct consent_result {
  consent::Message values;
};
struct consent_client {
  std::unique_ptr<consent::Client> impl;
};
#endif  // CONSENT_CLIENT_HH_
