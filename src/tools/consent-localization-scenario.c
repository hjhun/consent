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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
  fprintf(stderr, "FAIL line=%d expression=%s\n", __LINE__, #expression); exit(1); \
} } while (0)
#define CALL(expression) do { int status = (expression); if (status) { \
  fprintf(stderr, "FAIL line=%d expression=%s status=%d\n", __LINE__, \
      #expression, status); exit(1); } } while (0)

static consent_client_h actor;
static consent_client_h ui;
static char definition[96];
static char run_id[64];
static const char* installation;
static char* session;
static char* generation;
static const char* recipient = "수신{name}%<tag>";
static unsigned serial;
static int returned;
static int callbacks;
static int callback_status;
static consent_decision_e callback_decision;

static void set(consent_params_t* params, const char* key, const char* value) {
  CALL(consent_params_set(params, key, value));
}

static consent_params_t* context(void) {
  consent_params_t* params = NULL;
  CALL(consent_params_create(&params));
  set(params, "subject", "demo.subject");
  set(params, "profile", "demo.profile");
  if (session) {
    set(params, "session", session);
    set(params, "generation", generation);
  }
  return params;
}

static char* field(consent_result_t* result, const char* key) {
  const char* value = consent_result_get(result, key);
  CHECK(value != NULL);
  return g_strdup(value);
}

static void identify(consent_params_t* params, char* id, size_t size) {
  snprintf(id, size, "%s-%u", run_id, ++serial);
  set(params, "operation_id", id);
  set(params, "client_request_id", id);
}

static consent_params_t* query(const char* scope) {
  consent_params_t* params = context();
  CALL(consent_params_add_requirement(params, definition, "read", scope,
      "answer", recipient));
  /* Existing numeric admission permits this spelling; prompt output must
   * normalize it without changing request/cache/retry scope identity. */
  set(params, "count", "01");
  set(params, "r0.holder", "localization-scenario");
  set(params, "r0.policy_version", "1");
  return params;
}

static void check_scope(const char* scope, consent_decision_e expected) {
  consent_params_t* params = query(scope);
  consent_result_t* result = NULL;
  CALL(consent_check(actor, params, 5000, &result));
  CHECK(consent_result_get_decision(result) == expected);
  consent_result_free(result);
  consent_params_free(params);
}

static consent_params_t* registration(void) {
  consent_params_t* params = context();
  char id[96];
  identify(params, id, sizeof(id));
  set(params, "definition", definition);
  set(params, "expected_generation", installation);
  set(params, "enforcer", "localization-scenario");
  set(params, "policy_version", "1");
  set(params, "text_revision", "1");
  set(params, "level", "1");
  set(params, "modes", "ONCE,SESSION,PERSISTENT");
  set(params, "retention_ms", "60000");
  set(params, "template_version", "1");
  set(params, "parameter.days.source", "scope");
  set(params, "parameter.days.type", "integer");
  set(params, "parameter.days.min", "1");
  set(params, "parameter.days.max", "365");
  set(params, "parameter.purpose.source", "purpose");
  set(params, "parameter.purpose.type", "string");
  set(params, "parameter.purpose.max_bytes", "32");
  set(params, "parameter.recipient.source", "recipient");
  set(params, "parameter.recipient.type", "string");
  set(params, "parameter.recipient.max_bytes", "64");
  set(params, "parameter.retention_ms.source", "retention_ms");
  set(params, "parameter.retention_ms.type", "integer");
  set(params, "parameter.retention_ms.min", "0");
  set(params, "parameter.retention_ms.max", "86400000");
  set(params, "default_locale", "en");
  set(params, "locale_fallback.fr-CA", "ko");
  set(params, "message.en.title", "Read history");
  set(params, "message.en.body", "Read {days} days for {purpose} to {recipient}; keep result {retention_ms} ms.");
  set(params, "message.ko.title", "기록 읽기");
  set(params, "message.ko.body", "{days}일 기록을 {purpose} 목적으로 {recipient}에 제공하고 결과를 {retention_ms} ms 보관합니다.");
  return params;
}

static void completed(int status, const consent_result_t* result, void* data) {
  (void)data;
  CHECK(returned);
  ++callbacks;
  callback_status = status;
  callback_decision = consent_result_get_decision(result);
}

static void await_decision(consent_decision_e expected) {
  gint64 deadline = g_get_monotonic_time() + 5000000;
  while (!callbacks && g_get_monotonic_time() < deadline) {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000);
  }
  CHECK(callbacks == 1 && callback_status == 0 && callback_decision == expected);
}

