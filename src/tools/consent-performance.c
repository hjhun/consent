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
#include "performance_fixture.h"
#include <time.h>
#include <sys/resource.h>
#include <dirent.h>
#include <unistd.h>

#define SAMPLES 1000
#define WARMUP 100
static double elapsed[SAMPLES];
static double accepted[SAMPLES];
static int sources;
static int daemon_sources;
static char callback_source[32];
static char callback_cacheable[8];
static gint64 callback_ns;
static gboolean watchdog_expired;
static void perf_completed(int status, const consent_result_t* result,
                           void* data) {
  completed(status, result, data);
  const char* source = consent_result_get(result, "source");
  const char* cacheable = consent_result_get(result, "cacheable");
  snprintf(callback_source, sizeof(callback_source), "%s",
           source ? source : "");
  snprintf(callback_cacheable, sizeof(callback_cacheable), "%s",
           cacheable ? cacheable : "");
  struct timespec t;
  CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
  callback_ns = (gint64)t.tv_sec * 1000000000 + t.tv_nsec;
}
static gboolean expired(void* unused) {
  (void)unused;
  watchdog_expired = TRUE;
  return G_SOURCE_REMOVE;
}

static gint64 now_ns(void) {
  struct timespec t;
  CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
  return (gint64)t.tv_sec * 1000000000 + t.tv_nsec;
}
static int compare(const void* a, const void* b) {
  double x = *(const double*)a, y = *(const double*)b;
  return (x > y) - (x < y);
}
static void report(const char* api, double* values, double wall_s) {
  double total = 0;
  for (int i = 0; i < SAMPLES; ++i) total += values[i];
  qsort(values, SAMPLES, sizeof(*values), compare);
  printf(
      "METRIC api=%s n=%d warmup=%d acceptance_status=0 "
      "callback_or_sync_decision=ALLOWED "
      "p50_us=%.3f p95_us=%.3f p99_us=%.3f mean_us=%.3f "
      "interval_reciprocal_per_s=%.3f wall_throughput_per_s=%.3f "
      "cache_results=%d daemon_results=%d\n",
      api, SAMPLES, WARMUP, values[499], values[949], values[989],
      total / SAMPLES, 1000000.0 * SAMPLES / total, SAMPLES / wall_s, sources,
      daemon_sources);
}
static void measure(const char* api, int type) {
  consent_params_t* p = query("performance-scope", NULL, NULL);
  if (type == 1) {
    set(p, "mode", "AUTHORIZE");
    set(p, "step_id", "read");
  }
  if (type == 5) {
    const char* fields[][2] = {{"approval_version", "1"},
                               {"count", "1"},
                               {"generation", ""},
                               {"grant_mode", "ONCE"},
                               {"profile", "demo.profile"},
                               {"r0.definition", "demo.read"},
                               {"r0.feature_id", "performance-read"},
                               {"r0.feature_revision", "1"},
                               {"r0.holder", "scenario"},
                               {"r0.operation", "read"},
                               {"r0.policy_version", "1"},
                               {"r0.purpose", "answer"},
                               {"r0.recipient", ""},
                               {"r0.scope", "performance-scope"},
                               {"request_kind", "TASK"},
                               {"selection_id", "performance-selection"},
                               {"selection_revision", "1"},
                               {"session", ""},
                               {"subject", "demo.subject"}};
    GString* canonical = g_string_new("consent-selection-v1\n");
    for (unsigned f = 0; f < G_N_ELEMENTS(fields); ++f) {
      set(p, fields[f][0], fields[f][1]);
      g_string_append_printf(canonical, "%zu:%s%zu:%s", strlen(fields[f][0]),
                             fields[f][0], strlen(fields[f][1]), fields[f][1]);
    }
    char* digest = g_compute_checksum_for_string(
        G_CHECKSUM_SHA256, canonical->str, canonical->len);
    set(p, "selection_digest", digest);
    g_free(digest);
    g_string_free(canonical, TRUE);
  }
  sources = 0;
  daemon_sources = 0;
  gint64 wall_begin = 0;
  for (int i = -WARMUP; i < SAMPLES; ++i) {
    if (type == 1) {
      char id[100];
      snprintf(id, sizeof(id), "%s-authorize-%d", phase, i);
      set(p, "operation_id", id);
    }
    if (type == 2 || type == 3 || type == 5) {
      char id[100];
      snprintf(id, sizeof(id), "%s-request-%d-%d", phase, type, i);
      set(p, "operation_id", id);
      set(p, "client_request_id", id);
    }
    consent_result_t* r = NULL;
    GSource* watchdog = NULL;
    if (type >= 3) {
      watchdog_expired = FALSE;
      watchdog = g_timeout_source_new(5000);
      g_source_set_callback(watchdog, expired, NULL, NULL);
      g_source_attach(watchdog, NULL);
    }
    if (i == 0) wall_begin = now_ns();
    gint64 begin = now_ns();
    int status;
    if (type >= 3) {
      consent_async_id_t id;
      callbacks = 0;
      returned = 0;
      status =
          type == 4
              ? consent_check_async(client, p, perf_completed, NULL, &id)
              : consent_request_async(client, p, perf_completed, NULL, &id);
      returned = 1;
      gint64 end = now_ns();
      while (!callbacks && !watchdog_expired)
        g_main_context_iteration(NULL, TRUE);
      CHECK(!watchdog_expired);
      g_source_destroy(watchdog);
      g_source_unref(watchdog);
      CHECK(callbacks == 1 && callback_status == 0);
      CHECK(callback_decision == CONSENT_DECISION_ALLOWED);
      if (i >= 0) {
        accepted[i] = (end - begin) / 1000.0;
        if (!strcmp(callback_source, "CACHE")) ++sources;
        if (!strcmp(callback_source, "DAEMON")) ++daemon_sources;
      }
      if (type == 5) CHECK(!strcmp(callback_cacheable, "0"));
    } else {
      status = type == 2 ? consent_request(client, p, 5000, &r)
                         : consent_check(client, p, 5000, &r);
      if (status) fprintf(stderr, "FAIL API %s status=%d\n", api, status);
      CHECK(status == 0);
      CHECK(consent_result_get_decision(r) == CONSENT_DECISION_ALLOWED);
      const char* source = consent_result_get(r, "source");
      if (i >= 0 && source && !strcmp(source, "CACHE")) ++sources;
      if (i >= 0 && source && !strcmp(source, "DAEMON")) ++daemon_sources;
    }
    gint64 end = now_ns();
    CHECK(status == 0);
    if (i >= 0) elapsed[i] = ((type >= 3 ? callback_ns : end) - begin) / 1000.0;
    consent_result_free(r);
  }
  double wall_s = (now_ns() - wall_begin) / 1000000000.0;
  if (type == 2 || type == 3) CHECK(sources > 0);
  if (type == 4 || type == 5) CHECK(sources == 0 && daemon_sources == SAMPLES);
  report(api, elapsed, wall_s);
  if (type >= 3)
    report(type == 4   ? "CHECK_ASYNC_ACCEPT"
           : type == 5 ? "D16_REQUEST_ASYNC_ACCEPT"
                       : "LEGACY_REQUEST_ASYNC_ACCEPT",
           accepted, wall_s);
  consent_params_free(p);
}
static unsigned long context_switches(void) {
  unsigned long sum = 0;
  DIR* directory = opendir("/proc/self/task");
  CHECK(directory);
  struct dirent* e;
  while ((e = readdir(directory))) {
    if (e->d_name[0] == '.') continue;
    char path[512];
    snprintf(path, sizeof(path), "/proc/self/task/%s/status", e->d_name);
    FILE* file = fopen(path, "r");
    CHECK(file);
    char line[256];
    unsigned long value;
    while (fgets(line, sizeof(line), file)) {
      if (sscanf(line, "voluntary_ctxt_switches: %lu", &value) == 1 ||
          sscanf(line, "nonvoluntary_ctxt_switches: %lu", &value) == 1)
        sum += value;
    }
    fclose(file);
  }
  closedir(directory);
  return sum;
}
int main(int argc, char** argv) {
  CHECK(argc == 2);
  snprintf(phase, sizeof(phase), "perf-%" G_GINT64_FORMAT,
           g_get_monotonic_time());
  printf(
      "CONDITION actor_pid=%ld policy=PERSISTENT target=x86_64 "
      "single_persistent_handle=1 serialized=1\n",
      (long)getpid());
  CHECK(consent_client_create(&client) == 0);
  CHECK(consent_client_create(&ui) == 0);
  define("demo.app", "demo.read", phase, argv[1]);
  approve("performance-scope", "PERSISTENT", NULL, NULL);
  struct rusage before, after;
  CHECK(getrusage(RUSAGE_SELF, &before) == 0);
  measure("QUERY_SYNC", 0);
  measure("AUTHORIZE_SYNC_DISTINCT", 1);
  measure("REQUEST_SYNC_LEGACY_CACHE", 2);
  measure("REQUEST_ASYNC_COMPLETION_LEGACY_CACHE", 3);
  measure("CHECK_ASYNC_DAEMON_COMPLETION", 4);
  measure("D16_REQUEST_ASYNC_DAEMON_COMPLETION", 5);
  CHECK(getrusage(RUSAGE_SELF, &after) == 0);
  double cpu = (after.ru_utime.tv_sec - before.ru_utime.tv_sec) +
               (after.ru_utime.tv_usec - before.ru_utime.tv_usec) / 1000000.0 +
               (after.ru_stime.tv_sec - before.ru_stime.tv_sec) +
               (after.ru_stime.tv_usec - before.ru_stime.tv_usec) / 1000000.0;
  unsigned long switches = context_switches();
  struct timespec c0, c1;
  CHECK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c0) == 0);
  g_usleep(2000000);
  CHECK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c1) == 0);
  unsigned long idle = context_switches() - switches;
  printf(
      "RESOURCE cpu_s=%.6f maxrss_kib=%ld idle_seconds=2 "
      "idle_process_context_switches=%lu idle_cpu_ns=%lld "
      "wakeups_metric=context_switch_proxy\n",
      cpu, after.ru_maxrss, idle,
      (long long)(c1.tv_sec - c0.tv_sec) * 1000000000 + c1.tv_nsec -
          c0.tv_nsec);
  CALL(consent_client_destroy(ui));
  CALL(consent_client_destroy(client));
  puts("PASS persistent-handle performance");
  return 0;
}
