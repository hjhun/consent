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
#include "example_common.h"

#include <stdio.h>

int main(int argc, char** argv) {
  consent_client_h client = NULL;
  consent_params_t* params = NULL;
  int status;
  if (argc != 10) {
    fprintf(stderr, "Usage: %s IMAGE_ROOT PACKAGE APP DEFINITION ENFORCER GENERATION OPERATION_ID POLICY_VERSION TEXT_REVISION\n", argv[0]);
    return 2;
  }
  /* Explicit image construction only. Real/effective UID must be root. Use
   * the image Installer's stable committed generation and registration ID.
   * Neither a failed online call nor this example creates that authority. */
  status = example_definition(argv[4], argv[5], argv[6], argv[7], argv[8],
      argv[9], &params);
  if (!status)
    status = consent_client_create_offline_registration(argv[1], &client);
  if (!status)
    status = consent_register(client, argv[2], argv[3], params);
  if (status)
    example_error("offline register", status);
  else
    puts("STAGED: durable definition only; startup identity/generation validation is still required.");
  consent_params_free(params);
  if (client) {
    int destroy_status = consent_client_destroy(client);
    if (destroy_status) {
      example_error("destroy", destroy_status);
      if (!status)
        status = destroy_status;
    }
  }
  return status ? 1 : 0;
}
