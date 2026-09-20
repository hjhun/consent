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
  if (!strcmp(mode, "TIMED")) set(lookup, "duration_ms", "1000");
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

struct race_gate {
  GMutex mutex;
  GCond condition;
  int ready;
  int go;
};

struct race_job {
  struct race_gate* gate;
  consent_client_h handle;
  consent_params_t* request;
  int kind;  /* 0 authorize, 1 cancel, 2 respond */
  int status;
  consent_result_t* result;
};

static gpointer race_worker(gpointer value) {
  struct race_job* job = value;
  g_mutex_lock(&job->gate->mutex);
  ++job->gate->ready;
  g_cond_broadcast(&job->gate->condition);
  while (!job->gate->go)
    g_cond_wait(&job->gate->condition, &job->gate->mutex);
  g_mutex_unlock(&job->gate->mutex);
  if (job->kind == 0)
    job->status = consent_check(job->handle, job->request, 5000, &job->result);
  else if (job->kind == 1)
    job->status = consent_cancel_request(job->handle, job->request, &job->result);
  else
    job->status = consent_respond(job->handle, job->request, &job->result);
  return NULL;
}

static void run_race(struct race_job jobs[2]) {
  struct race_gate gate = {0};
  g_mutex_init(&gate.mutex);
  g_cond_init(&gate.condition);
  jobs[0].gate = jobs[1].gate = &gate;
  GThread* first = g_thread_new("contender-a", race_worker, &jobs[0]);
  GThread* second = g_thread_new("contender-b", race_worker, &jobs[1]);
  g_mutex_lock(&gate.mutex);
  while (gate.ready != 2)
    g_cond_wait(&gate.condition, &gate.mutex);
  gate.go = 1;
  g_cond_broadcast(&gate.condition);
  g_mutex_unlock(&gate.mutex);
  g_thread_join(first);
  g_thread_join(second);
  g_cond_clear(&gate.condition);
  g_mutex_clear(&gate.mutex);
}

static void await_decision(consent_decision_e decision) {
  gint64 deadline = g_get_monotonic_time() + 5000000;
  while (!callbacks && g_get_monotonic_time() < deadline) {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000);
  }
  CHECK(callbacks == 1 && callback_status == 0 && callback_decision == decision);
}

static consent_params_t* pending_request(consent_params_t* request,
    const char* deadline, int mixed) {
  char id[128];
  snprintf(id, sizeof(id), "%s-pending-%u", phase, ++serial);
  set(request, "client_request_id", id);
  set(request, "operation_id", id);
  set(request, "deadline_ms", deadline);
  callbacks = returned = 0;
  consent_async_id_t local;
  CALL(consent_request_async(client, request, completed, NULL, &local));
  returned = 1;
  consent_params_free(request);
  consent_params_t* lookup = params();
  set(lookup, "client_request_id", id);
  consent_result_t* result = NULL;
  int status = CONSENT_ERROR_NOT_FOUND;
  for (int attempt = 0; attempt < 100 && status == CONSENT_ERROR_NOT_FOUND; ++attempt) {
    g_usleep(10000);
    status = consent_get_request_result(ui, lookup, &result);
  }
  CHECK(status == 0 && consent_result_get_decision(result) == CONSENT_DECISION_PENDING);
  if (mixed) {
    CHECK(!strcmp(consent_result_get(result, "r0.decision"), "ALLOWED"));
    CHECK(!strcmp(consent_result_get(result, "r1.decision"), "CONSENT_REQUIRED"));
  }
  set(lookup, "request_id", consent_result_get(result, "request_id"));
  consent_result_free(result);
  set(lookup, "locale", "en");
  CALL(consent_get_prompt(ui, lookup, &result));
  set(lookup, "prompt_token", consent_result_get(result, "prompt_token"));
  set(lookup, "decision", "ALLOWED");
  set(lookup, "grant_mode", "ONCE");
  consent_result_free(result);
  return lookup;
}

static consent_params_t* pending(const char* scope, const char* deadline) {
  return pending_request(query(scope, NULL, NULL), deadline, 0);
}

