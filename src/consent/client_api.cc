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
#include "consent_client.h"

#include "api.hh"

#ifndef CONSENT_AUTHORITY_DIR
#define CONSENT_AUTHORITY_DIR "/opt/var/lib/consent-authority"
#endif

namespace {
using consent::api::Guard;
}  // namespace

extern "C" {

int consent_client_create(consent_client_h* client) {
  return consent_client_create_with_context(nullptr, client);
}

int consent_client_create_with_context(GMainContext* context, consent_client_h* client) {
  if (client)
    *client = nullptr;
  return Guard([&]() -> int {
    if (!client)
      return CONSENT_ERROR_INVALID_PARAMETER;
    auto value = std::unique_ptr<consent_client>(new consent_client);
    value->impl.reset(new consent::Client(context));
    int status = value->impl->Connect();
    if (!status)
      *client = value.release();
    return status;
  });
}

int consent_client_destroy(consent_client_h client) {
  return Guard([&]() -> int {
    if (client && client->offline) {
      if (!client->offline->IsCurrentProcess() || !client->offline->IsOwner())
        return CONSENT_ERROR_INVALID_PARAMETER;
      delete client;
      return 0;
    }
    if (!client || !client->impl->IsOwner() || !client->impl->IsCurrentProcess())
      return CONSENT_ERROR_INVALID_PARAMETER;
    int status = client->impl->Close();
    delete client;
    return status;
  });
}

int consent_client_create_offline_registration(const char* image_root,
    consent_client_h* client) {
  if (client)
    *client = nullptr;
  return Guard([&]() -> int {
    if (!client || !image_root || !*image_root)
      return CONSENT_ERROR_INVALID_PARAMETER;
    auto value = std::unique_ptr<consent_client>(new consent_client);
    value->offline.reset(new consent::offline::RegistrationWriter);
    int status = value->offline->Open(image_root, CONSENT_AUTHORITY_DIR);
    if (!status)
      *client = value.release();
    return status;
  });
}

}  // extern "C"
