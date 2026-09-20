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
#include "example_common.h"

#include <stdio.h>

struct field {
  const char* key;
  const char* value;
};

static int set_fields(consent_params_t* params, const struct field* fields,
    size_t count) {
  for (size_t i = 0; i < count; ++i) {
    int status = consent_params_set(params, fields[i].key, fields[i].value);
    if (status)
      return status;
  }
  return 0;
}

int example_requirement(const char* subject, const char* profile,
    const char* definition, const char* revision, const char* scope,
    const char* purpose, const char* recipient, const char* session,
    const char* generation, consent_params_t** params) {
  const struct field fields[] = {{"subject", subject}, {"profile", profile},
      {"r0.policy_version", revision}};
  int status = consent_params_create(params);
  if (!status)
    status = set_fields(*params, fields, sizeof(fields) / sizeof(fields[0]));
  if (!status)
    status = consent_params_add_requirement(*params, definition, "read", scope,
        purpose, recipient);
  if (!status && session)
    status = consent_params_set(*params, "session", session);
  if (!status && session)
    status = consent_params_set(*params, "generation", generation);
  if (status) {
    consent_params_free(*params);
    *params = NULL;
  }
  return status;
}

int example_definition(const char* definition, const char* enforcer,
    const char* generation, const char* operation_id, const char* policy_version,
    const char* text_revision, consent_params_t** params) {
  const struct field fields[] = {
    {"definition", definition}, {"enforcer", enforcer},
    {"expected_generation", generation}, {"operation_id", operation_id},
    {"policy_version", policy_version}, {"text_revision", text_revision},
    {"level", "1"}, {"modes", "ONCE,SESSION,TIMED,PERSISTENT"},
    {"retention_ms", "60000"}, {"default_locale", "en"},
    {"template_version", "1"},
    {"parameter.scope.type", "string"},
    {"parameter.scope.source", "scope"},
    {"parameter.scope.max_bytes", "512"},
    {"parameter.purpose.type", "string"},
    {"parameter.purpose.source", "purpose"},
    {"parameter.purpose.max_bytes", "256"},
    {"parameter.recipient.type", "string"},
    {"parameter.recipient.source", "recipient"},
    {"parameter.recipient.max_bytes", "512"},
    {"message.en.title", "Read the requested resource"},
    {"message.en.body", "Allow reading scope \"{scope}\" for purpose \"{purpose}\" and recipient \"{recipient}\"?"},
    {"message.ko.title", "요청한 자원 읽기"},
    {"message.ko.body", "범위 \"{scope}\"를 목적 \"{purpose}\", 수신자 \"{recipient}\"에 대해 읽도록 허용합니까?"}
  };
  int status = consent_params_create(params);
  if (!status)
    status = set_fields(*params, fields, sizeof(fields) / sizeof(fields[0]));
  if (status) {
    consent_params_free(*params);
    *params = NULL;
  }
  return status;
}

void example_error(const char* operation, int status) {
  fprintf(stderr, "%s: %d (%s)\n", operation, status, consent_error_string(status));
  if (status == CONSENT_ERROR_OUTCOME_UNKNOWN || status == CONSENT_ERROR_TIMEOUT)
    fputs("Do not execute protected work. Reconcile using the same IDs and payload.\n", stderr);
}

const char* example_decision(const consent_result_t* result) {
  const char* value = consent_result_get(result, "decision");
  return value ? value : "UNKNOWN";
}
