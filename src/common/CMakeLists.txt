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

FILE(GLOB COMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
IF(COMMON_SOURCES)
  ADD_LIBRARY(consent-common STATIC ${COMMON_SOURCES})
  TARGET_INCLUDE_DIRECTORIES(consent-common PUBLIC
    "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}/generated")
  TARGET_LINK_LIBRARIES(consent-common PUBLIC PkgConfig::GLIB PkgConfig::PARCEL Threads::Threads)
ELSE()
  ADD_LIBRARY(consent-common INTERFACE)
  TARGET_INCLUDE_DIRECTORIES(consent-common INTERFACE
    "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}/generated")
  TARGET_LINK_LIBRARIES(consent-common INTERFACE PkgConfig::GLIB PkgConfig::PARCEL Threads::Threads)
ENDIF()
ADD_DEPENDENCIES(consent-common consent-wire)
