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
static int completed;
static int returned;
static int denied;
static void on_result(int status, const consent_result_t* result, void* data) {
  (void)data;
  smoke_expect(returned, "callback after return");
  smoke_call(status, "async result");
  smoke_expect(!completed, "callback at most once");
  smoke_expect(
      consent_result_get_decision(result) ==
          (denied ? CONSENT_DECISION_DENIED : CONSENT_DECISION_ALLOWED),
      "expected decision callback");
  completed = 1;
}
int main(int argc, char** argv) {
  if (argc != 3 && (argc != 4 || strcmp(argv[3], "--expect-denied"))) {
    fprintf(stderr, "Usage: %s DEFINITION REQUEST_OPERATION_ID\n", argv[0]);
    return 2;
  }
  denied = argc == 4;
  consent_client_h client = NULL;
  consent_params_t* params = smoke_requirement(argv[1]);
  consent_async_id_t async_id;
  smoke_set(params, "client_request_id", argv[2]);
  smoke_set(params, "operation_id", argv[2]);
  smoke_set(params, "deadline_ms", "20000");
  smoke_call(consent_client_create(&client), "create");
  smoke_call(consent_request_async(client, params, on_result, NULL, &async_id),
             "argo request_async acceptance");
  returned = 1;
  consent_params_t* lookup = smoke_params();
  smoke_set(lookup, "client_request_id", argv[2]);
  consent_result_t* result = NULL;
  int status = CONSENT_ERROR_NOT_FOUND;
  for (int i = 0; i < 200 && status == CONSENT_ERROR_NOT_FOUND; ++i) {
    g_usleep(10000);
    status = consent_get_request_result(client, lookup, &result);
  }
  smoke_call(status, "argo lookup");
  const char* id = consent_result_get(result, "request_id");
  smoke_expect(id != NULL, "request ID handed to UI");
  printf("REQUEST_ID %s\n", id);
  fflush(stdout);
  gint64 deadline = g_get_monotonic_time() + 20000000;
  while (!completed && g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    g_usleep(1000);
  }
  smoke_expect(completed, "approval callback deadline");
  puts(denied ? "PASS argo accepted then denied callback"
              : "PASS argo accepted then allowed callback");
  consent_result_free(result);
  consent_params_free(lookup);
  consent_params_free(params);
  smoke_call(consent_client_destroy(client), "destroy");
  return 0;
}
