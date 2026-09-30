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

#include "tool-gate.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>

#include "common.h"
#include "tool-process.h"

#define TOOL_ROOT "/tmp/consent-smoke"
#define TOOL_PROVIDER SMOKE_TOOL_PACKAGE "/bin/consent-smoke-tool"

JsonParser* tool_load_metadata(void) {
  int fd = tool_open(TOOL_ROOT "/tool-metadata.json", 0);
  char bytes[TOOL_LIMIT + 1];
  size_t size = 0;
  while (size < sizeof(bytes)) {
    ssize_t count = read(fd, bytes + size, sizeof(bytes) - size);
    if (count < 0 && errno == EINTR)
      continue;
    smoke_expect(count >= 0, "metadata read");
    if (!count)
      break;
    size += count;
  }
  close(fd);
  smoke_expect(size > 0 && size <= TOOL_LIMIT && !memchr(bytes, 0, size),
               "bounded complete metadata without NUL");
  bytes[size] = 0;
  JsonParser* parser = tool_parse(bytes);
  smoke_expect(parser != NULL, "tool metadata JSON");
  return parser;
}

int tool_validate_catalog(int verbose) {
  int anchor = tool_open(TOOL_ROOT "/catalog-tool.db", 0);
  sqlite3* database = NULL;
  sqlite3_stmt* statement = NULL;
  smoke_expect(sqlite3_open_v2(TOOL_ROOT "/catalog-tool.db", &database,
                               SQLITE_OPEN_READONLY, NULL) == SQLITE_OK,
               "CM fixture catalog open");
  smoke_expect(sqlite3_prepare_v2(database,
                                  "SELECT id,owner,executable FROM capability "
                                  "WHERE id='cli:smoke-tool'",
                                  -1, &statement, NULL) == SQLITE_OK &&
                   sqlite3_step(statement) == SQLITE_ROW,
               "canonical CLI catalog lookup");
  const char* id = (const char*)sqlite3_column_text(statement, 0);
  const char* owner = (const char*)sqlite3_column_text(statement, 1);
  const char* executable = (const char*)sqlite3_column_text(statement, 2);
  smoke_expect(id && owner && executable && !strcmp(id, "cli:smoke-tool") &&
                   !strcmp(owner, "smoke.package") &&
                   !strcmp(executable, TOOL_PROVIDER),
               "actual canonical identity/owner/fixed installed executable");
  /* Pin the actual catalog executable inode before any protected admission. */
  int fd = tool_open(executable, 1);
  struct stat st;
  smoke_expect(!fstat(fd, &st), "installed provider inode");
  if (verbose)
    printf("CATALOG binding id=%s owner=%s executable=%s inode=%lu\n", id,
           owner, executable, (unsigned long)st.st_ino);
  smoke_expect(sqlite3_step(statement) == SQLITE_DONE, "one canonical entry");
  sqlite3_finalize(statement);
  smoke_expect(sqlite3_close(database) == SQLITE_OK, "catalog close");
  close(anchor);
  return fd;
}

void tool_validate_binding(JsonObject* binding, const char* definition,
                           const char* record, int verbose) {
  smoke_expect(binding != NULL, "fixture binding object");
  const char* role = tool_string(binding, "enforcer");
  const char* actual_record = tool_string(binding, "record");
  const char* actual_definition = tool_string(binding, "definition");
  JsonNode* level = json_object_get_member(binding, "level");
  int cm = g_str_has_prefix(definition, "smoke.cm.tool.");
  smoke_expect(role && actual_record && actual_definition &&
                   !strcmp(actual_record, record) &&
                   !strcmp(actual_definition, definition) &&
                   !strcmp(role, cm ? "cm" : "ce") && level &&
                   JSON_NODE_HOLDS_VALUE(level) &&
                   json_node_get_value_type(level) == G_TYPE_INT64 &&
                   json_node_get_int(level) == (cm ? 1 : record[5] - '0'),
               "root metadata definition/record/trusted level");
  if (verbose)
    printf("METADATA definition=%s record=%s level=%lld enforcer=%s\n",
           definition, record, (long long)json_node_get_int(level), role);
}

char* tool_context_read(const char* record, const char* id) {
  char* path = g_strdup_printf(TOOL_ROOT "/tool-context/%s.txt", record);
  int fd = tool_open(path, 0);
  g_free(path);
  char data[1025];
  size_t size = 0;
  while (size < sizeof(data)) {
    ssize_t count = read(fd, data + size, sizeof(data) - size);
    if (count < 0 && errno == EINTR)
      continue;
    if (count < 0) {
      close(fd);
      return NULL;
    }
    if (!count)
      break;
    size += count;
  }
  close(fd);
  if (!size || size > 1024 || memchr(data, 0, size))
    return NULL;
  data[size] = 0;
  char* response = tool_response(id, data, 0);
  return response;
}
