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

int main(void) {
  struct {
    const char* json;
    int expected;
  } cases[] = {
      {"{\"jsonrpc\":\"2.0\",\"id\":\"x\","
       "\"result\":{\"summary\":\"ok\"}}",
       1},
      {"{\"jsonrpc\":\"2.0\",\"id\":\"x\","
       "\"error\":{\"code\":-32001,\"message\":\"failed\"}}",
       -1},
      {"{\"jsonrpc\":\"2.0\",\"id\":\"x\","
       "\"result\":{\"summary\":\"ok\"},\"error\":\"bad\"}",
       0},
      {"{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"error\":\"bad\"}", 0},
      {"{\"jsonrpc\":\"2.0\",\"id\":\"different\","
       "\"result\":{\"summary\":\"ok\"}}",
       0},
      {"{\"jsonrpc\":\"2.0\",\"id\":\"\","
       "\"result\":{\"summary\":\"ok\"}}",
       0},
      {"{\"jsonrpc\":\"2.0\",\"id\":\"x\","
       "\"error\":{\"code\":\"wrong\",\"message\":\"failed\"}}",
       0},
      {"{\"jsonrpc\":\"2.0\",\"id\":\"x\","
       "\"result\":{\"summary\":\"ok\"}} trailing",
       0},
  };
  for (unsigned i = 0; i < G_N_ELEMENTS(cases); ++i) {
    if (tool_validate_response(cases[i].json, "x") != cases[i].expected) {
      fprintf(stderr, "FAIL JSON response case %u\n", i);
      return 1;
    }
  }
  puts("PASS fixture JSON matching id, typed error, result/error presence XOR");
  return 0;
}
