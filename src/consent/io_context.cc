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
#include "io_context.hh"

#include "common/resource.hh"

#include <unistd.h>

#include <utility>

namespace consent {
namespace {
GSource* ReadySource(GMainContext* context, GSourceFunc function, void* data) {
  static GSourceFuncs functions = {
      nullptr,
      nullptr,
      [](GSource*, GSourceFunc callback, gpointer argument) -> gboolean {
        return callback(argument);
      },
      nullptr,
      nullptr,
      nullptr};
  auto* source = g_source_new(&functions, sizeof(GSource));
  g_source_set_callback(source, function, data, nullptr);
  g_source_set_ready_time(source, -1);
  g_source_attach(source, context);
  return source;
}
void Destroy(GSource*& source) noexcept {
  if (!source) return;
  g_source_destroy(source);
  g_source_unref(source);
  source = nullptr;
}
}  // namespace

IoContext::IoContext(std::function<void()> work, std::function<void()> retire)
    : pid_(getpid()), work_(std::move(work)), retire_(std::move(retire)) {
  context_ = g_main_context_new();
  loop_ = g_main_loop_new(context_, FALSE);
  wake_ = ReadySource(context_, Dispatch, this);
  deadline_ = ReadySource(context_, Dispatch, this);
}
IoContext::~IoContext() {
  if (pid_ != getpid()) {
    Abandon();
    return;
  }
  Stop();
  Join();
  Destroy(wake_);
  Destroy(deadline_);
  g_main_loop_unref(loop_);
  g_main_context_unref(context_);
}
void IoContext::Abandon() noexcept {
  // Do not invoke any inherited GLib destructor, lock or join in a child.
  context_ = nullptr;
  loop_ = nullptr;
  wake_ = nullptr;
  deadline_ = nullptr;
  thread_ = nullptr;
}
bool IoContext::Start() noexcept {
  if (pid_ != getpid() || thread_) return false;
  GError* error = nullptr;
  thread_ = g_thread_try_new(
      "consent-io",
      [](gpointer data) -> gpointer {
        auto* self = static_cast<IoContext*>(data);
        g_main_context_push_thread_default(self->context_);
        g_main_loop_run(self->loop_);
        g_main_context_pop_thread_default(self->context_);
        return nullptr;
      },
      this, &error);
  g_clear_error(&error);
  return thread_ != nullptr;
}
void IoContext::Wake() noexcept {
  if (pid_ != getpid()) return;
  g_source_set_ready_time(wake_, 0);
  g_main_context_wakeup(context_);
}
void IoContext::Stop() noexcept {
  if (pid_ != getpid()) return;
  stopping_.store(true);
  Wake();
}
void IoContext::Join() noexcept {
  if (pid_ != getpid() || !thread_) return;
  // Delivery belongs to a different context; no user callback runs here.
  g_assert(thread_ != g_thread_self());
  g_thread_join(thread_);
  thread_ = nullptr;
}
void IoContext::Deadline(gint64 when) noexcept {
  if (pid_ == getpid()) g_source_set_ready_time(deadline_, when);
}
gboolean IoContext::Dispatch(gpointer data) noexcept {
  auto* self = static_cast<IoContext*>(data);
  if (self->pid_ != getpid()) return G_SOURCE_REMOVE;
  // Disarm before inspecting work. An admission after the snapshot retains
  // its new ready_time=0; callback return never overwrites that signal.
  g_source_set_ready_time(g_main_current_source(), -1);
  try {
    if (self->stopping_.load()) {
      try {
        self->retire_();
      } catch (...) {
      }
      g_main_loop_quit(self->loop_);
    } else {
      self->work_();
    }
  } catch (...) {
    self->Stop();
  }
  return G_SOURCE_CONTINUE;
}
bool IoContext::Wait(GSocket* socket, unsigned timeout_ms) {
  if (pid_ != getpid() || thread_ || stopping_.load()) return false;
  struct Waiter {
    GMainLoop* loop;
    bool ready = false;
  } waiter{loop_};
  Source ready(g_socket_create_source(
      socket, static_cast<GIOCondition>(G_IO_OUT | G_IO_ERR | G_IO_HUP),
      nullptr));
  g_source_set_callback(
      ready.get(),
      G_SOURCE_FUNC(+[](GSocket*, GIOCondition, gpointer data) -> gboolean {
        auto* waiter = static_cast<Waiter*>(data);
        waiter->ready = true;
        g_main_loop_quit(waiter->loop);
        return G_SOURCE_REMOVE;
      }),
      &waiter, nullptr);
  Source timeout(g_timeout_source_new(timeout_ms));
  g_source_set_callback(
      timeout.get(),
      [](gpointer data) -> gboolean {
        g_main_loop_quit(static_cast<Waiter*>(data)->loop);
        return G_SOURCE_REMOVE;
      },
      &waiter, nullptr);
  g_source_attach(ready.get(), context_);
  g_source_attach(timeout.get(), context_);
  g_main_context_push_thread_default(context_);
  g_main_loop_run(loop_);
  g_main_context_pop_thread_default(context_);
  return waiter.ready;
}
}  // namespace consent
