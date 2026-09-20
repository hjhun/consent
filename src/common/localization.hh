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
#ifndef CONSENT_COMMON_LOCALIZATION_HH_
#define CONSENT_COMMON_LOCALIZATION_HH_

#include "message.hh"

namespace consent {
namespace localization {

// Invalid input returns false and optionally a nonsensitive diagnostic.
// Allocation errors propagate to the caller's existing exception boundary.
// Successful output assignment is atomic; failure leaves outputs unchanged.
bool IsDefinitionField(const std::string& key);
bool IsPolicyField(const std::string& key);
bool ValidateDefinition(const Message& definition, std::string* error = nullptr);
bool ValidateArguments(const Message& definition, const Message& request,
    size_t requirement, std::string* error = nullptr);
bool AppendArguments(const Message& definition, const Message& request,
    size_t requirement, Message* prompt, std::string* error = nullptr);
bool SelectLocale(const Message& definition, const std::string& requested,
    std::string* selected, std::string* error = nullptr);
bool FormatPrompt(const Message& prompt, size_t requirement,
    const std::string& field, std::string* formatted, std::string* error = nullptr);

}  // namespace localization
}  // namespace consent
#endif  // CONSENT_COMMON_LOCALIZATION_HH_
