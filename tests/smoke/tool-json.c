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

#include "tool-json.h"

#include <string.h>

const char* tool_string(JsonObject* object, const char* key) {
  JsonNode* node = json_object_get_member(object, key);
  if (!node || !JSON_NODE_HOLDS_VALUE(node) ||
      json_node_get_value_type(node) != G_TYPE_STRING)
    return NULL;
  return json_node_get_string(node);
}

JsonObject* tool_object(JsonObject* object, const char* key) {
  JsonNode* node = json_object_get_member(object, key);
  return node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node)
                                              : NULL;
}

JsonParser* tool_parse(const char* data) {
  if (!data || strlen(data) > TOOL_LIMIT) return NULL;
  JsonParser* parser = json_parser_new();
  if (!json_parser_load_from_data(parser, data, -1, NULL) ||
      !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
    g_object_unref(parser);
    return NULL;
  }
  return parser;
}

static char* serialize(JsonObject* object) {
  JsonNode* node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, object);
  JsonGenerator* generator = json_generator_new();
  json_generator_set_root(generator, node);
  char* result = json_generator_to_data(generator, NULL);
  g_object_unref(generator);
  json_node_free(node);
  return result;
}

char* tool_request(const char* id, const char* name, const char* record) {
  JsonObject* root = json_object_new();
  JsonObject* params = json_object_new();
  JsonObject* arguments = json_object_new();
  json_object_set_string_member(root, "jsonrpc", "2.0");
  json_object_set_string_member(root, "id", id);
  json_object_set_string_member(root, "method", "tools/call");
  json_object_set_string_member(params, "name", name);
  json_object_set_string_member(arguments, "record", record);
  json_object_set_object_member(params, "arguments", arguments);
  json_object_set_object_member(root, "params", params);
  return serialize(root);
}

char* tool_response(const char* id, const char* text, int error) {
  JsonObject* root = json_object_new();
  JsonObject* body = json_object_new();
  json_object_set_string_member(root, "jsonrpc", "2.0");
  if (id)
    json_object_set_string_member(root, "id", id);
  else
    json_object_set_null_member(root, "id");
  if (error) {
    json_object_set_int_member(body, "code", error);
    json_object_set_string_member(body, "message", text);
  } else {
    json_object_set_string_member(body, "summary", text);
  }
  json_object_set_object_member(root, error ? "error" : "result", body);
  return serialize(root);
}

int tool_validate_response(const char* data, const char* id) {
  JsonParser* parser = tool_parse(data);
  if (!parser) return 0;
  JsonObject* root = json_node_get_object(json_parser_get_root(parser));
  const char* version = tool_string(root, "jsonrpc");
  const char* actual_id = tool_string(root, "id");
  JsonObject* result = tool_object(root, "result");
  JsonObject* error = tool_object(root, "error");
  int valid = version && !strcmp(version, "2.0") && actual_id && *actual_id &&
              strlen(actual_id) <= 95 && !strcmp(actual_id, id) &&
              (json_object_has_member(root, "result") !=
               json_object_has_member(root, "error")) &&
              (!!result != !!error);
  if (result) valid = valid && tool_string(result, "summary");
  if (error) {
    JsonNode* code = json_object_get_member(error, "code");
    valid = valid && code && JSON_NODE_HOLDS_VALUE(code) &&
            json_node_get_value_type(code) == G_TYPE_INT64 &&
            tool_string(error, "message");
  }
  g_object_unref(parser);
  return valid ? (error ? -1 : 1) : 0;
}
