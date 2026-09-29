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
#include "dispatch.hh"

#include "common/logging.hh"
#include "common/resource.hh"

#include <set>
#include <utility>

namespace consent {
struct Dispatcher::State {
  struct Job {
    std::shared_ptr<State> state;
    std::function<void()> work;
    GSource* source = nullptr;
  };
  State() { g_mutex_init(&mutex); }
  ~State() { g_mutex_clear(&mutex); }
  void Remove(GSource* source) noexcept {
    bool removed;
    {
      MutexLock lock(mutex);
      removed = sources.erase(source) != 0;
    }
    if (removed) g_source_unref(source);
  }
  GMutex mutex;
  std::set<GSource*> sources;
  bool accepting = true;
};

Dispatcher::Dispatcher() : state_(std::make_shared<State>()) {}
Dispatcher::~Dispatcher() { Cancel(); }

bool Dispatcher::Post(GMainContext* context,
                      std::function<void()> work) noexcept {
  try {
    auto job = std::make_unique<State::Job>(
        State::Job{state_, std::move(work), nullptr});
    Source source(g_idle_source_new());
    job->source = source.get();
    g_source_set_callback(
        source.get(),
        [](gpointer data) noexcept -> gboolean {
          try {
            static_cast<State::Job*>(data)->work();
          } catch (...) {
            LOG(ERROR) << "event=dispatch-failed reason=exception";
          }
          return G_SOURCE_REMOVE;
        },
        job.release(),
        [](gpointer data) {
          auto* job = static_cast<State::Job*>(data);
          job->state->Remove(job->source);
          delete job;
        });
    {
      MutexLock lock(state_->mutex);
      if (!state_->accepting || state_->sources.size() >= 512) return false;
      state_->sources.insert(source.get());
      g_source_ref(source.get());
    }
    if (!g_source_attach(source.get(), context)) return false;
    g_source_unref(source.release());
    return true;
  } catch (...) {
    return false;
  }
}

void Dispatcher::Cancel() noexcept {
  std::set<GSource*> sources;
  {
    MutexLock lock(state_->mutex);
    state_->accepting = false;
    sources.swap(state_->sources);
  }
  for (auto* source : sources) {
    g_source_destroy(source);
    g_source_unref(source);
  }
}

}  // namespace consent
