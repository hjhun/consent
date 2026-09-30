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
#include <consent.h>

#include <stdio.h>

int main(int argc, char **argv) {
  consent_client_h client = NULL;
  consent_params_t *params = NULL;
  consent_result_t *result = NULL;
  int status;

  if (argc != 1) {
    fprintf(stderr, "Usage: %s (fixed UI09 calendar fixture only)\n", argv[0]);
    return 2;
  }

  status = consent_client_create(&client);
  if (!status)
    status = consent_params_create(&params);
  if (!status)
    status = consent_params_set(params, "subject", "owner");
  if (!status)
    status = consent_params_set(params, "profile", "default");
  if (!status)
    status = consent_params_set(params, "definition", "mock.calendar.read");
  if (!status)
    status = consent_revoke(client, params, &result);
  printf("UI09_REVOKE status=%d\n", status);

  consent_result_free(result);
  consent_params_free(params);
  if (client) {
    int destroy_status = consent_client_destroy(client);
    if (!status)
      status = destroy_status;
  }
  return status ? 1 : 0;
}
