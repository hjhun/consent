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
#include <gio/gio.h>
#include <glib-unix.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROFILE_BUS "unix:path=/opt/var/lib/consent-smoke-authority/" \
                    "profile-bus.sock"
#define PROFILE_PATH "/org/tizen/sessiond"
#define PROFILE_INTERFACE "org.tizen.sessiond.subsession.Manager"

static GDBusConnection* bus;
static GMainLoop* loop;
static const char* current = "A";
static unsigned registrations;
static unsigned acknowledgements;
static char input[256];
static size_t buffered;
static int exit_status;
static const char xml[] =
    "<node><interface name='org.tizen.sessiond.subsession.Manager'>"
    "<method name='SwitchUserWait'><arg type='i' direction='in'/></method>"
    "<method name='RemoveUserWait'><arg type='i' direction='in'/></method>"
    "<method name='SwitchUserDone'><arg type='i' direction='in'/>"
    "<arg type='x' direction='in'/></method>"
    "<method name='RemoveUserDone'><arg type='i' direction='in'/>"
    "<arg type='s' direction='in'/></method>"
    "<method name='GetCurrentUser'><arg type='i' direction='in'/>"
    "<arg type='s' direction='out'/></method>"
    "</interface></node>";

static void method(GDBusConnection* connection, const char* sender,
                   const char* path, const char* interface, const char* name,
                   GVariant* params, GDBusMethodInvocation* invocation,
                   void* data) {
  (void)connection;
  (void)sender;
  (void)path;
  (void)interface;
  (void)params;
  (void)data;
  if (!strcmp(name, "GetCurrentUser")) {
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new("(s)", current));
    return;
  }
  if (!strcmp(name, "SwitchUserWait") || !strcmp(name, "RemoveUserWait"))
    ++registrations;
  else
    ++acknowledgements;
  g_dbus_method_invocation_return_value(invocation, NULL);
}

static int own(const char* method_name, const char* name) {
  GVariant* params = !strcmp(method_name, "RequestName")
      ? g_variant_new("(su)", name, 0u) : g_variant_new("(s)", name);
  GVariant* reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus",
      "/org/freedesktop/DBus", "org.freedesktop.DBus", method_name, params,
      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
  if (!reply)
    return 0;
  guint status = 0;
  g_variant_get(reply, "(u)", &status);
  g_variant_unref(reply);
  return status == 1;
}

static const char* user(const char* value) {
  if (!strcmp(value, "A")) return "A";
  if (!strcmp(value, "B")) return "B";
  if (!strcmp(value, "-")) return "";
  return NULL;
}

static int command(char* line) {
  char** words = g_strsplit(line, " ", -1);
  unsigned count = g_strv_length(words);
  int valid = 0;
  if (count == 2 && !strcmp(words[0], "current")) {
    const char* value = user(words[1]);
    if (value) {
      current = value;
      valid = 1;
    }
  } else if (count == 4 && (!strcmp(words[0], "started") ||
                            !strcmp(words[0], "completed"))) {
    char* end = NULL;
    errno = 0;
    gint64 id = g_ascii_strtoll(words[1], &end, 10);
    const char* previous = user(words[2]);
    const char* next = user(words[3]);
    if (!errno && end && !*end && id > 0 && previous && next) {
      valid = g_dbus_connection_emit_signal(bus, NULL, PROFILE_PATH,
          PROFILE_INTERFACE, !strcmp(words[0], "started")
              ? "SwitchUserStarted" : "SwitchUserCompleted",
          g_variant_new("(ixss)", 1, id, previous, next), NULL);
    }
  } else if (count == 2 && !strcmp(words[0], "remove")) {
    const char* value = user(words[1]);
    if (value)
      valid = g_dbus_connection_emit_signal(bus, NULL, PROFILE_PATH,
          PROFILE_INTERFACE, "RemoveUserStarted",
          g_variant_new("(is)", 1, value), NULL);
  } else if (count == 1 && !strcmp(words[0], "loss")) {
    valid = own("ReleaseName", "org.tizen.sessiond.fully_ready") &&
            own("ReleaseName", "org.tizen.sessiond");
  } else if (count == 1 && !strcmp(words[0], "ready")) {
    valid = own("RequestName", "org.tizen.sessiond") &&
            own("RequestName", "org.tizen.sessiond.fully_ready");
  } else if (count == 1 && !strcmp(words[0], "status")) {
    printf("STATE registrations=%u ack=%u current=%s\n",
           registrations, acknowledgements, *current ? current : "-");
    valid = 1;
  }
  if (valid) {
    g_dbus_connection_flush_sync(bus, NULL, NULL);
    printf("DONE %s\n", words[0]);
  }
  g_strfreev(words);
  return valid;
}

static gboolean receive(gint fd, GIOCondition condition, gpointer data) {
  (void)condition;
  (void)data;
  char bytes[256];
  ssize_t size = read(fd, bytes, sizeof(bytes));
  if (size < 0 && (errno == EINTR || errno == EAGAIN))
    return G_SOURCE_CONTINUE;
  if (size <= 0) {
    exit_status = buffered ? 2 : 0;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
  }
  for (ssize_t i = 0; i < size; ++i) {
    if (!bytes[i] || buffered == sizeof(input) - 1) {
      exit_status = 2;
      g_main_loop_quit(loop);
      return G_SOURCE_REMOVE;
    }
    if (bytes[i] == '\n') {
      input[buffered] = 0;
      if (!command(input)) {
        exit_status = 2;
        g_main_loop_quit(loop);
        return G_SOURCE_REMOVE;
      }
      buffered = 0;
    } else {
      input[buffered++] = bytes[i];
    }
  }
  return G_SOURCE_CONTINUE;
}

int main(int argc, char** argv) {
  if (argc != 1) {
    fprintf(stderr, "Usage: %s (owned private fixture bus only)\n", argv[0]);
    return 2;
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  GError* error = NULL;
  bus = g_dbus_connection_new_for_address_sync(PROFILE_BUS,
      G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
      G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &error);
  if (!bus) {
    fprintf(stderr, "FAIL profile fixture bus: %s\n",
            error ? error->message : "unavailable");
    g_clear_error(&error);
    return 1;
  }
  GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(xml, NULL);
  GDBusInterfaceVTable vtable = {method, NULL, NULL, {NULL}};
  guint object = g_dbus_connection_register_object(bus, PROFILE_PATH,
      info->interfaces[0], &vtable, NULL, NULL, NULL);
  if (!object || !own("RequestName", "org.tizen.sessiond") ||
      !own("RequestName", "org.tizen.sessiond.fully_ready"))
    return 1;
  loop = g_main_loop_new(NULL, FALSE);
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
  guint source = g_unix_fd_add(STDIN_FILENO, G_IO_IN | G_IO_HUP, receive, NULL);
  puts("READY profile-fixture");
  g_main_loop_run(loop);
  GSource* remaining = g_main_context_find_source_by_id(NULL, source);
  if (remaining)
    g_source_destroy(remaining);
  g_dbus_connection_unregister_object(bus, object);
  g_dbus_node_info_unref(info);
  g_dbus_connection_close_sync(bus, NULL, NULL);
  g_object_unref(bus);
  g_main_loop_unref(loop);
  return exit_status;
}
