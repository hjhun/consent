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
#include "common/localization.hh"

#include "consent/client.hh"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using consent::Get;
using consent::Message;
namespace locale = consent::localization;

void Check(bool condition, const char* description) {
  if (!condition)
    throw std::runtime_error(description);
}

Message Definition() {
  return {{"template_version", "1"}, {"default_locale", "en"},
      {"retention_ms", "60000"},
      {"parameter.count.source", "scope"}, {"parameter.count.type", "integer"},
      {"parameter.count.min", "-5"}, {"parameter.count.max", "20"},
      {"parameter.recipient.source", "recipient"}, {"parameter.recipient.type", "string"},
      {"parameter.recipient.max_bytes", "64"},
      {"parameter.retention.source", "retention_ms"}, {"parameter.retention.type", "integer"},
      {"parameter.retention.min", "0"}, {"parameter.retention.max", "86400000"},
      {"message.en.title", "Read {count} records"},
      {"message.en.body", "Send to {recipient}; retain {retention} ms"},
      {"message.ko.title", "기록 {count}개 읽기"},
      {"message.ko.body", "수신자 {recipient}; 보관 {retention} ms"}};
}

Message Request() {
  return {{"count", "1"}, {"r0.scope", "3"}, {"r0.operation", "read"},
      {"r0.purpose", "answer"}, {"r0.recipient", "sink{count}%<tag>"}};
}

Message Prompt(const Message& definition, const Message& request,
    const std::string& language = "en") {
  Message result{{"count", "1"}, {"r0.title", Get(definition, "message." + language + ".title")},
      {"r0.body", Get(definition, "message." + language + ".body")}};
  Check(locale::AppendArguments(definition, request, 0, &result), "append typed arguments");
  return result;
}

void DefinitionValidation() {
  Check(locale::ValidateDefinition(Definition()), "valid typed definition");
  for (const auto& mutation : std::vector<std::pair<std::string, std::string>>{{"template_version", "2"},
      {"parameter.count.min", "-05"}, {"parameter.count.max", "-6"},
      {"parameter.count.max", "9223372036854775808"},
      {"parameter.count.type", "float"}, {"parameter.count.source", "purpose"},
      {"parameter.recipient.max_bytes", "513"}, {"parameter.recipient.source", "retention_ms"},
      {"parameter.retention.max", "59999"}, {"parameter.count.unknown", "x"},
      {"message.en.title", "Missing {undefined}"}, {"message.en.body", "No arguments"},
      {"message.en.title", "{count:03}"}}) {
    auto definition = Definition();
    definition[mutation.first] = mutation.second;
    Check(!locale::ValidateDefinition(definition), "invalid schema or placeholder rejected");
  }
  for (const char* key : {"parameter.count.min", "parameter.count.source", "message.ko.body"}) {
    auto definition = Definition();
    definition.erase(key);
    Check(!locale::ValidateDefinition(definition), "required schema/template field rejected when missing");
  }
  auto definition = Definition();
  definition.erase("retention_ms");
  Check(locale::ValidateDefinition(definition), "retention source uses policy's actual default zero");
  definition["parameter.retention.min"] = "1";
  Check(!locale::ValidateDefinition(definition), "default retention must satisfy static bounds");
  for (const auto& text : {std::string("{count"), std::string("count}"), std::string("{{count}}"),
      std::string("{}"), std::string("{count.field}"), std::string(4097, 'a'),
      std::string("bad\0text", 8), std::string("\xff", 1)}) {
    definition = Definition();
    definition["message.en.title"] = text;
    Check(!locale::ValidateDefinition(definition), "malformed or oversized UTF-8 template rejected");
  }
  definition = Definition();
  definition["message.en.title"] = "{count} then {count}";
  Check(locale::ValidateDefinition(definition), "repeated placeholders are allowed");
  definition = {{"template_version", "1"}, {"default_locale", "en"},
      {"message.en.title", "All values"}, {"message.en.body", ""},
      {"message.ko.title", "모든 값"}, {"message.ko.body", ""}};
  for (unsigned index = 0; index < 8; ++index) {
    auto name = "extra" + std::to_string(index);
    auto prefix = "parameter." + name + ".";
    definition[prefix + "source"] = "scope";
    definition[prefix + "type"] = "string";
    definition[prefix + "max_bytes"] = "32";
    for (const char* language : {"en", "ko"})
      definition[std::string("message.") + language + ".body"] += "{" + name + "}";
  }
  Check(locale::ValidateDefinition(definition), "exactly eight complete schema/template parameters accepted");
  auto eight = Prompt(definition, Request());
  std::string eight_rendered;
  Check(Get(eight, "r0.arg_count") == "8" &&
      locale::FormatPrompt(eight, 0, "body", &eight_rendered) && eight_rendered == "33333333",
      "all eight declared arguments are emitted and rendered");
  definition["parameter.extra8.source"] = "scope";
  definition["parameter.extra8.type"] = "string";
  definition["parameter.extra8.max_bytes"] = "32";
  for (const char* language : {"en", "ko"})
    definition[std::string("message.") + language + ".body"] += "{extra8}";
  Check(!locale::ValidateDefinition(definition), "ninth matching parameter rejected solely by count limit");
  definition = Definition();
  definition["parameter.0bad.source"] = "scope";
  Check(!locale::ValidateDefinition(definition), "parameter names use ASCII identifiers");
  Check(locale::IsDefinitionField("locale_fallback.en-US") &&
      !locale::IsPolicyField("locale_fallback.en-US") &&
      locale::IsPolicyField("parameter.count.min"), "policy schema and text aliases are distinct");
  std::cout << "PASS localization definition grammar, schema, locale union and static bounds\n";
}