static consent_params_t* begin(const char* scope) {
  consent_params_t* params = query(scope);
  char id[96];
  identify(params, id, sizeof(id));
  callbacks = returned = 0;
  consent_async_id_t local;
  CALL(consent_request_async(actor, params, completed, NULL, &local));
  returned = 1;
  consent_params_free(params);
  consent_params_t* lookup = context();
  set(lookup, "client_request_id", id);
  consent_result_t* result = NULL;
  int status = CONSENT_ERROR_NOT_FOUND;
  for (int attempt = 0; attempt < 100 && status == CONSENT_ERROR_NOT_FOUND; ++attempt) {
    g_usleep(10000);
    status = consent_get_request_result(ui, lookup, &result);
  }
  CHECK(status == 0 && consent_result_get_decision(result) == CONSENT_DECISION_PENDING);
  set(lookup, "request_id", consent_result_get(result, "request_id"));
  consent_result_free(result);
  return lookup;
}

static char* prompt(consent_params_t* lookup, const char* locale, const char* selected) {
  set(lookup, "locale", locale);
  consent_result_t* result = NULL;
  CALL(consent_get_prompt(ui, lookup, &result));
  CHECK(!strcmp(consent_result_get(result, "r0.locale"), selected));
  CHECK(!strcmp(consent_result_get(result, "count"), "1"));
  char* formatted = NULL;
  CALL(consent_prompt_format(result, 0, "body", &formatted));
  const char* expected = !strcmp(selected, "ko") ?
      "30일 기록을 answer 목적으로 수신{name}%<tag>에 제공하고 결과를 60000 ms 보관합니다." :
      "Read 30 days for answer to 수신{name}%<tag>; keep result 60000 ms.";
  CHECK(!strcmp(formatted, expected));
  free(formatted);
  formatted = (char*)1;
  CHECK(consent_prompt_format(result, 0, "invalid", &formatted) == CONSENT_ERROR_INVALID_PARAMETER);
  CHECK(formatted == NULL);
  char* token = field(result, "prompt_token");
  consent_result_free(result);
  return token;
}

