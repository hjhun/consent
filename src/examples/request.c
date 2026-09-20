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

#include <glib.h>
#include <stdio.h>

struct completion {
  GMainLoop* loop;
  consent_result_t* owned_result;
  int status;
  int api_returned;
  int timed_out;
};

static void completed(int status, const consent_result_t* result, void* data) {
  struct completion* completion = data;
  completion->status = completion->api_returned ? status : CONSENT_ERROR_PROTOCOL;
  /* The callback result is borrowed. Clone before returning to use it below. */
  if (!completion->status)
    completion->status = consent_result_clone(result, &completion->owned_result);
  /* A synchronous check/cancel here would own this client's context and fail
   * with WOULD_DEADLOCK. Leave the loop before making such a call. */
  g_main_loop_quit(completion->loop);
}

static gboolean stop_waiting(gpointer data) {
  struct completion* completion = data;
  completion->timed_out = 1;
  completion->status = CONSENT_ERROR_TIMEOUT;
  g_main_loop_quit(completion->loop);
  return G_SOURCE_REMOVE;
}

static void cancel_remote(consent_client_h client, const char* subject,
    const char* profile, const char* client_request_id) {
  consent_params_t* params = NULL;
  consent_result_t* result = NULL;
  int status = consent_params_create(&params);
  if (!status)
    status = consent_params_set(params, "subject", subject);
  if (!status)
    status = consent_params_set(params, "profile", profile);
  if (!status)
    status = consent_params_set(params, "client_request_id", client_request_id);
  if (!status)
    status = consent_cancel_request(client, params, &result);
  if (status)
    example_error("remote cancellation (outcome still needs reconciliation)", status);
  else
    printf("Remote state after cancellation: %s\n", example_decision(result));
  consent_result_free(result);
  consent_params_free(params);
}

int main(int argc, char** argv) {
  consent_client_h client = NULL;
  consent_params_t* params = NULL;
  consent_async_id_t operation = 0;
  GMainContext* context;
  GSource* watchdog = NULL;
  struct completion completion = {0};
  int exit_status = 1;
  int status;
  if (argc != 10 && argc != 12) {
    fprintf(stderr, "Usage: %s SUBJECT PROFILE DEFINITION POLICY_VERSION SCOPE PURPOSE RECIPIENT CLIENT_REQUEST_ID OPERATION_ID [SESSION GENERATION]\n", argv[0]);
    return 2;
  }
  /* This executable must be authenticated as argo. A separate authenticated
   * approval UI handles prompts; the example never impersonates that UI. */
  context = g_main_context_new();
  completion.loop = g_main_loop_new(context, FALSE);
  status = example_requirement(argv[1], argv[2], argv[3], argv[4], argv[5],
      argv[6], argv[7], argc == 12 ? argv[10] : NULL,
      argc == 12 ? argv[11] : NULL, &params);
  if (!status)
    status = consent_params_set(params, "client_request_id", argv[8]);
  if (!status)
    status = consent_params_set(params, "operation_id", argv[9]);
  if (!status)
    status = consent_params_set_int64(params, "deadline_ms", 60000);
  if (!status)
    status = consent_client_create_with_context(context, &client);
  if (!status)
    status = consent_request_async(client, params, completed, &completion, &operation);
  completion.api_returned = 1;
  consent_params_free(params);  // Accepted operations have copied every field.
  if (status)
    goto done;
  watchdog = g_timeout_source_new(65000);
  g_source_set_callback(watchdog, stop_waiting, &completion, NULL);
  g_source_attach(watchdog, context);
  /* Only this creating thread iterates the context, after submission returns. */
  g_main_loop_run(completion.loop);
  status = completion.status;
  if (completion.timed_out) {
    int detach_status = consent_async_detach(client, operation);
    if (detach_status && detach_status != CONSENT_ERROR_NOT_FOUND)
      example_error("detach", detach_status);
    /* Detach is local. The explicit remote cancel runs outside the dispatcher
     * and preserves request identity. It cannot undo an already final grant. */
    cancel_remote(client, argv[1], argv[2], argv[8]);
  } else if (!status) {
    printf("Approval result: %s (advisory; AUTHORIZE is still required)\n",
        example_decision(completion.owned_result));
    exit_status = consent_result_get_decision(completion.owned_result) ==
        CONSENT_DECISION_ALLOWED ? 0 : 3;
  }
done:
  if (status)
    example_error("request", status);
  if (watchdog) {
    g_source_destroy(watchdog);
    g_source_unref(watchdog);
  }
  /* Destroy suppresses queued callbacks before stack user_data/loop go away. */
  if (client) {
    int destroy_status = consent_client_destroy(client);
    if (destroy_status) {
      example_error("destroy", destroy_status);
      exit_status = 1;
    }
  }
  consent_result_free(completion.owned_result);
  g_main_loop_unref(completion.loop);
  g_main_context_unref(context);
  return exit_status;
}
