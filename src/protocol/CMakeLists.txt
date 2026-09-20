# Copyright (c) 2026 Samsung Electronics Co., Ltd. All Rights Reserved
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

SET(CONSENT_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
SET(CONSENT_WIRE_HEADER "${CONSENT_GENERATED_DIR}/consent_wire.hh")

ADD_CUSTOM_COMMAND(OUTPUT "${CONSENT_WIRE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/tools/parcel_codegen.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/consent.idl.json" "${CONSENT_WIRE_HEADER}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/consent.idl.json"
    "${PROJECT_SOURCE_DIR}/src/tools/parcel_codegen.py"
  COMMENT "Generating native Tizen Parcelable message classes"
  VERBATIM)
ADD_CUSTOM_TARGET(consent-wire DEPENDS "${CONSENT_WIRE_HEADER}")
