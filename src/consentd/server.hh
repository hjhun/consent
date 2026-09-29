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

#ifndef CONSENTD_SERVER_HH_
#define CONSENTD_SERVER_HH_

#include <gio/gio.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "common/message.hh"
#include "common/dispatch.hh"
#include "identity.hh"

namespace consentd {

class Repository;
class Server {
  friend class ServerTestPeer;

 public:
  Server();
  ~Server();
  int Run(int listener_fd);

 private:
  struct Connection;
  struct ParseJob;
  void AddConnection(GSocketConnection* stream);
  void ArmRead(const std::shared_ptr<Connection>& connection);
  void Receive(const std::shared_ptr<Connection>& connection);
  void Parse(const std::shared_ptr<Connection>& connection,
             std::vector<uint8_t> payload);
  void Execute(const std::shared_ptr<Connection>& connection,
               consent::Message request);
  void Queue(const std::shared_ptr<Connection>& connection,
             const consent::Message& message);
  void Write(const std::shared_ptr<Connection>& connection);
  void Close(const std::shared_ptr<Connection>& connection,
             const char* reason) noexcept;
  void Publish(const consent::Message& snapshot);
  void InitializeWakeSources();
  void StartThreads();
  void Stop() noexcept;
  void StopIo() noexcept;
  void ShutdownDatabase() noexcept;
  void DispatchFailed() noexcept;
  bool Submit(std::function<void()> work);
  bool Post(GMainContext* context, std::function<void()> work) noexcept;

  GMainContext* main_context_ = nullptr;
  GMainLoop* main_loop_ = nullptr;
  GMainContext* io_context_ = nullptr;
  GMainLoop* io_loop_ = nullptr;
  GSocketService* listener_ = nullptr;
  GThread* io_thread_ = nullptr;
  GThread* db_thread_ = nullptr;
  GAsyncQueue* db_queue_ = nullptr;
  GThreadPool* parser_pool_ = nullptr;
  consent::Dispatcher dispatcher_;
  GSource* io_quit_source_ = nullptr;
  GSource* emergency_source_ = nullptr;
  GSource* stop_io_source_ = nullptr;
  GSource* finished_source_ = nullptr;
  std::unique_ptr<std::function<void()>> shutdown_job_;
  std::function<void()> db_stop_;
  GSource* tick_ = nullptr;
  GSource* sigterm_ = nullptr;
  GSource* sigint_ = nullptr;
  IdentityPolicy identity_;
  std::unique_ptr<Repository> repository_;
  std::map<uint64_t, std::shared_ptr<Connection>> clients_;
  std::atomic<unsigned> connections_{0};
  std::atomic<unsigned> db_jobs_{0};
  std::atomic<unsigned> parse_jobs_{0};
  std::atomic<bool> stopping_{false};
  uint64_t next_client_ = 0;
  consent::Message published_;
  int exit_status_ = 0;
  int lifecycle_lock_ = -1;
};

}  // namespace consentd
#endif  // CONSENTD_SERVER_HH_
