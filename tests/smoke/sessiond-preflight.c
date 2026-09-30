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
#include <sessiond.h>
#include <gio/gio.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s TRUSTED_CONFIGURED_SESSION_UID\n", argv[0]);
    return 2;
  }
  char* end = NULL;
  errno = 0;
  long uid = strtol(argv[1], &end, 10);
  if (errno || !end || *end || uid <= 0 || uid > INT_MAX)
    return 2;
  subsession_user_t local = {0};
  int status = subsession_get_current_user((int)uid, local);
  printf("LIBSESSIOND_FILE_OBSERVATION status=%d "
         "untrusted_read_not_readiness=1\n", status);
  GError* error = NULL;
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!bus) {
    fprintf(stderr, "NATIVE_PROFILE_BLOCKED bus=%s\n",
            error ? error->message : "unavailable");
    g_clear_error(&error);
    return 3;
  }
  GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus",
      "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner",
      g_variant_new("(s)", "org.tizen.sessiond"), G_VARIANT_TYPE("(s)"),
      G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, NULL, &error);
  if (!reply) {
    fprintf(stderr, "NATIVE_PROFILE_BLOCKED owner=%s\n",
            error ? error->message : "unavailable");
    g_clear_error(&error);
    g_object_unref(bus);
    return 3;
  }
  const char* owner = NULL;
  g_variant_get(reply, "(&s)", &owner);
  GVariant* current = g_dbus_connection_call_sync(bus, owner,
      "/org/tizen/sessiond", "org.tizen.sessiond.subsession.Manager",
      "GetCurrentUser", g_variant_new("(i)", (int)uid),
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000,
      NULL, &error);
  printf("NATIVE_PROFILE_READONLY owner=%s status=%s "
         "activation_not_tested=1\n", owner, current ? "reply" : "blocked");
  if (current)
    g_variant_unref(current);
  g_variant_unref(reply);
  g_clear_error(&error);
  g_object_unref(bus);
  return current ? 0 : 3;
}
