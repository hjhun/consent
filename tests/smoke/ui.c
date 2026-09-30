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

#include "common.h"

#include <stdio.h>
#include <string.h>
int main(int argc, char** argv) {
  if (argc != 4 || (strcmp(argv[1], "--auto-approve-smoke") &&
                    strcmp(argv[1], "--auto-deny-smoke"))) {
    fprintf(stderr,
            "Usage: %s --auto-approve-smoke|--auto-deny-smoke "
            "REQUEST_ID MODE\n",
            argv[0]);
    return 2;
  }
  if (strcmp(argv[3], "ONCE") && strcmp(argv[3], "PERSISTENT"))
    return 2;
  consent_client_h client = NULL;
  consent_params_t* params = smoke_params();
  consent_result_t* result = NULL;
  smoke_call(consent_client_create(&client), "create");
  smoke_set(params, "request_id", argv[2]);
  smoke_set(params, "locale", "en");
  smoke_call(consent_get_prompt(client, params, &result), "UI prompt");
  const char* token = consent_result_get(result, "prompt_token");
  smoke_expect(token != NULL, "display-bound prompt token");
  smoke_set(params, "prompt_token", token);
  consent_result_free(result);
  result = NULL;
  int deny = !strcmp(argv[1], "--auto-deny-smoke");
  smoke_set(params, "decision", deny ? "DENIED" : "ALLOWED");
  smoke_set(params, "grant_mode", argv[3]);
  smoke_call(consent_respond(client, params, &result), "smoke UI response");
  puts(deny
           ? "PASS smoke-only automatic denial; not a product user decision"
           : "PASS smoke-only automatic approval; not a product user decision");
  consent_result_free(result);
  consent_params_free(params);
  smoke_call(consent_client_destroy(client), "destroy");
  return 0;
}