static void typed_approval(void) {
  consent_params_t* lookup = begin("30");
  set(lookup, "locale", "ko-KR");
  consent_result_t* result = NULL;
  consent_params_t* retry = query("90");
  /* A changed exact scope must conflict before a pending request retry can
   * return the old request result. The original identifiers remain stable. */
  consent_result_t* stored = NULL;
  CALL(consent_get_request_result(ui, lookup, &stored));
  const char* request_key = consent_result_get(stored, "client_request_id");
  CHECK(request_key != NULL);
  set(retry, "client_request_id", request_key);
  set(retry, "operation_id", request_key);
  consent_result_free(stored);
  CHECK(consent_request(actor, retry, 1000, &result) == CONSENT_ERROR_CONFLICT);
  consent_params_free(retry);
  CHECK(consent_get_prompt(ui, lookup, &result) == CONSENT_ERROR_INVALID_PARAMETER);
  set(lookup, "template_version", "2");
  CHECK(consent_get_prompt(ui, lookup, &result) == CONSENT_ERROR_INVALID_PARAMETER);
  set(lookup, "template_version", "1");
  char* korean = prompt(lookup, "ko-KR", "ko");
  char* mapped = prompt(lookup, "fr-CA", "ko");
  set(lookup, "prompt_token", korean);
  set(lookup, "decision", "ALLOWED");
  set(lookup, "grant_mode", "PERSISTENT");
  CHECK(consent_respond(ui, lookup, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  set(lookup, "prompt_token", mapped);
  set(lookup, "locale", "en");
  CHECK(consent_respond(ui, lookup, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  char* english = prompt(lookup, "en-US", "en");
  char* fallback = prompt(lookup, "de-DE", "en");
  set(lookup, "prompt_token", english);
  CHECK(consent_respond(ui, lookup, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  set(lookup, "prompt_token", fallback);
  CALL(consent_respond(ui, lookup, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  consent_result_free(result);
  await_decision(CONSENT_DECISION_ALLOWED);
  g_free(korean);
  g_free(mapped);
  g_free(english);
  g_free(fallback);
  consent_params_free(lookup);
  puts("PASS typed UI: Korean/English/direct/default fallback, single-pass literal values, capability/locale/token binding");
}

static void reject_invalid_values(void) {
  const char* invalid[] = {"030", "0", "366", "-0", "thirty", "+30", "1.0", " 30"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    consent_params_t* params = query(invalid[i]);
    consent_result_t* result = NULL;
    CHECK(consent_check(actor, params, 5000, &result) == CONSENT_ERROR_INVALID_PARAMETER);
    consent_params_free(params);
  }
  consent_params_t* params = query("30");
  consent_result_t* rejected = NULL;
  set(params, "r0.policy_version", "");
  CHECK(consent_check(actor, params, 5000, &rejected) == -ESTALE);
  set(params, "r0.policy_version", "1");
  const char invalid_utf8[] = {(char)0xff, 0};
  CHECK(consent_params_set(params, "r0.recipient", invalid_utf8) == CONSENT_ERROR_INVALID_PARAMETER);
  char oversized[66];
  memset(oversized, 'a', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  set(params, "r0.recipient", oversized);
  CHECK(consent_check(actor, params, 5000, &rejected) == CONSENT_ERROR_INVALID_PARAMETER);
  set(params, "r0.recipient", recipient);
  set(params, "r0.display_args.days", "90");
  consent_result_t* result = NULL;
  CHECK(consent_check(actor, params, 5000, &result) == CONSENT_ERROR_INVALID_PARAMETER);
  consent_params_free(params);
  puts("PASS typed source validation: noncanonical/type/range/independent display arguments rejected");
}

static void bound_receipt_and_artifacts(void) {
  consent_params_t* params = query("30");
  char id[96];
  identify(params, id, sizeof(id));
  set(params, "mode", "AUTHORIZE");
  set(params, "step_id", "read");
  consent_result_t* result = NULL;
  CALL(consent_check(actor, params, 5000, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  char* receipt = field(result, "receipt");
  consent_result_free(result);
  CALL(consent_check(actor, params, 5000, &result));
  CHECK(!strcmp(consent_result_get(result, "receipt"), receipt));
  consent_result_free(result);
  set(params, "r0.scope", "90");
  CHECK(consent_check(actor, params, 5000, &result) == CONSENT_ERROR_CONFLICT);
  identify(params, id, sizeof(id));
  CALL(consent_check(actor, params, 5000, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_CONSENT_REQUIRED);
  consent_result_free(result);
  consent_params_free(params);

  params = context();
  set(params, "receipt", receipt);
  set(params, "scope", "90");
  set(params, "purpose", "answer");
  set(params, "recipient", recipient);
  CHECK(consent_data_register(actor, params, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  set(params, "scope", "30");
  CALL(consent_data_register(actor, params, &result));
  char* artifact = field(result, "artifact");
  consent_result_free(result);
  set(params, "artifact", artifact);
  set(params, "operation", "reuse-data");
  CALL(consent_check(actor, params, 5000, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  consent_result_free(result);
  set(params, "scope", "90");
  CHECK(consent_check(actor, params, 5000, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  set(params, "scope", "30");
  set(params, "count", "1");
  set(params, "parent0", artifact);
  CALL(consent_data_register_derived(actor, params, &result));
  char* derived = field(result, "artifact");
  consent_result_free(result);
  set(params, "artifact", derived);
  set(params, "scope", "90");
  CHECK(consent_check(actor, params, 5000, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  set(params, "scope", "30");
  CALL(consent_check(actor, params, 5000, &result));
  consent_result_free(result);
  set(params, "artifact", artifact);
  CALL(consent_data_release(actor, params, &result));
  consent_result_free(result);
  set(params, "artifact", derived);
  CALL(consent_data_release(actor, params, &result));
  consent_result_free(result);
  consent_params_free(params);
  g_free(receipt);
  g_free(artifact);
  g_free(derived);
  puts("PASS approved 30-day scope rejects 90-day authorization/retry/receipt/artifact/derived reuse");
}

static void cache_and_policy(consent_params_t* registration_params) {
  consent_params_t* params = query("30");
  consent_result_t* result = NULL;
  char id[96];
  for (int i = 0; i < 2; ++i) {
    identify(params, id, sizeof(id));
    CALL(consent_request(actor, params, 5000, &result));
    CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
    if (i == 1) CHECK(!strcmp(consent_result_get(result, "source"), "CACHE"));
    consent_result_free(result);
  }
  consent_params_free(params);
  consent_params_t* lookup = begin("90");
  CALL(consent_cancel_request(ui, lookup, &result));
  consent_result_free(result);
  await_decision(CONSENT_DECISION_CANCELLED);
  consent_params_free(lookup);

  lookup = begin("45");
  identify(registration_params, id, sizeof(id));
  set(registration_params, "parameter.days.max", "180");
  CHECK(consent_register(actor, "demo.package", "demo.app", registration_params) == CONSENT_ERROR_CONFLICT);
  set(registration_params, "policy_version", "2");
  CALL(consent_register(actor, "demo.package", "demo.app", registration_params));
  await_decision(CONSENT_DECISION_INVALIDATED);
  CALL(consent_get_request_result(ui, lookup, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_INVALIDATED);
  consent_result_free(result);
  consent_params_free(lookup);
  params = query("30");
  CHECK(consent_check(actor, params, 5000, &result) == -ESTALE);
  set(params, "r0.policy_version", "2");
  CALL(consent_check(actor, params, 5000, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_CONSENT_REQUIRED);
  consent_result_free(result);
  consent_params_free(params);
  puts("PASS typed cache separates scope; schema update requires policy version and invalidates pending/grants");
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  installation = argv[1];
  snprintf(run_id, sizeof(run_id), "localization-%lld", (long long)g_get_monotonic_time());
  snprintf(definition, sizeof(definition), "typed.%s", run_id);
  CALL(consent_client_create(&actor));
  CALL(consent_client_create(&ui));
  consent_params_t* params = context();
  consent_result_t* result = NULL;
  CALL(consent_session_open(actor, params, &result));
  session = field(result, "session");
  generation = field(result, "generation");
  consent_result_free(result);
  consent_params_free(params);
  consent_params_t* definition_params = registration();
  set(definition_params, "parameter.days.type", "date");
  CHECK(consent_register(actor, "demo.package", "demo.app", definition_params) == CONSENT_ERROR_INVALID_PARAMETER);
  set(definition_params, "parameter.days.type", "integer");
  const char* english_body = "Read {days} days for {purpose} to {recipient}; keep result {retention_ms} ms.";
  set(definition_params, "message.en.body", "Read {days, number} days for {purpose} to {recipient}; keep result {retention_ms} ms.");
  CHECK(consent_register(actor, "demo.package", "demo.app", definition_params) == CONSENT_ERROR_INVALID_PARAMETER);
  set(definition_params, "message.en.body", english_body);
  CALL(consent_register(actor, "demo.package", "demo.app", definition_params));
  reject_invalid_values();
  typed_approval();
  check_scope("30", CONSENT_DECISION_ALLOWED);
  check_scope("90", CONSENT_DECISION_CONSENT_REQUIRED);
  bound_receipt_and_artifacts();
  cache_and_policy(definition_params);
  consent_params_free(definition_params);
  params = context();
  CALL(consent_session_close(actor, params, &result));
  consent_result_free(result);
  consent_params_free(params);
  CALL(consent_client_destroy(ui));
  CALL(consent_client_destroy(actor));
  g_free(session);
  g_free(generation);
  puts("PASS localization C API scenario");
  return 0;
}