static void authorize_definition(const char* definition, const char* scope,
    consent_decision_e expected) {
  consent_params_t* p = query(scope, NULL, NULL);
  set(p, "r0.definition", definition);
  set(p, "mode", "AUTHORIZE");
  char operation[128];
  snprintf(operation, sizeof(operation), "%s-authorize-%u", phase, ++serial);
  set(p, "operation_id", operation);
  set(p, "step_id", "read");
  consent_result_t* result = NULL;
  CALL(consent_check(ui, p, 5000, &result));
  CHECK(consent_result_get_decision(result) == expected);
  consent_result_free(result);
  consent_params_free(p);
}

static void ui_reevaluate(const char* install_generation) {
  char operation[128];
  snprintf(operation, sizeof(operation), "%s-define-a", phase);
  define("demo.app", "demo.read", operation, install_generation);
  snprintf(operation, sizeof(operation), "%s-define-b", phase);
  define("demo.app2", "demo.other", operation, install_generation);
  const char* cases[] = {"revoke", "timed-expiry", "once-consumed", "normal", "denied"};
  for (int test = 0; test < 5; ++test) {
    char scope[128];
    snprintf(scope, sizeof(scope), "%s-%s", phase, cases[test]);
    approve(scope, test == 1 ? "TIMED" : test == 2 || test == 3 ? "ONCE" :
        "PERSISTENT", NULL, NULL);
    consent_params_t* request = query(scope, NULL, NULL);
    CALL(consent_params_add_requirement(request, "demo.other", "read", scope,
        "answer", ""));
    set(request, "r1.holder", "scenario");
    consent_params_t* lookup = pending_request(request, "5000", 1);
    consent_result_t* result = NULL;
    if (test == 0 || test == 4) {
      consent_params_t* revoke = params();
      set(revoke, "definition", "demo.read");
      CALL(consent_revoke(ui, revoke, &result));
      consent_result_free(result);
      consent_params_free(revoke);
    } else if (test == 1) {
      g_usleep(1100000);
    } else if (test == 2) {
      authorize_definition("demo.read", scope, CONSENT_DECISION_ALLOWED);
    }
    if (test == 4) set(lookup, "decision", "DENIED");
    consent_decision_e expected = test == 3 ? CONSENT_DECISION_ALLOWED :
        test == 4 ? CONSENT_DECISION_DENIED : CONSENT_DECISION_INVALIDATED;
    CALL(consent_respond(ui, lookup, &result));
    CHECK(consent_result_get_decision(result) == expected);
    CHECK(!strcmp(consent_result_get(result, "r0.decision"),
        test == 3 ? "ALLOWED" : "CONSENT_REQUIRED"));
    CHECK(!strcmp(consent_result_get(result, "r1.decision"),
        test == 4 ? "DENIED" : "ALLOWED"));
    if (expected == CONSENT_DECISION_INVALIDATED) {
      CHECK(consent_result_get(result, "reason") != NULL);
      CHECK(*consent_result_get(result, "reason") != '\0');
    }
    consent_result_free(result);
    await_decision(expected);
    CALL(consent_get_request_result(ui, lookup, &result));
    CHECK(consent_result_get_decision(result) == expected);
    consent_result_free(result);
    CHECK(consent_get_prompt(ui, lookup, &result) < 0);
    CHECK(consent_respond(ui, lookup, &result) < 0);
    if (test != 4) {
      authorize_definition("demo.other", scope, CONSENT_DECISION_ALLOWED);
      authorize_definition("demo.other", scope, CONSENT_DECISION_CONSENT_REQUIRED);
    }
    if (test == 3) {
      authorize_definition("demo.read", scope, CONSENT_DECISION_ALLOWED);
      authorize_definition("demo.read", scope, CONSENT_DECISION_CONSENT_REQUIRED);
      CALL(consent_get_request_result(ui, lookup, &result));
      CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
      consent_result_free(result);
    }
    for (int i = 0; i < 20; ++i) {
      while (g_main_context_iteration(NULL, FALSE)) {}
      g_usleep(1000);
    }
    CHECK(callbacks == 1);
    consent_params_free(lookup);
    printf("PASS UI current AND: %s, one final callback, no repeat prompt\n", cases[test]);
  }
}

