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
#include "localization.hh"

#include <algorithm>
#include <set>
#include <utility>

namespace consent {
namespace localization {
namespace {
constexpr size_t kMaxParameters = 8;
constexpr size_t kMaxTemplate = 4096;
constexpr size_t kMaxString = 512;
constexpr size_t kMaxRendered = 8192;

struct Parameter {
  std::string source;
  std::string type;
  int64_t minimum = 0;
  int64_t maximum = 0;
  size_t max_bytes = 0;
};
using Schema = std::map<std::string, Parameter>;

bool Error(std::string* error, const char* reason) {
  if (error)
    *error = reason;
  return false;
}

bool Starts(const std::string& value, const char* prefix) {
  return value.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

bool Name(const std::string& value) {
  if (value.empty() || value.size() > 32 ||
      (!g_ascii_isalpha(value[0]) && value[0] != '_'))
    return false;
  for (unsigned char character : value) {
    if (!g_ascii_isalnum(character) && character != '_')
      return false;
  }
  return true;
}

bool Locale(const std::string& value) {
  if (value.empty() || value.size() > 256)
    return false;
  for (unsigned char character : value) {
    if (!g_ascii_isalnum(character) && character != '-' && character != '_' &&
        character != '.' && character != ':')
      return false;
  }
  return true;
}

bool Text(const std::string& value, size_t maximum) {
  return value.size() <= maximum && value.find('\0') == std::string::npos &&
      g_utf8_validate(value.data(), value.size(), nullptr);
}

bool Integer(const std::string& value, int64_t* number) {
  return ParseNumber(value, number) && std::to_string(*number) == value;
}

bool Placeholders(const std::string& text, std::set<std::string>* names,
    std::string* error) {
  if (text.empty() || !Text(text, kMaxTemplate))
    return Error(error, "invalid localized template");
  for (size_t position = 0; position < text.size(); ++position) {
    if (text[position] == '}')
      return Error(error, "unmatched template brace");
    if (text[position] != '{')
      continue;
    size_t end = text.find('}', position + 1);
    if (end == std::string::npos)
      return Error(error, "unmatched template brace");
    auto name = text.substr(position + 1, end - position - 1);
    if (!Name(name))
      return Error(error, "invalid template parameter name");
    names->insert(std::move(name));
    if (names->size() > kMaxParameters)
      return Error(error, "too many template parameters");
    position = end;
  }
  return true;
}

bool ParseSchema(const Message& definition, Schema* schema, std::string* error) {
  if (definition.size() > kMaxFields)
    return Error(error, "definition field limit exceeded");
  bool typed = definition.count("template_version") != 0;
  if (typed && Get(definition, "template_version") != "1")
    return Error(error, "unsupported template version");
  std::map<std::string, Message> fields;
  for (const auto& field : definition) {
    if (!Starts(field.first, "parameter."))
      continue;
    if (!typed)
      return Error(error, "parameter schema requires template version");
    auto remainder = field.first.substr(10);
    auto separator = remainder.find('.');
    if (separator == std::string::npos || !Name(remainder.substr(0, separator)))
      return Error(error, "invalid parameter schema key");
    auto property = remainder.substr(separator + 1);
    if (property != "source" && property != "type" && property != "min" &&
        property != "max" && property != "max_bytes")
      return Error(error, "unknown parameter schema property");
    fields[remainder.substr(0, separator)][property] = field.second;
    if (fields.size() > kMaxParameters)
      return Error(error, "parameter schema limit exceeded");
  }
  for (const auto& entry : fields) {
    const auto& fields = entry.second;
    Parameter parameter;
    parameter.source = Get(fields, "source");
    parameter.type = Get(fields, "type");
    if (parameter.source != "scope" && parameter.source != "purpose" &&
        parameter.source != "recipient" && parameter.source != "operation" &&
        parameter.source != "retention_ms")
      return Error(error, "unsupported parameter source");
    if (parameter.type == "integer") {
      if ((parameter.source != "scope" && parameter.source != "retention_ms") ||
          fields.size() != 4 || !Integer(Get(fields, "min"), &parameter.minimum) ||
          !Integer(Get(fields, "max"), &parameter.maximum) ||
          parameter.minimum > parameter.maximum)
        return Error(error, "invalid integer parameter schema");
    } else if (parameter.type == "string") {
      int64_t maximum = 0;
      if (parameter.source == "retention_ms" || fields.size() != 3 ||
          !Integer(Get(fields, "max_bytes"), &maximum) || maximum < 1 ||
          maximum > static_cast<int64_t>(kMaxString))
        return Error(error, "invalid string parameter schema");
      parameter.max_bytes = static_cast<size_t>(maximum);
    } else {
      return Error(error, "unsupported parameter type");
    }
    schema->emplace(entry.first, std::move(parameter));
  }
  return true;
}

bool Value(const Parameter& parameter, const std::string& value, std::string* error) {
  if (parameter.type == "string") {
    if (!Text(value, parameter.max_bytes))
      return Error(error, "string argument exceeds schema or is not UTF-8");
  } else {
    int64_t number = 0;
    if (!Integer(value, &number) || number < parameter.minimum || number > parameter.maximum)
      return Error(error, "integer argument is not canonical or outside schema");
  }
  return true;
}

bool MessageLocales(const Message& definition, const Schema& schema,
    std::set<std::string>* registered, std::string* error) {
  std::map<std::string, Message> locales;
  size_t count = 0;
  for (const auto& field : definition) {
    if (!Starts(field.first, "message."))
      continue;
    auto remainder = field.first.substr(8);
    auto separator = remainder.rfind('.');
    if (separator == std::string::npos || !Locale(remainder.substr(0, separator)))
      return Error(error, "invalid message locale");
    auto name = remainder.substr(separator + 1);
    if (name != "title" && name != "body")
      return Error(error, "unknown localized message field");
    locales[remainder.substr(0, separator)][name] = field.second;
    if (++count > 32)
      return Error(error, "localized message limit exceeded");
  }
  std::set<std::string> declared;
  for (const auto& parameter : schema)
    declared.insert(parameter.first);
  for (const auto& locale : locales) {
    std::set<std::string> names;
    for (const auto& field : locale.second) {
      if (!Placeholders(field.second, &names, error))
        return false;
    }
    if (definition.count("template_version") && locale.second.size() != 2)
      return Error(error, "typed locale requires both title and body");
    if (names != declared)
      return Error(error, "locale placeholders do not match declared schema");
    if (locale.second.size() == 2)
      registered->insert(locale.first);
  }
  if (!registered->count(Get(definition, "default_locale")))
    return Error(error, "default locale requires title and body");
  std::set<std::string> aliases;
  for (const auto& field : definition) {
    if (!Starts(field.first, "locale_fallback."))
      continue;
    auto alias = field.first.substr(16);
    if (!Locale(alias) || registered->count(alias) || !registered->count(field.second))
      return Error(error, "locale fallback must name a registered locale");
    aliases.insert(std::move(alias));
  }
  for (const auto& alias : aliases) {
    if (aliases.count(Get(definition, "locale_fallback." + alias)))
      return Error(error, "locale fallback chains and cycles are forbidden");
  }
  return true;
}

bool CallerArguments(const Message& request, std::string* error) {
  for (const auto& field : request) {
    auto separator = field.first.find('.');
    bool row = field.first.size() > 1 && field.first[0] == 'r' &&
        separator != std::string::npos && separator > 1 &&
        std::all_of(field.first.begin() + 1, field.first.begin() + separator,
            [](unsigned char character) { return g_ascii_isdigit(character); });
    auto property = row ? field.first.substr(separator + 1) : field.first;
    if (field.first == "display_args" || Starts(field.first, "display_args.") ||
        (row && (property == "display_args" || Starts(property, "display_args.") ||
                 Starts(property, "arg"))))
      return Error(error, "caller-supplied display arguments are forbidden");
  }
  return true;
}

bool Arguments(const Message& definition, const Message& request, size_t requirement,
    Schema* schema, Message* values, std::string* error) {
  if (requirement >= kMaxRequirements || request.size() > kMaxFields ||
      !CallerArguments(request, error) || !ParseSchema(definition, schema, error))
    return Error(error, "invalid template argument context");
  auto row = "r" + std::to_string(requirement) + ".";
  for (const auto& entry : *schema) {
    const auto& parameter = entry.second;
    if (parameter.source != "retention_ms" && !request.count(row + parameter.source))
      return Error(error, "required template source field is missing");
    std::string value = parameter.source == "retention_ms" ?
        Get(definition, "retention_ms", "0") : Get(request, row + parameter.source);
    if (!Value(parameter, value, error))
      return false;
    if (values)
      values->emplace(entry.first, std::move(value));
  }
  return true;
}

bool Render(const std::string& text, const Message& values,
    std::string* formatted, std::string* error) {
  std::string rendered;
  size_t position = 0;
  while (position < text.size()) {
    size_t begin = text.find('{', position);
    size_t end = begin == std::string::npos ? text.size() : begin;
    if (end - position > kMaxRendered - rendered.size())
      return Error(error, "rendered message limit exceeded");
    rendered.append(text, position, end - position);
    if (begin == std::string::npos)
      break;
    size_t close = text.find('}', begin + 1);
    // Placeholders() already checked both template fields and names.
    auto value = values.find(text.substr(begin + 1, close - begin - 1));
    if (value == values.end() || value->second.size() > kMaxRendered - rendered.size())
      return Error(error, "rendered argument missing or output too large");
    rendered.append(value->second);  // Plain text, never recursively parsed.
    position = close + 1;
  }
  *formatted = std::move(rendered);
  return true;
}
}  // namespace

bool IsPolicyField(const std::string& key) {
  return key == "template_version" || Starts(key, "parameter.");
}

bool IsDefinitionField(const std::string& key) {
  return IsPolicyField(key) || Starts(key, "locale_fallback.");
}

bool ValidateDefinition(const Message& definition, std::string* error) {
  Schema schema;
  std::set<std::string> registered;
  if (!ParseSchema(definition, &schema, error) ||
      !MessageLocales(definition, schema, &registered, error))
    return false;
  for (const auto& parameter : schema) {
    if (parameter.second.source == "retention_ms" &&
        !Value(parameter.second, Get(definition, "retention_ms", "0"), error))
      return false;
  }
  return true;
}

bool ValidateArguments(const Message& definition, const Message& request,
    size_t requirement, std::string* error) {
  Schema schema;
  return Arguments(definition, request, requirement, &schema, nullptr, error);
}

bool AppendArguments(const Message& definition, const Message& request,
    size_t requirement, Message* prompt, std::string* error) {
  if (!prompt)
    return Error(error, "missing prompt output");
  Schema schema;
  Message values;
  if (!Arguments(definition, request, requirement, &schema, &values, error))
    return false;
  if (!definition.count("template_version"))
    return true;
  auto row = "r" + std::to_string(requirement) + ".";
  Message updated = *prompt;
  updated[row + "template_version"] = "1";
  updated[row + "arg_count"] = std::to_string(schema.size());
  size_t index = 0;
  for (const auto& parameter : schema) {
    auto prefix = row + "arg" + std::to_string(index++) + ".";
    updated[prefix + "name"] = parameter.first;
    updated[prefix + "type"] = parameter.second.type;
    updated[prefix + "value"] = values.at(parameter.first);
  }
  // The repository owns the whole-message frame/field budget so it can return
  // its explicit E2BIG error rather than silently dropping conditions/args.
  prompt->swap(updated);
  return true;
}

bool SelectLocale(const Message& definition, const std::string& requested,
    std::string* selected, std::string* error) {
  if (!selected || !Locale(requested))
    return Error(error, "invalid requested locale");
  Schema schema;
  std::set<std::string> registered;
  if (!ParseSchema(definition, &schema, error) ||
      !MessageLocales(definition, schema, &registered, error))
    return false;
  std::string locale = requested;
  if (!registered.count(locale)) {
    auto alias = definition.find("locale_fallback." + requested);
    if (alias != definition.end()) {
      locale = alias->second;
    } else if (requested == "ko-KR") {
      locale = "ko";
    } else if (requested == "en-US" || requested == "en-GB") {
      locale = "en";
    }
    if (!registered.count(locale))
      locale = Get(definition, "default_locale");
  }
  *selected = std::move(locale);
  return true;
}

bool FormatPrompt(const Message& prompt, size_t requirement, const std::string& field,
    std::string* formatted, std::string* error) {
  int64_t count = 0;
  if (!formatted || (field != "title" && field != "body") ||
      prompt.size() > kMaxFields || !Integer(Get(prompt, "count"), &count) ||
      count < 1 || count > static_cast<int64_t>(kMaxRequirements) ||
      requirement >= static_cast<size_t>(count))
    return Error(error, "invalid prompt or field index");
  auto row = "r" + std::to_string(requirement) + ".";
  std::set<std::string> placeholders;
  for (const char* name : {"title", "body"}) {
    if (!Placeholders(Get(prompt, row + name), &placeholders, error))
      return false;
  }
  Message values;
  int64_t arguments = 0;
  if (prompt.count(row + "template_version")) {
    if (Get(prompt, row + "template_version") != "1" ||
        !Integer(Get(prompt, row + "arg_count"), &arguments) || arguments < 0 ||
        arguments > static_cast<int64_t>(kMaxParameters))
      return Error(error, "unsupported prompt template schema");
    for (int64_t index = 0; index < arguments; ++index) {
      auto prefix = row + "arg" + std::to_string(index) + ".";
      auto name = Get(prompt, prefix + "name");
      auto type = Get(prompt, prefix + "type");
      auto value = Get(prompt, prefix + "value");
      int64_t number = 0;
      if (!Name(name) || !prompt.count(prefix + "value") ||
          (type != "integer" && type != "string") ||
          (type == "integer" && !Integer(value, &number)) ||
          (type == "string" && !Text(value, kMaxString)) ||
          !values.emplace(std::move(name), std::move(value)).second)
        return Error(error, "invalid prompt argument");
    }
  }
  for (const auto& item : prompt) {
    const auto prefix = row + "arg";
    if (item.first.compare(0, prefix.size(), prefix) != 0)
      continue;
    if (item.first == row + "arg_count" && prompt.count(row + "template_version"))
      continue;
    auto separator = item.first.find('.', prefix.size());
    int64_t index = -1;
    if (separator == std::string::npos ||
        !Integer(item.first.substr(prefix.size(), separator - prefix.size()), &index) ||
        index < 0 || index >= arguments)
      return Error(error, "unexpected prompt argument field");
    auto property = item.first.substr(separator + 1);
    if (property != "name" && property != "type" && property != "value")
      return Error(error, "unknown prompt argument property");
  }
  std::set<std::string> names;
  for (const auto& value : values)
    names.insert(value.first);
  if (placeholders != names)
    return Error(error, "prompt placeholders and arguments differ");
  return Render(Get(prompt, row + field), values, formatted, error);
}

}  // namespace localization
}  // namespace consent
