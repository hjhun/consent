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
  consent_result_t* result = NULL;
  int exit_status = 1;
  int status;
  if (argc != 10 && argc != 12) {
    fprintf(stderr, "Usage: %s SUBJECT PROFILE DEFINITION POLICY_VERSION SCOPE PURPOSE RECIPIENT OPERATION_ID STEP_ID [SESSION GENERATION]\n", argv[0]);
    return 2;
  }
  /* The process must be an authenticated checker/cm/ce/holder with enforcer
   * delegation. IDs identify one real execution, and must survive retries. */
  status = example_requirement(argv[1], argv[2], argv[3], argv[4], argv[5],
      argv[6], argv[7], argc == 12 ? argv[10] : NULL,
      argc == 12 ? argv[11] : NULL, &params);
  if (!status)
    status = consent_client_create(&client);
  if (!status)
    status = consent_params_set_check_mode(params, CONSENT_CHECK_QUERY);
  if (!status)
    status = consent_check(client, params, 5000, &result);
  if (status)
    goto done;
  printf("QUERY: %s (advisory)\n", example_decision(result));
  consent_result_free(result);
  result = NULL;

  /* Always let AUTHORIZE reconcile the same execution IDs. A previous ONCE
   * consumption can make QUERY say CONSENT_REQUIRED while its retry receipt
   * remains valid. Do not turn that advisory result into a new operation ID. */
  status = consent_params_set_check_mode(params, CONSENT_CHECK_AUTHORIZE);
  if (!status)
    status = consent_params_set(params, "operation_id", argv[8]);
  if (!status)
    status = consent_params_set(params, "step_id", argv[9]);
  if (!status)
    status = consent_check(client, params, 5000, &result);
  if (status)
    goto done;
  printf("AUTHORIZE: %s\n", example_decision(result));
  exit_status = 3;
  if (consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED) {
    if (!consent_result_get(result, "receipt")) {
      status = CONSENT_ERROR_PROTOCOL;
      exit_status = 1;
      goto done;
    }
    /* The real enforcer may execute the bound action here. This example does
     * not access a resource. A retry receipt is not permission to repeat an
     * external side effect; the enforcer must deduplicate that action too. */
    puts("Authorization receipt available; no protected action performed.");
    exit_status = 0;
  }
done:
  if (status)
    example_error("check", status);
  consent_result_free(result);
  consent_params_free(params);
  if (client) {
    int destroy_status = consent_client_destroy(client);
    if (destroy_status) {
      example_error("destroy", destroy_status);
      exit_status = 1;
    }
  }
  return exit_status;
}