void SourcesAndTypes() {
  const auto definition = Definition();
  Check(locale::ValidateArguments(definition, Request(), 0), "valid typed context");
  for (const char* value : {"03", "-0", "+3", " 3", "3 ", "3.0", "3e0", "٣", "21", "-6",
      "9223372036854775808", "-9223372036854775809", ""}) {
    auto request = Request();
    request["r0.scope"] = value;
    Check(!locale::ValidateArguments(definition, request, 0), "noncanonical or out-of-range integer");
  }
  for (const char* key : {"r0.scope", "r0.recipient"}) {
    auto request = Request();
    request.erase(key);
    Check(!locale::ValidateArguments(definition, request, 0), "typed dynamic source must explicitly exist");
  }
  auto request = Request();
  request["r0.recipient"] = "";
  Check(locale::ValidateArguments(definition, request, 0), "explicit empty string is permitted");
  request["r0.recipient"] = std::string(65, 'a');
  Check(!locale::ValidateArguments(definition, request, 0), "string byte bound enforced");
  request["r0.recipient"] = std::string("a\0b", 3);
  Check(!locale::ValidateArguments(definition, request, 0), "embedded NUL rejected");
  request["r0.recipient"] = std::string("\xff", 1);
  Check(!locale::ValidateArguments(definition, request, 0), "invalid UTF-8 rejected");
  for (const char* key : {"display_args", "display_args.count", "r0.display_args",
      "r0.display_args.count", "r0.arg_count", "r0.arg0.value", "r9.display_args.count"}) {
    request = Request();
    request[key] = "forged";
    Check(!locale::ValidateArguments(definition, request, 0), "independent caller display arguments rejected");
  }
  auto full_range = Definition();
  full_range["parameter.count.min"] = "-9223372036854775808";
  full_range["parameter.count.max"] = "9223372036854775807";
  Check(locale::ValidateDefinition(full_range), "signed 64-bit schema limits supported");
  for (const char* value : {"-9223372036854775808", "9223372036854775807"}) {
    request = Request();
    request["r0.scope"] = value;
    Check(locale::ValidateArguments(full_range, request, 0), "signed integer boundary accepted");
  }
  request = Request();
  request["r0.retention_ms"] = "1";
  request["retention_ms"] = "1";
  auto prompt = Prompt(definition, request);
  Check(Get(prompt, "r0.arg0.name") == "count" && Get(prompt, "r0.arg1.name") == "recipient" &&
      Get(prompt, "r0.arg2.name") == "retention", "stable sorted argument descriptors");
  Check(Get(prompt, "r0.arg2.value") == "60000", "retention comes only from registered policy");
  Message unchanged{{"sentinel", "value"}};
  request["r0.scope"] = "bad";
  Check(!locale::AppendArguments(definition, request, 0, &unchanged) && unchanged.size() == 1,
      "failed append preserves existing prompt");
  Check(!locale::ValidateArguments(definition, Request(), 16), "requirement index bounded");
  std::cout << "PASS localization source binding, canonical integer/string types and injection rejection\n";
}

void LocaleSelection() {
  auto definition = Definition();
  definition["locale_fallback.en-US"] = "ko";
  definition["locale_fallback.ko-Latn-KR"] = "ko";
  Check(locale::ValidateDefinition(definition), "direct locale aliases accepted");
  for (const auto& language : Message{{"ko", "ko"}, {"ko-KR", "ko"}, {"en-US", "ko"},
      {"en-GB", "en"}, {"ko-Latn-KR", "ko"}, {"unknown-region", "en"}}) {
    std::string selected;
    Check(locale::SelectLocale(definition, language.first, &selected) && selected == language.second,
        "exact locale, explicit alias, legacy reduction, then default");
  }
  for (const auto& alias : Message{{"locale_fallback.en", "ko"},
      {"locale_fallback.xx", "missing"}, {"locale_fallback.fr", "en-US"}}) {
    auto invalid = definition;
    invalid[alias.first] = alias.second;
    Check(!locale::ValidateDefinition(invalid), "shadow alias, missing locale and chain rejected");
  }
  definition["locale_fallback.cycle1"] = "cycle2";
  definition["locale_fallback.cycle2"] = "cycle1";
  Check(!locale::ValidateDefinition(definition), "fallback cycle rejected");
  std::cout << "PASS localization direct aliases and deterministic fallback without chains/shadowing\n";
}

