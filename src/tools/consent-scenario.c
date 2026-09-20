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
#include <consent.h>
#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
  fprintf(stderr, "FAIL line=%d expression=%s\n", __LINE__, #expression); exit(1); \
} } while (0)
#define CALL(expression) do { int call_status = (expression); if (call_status) { \
  fprintf(stderr, "FAIL line=%d expression=%s status=%d (%s)\n", __LINE__, \
      #expression, call_status, consent_error_string(call_status)); exit(1); \
} } while (0)

static consent_client_h client;
static consent_client_h ui;
static unsigned serial;
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
  CHECK(consent_params_add_requirement(p, "demo.read", "read", scope,
      "answer", "") == 0);
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
  set(p, "message.en.body", "Allow this exact scope for the selected duration?");
  set(p, "message.ko.title", "시험 자원 읽기");
  set(p, "message.ko.body", "표시된 범위를 선택한 기간 동안 허용합니까?");
  CALL(consent_register(client, "demo.package", app, p));
  CALL(consent_register(client, "demo.package", app, p));
  set(p, "message.en.title", "Conflicting duplicate");
  CHECK(consent_register(client, "demo.package", app, p) == CONSENT_ERROR_CONFLICT);
  consent_params_free(p);
}

static void approve(const char* scope, const char* mode, const char* session,
    const char* generation) {
  consent_params_t* p = query(scope, session, generation);
  char operation[64];
  snprintf(operation, sizeof(operation), "approval-%u", ++serial);
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
  for (int attempt = 0; attempt < 100 && status == CONSENT_ERROR_NOT_FOUND; ++attempt) {
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
  CHECK(consent_respond(ui, lookup, &result) == 0);
  consent_result_free(result);
  gint64 deadline = g_get_monotonic_time() + 5000000;
  while (!callbacks && g_get_monotonic_time() < deadline) {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000);
  }
  CHECK(callbacks == 1 && callback_status == 0 &&
      callback_decision == CONSENT_DECISION_ALLOWED);
  g_free(request_id);
  g_free(token);
  consent_params_free(lookup);
  consent_params_free(p);
}

static void check(const char* scope, consent_decision_e decision) {
  consent_params_t* p = query(scope, NULL, NULL);
  consent_result_t* result = NULL;
  CHECK(consent_check(client, p, 5000, &result) == 0);
  CHECK(consent_result_get_decision(result) == decision);
  consent_result_free(result);
  consent_params_free(p);
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s basic|persistent|recovered GENERATION\n", argv[0]);
    return 2;
  }
  CHECK(consent_client_create(&client) == 0);
  CHECK(consent_client_create(&ui) == 0);
  if (!strcmp(argv[1], "persistent") || !strcmp(argv[1], "recovered")) {
    check("persistent-scope", !strcmp(argv[1], "persistent") ?
        CONSENT_DECISION_ALLOWED : CONSENT_DECISION_CONSENT_REQUIRED);
    printf("PASS %s\n", argv[1]);
    consent_client_destroy(ui);
    consent_client_destroy(client);
    return 0;
  }
  CHECK(!strcmp(argv[1], "basic"));
  define("demo.app", "demo.read", "register-app1", argv[2]);
  define("demo.app2", "demo.other", "register-app2", argv[2]);
  check("persistent-scope", CONSENT_DECISION_CONSENT_REQUIRED);
  approve("persistent-scope", "PERSISTENT", NULL, NULL);
  check("persistent-scope", CONSENT_DECISION_ALLOWED);
  consent_params_t* p = query("persistent-scope", NULL, NULL);
  set(p, "mode", "AUTHORIZE");
  set(p, "operation_id", "execute-persistent");
  set(p, "step_id", "read");
  consent_result_t* result = NULL;
  CHECK(consent_check(client, p, 5000, &result) == 0);
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  consent_result_free(result);
  CHECK(consent_check(client, p, 5000, &result) == 0);
  CHECK(!strcmp(consent_result_get(result, "retry"), "1"));
  consent_result_free(result);
  set(p, "r0.scope", "different-scope");
  CHECK(consent_check(client, p, 5000, &result) == CONSENT_ERROR_CONFLICT);
  consent_params_free(p);

  p = params();
  CHECK(consent_session_open(client, p, &result) == 0);
  char* session = field(result, "session");
  char* generation = field(result, "generation");
  consent_result_free(result);
  consent_params_free(p);
  approve("session-scope", "SESSION", session, generation);
  p = query("session-scope", session, generation);
  set(p, "mode", "AUTHORIZE");
  set(p, "operation_id", "session-data");
  set(p, "step_id", "read");
  CHECK(consent_check(client, p, 5000, &result) == 0);
  char* receipt = field(result, "receipt");
  consent_result_free(result);
  consent_params_free(p);
  p = params();
  set(p, "session", session);
  set(p, "generation", generation);
  set(p, "receipt", receipt);
  set(p, "scope", "session-scope");
  set(p, "purpose", "answer");
  CHECK(consent_data_register(client, p, &result) == 0);
  char* artifact = field(result, "artifact");
  consent_result_free(result);
  CHECK(consent_session_close(client, p, &result) == 0);
  CHECK(!strcmp(consent_result_get(result, "state"), "CLOSING"));
  consent_result_free(result);
  consent_params_t* q = query("session-scope", session, generation);
  CHECK(consent_check(client, q, 5000, &result) == CONSENT_ERROR_SESSION_CLOSED);
  consent_params_free(q);
  set(p, "artifact", artifact);
  CHECK(consent_data_release(client, p, &result) == 0);
  consent_result_free(result);
  CHECK(consent_session_get_state(client, p, &result) == 0);
  CHECK(!strcmp(consent_result_get(result, "state"), "CLOSED"));
  consent_result_free(result);
  consent_params_free(p);
  g_free(session);
  g_free(generation);
  g_free(receipt);
  g_free(artifact);
  CHECK(consent_client_destroy(ui) == 0);
  CHECK(consent_client_destroy(client) == 0);
  puts("PASS basic: registration/retry, SYNC/ASYNC, UI, authorization, session, artifact cleanup");
  return 0;
}
