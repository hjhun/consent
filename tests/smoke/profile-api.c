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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Pending {
  int active;
  int returned;
  int done;
  int status;
  consent_result_t* result;
};

static void pending_reset(struct Pending* pending) {
  smoke_expect(!pending->active && !pending->result, "one pending operation");
  *pending = (struct Pending){.active = 1};
}

// Call only after client destruction has detached all accepted callbacks.
static void pending_clear(struct Pending* pending) {
  consent_result_free(pending->result);
  *pending = (struct Pending){0};
}

static void on_result(int status, const consent_result_t* result, void* data) {
  struct Pending* pending = data;
  smoke_expect(pending->active && pending->returned && !pending->done,
               "async callback contract");
  pending->status = status;
  if (result)
    smoke_call(consent_result_clone(result, &pending->result), "clone result");
  pending->done = 1;
}

static consent_result_t* pending_take(struct Pending* pending, int* status) {
  smoke_expect(pending->active && pending->done, "completed pending result");
  *status = pending->status;
  consent_result_t* result = pending->result;
  pending->result = NULL;
  pending->active = 0;
  return result;
}

static void pending_wait(struct Pending* pending) {
  gint64 deadline = g_get_monotonic_time() + 5000000;
  while (!pending->done && g_get_monotonic_time() < deadline) {
    g_main_context_iteration(NULL, FALSE);
    g_usleep(1000);
  }
  smoke_expect(pending->done, "bounded profile async callback");
}

