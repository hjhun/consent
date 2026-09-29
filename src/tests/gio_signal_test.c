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
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static void Handler(int signal_number) {
  (void)signal_number;
}
int main(void) {
  // This executable is a dedicated process-global behavior probe. It never
  // saves/restores a handler around application GIO calls in production.
  struct sigaction before = {0}, after = {0};
  before.sa_handler = Handler;
  sigemptyset(&before.sa_mask);
  if (sigaction(SIGPIPE, &before, NULL))
    return 1;
  GError* error = NULL;
  GSocket* socket = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM,
                                 G_SOCKET_PROTOCOL_DEFAULT, &error);
  if (!socket) {
    g_clear_error(&error);
    return 1;
  }
  int result = sigaction(SIGPIPE, NULL, &after);
  printf(
      "actor_pid=%ld GLib=%u.%u.%u SIGPIPE_before=custom "
      "SIGPIPE_after=%s\n",
      (long)getpid(), glib_major_version, glib_minor_version,
      glib_micro_version, after.sa_handler == SIG_IGN ? "SIG_IGN" : "other");
  g_object_unref(socket);
  return result || after.sa_handler != SIG_IGN;
}
