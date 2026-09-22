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
using System.Globalization;

namespace ConsentUI;

// Labels are defined only for the protected PoC catalog's exact tuples. This
// layer never changes a request field, registered message, or approval token.
internal static class FeatureDisplay
{
  public static string Metadata(Feature feature, IReadOnlyDictionary<string, string> mapping,
      string period, bool korean)
  {
    if (!feature.Mappings.Any(item => item.Values.Count == mapping.Count &&
        item.Values.All(field => mapping.TryGetValue(field.Key, out string? value) && value == field.Value)))
      throw new InvalidOperationException("Display tuple is not in the protected catalog");
    string operation, scope, purpose, recipient;
    if (feature.Id == "calendar.read" && mapping["operation"] == "read" &&
        mapping["purpose"] == "conversation-summary" && mapping["recipient"] == "local-conversation" &&
        mapping["holder"] == "mock-holder")
    {
      operation = korean ? "달력 읽기" : "Read calendar";
      scope = mapping["scope"] switch
      {
        "calendar.default.next7days" => korean ? "향후 7일" : "Next 7 days",
        "calendar.default.next30days" => korean ? "향후 30일" : "Next 30 days",
        _ => throw new InvalidOperationException("Unknown calendar display scope"),
      };
      purpose = korean ? "대화 요약" : "Conversation summary";
      recipient = korean ? "이 기기의 현재 대화" : "Current conversation on this device";
    }
    else if (feature.Id == "device.control" && mapping["operation"] == "control" &&
        mapping["scope"] == "device.study-lamp.action.off.impact.study-dark" &&
        mapping["purpose"] == "requested-device-control" && mapping["recipient"] == "local-device" &&
        mapping["holder"] == "")
    {
      operation = korean ? "기기 제어" : "Control device";
      scope = korean ? "서재 조명을 끔(서재가 어두워짐)" : "Turn off study lamp (study becomes dark)";
      purpose = korean ? "사용자가 요청한 기기 제어" : "User-requested device control";
      recipient = korean ? "이 기기의 서재 조명" : "Study lamp on this device";
    }
    else throw new InvalidOperationException("Unknown protected display mapping");
    string name = korean ? feature.TitleKorean : feature.TitleEnglish;
    string retention = Retention(mapping["retention_ms"], korean);
    return korean ?
        $"기능: {name}\n제공 앱: {feature.Provider}\n동작: {operation}\n범위: {scope}\n목적: {purpose}\n수신자: {recipient}\n접근 승인 기간: {period}\n취득 데이터 보관: {retention}" :
        $"Feature: {name}\nProvider app: {feature.Provider}\nOperation: {operation}\nScope: {scope}\nPurpose: {purpose}\nRecipient: {recipient}\nAccess approval period: {period}\nAcquired-data retention: {retention}";
  }

  private static string Retention(string value, bool korean)
  {
    if (!long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long milliseconds) ||
        milliseconds < 0 || milliseconds > 86400000 || milliseconds.ToString(CultureInfo.InvariantCulture) != value)
      throw new InvalidOperationException("Invalid retention duration");
    if (milliseconds == 0) return korean ? "결과 보관 없음" : "No result retention";
    string duration = milliseconds % 60000 == 0 ? (korean ? $"{milliseconds / 60000}분" :
        $"{milliseconds / 60000} {(milliseconds == 60000 ? "minute" : "minutes")}") :
        milliseconds % 1000 == 0 ? (korean ? $"{milliseconds / 1000}초" :
        $"{milliseconds / 1000} {(milliseconds == 1000 ? "second" : "seconds")}") :
        (korean ? $"{milliseconds}밀리초" : $"{milliseconds} milliseconds");
    return korean ? $"최대 {duration}. 대화 종료나 접근 승인 취소 시 더 일찍 종료됩니다." :
        $"Up to {duration}; ends earlier when the conversation closes or access is revoked.";
  }
}
