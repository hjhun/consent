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
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "tool-gate.h"
#include "tool-process.h"

#define MOCK_ROOT "/tmp/consent-smoke"
#define MOCK_PROVIDER SMOKE_TOOL_PACKAGE "/bin/consent-smoke-tool"

/* Private stdio fixture service. No product CM/CE security contract implied. */
static int provider = -1;
static int is_cm;
static consent_client_h client;
static JsonParser* metadata;
static GHashTable* ledger;
static unsigned admissions;
static unsigned generation = 1;
#define MOCK_LEDGER_LIMIT 128

static char* serialize(JsonObject* object) {
  JsonNode* node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, object);
  JsonGenerator* generator = json_generator_new();
  json_generator_set_root(generator, node);
  char* text = json_generator_to_data(generator, NULL);
  json_node_free(node);
  g_object_unref(generator);
  return text;
}

static void reply(const char* id, JsonObject* result, int code,
                  const char* message) {
  JsonObject* root = json_object_new();
  json_object_set_string_member(root, "jsonrpc", "2.0");
  if (id)
    json_object_set_string_member(root, "id", id);
  else
    json_object_set_null_member(root, "id");
  if (code) {
    JsonObject* error = json_object_new();
    json_object_set_int_member(error, "code", code);
    json_object_set_string_member(error, "message", message);
    json_object_set_object_member(root, "error", error);
  } else {
    json_object_set_object_member(root, "result", result);
  }
  char* text = serialize(root);
  smoke_expect(strlen(text) <= TOOL_LIMIT, "bounded mock response");
  puts(text);
  g_free(text);
}

static int identifier(const char* value) {
  if (!value || !*value || strlen(value) > 95)
    return 0;
  for (const char* p = value; *p; ++p)
    if (!g_ascii_isalnum(*p) && !strchr("._:-", *p))
      return 0;
  return 1;
}

static JsonObject* root_metadata(void) {
  return json_node_get_object(json_parser_get_root(metadata));
}

static const char* definition_for(const char* record) {
  if (!record)
    return NULL;
  JsonObject* root = root_metadata();
  GList* names = json_object_get_members(root);
  const char* found = NULL;
  for (GList* it = names; it; it = it->next) {
    JsonObject* binding = tool_object(root, it->data);
    const char* role = binding ? tool_string(binding, "enforcer") : NULL;
    const char* actual = binding ? tool_string(binding, "record") : NULL;
    if (role && actual && !strcmp(role, SMOKE_TOOL_ROLE) &&
        !strcmp(actual, record)) {
      found = it->data;
      break;
    }
  }
  /* Member names belong to the parser, not to the temporary GList. */
  g_list_free(names);
  return found;
}

static JsonObject* observation(int status, const char* decision) {
  JsonObject* result = json_object_new();
  json_object_set_int_member(result, "api_status", status);
  json_object_set_string_member(result, "decision", decision);
  json_object_set_int_member(result, "admissions", admissions);
  json_object_set_int_member(result, "handle_generation", generation);
  return result;
}

static JsonObject* check(const char* definition, const char* operation,
                         const char* step, int execute) {
  /* Terminate explicitly at the finite fixture cap before consuming a grant.
   * Existing receipt replays below the cap remain authoritatively checked. */
  smoke_expect(g_hash_table_size(ledger) < MOCK_LEDGER_LIMIT,
               "fixture ledger capacity reached; service must restart");
  consent_params_t* params = smoke_requirement(definition);
  smoke_set(params, "operation_id", operation);
  smoke_set(params, "step_id", step);
  smoke_call(
      consent_params_set_check_mode(
          params, execute ? CONSENT_CHECK_AUTHORIZE : CONSENT_CHECK_QUERY),
      "mock check mode");
  consent_result_t* consent = NULL;
  int status = consent_check(client, params, 5000, &consent);
  const char* decision =
      status ? "API_ERROR" : consent_result_get(consent, "decision");
  smoke_expect(decision != NULL, "mock decision");
  JsonObject* output = observation(status, decision);
  const char* epoch = consent ? consent_result_get(consent, "epoch") : NULL;
  if (epoch)
    json_object_set_string_member(output, "epoch", epoch);
  /* Current authoritative check precedes every cached result delivery. */
  if (!status && execute && !strcmp(decision, "ALLOWED")) {
    const char* receipt = consent_result_get(consent, "receipt");
    const char* retry = consent_result_get(consent, "retry");
    smoke_expect(receipt != NULL, "mock receipt");
    JsonObject* previous = g_hash_table_lookup(ledger, receipt);
    if (previous) {
      json_object_set_boolean_member(output, "deduplicated", TRUE);
      json_object_set_object_member(output, "execution",
                                    json_object_ref(previous));
    } else if (retry && !strcmp(retry, "1")) {
      json_object_set_string_member(output, "state", "blocked_unknown_receipt");
    } else {
      JsonObject* execution = json_object_new();
      json_object_set_string_member(execution, "state", "unknown");
      g_hash_table_insert(ledger, g_strdup(receipt), execution);
      ++admissions;
      const char* record = smoke_tool_record(definition);
      char* native = NULL;
      int outcome = 0;
      int wait_status = -1;
      if (is_cm) {
        char* request = tool_request(operation, "cli:smoke-tool", record);
        outcome = tool_execute_capture(provider, request, operation, &native,
                                       &wait_status, 0);
        g_free(request);
      } else {
        native = tool_context_read(record, operation);
        outcome = native ? tool_validate_response(native, operation) : 0;
      }
      const char* state = outcome == 1    ? "succeeded"
                          : outcome == -1 ? "native_error"
                                          : "unknown";
      json_object_set_string_member(execution, "state", state);
      json_object_set_int_member(execution, "wait_status", wait_status);
      if (native) {
        JsonParser* parsed = tool_parse(native);
        smoke_expect(parsed != NULL, "validated mock native reply");
        JsonObject* object = json_node_get_object(json_parser_get_root(parsed));
        /* Retain payload, never the provider's transport envelope/id. */
        JsonNode* payload =
            json_object_get_member(object, outcome == -1 ? "error" : "result");
        smoke_expect(payload != NULL, "native payload");
        json_object_set_member(execution,
                               outcome == -1 ? "native_error" : "data",
                               json_node_copy(payload));
        g_object_unref(parsed);
        g_free(native);
      }
      json_object_set_object_member(output, "execution",
                                    json_object_ref(execution));
    }
    json_object_set_int_member(output, "admissions", admissions);
  }
  consent_result_free(consent);
  consent_params_free(params);
  return output;
}

