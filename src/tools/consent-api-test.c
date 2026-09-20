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
#include <glib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*management_fn)(consent_client_h, const consent_params_t*,
    consent_result_t**);

struct management_entry {
  const char* name;
  management_fn function;
};

static const struct management_entry management[] = {
  {"prompt", consent_get_prompt}, {"respond", consent_respond},
  {"result", consent_get_request_result}, {"cancel", consent_cancel_request},
  {"revoke", consent_revoke}, {"session_open", consent_session_open},
  {"session_suspend", consent_session_suspend}, {"session_resume", consent_session_resume},
  {"session_close", consent_session_close}, {"session_state", consent_session_get_state},
  {"data_register", consent_data_register}, {"data_derived", consent_data_register_derived},
  {"data_release", consent_data_release}, {"cleanup", consent_cleanup_get_state},
  {"cleanup_list", consent_cleanup_get_pending}
};

struct callback_state {
  int returned;
  int callbacks;
  int status;
  int ordering_failure;
  consent_result_t* result;
};

static void result_cb(int status, const consent_result_t* result, void* user_data) {
  struct callback_state* state = user_data;
  if (!state->returned)
    state->ordering_failure = 1;
  ++state->callbacks;
  state->status = status;
  if (!status)
    state->status = consent_result_clone(result, &state->result);
}

static void print_result(int status, const consent_result_t* result) {
  size_t i;
  printf("status=%d\n", status);
  if (status)
    printf("error=%s\n", consent_error_string(status));
  for (i = 0; i < consent_result_size(result); ++i) {
    const char* key;
    const char* value;
    if (!consent_result_get_at(result, i, &key, &value) && strcmp(key, "status"))
      printf("%s=%s\n", key, value);
  }
}

static int parse_number(const char* value, long* number) {
  char* end;
  errno = 0;
  *number = strtol(value, &end, 10);
  return !errno && *value && !*end;
}

static void usage(const char* program) {
  fprintf(stderr,
      "Usage: %s METHOD [PACKAGE [APP]] [key=value ...] [options]\n"
      "Methods: register update unregister request check prompt respond result\n"
      " cancel revoke session_open session_suspend session_resume session_close\n"
      " session_state data_register data_derived data_release cleanup cleanup_list\n"
      "Options: --async --timeout-ms=N --repeat=N --expect-status=N\n"
      " --expect-decision=ALLOWED|DENIED|CONSENT_REQUIRED|...\n"
      "register/update require package, app, expected_generation, operation_id,\n"
      " definition, enforcer, policy_version, text_revision, level, modes,\n"
      " default_locale and message.<locale>.title/body.\n"
      "unregister requires package, expected_generation and operation_id.\n"
      "check/request accept count and r0.definition/operation/scope/purpose,\n"
      " subject/profile; AUTHORIZE additionally operation_id and step_id.\n",
      program);
}

int main(int argc, char** argv) {
  consent_client_h client = NULL;
  consent_params_t* params = NULL;
  const char* method;
  const char* package = NULL;
  const char* app = NULL;
  const char* expected_decision = NULL;
  long expected_status = 0;
  long timeout = 5000;
  long repeat = 1;
  int async = 0;
  int cursor = 2;
  int status;
  int failed = 0;
  long iteration;
  if (argc < 2 || !strcmp(argv[1], "--help")) {
    usage(argv[0]);
    return argc < 2 ? 2 : 0;
  }
  method = argv[1];
  if (!strcmp(method, "register") || !strcmp(method, "update")) {
    if (argc < 4) {
      usage(argv[0]);
      return 2;
    }
    package = argv[cursor++];
    app = argv[cursor++];
  } else if (!strcmp(method, "unregister")) {
    if (argc < 3) {
      usage(argv[0]);
      return 2;
    }
    package = argv[cursor++];
  }
  status = consent_params_create(&params);
  if (status)
    return 2;
  for (; cursor < argc; ++cursor) {
    char* argument = argv[cursor];
    char* equal;
    if (!strcmp(argument, "--async")) {
      async = 1;
      continue;
    }
    if (!strncmp(argument, "--expect-status=", 16)) {
      if (!parse_number(argument + 16, &expected_status))
        failed = 1;
      continue;
    }
    if (!strncmp(argument, "--expect-decision=", 18)) {
      expected_decision = argument + 18;
      continue;
    }
    if (!strncmp(argument, "--timeout-ms=", 13)) {
      if (!parse_number(argument + 13, &timeout) || timeout < 1 || timeout > 300000)
        failed = 1;
      continue;
    }
    if (!strncmp(argument, "--repeat=", 9)) {
      if (!parse_number(argument + 9, &repeat) || repeat < 1 || repeat > 10000)
        failed = 1;
      continue;
    }
    equal = strchr(argument, '=');
    if (!equal) {
      failed = 1;
      continue;
    }
    *equal = '\0';
    status = consent_params_set(params, argument, equal + 1);
    *equal = '=';
    if (status) {
      fprintf(stderr, "Invalid parameter %s: %s\n", argument, consent_error_string(status));
      failed = 1;
    }
  }
  if (failed || (async && strcmp(method, "check") && strcmp(method, "request"))) {
    consent_params_free(params);
    usage(argv[0]);
    return 2;
  }
  status = consent_client_create(&client);
  if (status) {
    print_result(status, NULL);
    consent_params_free(params);
    return status == expected_status ? 0 : 1;
  }
  for (iteration = 0; iteration < repeat && !failed; ++iteration) {
    consent_result_t* result = NULL;
    if (async) {
      struct callback_state state = {0};
      consent_async_id_t operation;
      gint64 deadline = g_get_monotonic_time() + timeout * 1000;
      status = !strcmp(method, "request") ?
          consent_request_async(client, params, result_cb, &state, &operation) :
          consent_check_async(client, params, result_cb, &state, &operation);
      state.returned = 1;
      if (!status) {
        while (!state.callbacks && g_get_monotonic_time() < deadline) {
          while (g_main_context_iteration(NULL, FALSE)) {}
          if (!state.callbacks)
            g_usleep(1000);
        }
        if (!state.callbacks) {
          consent_async_detach(client, operation);
          status = CONSENT_ERROR_TIMEOUT;
        } else {
          status = state.status;
          result = state.result;
          if (state.ordering_failure || state.callbacks != 1)
            failed = 1;
        }
      }
      printf("callbacks=%d\ncallback_after_return=%s\n", state.callbacks,
          state.ordering_failure ? "false" : "true");
    } else if (!strcmp(method, "register") || !strcmp(method, "update")) {
      status = consent_register(client, package, app, params);
    } else if (!strcmp(method, "unregister")) {
      status = consent_unregister(client, package, params);
    } else if (!strcmp(method, "request")) {
      status = consent_request(client, params, (unsigned)timeout, &result);
    } else if (!strcmp(method, "check")) {
      status = consent_check(client, params, (unsigned)timeout, &result);
    } else {
      size_t i;
      status = CONSENT_ERROR_INVALID_PARAMETER;
      for (i = 0; i < sizeof(management) / sizeof(management[0]); ++i) {
        if (!strcmp(method, management[i].name)) {
          status = management[i].function(client, params, &result);
          break;
        }
      }
    }
    printf("iteration=%ld\n", iteration + 1);
    print_result(status, result);
    if (status != expected_status)
      failed = 1;
    if (expected_decision) {
      const char* actual = consent_result_get(result, "decision");
      if (!actual || strcmp(actual, expected_decision))
        failed = 1;
    }
    consent_result_free(result);
  }
  consent_client_destroy(client);
  consent_params_free(params);
  return failed ? 1 : 0;
}
