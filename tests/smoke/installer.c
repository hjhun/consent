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
int main(int argc, char** argv) {
  if (argc != 6 && argc != 7) {
    fprintf(stderr,
            "Usage: %s GENERATION DEFINITION ENFORCER LEVEL OPERATION "
            "[--probe-invalid]\n",
            argv[0]);
    return 2;
  }
  int probe_invalid = argc == 7 && !strcmp(argv[6], "--probe-invalid");
  if (argc == 7 && !probe_invalid) return 2;
  const char* definition = argv[2];
  char* end = NULL;
  long level = strtol(argv[4], &end, 10);
  if (!*argv[4] || *end || level < 0 || level > (probe_invalid ? 4 : 3))
    return 2;
  consent_client_h client = NULL;
  consent_params_t* params = smoke_params();
  smoke_set(params, "definition", definition);
  smoke_set(params, "enforcer", argv[3]);
  smoke_set(params, "expected_generation", argv[1]);
  smoke_set(params, "operation_id", argv[5]);
  smoke_set(params, "policy_version", "1");
  smoke_set(params, "text_revision", "1");
  smoke_set(params, "level", argv[4]);
  smoke_set(
      params, "modes",
      probe_invalid ? "PERSISTENT" : (level == 3 ? "ONCE" : "ONCE,PERSISTENT"));
  smoke_set(params, "retention_ms", "60000");
  smoke_set(params, "default_locale", "en");
  smoke_set(params, "message.en.title", "Developer smoke approval");
  smoke_set(params, "message.en.body",
            smoke_tool_record(definition)
                ? "Allow this isolated fixture tool operation?"
                : "Allow isolated smoke resource read?");
  smoke_call(consent_client_create(&client), "create");
  int status = consent_register(client, "smoke.package", "smoke.app", params);
  if (probe_invalid) {
    smoke_expect(status == CONSENT_ERROR_INVALID_PARAMETER,
                 "daemon rejects invalid level/modes");
    printf("PASS daemon invalid level/modes rejected status=%d\n", status);
  } else {
    smoke_call(status, "Installer register(package, app)");
  }
  if (!probe_invalid)
    printf("PASS installer definition=%s level=%ld modes=%s\n", definition,
           level, level == 3 ? "ONCE" : "ONCE,PERSISTENT");
  consent_params_free(params);
  smoke_call(consent_client_destroy(client), "destroy");
  return 0;
}