static int probe(const char* kind) {
  consent_params_t* params = smoke_requirement(is_cm ? "smoke.ce.tool.level0"
                                                     : "smoke.cm.tool.summary");
  smoke_set(params, "operation_id", "mock-role-probe");
  smoke_set(params, "step_id", "mock-role-probe");
  smoke_set(params, "client_request_id", "mock-role-probe");
  consent_result_t* result = NULL;
  int status;
  if (!strcmp(kind, "register"))
    status = consent_register(client, "smoke.package", "smoke.app", params);
  else if (!strcmp(kind, "request"))
    status = consent_request(client, params, 1000, &result);
  else {
    smoke_call(consent_params_set_check_mode(params, CONSENT_CHECK_AUTHORIZE),
               "cross enforcer mode");
    status = consent_check(client, params, 1000, &result);
  }
  consent_result_free(result);
  consent_params_free(params);
  return status;
}

static void dispatch(JsonObject* root) {
  const char* id = tool_string(root, "id");
  const char* method = tool_string(root, "method");
  const char* version = tool_string(root, "jsonrpc");
  JsonObject* params = tool_object(root, "params");
  if (!identifier(id) || !method || !version || strcmp(version, "2.0") ||
      !params || json_object_get_size(root) != 4) {
    reply(NULL, NULL, -32600, "Invalid fixture request envelope");
    return;
  }
  int discover = !strcmp(method, is_cm ? "catalog.discover" : "context.list");
  int reconnect = !strcmp(method, "mock.reconnect");
  int role_probe = !strcmp(method, "mock.probe-role");
  int metadata_only = !strcmp(method, "context.metadata") && !is_cm;
  int query = !strcmp(method, is_cm ? "capability.query" : "context.query");
  int execute = !strcmp(method, is_cm ? "capability.execute" : "context.get");
  if (!discover && !reconnect && !role_probe && !metadata_only && !query &&
      !execute) {
    reply(id, NULL, -32601, "Unknown fixture method");
    return;
  }
  if (discover || reconnect) {
    if (json_object_get_size(params))
      goto invalid;
    if (reconnect) {
      smoke_call(consent_client_destroy(client), "mock old handle destroy");
      smoke_call(consent_client_create(&client), "mock new authentication");
      ++generation;
      reply(id, observation(0, "RECONNECTED"), 0, NULL);
    } else {
      JsonObject* result = observation(0, "METADATA_ONLY");
      JsonObject* records = json_object_new();
      GList* names = json_object_get_members(root_metadata());
      for (GList* it = names; it; it = it->next) {
        JsonObject* binding = tool_object(root_metadata(), it->data);
        const char* role = tool_string(binding, "enforcer");
        if (!strcmp(role, SMOKE_TOOL_ROLE))
          json_object_set_object_member(records, it->data,
                                        json_object_ref(binding));
      }
      g_list_free(names);
      json_object_set_object_member(result, "records", records);
      reply(id, result, 0, NULL);
    }
    return;
  }
  if (role_probe) {
    const char* kind = tool_string(params, "kind");
    if (json_object_get_size(params) != 1 || !kind ||
        (strcmp(kind, "register") && strcmp(kind, "request") &&
         strcmp(kind, "cross")))
      goto invalid;
    reply(id, observation(probe(kind), "ROLE_PROBE"), 0, NULL);
    return;
  }
  const char* record = tool_string(params, "record");
  const char* definition = definition_for(record);
  if (!definition)
    goto invalid;
  if (metadata_only) {
    if (json_object_get_size(params) != 1)
      goto invalid;
    JsonObject* result = observation(0, "METADATA_ONLY");
    json_object_set_object_member(
        result, "metadata",
        json_object_ref(tool_object(root_metadata(), definition)));
    reply(id, result, 0, NULL);
    return;
  }
  const char* operation = tool_string(params, "operation_id");
  const char* step = tool_string(params, "step_id");
  const char* capability = is_cm ? tool_string(params, "capability") : NULL;
  if (json_object_get_size(params) != (is_cm ? 4 : 3) ||
      !identifier(operation) || !identifier(step) ||
      (is_cm && (!capability || strcmp(capability, "cli:smoke-tool"))))
    goto invalid;
  reply(id, check(definition, operation, step, execute), 0, NULL);
  return;
invalid:
  reply(id, NULL, -32602, "Invalid fixture parameters");
}

