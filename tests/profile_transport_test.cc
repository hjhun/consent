/*
 * Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
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
#include "consentd/profile_authority.hh"
#include <gio/gio.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>

static_assert(!std::is_copy_constructible<consentd::ProfileAuthority>::value);
static_assert(!std::is_copy_assignable<consentd::ProfileAuthority>::value);
static_assert(!std::is_move_constructible<consentd::ProfileAuthority>::value);
static_assert(!std::is_move_assignable<consentd::ProfileAuthority>::value);

namespace {

constexpr char kPath[] = "/org/tizen/sessiond";
constexpr char kInterface[] = "org.tizen.sessiond.subsession.Manager";
constexpr char kXml[] =
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

void Check(bool condition, const char* reason) {
  if (!condition)
    throw std::runtime_error(reason);
}

void Until(const std::function<bool()>& predicate, unsigned timeout = 3000) {
  const auto end = g_get_monotonic_time() + timeout * 1000;
  while (!predicate() && g_get_monotonic_time() < end) {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    g_usleep(1000);
  }
  Check(predicate(), "bounded adapter wait");
}

void Drain(unsigned time = 50) {
  const auto end = g_get_monotonic_time() + time * 1000;
  while (g_get_monotonic_time() < end) {
    while (g_main_context_iteration(nullptr, FALSE)) {}
    g_usleep(1000);
  }
}

GDBusConnection* Connect(const char* address) {
  GError* error = nullptr;
  auto* connection = g_dbus_connection_new_for_address_sync(address,
      static_cast<GDBusConnectionFlags>(
          G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
          G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
      nullptr, nullptr, &error);
  g_clear_error(&error);
  Check(connection != nullptr, "private bus connection");
  g_dbus_connection_set_exit_on_close(connection, FALSE);
  return connection;
}

void Name(GDBusConnection* connection, const char* method, const char* name) {
  GVariant* params = !strcmp(method, "RequestName")
      ? g_variant_new("(su)", name, 0u) : g_variant_new("(s)", name);
  auto* reply = g_dbus_connection_call_sync(connection,
      "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", method, params, G_VARIANT_TYPE("(u)"),
      G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
  Check(reply != nullptr, "name ownership call");
  g_variant_unref(reply);
}

struct Fake {
  GDBusConnection* connection = nullptr;
  std::string current = "A";
  unsigned registrations = 0;
  unsigned acknowledgements = 0;
  bool delay_registration = false;
  bool delay_read = false;
  bool fail_registration = false;
  bool delay_ack = false;
  std::vector<GDBusMethodInvocation*> held;

  void Signal(const char* name, GVariant* params) {
    Check(g_dbus_connection_emit_signal(connection, nullptr, kPath,
        kInterface, name, params, nullptr), "fake manager signal");
    g_dbus_connection_flush_sync(connection, nullptr, nullptr);
  }

  static void Method(GDBusConnection*, const gchar*, const gchar*,
                     const gchar*, const gchar* method, GVariant*,
                     GDBusMethodInvocation* invocation, gpointer data) {
    auto* fake = static_cast<Fake*>(data);
    if (!strcmp(method, "SwitchUserWait") ||
        !strcmp(method, "RemoveUserWait")) {
      ++fake->registrations;
      if (fake->fail_registration) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            "org.tizen.Error.PermissionDenied", "registration denied");
        return;
      }
      if (fake->delay_registration) {
        fake->held.push_back(
            G_DBUS_METHOD_INVOCATION(g_object_ref(invocation)));
        return;
      }
    } else if (!strcmp(method, "GetCurrentUser")) {
      if (fake->delay_read) {
        fake->held.push_back(
            G_DBUS_METHOD_INVOCATION(g_object_ref(invocation)));
        return;
      }
      g_dbus_method_invocation_return_value(invocation,
          g_variant_new("(s)", fake->current.c_str()));
      return;
    } else {
      ++fake->acknowledgements;
      if (fake->delay_ack) {
        fake->held.push_back(
            G_DBUS_METHOD_INVOCATION(g_object_ref(invocation)));
        return;
      }
    }
    g_dbus_method_invocation_return_value(invocation, nullptr);
  }
};

void Test() {
  auto* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(bus);
  const char* address = g_test_dbus_get_bus_address(bus);
  auto* server = Connect(address);
  auto* client = Connect(address);
  auto* forged = Connect(address);
  auto* info = g_dbus_node_info_new_for_xml(kXml, nullptr);
  GDBusInterfaceVTable vtable = {Fake::Method, nullptr, nullptr, {nullptr}};
  Fake fake;
  fake.connection = server;
  guint object = g_dbus_connection_register_object(server, kPath,
      info->interfaces[0], &vtable, &fake, nullptr, nullptr);
  Check(object != 0, "fake manager object");
  Name(server, "RequestName", "org.tizen.sessiond");
  Name(server, "RequestName", "org.tizen.sessiond.fully_ready");
  auto state = std::make_shared<consentd::ProfileState>();
  Check(state->Configure({{"agent", "A", "profile.A"},
                          {"agent", "B", "profile.B"},
                          {"agent", "", "profile.default"}}), "profile map");
  unsigned retire = 0;
  unsigned barriers = 0;
  std::string removed;
  bool hold_barrier = false;
  bool throw_barrier = false;
  std::vector<consentd::ProfileAuthority::Done> pending;
  auto barrier = [&](bool gap, const std::string& user,
                     consentd::ProfileAuthority::Done done) {
    ++barriers;
    retire += gap;
    removed = user;
    if (throw_barrier) {
      pending.push_back(std::move(done));
      throw std::runtime_error("fixture barrier failure");
    }
    if (hold_barrier)
      pending.push_back(std::move(done));
    else
      done(true);
  };
  auto authority = std::make_unique<consentd::ProfileAuthority>(
      state, 1, barrier, client, 150);
  authority->Start();
  Until([&] { return state->Check("agent", "profile.A") == 0; });
  Check(fake.registrations == 2 && retire == 1, "registered initial gap");
  Check(state->Check("agent", "profile.B") == -EACCES, "inactive B");
  authority->Start();
  Drain();
  Check(fake.registrations == 2, "Start does not duplicate watchers");
  const auto initial_generation = state->Generation();
  Fake impostor;
  impostor.connection = forged;
  impostor.Signal("SwitchUserStarted", g_variant_new("(ixss)", 1,
                   static_cast<gint64>(99), "A", "B"));
  Drain();
  Check(state->Generation() == initial_generation, "forged sender ignored");
  fake.Signal("RemoveUserStarted", g_variant_new("(is)", 1, "B"));
  Until([&] { return fake.acknowledgements == 1 &&
                     state->Check("agent", "profile.A") == 0; });
  Check(removed.empty() && retire == 1, "removal does not gap-retire A");
  fake.Signal("SwitchUserStarted", g_variant_new("(ixss)", 1,
                static_cast<gint64>(1), "A", "B"));
  // Actual manager changes physical state before waiting for client ACK.
  fake.current = "B";
  Until([&] { return fake.acknowledgements == 2; });
  Check(state->Check("agent", "profile.A") == -EBUSY, "Started fence");
  fake.Signal("SwitchUserCompleted", g_variant_new("(ixss)", 1,
                static_cast<gint64>(1), "A", "B"));
  Until([&] { return state->Check("agent", "profile.B") == 0; });
  Check(state->Check("agent", "profile.A") == -EACCES, "A inactive");

  hold_barrier = true;
  Name(server, "ReleaseName", "org.tizen.sessiond.fully_ready");
  Until([&] { return !pending.empty(); });
  Check(state->Check("agent", "profile.B") == -EBUSY, "owner loss fenced");
  authority->Stop();
  const auto stopped = state->Generation();
  for (auto& done : pending)
    done(true);
  pending.clear();
  Drain();
  Check(state->Generation() == stopped, "late loss completion after Stop");
  authority.reset();
  hold_barrier = false;
  Name(server, "RequestName", "org.tizen.sessiond.fully_ready");

  fake.delay_registration = true;
  authority = std::make_unique<consentd::ProfileAuthority>(
      state, 1, barrier, client, 150);
  const auto registered = fake.registrations;
  authority->Start();
  Until([&] { return fake.registrations > registered; });
  const auto before_other_uid = state->Generation();
  fake.Signal("SwitchUserStarted", g_variant_new("(ixss)", 2,
                static_cast<gint64>(2), "", "B"));
  Drain();
  Check(state->Generation() == before_other_uid &&
        fake.registrations == registered + 1,
        "other-account event ignored during registration");
  fake.Signal("SwitchUserStarted", g_variant_new("(ixss)", 1,
                static_cast<gint64>(2), "", "B"));
  Drain();
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "early Started cannot bypass initial retirement");
  for (auto* invocation : fake.held) {
    g_dbus_method_invocation_return_value(invocation, g_variant_new("()"));
    g_object_unref(invocation);
  }
  fake.held.clear();
  Drain(200);
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "stale successful registration cannot activate");
  authority->Stop();
  authority.reset();
  auto release_held = [&] {
    for (auto* invocation : fake.held) {
      g_dbus_method_invocation_return_dbus_error(invocation,
          "org.tizen.Error.Canceled", "fixture cancellation");
      g_object_unref(invocation);
    }
    fake.held.clear();
    Drain(200);
  };
  fake.delay_registration = false;
  fake.fail_registration = true;
  authority = std::make_unique<consentd::ProfileAuthority>(
      state, 1, barrier, client, 150);
  unsigned previous_retire = retire;
  authority->Start();
  Until([&] { return retire > previous_retire; });
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "failed registration cannot activate");
  authority->Stop();
  authority.reset();
  fake.fail_registration = false;
  fake.delay_read = true;
  authority = std::make_unique<consentd::ProfileAuthority>(
      state, 1, barrier, client, 150);
  authority->Start();
  Until([&] { return !fake.held.empty(); });
  fake.Signal("SwitchUserStarted", g_variant_new("(ixss)", 1,
                static_cast<gint64>(3), "B", "A"));
  Drain();
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "Started invalidates outstanding authoritative initial read");
  for (auto* invocation : fake.held) {
    g_dbus_method_invocation_return_value(invocation,
                                         g_variant_new("(s)", "B"));
    g_object_unref(invocation);
  }
  fake.held.clear();
  Drain(200);
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "stale initial read cannot reopen authority");
  authority->Stop();
  authority.reset();
  fake.delay_read = false;
  fake.current = "A";
  authority = std::make_unique<consentd::ProfileAuthority>(
      state, 1, barrier, client, 150);
  authority->Start();
  Until([&] { return state->Check("agent", "profile.A") == 0; });
  fake.delay_ack = true;
  fake.Signal("SwitchUserStarted", g_variant_new("(ixss)", 1,
                static_cast<gint64>(4), "A", "B"));
  fake.current = "B";
  Until([&] { return !fake.held.empty(); });
  fake.Signal("SwitchUserCompleted", g_variant_new("(ixss)", 1,
                static_cast<gint64>(4), "A", "B"));
  Drain(30);
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "completion before ACK response remains fenced");
  previous_retire = retire;
  Until([&] { return retire > previous_retire; });
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "bounded ACK timeout cannot publish READY");
  authority->Stop();
  release_held();
  authority.reset();
  fake.delay_ack = false;
  // A real GetCurrentUser completion invokes a throwing barrier. The
  // noexcept method adapter must Stop rather than escape across GLib.
  fake.current = "A";
  throw_barrier = true;
  authority = std::make_unique<consentd::ProfileAuthority>(
      state, 1, barrier, client, 150);
  authority->Start();
  Until([&] { return !pending.empty(); });
  Check(state->Check("agent", "profile.A") == -EBUSY,
        "adapter exception fences authority");
  const auto failed_generation = state->Generation();
  const auto failed_registrations = fake.registrations;
  for (auto& done : pending)
    done(true);
  pending.clear();
  authority->Start();
  Drain(200);
  Check(state->Generation() == failed_generation &&
        fake.registrations == failed_registrations &&
        state->Check("agent", "profile.A") == -EBUSY,
        "stale callbacks cannot activate after adapter exception");
  authority.reset();
  throw_barrier = false;

  Name(server, "ReleaseName", "org.tizen.sessiond.fully_ready");
  Name(forged, "RequestName", "org.tizen.sessiond.fully_ready");
  authority = std::make_unique<consentd::ProfileAuthority>(
      state, 1, barrier, client, 150);
  previous_retire = retire;
  authority->Start();
  Until([&] { return retire > previous_retire; });
  Check(state->Check("agent", "profile.B") == -EBUSY,
        "primary/ready owner mismatch cannot activate");
  authority->Stop();
  authority.reset();
  Name(forged, "ReleaseName", "org.tizen.sessiond.fully_ready");
  g_dbus_connection_unregister_object(server, object);
  g_dbus_node_info_unref(info);
  for (auto* connection : {client, forged, server}) {
    g_dbus_connection_close_sync(connection, nullptr, nullptr);
    g_object_unref(connection);
  }
  g_test_dbus_down(bus);
  g_object_unref(bus);
  std::puts("PASS authenticated native profile transport and lifecycle");
}

}  // namespace

int main() {
  try {
    Test();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL profile transport: %s\n", error.what());
    return 1;
  }
}
