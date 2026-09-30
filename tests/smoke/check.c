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

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
/* One actor preserves its ledger; reconnect explicitly replaces dead handles.
 * AUTHORIZE is always authoritative; QUERY/cache is never permission for the
 * protected action. */
int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s DEFINITION PROTECTED_FILE\n", argv[0]);
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  consent_client_h client = NULL;
  smoke_call(consent_client_create(&client), "create");
  unsigned reads = 0;
  unsigned handle_generation = 1;
  GHashTable* executed =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  char command[256], mode[32], operation[96], expected[32];
  puts("READY live-check-handle");
  fflush(stdout);
  while (fgets(command, sizeof(command), stdin)) {
    if (sscanf(command, "%31s %95s %31s", mode, operation, expected) != 3)
      return 2;
    smoke_expect(
        !strcmp(expected, "ALLOWED") || !strcmp(expected, "CONSENT_REQUIRED") ||
            !strcmp(expected, "DENIED") || !strcmp(expected, "DISCONNECTED") ||
            !strcmp(expected, "STORAGE_ERROR") ||
            !strcmp(expected, "PERMISSION_DENIED") ||
            !strcmp(expected, "RECONNECTED"),
        "known expectation");
    if (!strcmp(mode, "reconnect")) {
      smoke_expect(!strcmp(expected, "RECONNECTED"), "reconnect expectation");
      smoke_call(consent_client_destroy(client), "destroy old handle");
      client = NULL;
      smoke_call(consent_client_create(&client), "authenticate new handle");
      printf("RECONNECTED handle_generation=%u\n", ++handle_generation);
      continue;
    }
    smoke_expect(!strcmp(mode, "register") || !strcmp(mode, "request") ||
                     !strcmp(mode, "query") || !strcmp(mode, "authorize"),
                 "known command mode");
    consent_params_t* params = smoke_requirement(argv[1]);
    consent_result_t* result = NULL;
    if (!strcmp(mode, "register")) {
      int status =
          consent_register(client, "smoke.package", "smoke.app", params);
      smoke_expect(status == CONSENT_ERROR_PERMISSION_DENIED,
                   "CM/CE cannot register");
      puts("PASS wrong-role register denied");
    } else if (!strcmp(mode, "request")) {
      smoke_set(params, "client_request_id", operation);
      smoke_set(params, "operation_id", operation);
      int status = consent_request(client, params, 1000, &result);
      smoke_expect(status == CONSENT_ERROR_PERMISSION_DENIED,
                   "CM/CE cannot request approval");
      puts("PASS wrong-role request denied");
    } else {
      int authorize = !strcmp(mode, "authorize");
      smoke_call(consent_params_set_check_mode(
                     params,
                     authorize ? CONSENT_CHECK_AUTHORIZE : CONSENT_CHECK_QUERY),
                 "mode");
      if (authorize) {
        smoke_set(params, "operation_id", operation);
        smoke_set(params, "step_id", "read");
      }
      int status = consent_check(client, params, 5000, &result);
      if (!strcmp(expected, "PERMISSION_DENIED")) {
        smoke_expect(status == CONSENT_ERROR_PERMISSION_DENIED,
                     "cross-enforcer authorization denied");
        printf("PASS blocked error=%d\n", status);
      } else if (!strcmp(expected, "DISCONNECTED")) {
        smoke_expect(status == CONSENT_ERROR_DISCONNECTED,
                     "old handle must remain disconnected");
        printf("PASS blocked error=%d\n", status);
      } else if (!strcmp(expected, "STORAGE_ERROR")) {
        smoke_expect(status == CONSENT_ERROR_STORAGE ||
                         status == CONSENT_ERROR_DISCONNECTED,
                     "only storage/disconnection error is expected");
        printf("PASS blocked error=%d\n", status);
      } else {
        smoke_call(status, "check");
        const char* decision = consent_result_get(result, "decision");
        smoke_expect(decision && !strcmp(decision, expected), "decision");
        printf("PASS %s decision=%s epoch=%s\n", mode, decision,
               consent_result_get(result, "epoch"));
        if (authorize && !strcmp(decision, "ALLOWED")) {
          smoke_expect(consent_result_get(result, "receipt") != NULL,
                       "authorization receipt");
          const char* receipt = consent_result_get(result, "receipt");
          if (g_hash_table_contains(executed, receipt)) {
            printf("PROTECTED_READ deduplicated count=%u\n", reads);
            consent_result_free(result);
            consent_params_free(params);
            continue;
          }
          const char* retry = consent_result_get(result, "retry");
          if (retry && !strcmp(retry, "1")) {
            printf("PROTECTED_READ unknown prior execution blocked count=%u\n",
                   reads);
            consent_result_free(result);
            consent_params_free(params);
            continue;
          }
          int fd = open(argv[2], O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
          struct stat file_info;
          smoke_expect(fd >= 0 && fstat(fd, &file_info) == 0 &&
                           S_ISREG(file_info.st_mode),
                       "fixture regular file");
          FILE* resource = fdopen(fd, "r");
          smoke_expect(resource != NULL, "protected fixture open");
          char data[128];
          smoke_expect(fgets(data, sizeof(data), resource) != NULL,
                       "protected fixture read");
          smoke_expect(fclose(resource) == 0, "protected fixture close");
          g_hash_table_add(executed, g_strdup(receipt));
          ++reads;
          printf("PROTECTED_READ count=%u data=%s", reads, data);
        } else {
          printf("PROTECTED_READ blocked/advisory count=%u\n", reads);
        }
      }
    }
    consent_result_free(result);
    consent_params_free(params);
    fflush(stdout);
  }
  g_hash_table_destroy(executed);
  smoke_call(consent_client_destroy(client), "destroy");
  return 0;
}