/* Complete byte frame, including partial-line waits, is bounded to 12s. */
static int read_frame(char* bytes) {
  size_t size = 0;
  gint64 deadline = 0;
  while (!tool_interrupted()) {
    struct pollfd descriptor = {STDIN_FILENO, POLLIN, 0};
    int ready = poll(&descriptor, 1, 100);
    if (ready < 0 && errno != EINTR)
      return -1;
    if (deadline && g_get_monotonic_time() >= deadline)
      return -1;
    if (ready <= 0)
      continue;
    char byte;
    ssize_t count = read(STDIN_FILENO, &byte, 1);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return size ? -1 : 0;
    if (!deadline)
      deadline = g_get_monotonic_time() + 12000000;
    if (!byte || size >= TOOL_LIMIT)
      return -1;
    if (byte == '\n') {
      bytes[size] = 0;
      return 1;
    }
    bytes[size++] = byte;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc != 2 || (strcmp(argv[1], "fixture") && strcmp(argv[1], "actual"))) {
    fprintf(stderr, "Usage: %s fixture|actual\n", argv[0]);
    return 2;
  }
  is_cm = !strcmp(SMOKE_TOOL_ROLE, "cm");
  setvbuf(stdout, NULL, _IONBF, 0);
  tool_install_signal_handlers();
  metadata = tool_load_metadata();
  GList* names = json_object_get_members(root_metadata());
  for (GList* it = names; it; it = it->next) {
    const char* record = smoke_tool_record(it->data);
    smoke_expect(record != NULL, "finite fixture metadata");
    tool_validate_binding(tool_object(root_metadata(), it->data), it->data,
                          record, 0);
  }
  g_list_free(names);
  if (is_cm) {
    if (!strcmp(argv[1], "actual")) {
      provider = tool_validate_catalog(0);
    } else {
      /* Fixture descriptor validation independent of product library/tool. */
      int fd = tool_open(MOCK_ROOT "/mock-catalog.json", 0);
      char bytes[1025];
      size_t size = 0;
      while (size < sizeof(bytes)) {
        ssize_t count = read(fd, bytes + size, sizeof(bytes) - size);
        if (count < 0 && errno == EINTR)
          continue;
        smoke_expect(count >= 0, "complete fixture catalog read");
        if (!count)
          break;
        size += count;
      }
      close(fd);
      smoke_expect(size > 0 && size <= 1024 && !memchr(bytes, 0, size),
                   "fixture catalog bound");
      bytes[size] = 0;
      JsonParser* catalog = tool_parse(bytes);
      smoke_expect(catalog != NULL, "fixture catalog JSON");
      JsonObject* object = json_node_get_object(json_parser_get_root(catalog));
      const char* id = tool_string(object, "id");
      const char* owner = tool_string(object, "owner");
      const char* executable = tool_string(object, "executable");
      smoke_expect(json_object_get_size(object) == 3 && id && owner &&
                       executable && !strcmp(id, "cli:smoke-tool") &&
                       !strcmp(owner, "smoke.package") &&
                       !strcmp(executable, MOCK_PROVIDER),
                   "fixed trusted fixture catalog");
      provider = tool_open(executable, 1);
      g_object_unref(catalog);
    }
  }
  smoke_call(consent_client_create(&client), "mock authentication");
  ledger = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                 (GDestroyNotify)json_object_unref);
  fprintf(stderr, "READY mock-%s pid=%ld handle_generation=1\n",
          SMOKE_TOOL_ROLE, (long)getpid());
  char bytes[TOOL_LIMIT + 1];
  int status;
  while ((status = read_frame(bytes)) > 0) {
    JsonParser* parsed = tool_parse(bytes);
    if (parsed) {
      dispatch(json_node_get_object(json_parser_get_root(parsed)));
      g_object_unref(parsed);
    } else {
      reply(NULL, NULL, -32700, "Invalid fixture JSON");
    }
  }
  if (status < 0)
    reply(NULL, NULL, -32700, "Invalid bounded byte frame");
  g_hash_table_destroy(ledger);
  smoke_call(consent_client_destroy(client), "mock destroy");
  g_object_unref(metadata);
  if (provider >= 0)
    close(provider);
  return status < 0 ? 2 : 0;
}