static void races(void) {
  approve("once-race", "ONCE", NULL, NULL);
  struct race_job jobs[2] = {{0}, {0}};
  for (int i = 0; i < 2; ++i) {
    jobs[i].handle = i ? ui : client;
    jobs[i].request = query("once-race", NULL, NULL);
    set(jobs[i].request, "mode", "AUTHORIZE");
    char operation[96];
    snprintf(operation, sizeof(operation), "%s-once-%d", phase, i);
    set(jobs[i].request, "operation_id", operation);
    set(jobs[i].request, "step_id", "read");
  }
  run_race(jobs);
  CHECK(jobs[0].status == 0 && jobs[1].status == 0);
  int allowed = 0;
  for (int i = 0; i < 2; ++i) {
    consent_decision_e decision = consent_result_get_decision(jobs[i].result);
    if (decision == CONSENT_DECISION_ALLOWED) {
      ++allowed;
      char* receipt = field(jobs[i].result, "receipt");
      consent_result_t* retry = NULL;
      CALL(consent_check(jobs[i].handle, jobs[i].request, 5000, &retry));
      CHECK(consent_result_get_decision(retry) == CONSENT_DECISION_ALLOWED);
      CHECK(!strcmp(receipt, consent_result_get(retry, "receipt")));
      CHECK(!strcmp("1", consent_result_get(retry, "retry")));
      g_free(receipt);
      consent_result_free(retry);
    } else {
      CHECK(decision == CONSENT_DECISION_CONSENT_REQUIRED);
    }
    consent_result_free(jobs[i].result);
    consent_params_free(jobs[i].request);
  }
  CHECK(allowed == 1);
  puts("PASS two independent connections: one ONCE winner, stable receipt retry");

  consent_params_t* lookup = pending("cancel-first", "5000");
  consent_result_t* result = NULL;
  CALL(consent_cancel_request(ui, lookup, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_CANCELLED);
  consent_result_free(result);
  CHECK(consent_respond(ui, lookup, &result) < 0);
  await_decision(CONSENT_DECISION_CANCELLED);
  consent_params_free(lookup);

  lookup = pending("respond-first", "5000");
  CALL(consent_respond(ui, lookup, &result));
  consent_result_free(result);
  CALL(consent_cancel_request(ui, lookup, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  consent_result_free(result);
  await_decision(CONSENT_DECISION_ALLOWED);
  consent_params_free(lookup);

  lookup = pending("cancel-response-race", "5000");
  jobs[0] = (struct race_job){ .handle = client, .request = lookup, .kind = 1 };
  jobs[1] = (struct race_job){ .handle = ui, .request = lookup, .kind = 2 };
  run_race(jobs);
  CHECK(jobs[0].status == 0);
  consent_decision_e final = consent_result_get_decision(jobs[0].result);
  CHECK((final == CONSENT_DECISION_CANCELLED && jobs[1].status < 0) ||
      (final == CONSENT_DECISION_ALLOWED && jobs[1].status == 0));
  consent_result_free(jobs[0].result);
  consent_result_free(jobs[1].result);
  await_decision(final);
  consent_params_free(lookup);

  lookup = pending("deadline-first", "100");
  g_usleep(150000);
  CALL(consent_get_request_result(ui, lookup, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_EXPIRED);
  consent_result_free(result);
  CHECK(consent_respond(ui, lookup, &result) < 0);
  await_decision(CONSENT_DECISION_EXPIRED);
  consent_params_free(lookup);
  check("cancel-first", CONSENT_DECISION_CONSENT_REQUIRED);
  check("deadline-first", CONSENT_DECISION_CONSENT_REQUIRED);
  puts("PASS remote cancellation/respond race/deadline with exactly one final callback");
}

static void holder_seed(const char* install_generation) {
  define("demo.app", "demo.read", phase, install_generation);
  consent_params_t* p = params();
  consent_result_t* result = NULL;
  CALL(consent_session_open(client, p, &result));
  char* session = field(result, "session");
  char* generation = field(result, "generation");
  consent_result_free(result);
  consent_params_free(p);
  approve("holder-restart-scope", "SESSION", session, generation);
  p = query("holder-restart-scope", session, generation);
  set(p, "mode", "AUTHORIZE");
  char operation[96];
  snprintf(operation, sizeof(operation), "%s-acquire", phase);
  set(p, "operation_id", operation);
  set(p, "step_id", "read");
  CALL(consent_check(client, p, 5000, &result));
  char* receipt = field(result, "receipt");
  consent_result_free(result);
  consent_params_free(p);
  p = params();
  set(p, "session", session);
  set(p, "generation", generation);
  set(p, "receipt", receipt);
  set(p, "scope", "holder-restart-scope");
  set(p, "purpose", "answer");
  CALL(consent_data_register(client, p, &result));
  consent_result_free(result);
  CALL(consent_session_close(client, p, &result));
  CHECK(!strcmp(consent_result_get(result, "state"), "CLOSING"));
  consent_result_free(result);
  consent_params_free(p);
  g_free(session);
  g_free(generation);
  g_free(receipt);
  puts("PASS holder seed: exited with cleanup pending, no artifact ID handoff");
}

static void holder_reconcile(void) {
  consent_params_t* p = params();
  set(p, "reconcile", "1");
  consent_result_t* result = NULL;
  CALL(consent_cleanup_get_pending(client, p, &result));
  CHECK(!strcmp(consent_result_get(result, "count"), "1"));
  char* artifact = field(result, "a0.artifact");
  consent_result_free(result);
  set(p, "artifact", artifact);
  set(p, "reconcile", "0");
  CHECK(consent_data_release(client, p, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  set(p, "reconcile", "1");
  CHECK(consent_data_register(client, p, &result) == CONSENT_ERROR_PERMISSION_DENIED);
  set(p, "success", "0");
  CALL(consent_data_release(client, p, &result));
  CHECK(!strcmp(consent_result_get(result, "state"), "CLEANUP_FAILED"));
  consent_result_free(result);
  CALL(consent_cleanup_get_pending(client, p, &result));
  CHECK(!strcmp(consent_result_get(result, "count"), "1"));
  CHECK(!strcmp(consent_result_get(result, "a0.state"), "CLEANUP_FAILED"));
  consent_result_free(result);
  set(p, "success", "1");
  CALL(consent_data_release(client, p, &result));
  CHECK(!strcmp(consent_result_get(result, "state"), "DELETED"));
  consent_result_free(result);
  CALL(consent_data_release(client, p, &result));
  consent_result_free(result);
  CALL(consent_cleanup_get_pending(client, p, &result));
  CHECK(!strcmp(consent_result_get(result, "count"), "0"));
  consent_result_free(result);
  consent_params_free(p);
  g_free(artifact);
  puts("PASS new holder process: pending discovery, blocked reuse, failed cleanup/retry/ACK");
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s basic|persistent|recovered|races|holder-seed|holder-reconcile|reinstalled|ui-reevaluate GENERATION\n", argv[0]);
    return 2;
  }
  snprintf(phase, sizeof(phase), "%s-%" G_GINT64_FORMAT, argv[1], g_get_monotonic_time());
  CHECK(consent_client_create(&client) == 0);
  CHECK(consent_client_create(&ui) == 0);
  if (!strcmp(argv[1], "reinstalled")) {
    char operation[96];
    snprintf(operation, sizeof(operation), "%s-app1", phase);
    define("demo.app", "demo.read", operation, argv[2]);
    snprintf(operation, sizeof(operation), "%s-app2", phase);
    define("demo.app2", "demo.other", operation, argv[2]);
    check("persistent-scope", CONSENT_DECISION_CONSENT_REQUIRED);
    CALL(consent_client_destroy(ui));
    CALL(consent_client_destroy(client));
    puts("PASS reinstall: two apps registered in new generation without old grants");
    return 0;
  }
  if (!strcmp(argv[1], "persistent") || !strcmp(argv[1], "recovered")) {
    check("persistent-scope", !strcmp(argv[1], "persistent") ?
        CONSENT_DECISION_ALLOWED : CONSENT_DECISION_CONSENT_REQUIRED);
    printf("PASS %s\n", argv[1]);
    consent_client_destroy(ui);
    consent_client_destroy(client);
    return 0;
  }
  if (!strcmp(argv[1], "ui-reevaluate")) {
    ui_reevaluate(argv[2]);
    CALL(consent_client_destroy(ui));
    CALL(consent_client_destroy(client));
    return 0;
  }
  if (!strcmp(argv[1], "races")) {
    define("demo.app", "demo.read", phase, argv[2]);
    races();
    CALL(consent_client_destroy(ui));
    CALL(consent_client_destroy(client));
    return 0;
  }
  if (!strcmp(argv[1], "holder-seed") || !strcmp(argv[1], "holder-reconcile")) {
    if (!strcmp(argv[1], "holder-seed")) holder_seed(argv[2]);
    else holder_reconcile();
    CALL(consent_client_destroy(ui));
    CALL(consent_client_destroy(client));
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
