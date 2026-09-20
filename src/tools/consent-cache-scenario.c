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

#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
  fprintf(stderr, "FAIL line=%d expression=%s\n", __LINE__, #expression); exit(1); \
} } while (0)
#define CALL(expression) do { int status_ = (expression); if (status_) { \
  fprintf(stderr, "FAIL line=%d expression=%s status=%d (%s)\n", __LINE__, \
      #expression, status_, consent_error_string(status_)); exit(1); \
} } while (0)

static consent_client_h actor;
static consent_client_h controller;
static const char* installation;
static char run_id[64];
static char definition[96];
static unsigned sequence;
static char cache_step[96];
static gint64 warmed_at;
static gint64 warmed_deadline;

struct completion {
  int returned;
  int count;
  int status;
  consent_decision_e decision;
  int from_cache;
};

static void set(consent_params_t* p, const char* key, const char* value) {
  CALL(consent_params_set(p, key, value));
}

static consent_params_t* params(void) {
  consent_params_t* p = NULL;
  CALL(consent_params_create(&p));
  set(p, "subject", "demo.subject");
  set(p, "profile", "demo.profile");
  return p;
}

static char* field(const consent_result_t* result, const char* key) {
  const char* value = consent_result_get(result, key);
  CHECK(value);
  return g_strdup(value);
}

static void identify(consent_params_t* p, char* id, size_t size) {
  snprintf(id, size, "%s-%u", run_id, ++sequence);
  set(p, "client_request_id", id);
  set(p, "operation_id", id);
}

static consent_params_t* query(const char* scope, const char* session,
    const char* generation) {
  consent_params_t* p = params();
  CALL(consent_params_add_requirement(p, definition, "read", scope, "answer", ""));
  set(p, "r0.holder", "cache-scenario");
  if (cache_step[0])
    set(p, "step_id", cache_step);
  if (session) {
    set(p, "session", session);
    set(p, "generation", generation);
  }
  return p;
}

static void complete(int status, const consent_result_t* result, void* data) {
  struct completion* completion = data;
  CHECK(completion->returned);
  ++completion->count;
  completion->status = status;
  completion->decision = consent_result_get_decision(result);
  const char* source = consent_result_get(result, "source");
  completion->from_cache = source && !strcmp(source, "CACHE");
}

static void dispatch(struct completion* completion) {
  gint64 deadline = g_get_monotonic_time() + 5000000;
  while (!completion->count && g_get_monotonic_time() < deadline) {
    while (g_main_context_iteration(NULL, FALSE)) {}
    g_usleep(1000);
  }
  CHECK(completion->count == 1);
}

static consent_params_t* pending(consent_params_t* request,
    struct completion* completion) {
  char id[96];
  identify(request, id, sizeof(id));
  consent_async_id_t operation;
  CALL(consent_request_async(actor, request, complete, completion, &operation));
  completion->returned = 1;
  consent_params_t* lookup = params();
  set(lookup, "client_request_id", id);
  consent_result_t* result = NULL;
  int status = CONSENT_ERROR_NOT_FOUND;
  for (int attempt = 0; attempt < 100 && status == CONSENT_ERROR_NOT_FOUND; ++attempt) {
    g_usleep(10000);
    status = consent_get_request_result(controller, lookup, &result);
  }
  CHECK(status == 0);
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_PENDING);
  set(lookup, "request_id", consent_result_get(result, "request_id"));
  consent_result_free(result);
  return lookup;
}

static void define(unsigned version) {
  consent_params_t* p = params();
  char number[32];
  char operation[96];
  identify(p, operation, sizeof(operation));
  snprintf(number, sizeof(number), "%u", version);
  set(p, "definition", definition);
  set(p, "expected_generation", installation);
  set(p, "enforcer", "cache-scenario");
  set(p, "policy_version", number);
  set(p, "text_revision", number);
  set(p, "level", "1");
  set(p, "modes", "PERSISTENT,SESSION");
  set(p, "retention_ms", "60000");
  set(p, "default_locale", "en");
  set(p, "message.en.title", "Consent cache integration test");
  set(p, "message.en.body", "Allow the displayed test scope?");
  CALL(consent_register(controller, "demo.package", "demo.app", p));
  consent_params_free(p);
}

static void approve(const char* scope, const char* mode, const char* session,
    const char* generation) {
  consent_params_t* request = query(scope, session, generation);
  struct completion completion = {0};
  consent_params_t* lookup = pending(request, &completion);
  set(lookup, "locale", "en");
  consent_result_t* result = NULL;
  CALL(consent_get_prompt(controller, lookup, &result));
  set(lookup, "prompt_token", consent_result_get(result, "prompt_token"));
  consent_result_free(result);
  set(lookup, "decision", "ALLOWED");
  set(lookup, "grant_mode", mode);
  CALL(consent_respond(controller, lookup, &result));
  consent_result_free(result);
  dispatch(&completion);
  CHECK(completion.status == 0 && completion.decision == CONSENT_DECISION_ALLOWED);
  consent_params_free(lookup);
  consent_params_free(request);
}

