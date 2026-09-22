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
#include "feature_common.hh"
#include "feature_api.hh"

#include <cstdio>
#include <cstring>

extern "C" int consent_feature_register_main(const char* generation) {
  using namespace consent_mock;
  if (!generation || strnlen(generation, 129) > 128 || !Identifier(generation)) return 1;
  consent_client_h client = nullptr;
  int status = consent_client_create(&client);
  if (status) return 1;
  try {
    for (const auto& feature : Catalog()) {
      Message definition{{"definition", consent::Get(feature.row, "definition")},
          {"policy_version", "1"}, {"text_revision", "1"}, {"level", "1"},
          {"enforcer", "mock-" + feature.worker}, {"modes", "ONCE,SESSION,TIMED"},
          {"retention_ms", feature.id == "calendar.read" ? "1800000" : "0"},
          {"default_locale", "en"}, {"locale_fallback.en-US", "en"}, {"locale_fallback.ko-KR", "ko"},
          {"message.en.title", feature.title_en},
          {"message.ko.title", feature.title_ko},
          {"message.en.body", "Review the displayed operation, scope, purpose, recipient and approval period. " + std::string(feature.id == "calendar.read" ? "Mock data remains in this conversation for at most 30 minutes." : "Only the displayed study-lamp target and loss-of-light impact are permitted.")},
          {"message.ko.body", "표시된 동작, 범위, 목적, 수신자와 승인 기간을 확인해 주세요. " + std::string(feature.id == "calendar.read" ? "모의 데이터는 현재 대화에서 최대 30분 보관합니다." : "표시된 서재 조명 대상과 조명이 꺼지는 영향만 허용합니다.")},
          {"expected_generation", generation},
          {"operation_id", "feature-register-v1-" + feature.id + '-' + generation}};
      auto parameters = Parameters(definition);
      status = consent_register(client, "org.tizen.consentui", "org.tizen.consentui", parameters.get());
      std::printf("{\"event\":\"feature-register\",\"feature\":%s,\"status\":%d}\n", Json(feature.id).c_str(), status);
      if (status) break;
    }
  } catch (...) { status = -EINVAL; }
  consent_client_destroy(client);
  return status ? 1 : 0;
}
