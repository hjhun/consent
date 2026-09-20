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
#ifndef CONSENTD_REPOSITORY_HH_
#define CONSENTD_REPOSITORY_HH_

#include <functional>
#include <memory>
#include <string>

#include "common/message.hh"
#include "identity.hh"

namespace consentd {

// Every method, including destruction, belongs to the serialized DB executor.
// There are no callbacks into other daemon threads while a transaction is held.
class Repository final {
 public:
  using InstallationValidator = std::function<bool(const std::string&,
      const std::string&, const std::string&)>;

  Repository(std::string path, std::string recovery_dir);
  ~Repository();
  Repository(const Repository&) = delete;
  Repository& operator=(const Repository&) = delete;

  void SetInstallationValidator(InstallationValidator validator);
  void SetPackageGenerationValidator(
      std::function<bool(const std::string&, const std::string&)> validator);
  bool Open(std::string* error);
  consent::Message Execute(const Peer& peer, const consent::Message& request);
  consent::Message Snapshot();
  void Tick();
  void Shutdown();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace consentd
#endif  // CONSENTD_REPOSITORY_HH_