/* Authoritative checks are deliberately performed only AFTER the first
 * post-change request proves event-driven invalidation of an unexpired cache. */
static void check(const char* scope, const char* session, const char* generation,
    int expected_status, consent_decision_e expected) {
  consent_params_t* p = query(scope, session, generation);
  consent_result_t* result = NULL;
  int status = consent_check(actor, p, 5000, &result);
  CHECK(status == expected_status);
  if (!status) {
    CHECK(consent_result_get_decision(result) == expected);
    CHECK(strcmp(consent_result_get(result, "source"), "CACHE"));
  }
  consent_result_free(result);
  consent_params_free(p);
}

static void blocked_request(const char* scope, const char* session,
    const char* generation) {
  consent_params_t* p = query(scope, session, generation);
  struct completion completion = {0};
  consent_params_t* lookup = pending(p, &completion);
  consent_result_t* result = NULL;
  CALL(consent_cancel_request(controller, lookup, &result));
  consent_result_free(result);
  dispatch(&completion);
  CHECK(completion.status == 0 && completion.decision == CONSENT_DECISION_CANCELLED);
  CHECK(!completion.from_cache);
  consent_params_free(lookup);
  consent_params_free(p);
}

static char* warm(const char* scope, const char* session, const char* generation) {
  snprintf(cache_step, sizeof(cache_step), "%s-warm-%u", run_id, ++sequence);
  consent_params_t* p = query(scope, session, generation);
  char id[96];
  identify(p, id, sizeof(id));
  consent_result_t* result = NULL;
  warmed_at = g_get_monotonic_time();
  CALL(consent_request(actor, p, 5000, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  CHECK(!strcmp(consent_result_get(result, "cacheable"), "1"));
  CHECK(!strcmp(consent_result_get(result, "source"), "DAEMON"));
  const char* ttl = consent_result_get(result, "cache_ttl_ms");
  CHECK(ttl);
  warmed_deadline = warmed_at + g_ascii_strtoll(ttl, NULL, 10) * 1000;
  char* epoch = field(result, "epoch");
  consent_result_free(result);
  identify(p, id, sizeof(id));
  CALL(consent_request(actor, p, 5000, &result));
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_ALLOWED);
  CHECK(!strcmp(consent_result_get(result, "source"), "CACHE"));
  CHECK(!consent_result_get(result, "request_id"));
  consent_result_free(result);
  struct completion completion = {0};
  identify(p, id, sizeof(id));
  consent_async_id_t operation;
  CALL(consent_request_async(actor, p, complete, &completion, &operation));
  CHECK(completion.count == 0);
  completion.returned = 1;
  dispatch(&completion);
  CHECK(completion.status == 0 && completion.decision == CONSENT_DECISION_ALLOWED);
  CHECK(completion.from_cache);
  consent_params_free(p);
  return epoch;
}

static void before_invalidation_probe(void) {
  /* Cross-connection publication precedes the controller reply, but the actor
   * I/O thread still needs scheduling time to consume its unsolicited event. */
  g_usleep(20000);
  gint64 now = g_get_monotonic_time();
  CHECK(now < warmed_deadline && now - warmed_at < 500000);
  printf("cache invalidation probe before original expiry: elapsed_us=%lld\n",
      (long long)(now - warmed_at));
}

static void invalidated_request(const char* scope, const char* session,
    const char* generation, int expected_status, consent_decision_e expected) {
  before_invalidation_probe();
  consent_params_t* p = query(scope, session, generation);
  char id[96];
  identify(p, id, sizeof(id));
  consent_result_t* result = NULL;
  int status = consent_request(actor, p, 5000, &result);
  if (status != expected_status)
    fprintf(stderr, "request status=%d expected=%d scope=%s\n", status, expected_status, scope);
  CHECK(status == expected_status);
  if (!status) {
    CHECK(consent_result_get_decision(result) == expected);
    const char* source = consent_result_get(result, "source");
    CHECK(source && strcmp(source, "CACHE"));
  }
  consent_result_free(result);
  consent_params_free(p);
}

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s INSTALLATION_GENERATION\n", argv[0]);
    return 2;
  }
  installation = argv[1];
  snprintf(run_id, sizeof(run_id), "cache-%lld", (long long)g_get_real_time());
  snprintf(definition, sizeof(definition), "cache.read.%s", run_id);
  CALL(consent_client_create(&actor));
  CALL(consent_client_create(&controller));
  define(1);
  approve("persistent", "PERSISTENT", NULL, NULL);
  g_free(warm("persistent", NULL, NULL));
  consent_params_t* p = params();
  set(p, "definition", definition);
  consent_result_t* result = NULL;
  CALL(consent_revoke(controller, p, &result));
  consent_result_free(result);
  consent_params_free(p);
  before_invalidation_probe();
  blocked_request("persistent", NULL, NULL);
  check("persistent", NULL, NULL, 0, CONSENT_DECISION_CONSENT_REQUIRED);
  puts("PASS live persistent cache invalidated by revoke");

  approve("persistent", "PERSISTENT", NULL, NULL);
  g_free(warm("persistent", NULL, NULL));
  define(2);
  before_invalidation_probe();
  blocked_request("persistent", NULL, NULL);
  check("persistent", NULL, NULL, 0, CONSENT_DECISION_CONSENT_REQUIRED);
  puts("PASS live persistent cache invalidated by policy update");

  approve("persistent", "PERSISTENT", NULL, NULL);
  p = params();
  set(p, "lifecycle", "RESUMABLE_CONVERSATION");
  CALL(consent_session_open(controller, p, &result));
  char* session = field(result, "session");
  char* generation = field(result, "generation");
  consent_result_free(result);
  set(p, "session", session);
  set(p, "generation", generation);
  approve("session", "SESSION", session, generation);
  g_free(warm("session", session, generation));
  CALL(consent_session_suspend(controller, p, &result));
  char* suspended_generation = field(result, "generation");
  consent_result_free(result);
  invalidated_request("session", session, generation, CONSENT_ERROR_SESSION_INACTIVE,
      CONSENT_DECISION_UNKNOWN);
  set(p, "generation", suspended_generation);
  CALL(consent_session_close(controller, p, &result));
  consent_result_free(result);
  consent_params_free(p);
  g_free(session);
  g_free(generation);
  g_free(suspended_generation);
  puts("PASS live SESSION cache invalidated by suspend; suspended session closed");

  /* An independent ACTIVE session is warmed immediately before close. The
   * earlier suspend must not supply the invalidation being tested here. */
  p = params();
  set(p, "lifecycle", "RESUMABLE_CONVERSATION");
  CALL(consent_session_open(controller, p, &result));
  session = field(result, "session");
  generation = field(result, "generation");
  consent_result_free(result);
  set(p, "session", session);
  set(p, "generation", generation);
  approve("session-close", "SESSION", session, generation);
  g_free(warm("session-close", session, generation));
  CALL(consent_session_close(controller, p, &result));
  CHECK(!strcmp(consent_result_get(result, "state"), "CLOSED"));
  consent_result_free(result);
  invalidated_request("session-close", session, generation,
      CONSENT_ERROR_SESSION_CLOSED, CONSENT_DECISION_UNKNOWN);
  consent_params_free(p);
  g_free(session);
  g_free(generation);
  puts("PASS live SESSION cache independently invalidated by ACTIVE session close");

  g_free(warm("persistent", NULL, NULL));
  p = params();
  char operation[96];
  identify(p, operation, sizeof(operation));
  set(p, "expected_generation", installation);
  CALL(consent_unregister(controller, "demo.package", p));
  consent_params_free(p);
  invalidated_request("persistent", NULL, NULL, 0, CONSENT_DECISION_DENIED);
  puts("PASS live persistent cache invalidated by package removal");

