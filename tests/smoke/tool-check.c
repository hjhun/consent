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
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include "tool-json.h"
#include "tool-process.h"
#include "tool-gate.h"

#define TOOL_ROOT "/tmp/consent-smoke"
#define TOOL_PROVIDER SMOKE_TOOL_PACKAGE "/bin/consent-smoke-tool"

static int context_lookup(const char* record, const char* id) {
  char* response = tool_context_read(record, id);
  if (!response) return 0;
  printf("CONTEXT_RESPONSE %s\n", response);
  int valid = tool_validate_response(response, id);
  g_free(response);
  return valid;
}

int main(int argc, char** argv) {
  if (argc != 2 || !smoke_tool_record(argv[1])) {
    fprintf(stderr, "Usage: %s KNOWN_TOOL_DEFINITION\n", argv[0]);
    return 2;
  }
  tool_install_signal_handlers();
  setvbuf(stdout, NULL, _IONBF, 0);
  const char* definition = argv[1];
  const char* record = smoke_tool_record(definition);
  int cm = g_str_has_prefix(definition, "smoke.cm.tool.");
  JsonParser* metadata = tool_load_metadata();
  JsonObject* root = json_node_get_object(json_parser_get_root(metadata));
  JsonObject* binding = tool_object(root, definition);
  smoke_expect(binding != NULL, "known root-owned tool record");
  tool_validate_binding(binding, definition, record, 1);
  int provider = cm ? tool_validate_catalog(1) : -1;
  g_object_unref(metadata);
  consent_client_h client = NULL;
  smoke_call(consent_client_create(&client), "tool create");
  unsigned generation = 1;
  unsigned admissions = 0;
  GHashTable* ledger =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  puts("READY live-tool-handle");
  char command[256], mode[32], operation[96], expected[32], extra;
  while (!tool_interrupted() && fgets(command, sizeof(command), stdin)) {
    smoke_expect(sscanf(command, "%31s %95s %31s %c", mode, operation, expected,
                        &extra) == 3,
                 "tool command fields");
    smoke_expect(
        !strcmp(expected, "ALLOWED") || !strcmp(expected, "CONSENT_REQUIRED") ||
            !strcmp(expected, "DENIED") || !strcmp(expected, "DISCONNECTED") ||
            !strcmp(expected, "PERMISSION_DENIED") ||
            !strcmp(expected, "RECONNECTED"),
        "finite tool expectation");
    if (!strcmp(mode, "reconnect")) {
      smoke_expect(!strcmp(expected, "RECONNECTED"), "reconnect expectation");
      smoke_call(consent_client_destroy(client), "destroy old tool handle");
      smoke_call(consent_client_create(&client),
                 "authenticate tool new handle");
      printf("RECONNECTED handle_generation=%u\n", ++generation);
      continue;
    }
    smoke_expect(!strcmp(mode, "query") || !strcmp(mode, "authorize") ||
                     !strcmp(mode, "request") || !strcmp(mode, "register") ||
                     !strcmp(mode, "probe_scope") ||
                     !strcmp(mode, "probe_purpose") ||
                     !strcmp(mode, "probe_recipient") ||
                     !strcmp(mode, "probe_operation"),
                 "finite tool mode");
    consent_params_t* params = smoke_requirement(definition);
    consent_result_t* result = NULL;
    if (!strcmp(mode, "register") || !strcmp(mode, "request")) {
      smoke_set(params, "operation_id", operation);
      smoke_set(params, "client_request_id", operation);
      int status =
          !strcmp(mode, "register")
              ? consent_register(client, "smoke.package", "smoke.app", params)
              : consent_request(client, params, 1000, &result);
      smoke_expect(status == CONSENT_ERROR_PERMISSION_DENIED,
                   "tool enforcer cannot register/request");
      printf("PASS wrong-role %s denied\n", mode);
    } else {
      int probe = g_str_has_prefix(mode, "probe_");
      int authorize = !strcmp(mode, "authorize") || probe;
      if (probe) {
        char* key = g_strdup_printf("r0.%s", mode + 6);
        smoke_set(params, key, "unapproved-tool-tuple");
        g_free(key);
      }
      smoke_call(consent_params_set_check_mode(
                     params,
                     authorize ? CONSENT_CHECK_AUTHORIZE : CONSENT_CHECK_QUERY),
                 "tool check mode");
      smoke_set(params, "operation_id", operation);
      smoke_set(params, "step_id", "tool-admission");
      int status = consent_check(client, params, 5000, &result);
      if (!strcmp(expected, "PERMISSION_DENIED") ||
          !strcmp(expected, "DISCONNECTED")) {
        smoke_expect(status == (!strcmp(expected, "DISCONNECTED")
                                    ? CONSENT_ERROR_DISCONNECTED
                                    : CONSENT_ERROR_PERMISSION_DENIED),
                     "precise tool fail-closed error");
        printf("PASS blocked error=%d count=%u\n", status, admissions);
      } else {
        smoke_call(status, "tool authoritative check");
        const char* decision = consent_result_get(result, "decision");
        smoke_expect(decision && !strcmp(decision, expected), "tool decision");
        printf("PASS %s decision=%s epoch=%s actor_role=%s\n", mode, decision,
               consent_result_get(result, "epoch"), SMOKE_TOOL_ROLE);
        if (!authorize || strcmp(decision, "ALLOWED")) {
          printf("PROTECTED_TOOL blocked/advisory count=%u\n", admissions);
        } else {
          const char* receipt = consent_result_get(result, "receipt");
          const char* retry = consent_result_get(result, "retry");
          smoke_expect(receipt != NULL, "tool authorization receipt");
          const char* previous = g_hash_table_lookup(ledger, receipt);
          if (previous) {
            printf("PROTECTED_TOOL deduplicated state=%s count=%u\n", previous,
                   admissions);
          } else if (retry && !strcmp(retry, "1")) {
            printf("PROTECTED_TOOL unknown prior execution blocked count=%u\n",
                   admissions);
          } else {
            /* Admission is recorded BEFORE spawn/read, even if execution fails.
             * A real enforcer needs durable side-effect deduplication. */
            g_hash_table_insert(ledger, g_strdup(receipt), g_strdup("unknown"));
            ++admissions;
            printf("ADMISSION receipt_recorded count=%u record=%s\n",
                   admissions, record);
            char* request = tool_request(operation, "cli:smoke-tool", record);
            int executed = cm ? tool_execute(provider, request, operation)
                              : context_lookup(record, operation);
            g_free(request);
            const char* state = executed == 1    ? "succeeded"
                                : executed == -1 ? "native_error"
                                                 : "unknown";
            g_hash_table_replace(ledger, g_strdup(receipt), g_strdup(state));
            printf("PROTECTED_TOOL state=%s count=%u\n", state, admissions);
          }
        }
      }
    }
    consent_result_free(result);
    consent_params_free(params);
  }
  g_hash_table_destroy(ledger);
  if (provider >= 0) close(provider);
  smoke_call(consent_client_destroy(client), "tool destroy");
  return 0;
}
