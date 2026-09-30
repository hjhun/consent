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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void smoke_expect(int condition, const char* description) {
  if (!condition) {
    fprintf(stderr, "FAIL %s\n", description);
    exit(1);
  }
}

void smoke_call(int status, const char* operation) {
  if (status) {
    fprintf(stderr, "FAIL %s status=%d (%s)\n", operation, status,
            consent_error_string(status));
    exit(1);
  }
}

void smoke_set(consent_params_t* params, const char* key, const char* value) {
  smoke_call(consent_params_set(params, key, value), key);
}

consent_params_t* smoke_params(void) {
  consent_params_t* params = NULL;
  smoke_call(consent_params_create(&params), "params");
  smoke_set(params, "subject", "smoke.subject");
  smoke_set(params, "profile", "smoke.profile");
  return params;
}

const char* smoke_tool_record(const char* definition) {
  static const char* records[] = {"summary", "fail",    "timeout",  "malformed",
                                  "stderr",  "nonzero", "conflict", "nul"};
  for (unsigned i = 0; i < G_N_ELEMENTS(records); ++i) {
    char* name = g_strdup_printf("smoke.cm.tool.%s", records[i]);
    int match = !strcmp(definition, name);
    g_free(name);
    if (match) return records[i];
  }
  if (!strncmp(definition, "smoke.ce.tool.level", 19) &&
      strlen(definition) == 20 && definition[19] >= '0' &&
      definition[19] <= '3')
    return definition + 14;
  return NULL;
}

consent_params_t* smoke_requirement(const char* definition) {
  consent_params_t* params = smoke_params();
  const char* record = smoke_tool_record(definition);
  int cm = g_str_has_prefix(definition, "smoke.cm.tool.");
  char* scope =
      record ? g_strdup_printf(
                   "%s/%s", cm ? "cli:smoke-tool" : "context.fixture", record)
             : NULL;
  smoke_call(consent_params_add_requirement(
                 params, definition, record && cm ? "execute" : "read",
                 record ? scope : "smoke-resource",
                 record ? "developer-tool-smoke" : "developer-smoke",
                 record ? "fixture-provider" : ""),
             "requirement");
  g_free(scope);
  smoke_set(params, "r0.policy_version", "1");
  return params;
}