#ifdef CONSENT_TEST_DB_PATH
  define(3);
  approve("persistent", "PERSISTENT", NULL, NULL);
  char* previous_epoch = warm("persistent", NULL, NULL);
  struct stat database;
  struct passwd* service = getpwnam("security_fw");
  CHECK(service && service->pw_uid != 0);
  CHECK(lstat(CONSENT_TEST_DB_PATH, &database) == 0);
  CHECK(S_ISREG(database.st_mode) && database.st_nlink == 1 &&
      database.st_uid == service->pw_uid && database.st_gid == service->pw_gid &&
      (database.st_mode & 0777) == 0600);
  CHECK(unlink(CONSENT_TEST_DB_PATH) == 0);
  p = query("persistent", NULL, NULL);
  int status = CONSENT_ERROR_STORAGE;
  for (int attempt = 0; attempt < 100 && status == CONSENT_ERROR_STORAGE; ++attempt) {
    status = consent_check(controller, p, 5000, &result);
    if (status == CONSENT_ERROR_STORAGE)
      g_usleep(10000);
  }
  CHECK(status == 0);
  CHECK(consent_result_get_decision(result) == CONSENT_DECISION_CONSENT_REQUIRED);
  CHECK(strcmp(consent_result_get(result, "epoch"), previous_epoch));
  consent_result_free(result);
  consent_params_free(p);
  g_free(previous_epoch);
  before_invalidation_probe();
  blocked_request("persistent", NULL, NULL);
  approve("persistent", "PERSISTENT", NULL, NULL);
  g_free(warm("persistent", NULL, NULL));
  puts("PASS live cache invalidated after forced DB deletion; definitions restored; fresh approval works");
#else
  puts("SKIP DB deletion: available only in separately compiled isolated executable");
#endif
  CALL(consent_client_destroy(controller));
  CALL(consent_client_destroy(actor));
  puts("PASS cache scenario: both client handles remained live throughout");
  return 0;
}