int main(int argc, char** argv) {
  if (argc != 1) {
    fprintf(stderr, "Usage: %s (VERB PROFILE OPERATION EXPECTED_STATUS)\n",
            argv[0]);
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  struct Pending pending = {0};
  consent_client_h client = NULL;
  smoke_call(consent_client_create(&client), "profile actor create");
  char* session = NULL;
  char* generation = NULL;
  char* resume = NULL;
  char* request_id = NULL;
  char* prompt_token = NULL;
  puts("READY profile-api");
  char line[512];
  while (fgets(line, sizeof(line), stdin)) {
    char verb[32], profile[96], operation[96], expected_text[32], extra;
    if (!strchr(line, '\n') || sscanf(line, "%31s %95s %95s %31s %c",
        verb, profile, operation, expected_text, &extra) != 4)
      return 2;
    char* end = NULL;
    errno = 0;
    long expected = strtol(expected_text, &end, 10);
    if (errno || !end || *end || expected < INT_MIN || expected > INT_MAX)
      return 2;
    consent_params_t* params = NULL;
    smoke_call(consent_params_create(&params), "profile params");
    smoke_set(params, "subject", "smoke.subject");
    // The caller chooses profile explicitly. No active-profile substitution.
    if (strcmp(profile, "-"))
      smoke_set(params, "profile", profile);
    consent_result_t* result = NULL;
    int status = 0;
    int request = !strcmp(verb, "request") || !strcmp(verb, "request_async");
    int check = !strcmp(verb, "check") || !strcmp(verb, "check_async");
    if (request || check || !strcmp(verb, "begin")) {
      smoke_call(consent_params_add_requirement(params,
          "smoke.cm.tool.summary", "execute", "cli:smoke-tool/summary",
          "developer-tool-smoke", "fixture-provider"), "requirement");
      smoke_set(params, "r0.policy_version", "1");
      smoke_set(params, "operation_id", operation);
      smoke_set(params, "step_id", "probe");
      smoke_set(params, "client_request_id", operation);
      if (!strcmp(verb, "begin")) {
        pending_reset(&pending);
        consent_async_id_t id = 0;
        status = consent_request_async(client, params, on_result, &pending,
                                       &id);
        pending.returned = 1;
        if (status)
          pending.active = 0;
        if (!status) {
          for (unsigned i = 0; i < 100; ++i) {
            status = consent_get_request_result(client, params, &result);
            if (status != CONSENT_ERROR_NOT_FOUND)
              break;
            g_usleep(10000);
          }
          if (!status) {
            g_free(request_id);
            request_id = g_strdup(consent_result_get(result, "request_id"));
            printf("PROFILE_REQUEST_ID %s\n", request_id);
          }
        }
      } else if (strstr(verb, "_async")) {
        pending_reset(&pending);
        consent_async_id_t id = 0;
        status = request
            ? consent_request_async(client, params, on_result, &pending, &id)
            : consent_check_async(client, params, on_result, &pending, &id);
        pending.returned = 1;
        if (status)
          pending.active = 0;
        if (!status) {
          pending_wait(&pending);
          result = pending_take(&pending, &status);
        }
      } else {
        status = request ? consent_request(client, params, 5000, &result)
                         : consent_check(client, params, 5000, &result);
      }
    } else if (!strcmp(verb, "prompt") || !strcmp(verb, "respond")) {
      smoke_set(params, "request_id", operation);
      smoke_set(params, "locale", "en");
      if (!strcmp(verb, "prompt")) {
        status = consent_get_prompt(client, params, &result);
        if (!status) {
          g_free(prompt_token);
          prompt_token = g_strdup(consent_result_get(result, "prompt_token"));
        }
      } else {
        smoke_expect(prompt_token != NULL, "displayed prompt token");
        smoke_set(params, "prompt_token", prompt_token);
        smoke_set(params, "decision", "ALLOWED");
        smoke_set(params, "grant_mode", "PERSISTENT");
        status = consent_respond(client, params, &result);
      }
    } else if (!strcmp(verb, "poll")) {
      pending_wait(&pending);
      result = pending_take(&pending, &status);
    } else if (!strcmp(verb, "open")) {
      smoke_set(params, "lifecycle", "RESUMABLE_CONVERSATION");
      status = consent_session_open(client, params, &result);
      if (!status) {
        g_free(session);
        g_free(generation);
        g_free(resume);
        session = g_strdup(consent_result_get(result, "session"));
        generation = g_strdup(consent_result_get(result, "generation"));
        resume = g_strdup(consent_result_get(result, "resume_token"));
      }
    } else if (!strcmp(verb, "state") || !strcmp(verb, "resume") ||
               !strcmp(verb, "renew")) {
      smoke_expect(session && generation, "owned session exists");
      smoke_set(params, "session", session);
      smoke_set(params, "generation", generation);
      if (!strcmp(verb, "state")) {
        status = consent_session_get_state(client, params, &result);
      } else if (!strcmp(verb, "renew")) {
        status = consent_session_heartbeat(client, params, &result);
      } else {
        smoke_set(params, "resume_token", resume);
        status = consent_session_resume(client, params, &result);
      }
    } else if (!strcmp(verb, "reconnect")) {
      smoke_call(consent_client_destroy(client), "destroy old profile handle");
      pending_clear(&pending);
      client = NULL;
      status = consent_client_create(&client);
    } else {
      return 2;
    }
    smoke_expect(status == expected, "exact profile API status");
    const char* decision =
        result ? consent_result_get(result, "decision") : NULL;
    const char* state = result ? consent_result_get(result, "state") : NULL;
    const char* source = result ? consent_result_get(result, "source") : NULL;
    if ((request || !strcmp(verb, "poll")) && !status)
      smoke_expect(!source || strcmp(source, "CACHE"),
                   "profile authority request must reach daemon");
    printf("PROFILE_API verb=%s profile=%s status=%d decision=%s state=%s "
           "source=%s callbacks=%d\n",
           verb, profile, status,
           decision ? decision : "-", state ? state : "-",
           source ? source : "-", pending.done);
    consent_result_free(result);
    consent_params_free(params);
  }
  g_free(session);
  g_free(generation);
  g_free(resume);
  g_free(request_id);
  g_free(prompt_token);
  smoke_call(consent_client_destroy(client), "profile actor destroy");
  pending_clear(&pending);
  return 0;
}