void FormattingAndAbi() {
  auto definition = Definition();
  auto prompt = Prompt(definition, Request());
  std::string rendered;
  Check(locale::FormatPrompt(prompt, 0, "body", &rendered) &&
      rendered == "Send to sink{count}%<tag>; retain 60000 ms", "values are expanded once as plain text");
  Check(locale::FormatPrompt(Prompt(definition, Request(), "ko"), 0, "title", &rendered) &&
      rendered == "기록 3개 읽기", "UTF-8 template preserved");
  consent_result result{prompt};
  char* output = nullptr;
  Check(consent_prompt_format(&result, 0, "title", &output) == 0 &&
      output && !strcmp(output, "Read 3 records"), "public C formatter owns malloc text");
  free(output);
  for (const char* field : {"unknown", "", "TITLE"}) {
    output = reinterpret_cast<char*>(1);
    Check(consent_prompt_format(&result, 0, field, &output) == CONSENT_ERROR_INVALID_PARAMETER &&
        output == nullptr, "invalid C field clears output");
  }
  output = reinterpret_cast<char*>(1);
  Check(consent_prompt_format(&result, 1, "title", &output) == CONSENT_ERROR_INVALID_PARAMETER &&
      output == nullptr, "invalid C index clears output");
  Check(consent_prompt_format(nullptr, 0, "title", &output) == CONSENT_ERROR_INVALID_PARAMETER && !output,
      "null prompt rejected");
  Check(consent_prompt_format(&result, 0, nullptr, &output) == CONSENT_ERROR_INVALID_PARAMETER && !output,
      "null field rejected");
  Check(consent_prompt_format(&result, 0, "title", nullptr) == CONSENT_ERROR_INVALID_PARAMETER,
      "null output rejected");
  for (const auto& mutation : Message{{"r0.template_version", "2"}, {"r0.arg_count", "9"},
      {"r0.arg0.type", "float"}, {"r0.arg0.value", "03"}, {"r0.arg1.name", "count"},
      {"r0.arg3.value", "unexpected"}, {"r0.arg0.extra", "unexpected"}}) {
    auto invalid = prompt;
    invalid[mutation.first] = mutation.second;
    rendered = "unchanged";
    Check(!locale::FormatPrompt(invalid, 0, "title", &rendered) && rendered == "unchanged",
        "malformed prompt fails atomically");
  }
  Message legacy{{"default_locale", "en"}, {"message.en.title", "Literal %s <tag>"},
      {"message.en.body", "Literal body"}};
  Check(locale::ValidateDefinition(legacy), "legacy literal definition remains valid");
  result.values = Prompt(legacy, Request());
  Check(consent_prompt_format(&result, 0, "title", &output) == 0 &&
      !strcmp(output, "Literal %s <tag>"), "legacy literal formatter does not interpret percent or markup");
  free(output);
  legacy["message.en.title"] = "Legacy {placeholder}";
  Check(!locale::ValidateDefinition(legacy), "legacy placeholders remain rejected");
  result.values["r0.title"] = "Legacy {placeholder}";
  output = reinterpret_cast<char*>(1);
  Check(consent_prompt_format(&result, 0, "title", &output) == CONSENT_ERROR_INVALID_PARAMETER && !output,
      "legacy malformed prompt clears C output");
  std::cout << "PASS localization plain single-pass formatting, legacy compatibility and C ownership/errors\n";
}

void ExpandedBounds() {
  Message definition{{"template_version", "1"}, {"default_locale", "en"},
      {"parameter.value.source", "recipient"}, {"parameter.value.type", "string"},
      {"parameter.value.max_bytes", "512"}, {"message.en.title", ""},
      {"message.en.body", "{value}"}};
  for (int i = 0; i < 16; ++i)
    definition["message.en.title"] += "{value}";
  Check(locale::ValidateDefinition(definition), "bounded repeated placeholder schema valid");
  auto request = Request();
  request["r0.recipient"] = std::string(512, 'a');
  consent_result result{Prompt(definition, request)};
  char* output = nullptr;
  Check(consent_prompt_format(&result, 0, "title", &output) == 0 && strlen(output) == 8192,
      "exact maximum rendered length accepted");
  free(output);
  definition["message.en.title"] += "{value}";
  Check(locale::ValidateDefinition(definition), "runtime expansion bound does not reject short valid values");
  result.values = Prompt(definition, request);
  output = reinterpret_cast<char*>(1);
  Check(consent_prompt_format(&result, 0, "title", &output) == CONSENT_ERROR_INVALID_PARAMETER && !output,
      "oversized expanded output rejected before returning C allocation");
  request["r0.recipient"] = "x";
  result.values = Prompt(definition, request);
  Check(consent_prompt_format(&result, 0, "title", &output) == 0 && strlen(output) == 17,
      "same schema accepts shorter actual source value");
  free(output);
  std::cout << "PASS localization exact 8192-byte rendered bound and selected-value overflow rejection\n";
}
}  // namespace

int main() {
  try {
    DefinitionValidation();
    SourcesAndTypes();
    LocaleSelection();
    FormattingAndAbi();
    ExpandedBounds();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL localization: " << error.what() << '\n';
    return 1;
  }
}
