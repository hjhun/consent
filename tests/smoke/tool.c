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

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Synthetic provider: direct invocation is not a product security boundary. */
int main(int argc, char** argv) {
  if (argc != 3 || strcmp(argv[1], "--json")) {
    fprintf(stderr, "Usage: %s --json JSON_RPC_REQUEST\n", argv[0]);
    return 2;
  }
  JsonParser* parser = tool_parse(argv[2]);
  JsonObject* root =
      parser ? json_node_get_object(json_parser_get_root(parser)) : NULL;
  const char* id = root ? tool_string(root, "id") : NULL;
  JsonObject* params = root ? tool_object(root, "params") : NULL;
  JsonObject* args = params ? tool_object(params, "arguments") : NULL;
  const char* version = root ? tool_string(root, "jsonrpc") : NULL;
  const char* method = root ? tool_string(root, "method") : NULL;
  const char* name = params ? tool_string(params, "name") : NULL;
  const char* record = args ? tool_string(args, "record") : NULL;
  int valid = id && *id && strlen(id) <= 95 && version &&
              !strcmp(version, "2.0") && method &&
              !strcmp(method, "tools/call") && name &&
              !strcmp(name, "cli:smoke-tool") && record &&
              json_object_get_size(args) == 1 &&
              (!strcmp(record, "summary") || !strcmp(record, "fail") ||
               !strcmp(record, "timeout") || !strcmp(record, "malformed") ||
               !strcmp(record, "stderr") || !strcmp(record, "nonzero") ||
               !strcmp(record, "conflict") || !strcmp(record, "nul"));
  char* output = NULL;
  if (!valid) {
    output = tool_response(id, "Invalid fixture tool parameters", -32602);
  } else {
    if (!strcmp(record, "timeout")) sleep(4);
    if (!strcmp(record, "malformed")) {
      puts("deliberately malformed fixture response");
      g_object_unref(parser);
      return 0;
    }
    output = tool_response(id, "synthetic capability summary",
                           !strcmp(record, "fail") ? -32001 : 0);
  }
  int exit_status = 0;
  if (valid && !strcmp(record, "stderr"))
    fprintf(stderr, "%s\n", output);
  else
    puts(output);
  if (valid && !strcmp(record, "conflict")) {
    char* conflicting = tool_response(id, "conflicting fixture result", 0);
    fprintf(stderr, "%s\n", conflicting);
    g_free(conflicting);
  }
  if (valid && !strcmp(record, "nul")) {
    const char bytes[] = {0, 'x', '\n'};
    fwrite(bytes, 1, sizeof(bytes), stdout);
  }
  if (valid && !strcmp(record, "nonzero")) exit_status = 7;
  g_free(output);
  if (parser) g_object_unref(parser);
  return exit_status;
}
