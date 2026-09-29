/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
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

#ifndef CONSENT_PERFORMANCE_FIXTURE_H_
#define CONSENT_PERFORMANCE_FIXTURE_H_

#include <consent.h>
#include <glib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      fprintf(stderr, "FAIL line=%d expression=%s\n", __LINE__, #expression); \
      exit(1);                                                                \
    }                                                                         \
  } while (0)
#define CALL(expression)                                                       \
  do {                                                                         \
    int call_status = (expression);                                            \
    if (call_status) {                                                         \
      fprintf(stderr, "FAIL line=%d expression=%s status=%d (%s)\n", __LINE__, \
              #expression, call_status, consent_error_string(call_status));    \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static consent_client_h client;
static consent_client_h ui;
static unsigned serial;
static char phase[64] = "basic";
static int callbacks;
static int returned;
static int callback_status;
static consent_decision_e callback_decision;

static void set(consent_params_t* p, const char* key, const char* value) {
  CHECK(consent_params_set(p, key, value) == 0);
}

static consent_params_t* params(void) {
  consent_params_t* p = NULL;
  CHECK(consent_params_create(&p) == 0);
  set(p, "subject", "demo.subject");
  set(p, "profile", "demo.profile");
  return p;
}

static char* field(consent_result_t* r, const char* key) {
  const char* value = consent_result_get(r, key);
  CHECK(value != NULL);
  return g_strdup(value);
}

static void completed(int status, const consent_result_t* result, void* data) {
  (void)data;
  CHECK(returned);
  ++callbacks;
  callback_status = status;
  callback_decision = consent_result_get_decision(result);
}

static consent_params_t* query(const char* scope, const char* session,
                               const char* generation) {
  consent_params_t* p = params();
  CHECK(consent_params_add_requirement(p, "demo.read", "read", scope, "answer",
                                       "") == 0);
  set(p, "r0.holder", "scenario");
  if (session) {
    set(p, "session", session);
    set(p, "generation", generation);
  }
  return p;
}

static void define(const char* app, const char* definition,
                   const char* operation, const char* generation) {
  consent_params_t* p = params();
  set(p, "definition", definition);
  set(p, "expected_generation", generation);
  set(p, "operation_id", operation);
  set(p, "enforcer", "scenario");
  set(p, "policy_version", "1");
  set(p, "text_revision", "1");
  set(p, "level", "1");
  set(p, "modes", "ONCE,SESSION,TIMED,PERSISTENT");
  set(p, "retention_ms", "60000");
  set(p, "default_locale", "en");
  set(p, "message.en.title", "Read test resource");
  set(p, "message.en.body",
      "Allow this exact scope for the selected duration?");
  set(p, "message.ko.title", "시험 자원 읽기");
  set(p, "message.ko.body", "표시된 범위를 선택한 기간 동안 허용합니까?");
  CALL(consent_register(client, "demo.package", app, p));
  CALL(consent_register(client, "demo.package", app, p));
  set(p, "message.en.title", "Conflicting duplicate");
  CHECK(consent_register(client, "demo.package", app, p) ==
        CONSENT_ERROR_CONFLICT);
  consent_params_free(p);
}

static void approve(const char* scope, const char* mode, const char* session,
                    const char* generation) {
  consent_params_t* p = query(scope, session, generation);
  char operation[128];
  snprintf(operation, sizeof(operation), "%s-approval-%u", phase, ++serial);
  set(p, "client_request_id", operation);
  set(p, "operation_id", operation);
  consent_async_id_t async_id;
  callbacks = 0;
  returned = 0;
  CHECK(consent_request_async(client, p, completed, NULL, &async_id) == 0);
  returned = 1;
  consent_params_t* lookup = params();
  set(lookup, "client_request_id", operation);
  consent_result_t* result = NULL;
  int status = CONSENT_ERROR_NOT_FOUND;
  for (int attempt = 0; attempt < 100 && status == CONSENT_ERROR_NOT_FOUND;
       ++attempt) {
    g_usleep(10000);
    status = consent_get_request_result(ui, lookup, &result);
  }
  CHECK(status == 0);
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_PENDING);
  char* request_id = field(result, "request_id");
  consent_result_free(result);
  set(lookup, "request_id", request_id);
  set(lookup, "locale", "ko-KR");
  CHECK(consent_get_prompt(ui, lookup, &result) == 0);
  char* token = field(result, "prompt_token");
  consent_result_free(result);
  set(lookup, "prompt_token", token);
  set(lookup, "decision", "ALLOWED");
  set(lookup, "grant_mode", mode);
  if (!strcmp(mode, "TIMED"))
    set(lookup, "duration_ms", "1000");
  CHECK(consent_respond(ui, lookup, &result) == 0);
  consent_result_free(result);
  gint64 deadline = g_get_monotonic_time() + 5000000;
  while (!callbacks && g_get_monotonic_time() < deadline) {
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_usleep(1000);
  }
  CHECK(callbacks == 1 && callback_status == 0 &&
        callback_decision == CONSENT_DECISION_ALLOWED);
  g_free(request_id);
  g_free(token);
  consent_params_free(lookup);
  consent_params_free(p);
}

#endif  // CONSENT_PERFORMANCE_FIXTURE_H_
