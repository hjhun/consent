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

FILE(GLOB TOOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
FOREACH(SOURCE ${TOOL_SOURCES})
  GET_FILENAME_COMPONENT(TOOL_NAME "${SOURCE}" NAME_WE)
  STRING(REPLACE "_" "-" TOOL_NAME "${TOOL_NAME}")
  ADD_EXECUTABLE(${TOOL_NAME} "${SOURCE}")
  TARGET_LINK_LIBRARIES(${TOOL_NAME} PRIVATE consent PkgConfig::GLIB)
  INSTALL(TARGETS ${TOOL_NAME}
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  IF(TARGET consent-test-library)
    ADD_EXECUTABLE(${TOOL_NAME}-isolated "${SOURCE}")
    TARGET_LINK_LIBRARIES(${TOOL_NAME}-isolated PRIVATE
      consent-test-library PkgConfig::GLIB)
    IF(TOOL_NAME STREQUAL "consent-cache-scenario")
      TARGET_COMPILE_DEFINITIONS(${TOOL_NAME}-isolated PRIVATE
        CONSENT_TEST_DB_PATH="${CONSENT_TEST_STATE_DIR}/consent.db")
    ENDIF()
    INSTALL(TARGETS ${TOOL_NAME}-isolated
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  ENDIF()
ENDFOREACH()

IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/installation_authority.cc")
  ADD_EXECUTABLE(consent-installation-authority installation_authority.cc)
  TARGET_LINK_LIBRARIES(consent-installation-authority PRIVATE PkgConfig::GLIB)
  TARGET_COMPILE_DEFINITIONS(consent-installation-authority PRIVATE
    CONSENT_AUTHORITY_DIR="${CONSENT_AUTHORITY_DIR}"
    CONSENT_INSTALLATIONS="${CONSENT_INSTALLATIONS}")
  INSTALL(TARGETS consent-installation-authority
    RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}")
  IF(CONSENT_BUILD_TEST_DAEMON)
    ADD_EXECUTABLE(consent-installation-authority-isolated installation_authority.cc)
    TARGET_LINK_LIBRARIES(consent-installation-authority-isolated PRIVATE PkgConfig::GLIB)
    TARGET_COMPILE_DEFINITIONS(consent-installation-authority-isolated PRIVATE
      CONSENT_TEST_BUILD CONSENT_AUTHORITY_DIR="${CONSENT_TEST_AUTHORITY_DIR}"
      CONSENT_INSTALLATIONS="${CONSENT_TEST_AUTHORITY_DIR}/installations.conf")
    INSTALL(TARGETS consent-installation-authority-isolated
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  ENDIF()
ENDIF()

IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/storage_prepare.cc")
  ADD_EXECUTABLE(consent-storage-prepare storage_prepare.cc)
  TARGET_COMPILE_DEFINITIONS(consent-storage-prepare PRIVATE
    CONSENT_STATE_DIR="${CONSENT_STATE_DIR}"
    CONSENT_AUTHORITY_DIR="${CONSENT_AUTHORITY_DIR}"
    CONSENT_SERVICE_USER="${CONSENT_SERVICE_USER}"
    CONSENT_SERVICE_UNIT="consentd.service")
  INSTALL(TARGETS consent-storage-prepare
    RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}")
  IF(CONSENT_BUILD_TEST_DAEMON)
    ADD_EXECUTABLE(consent-storage-prepare-isolated storage_prepare.cc)
    TARGET_COMPILE_DEFINITIONS(consent-storage-prepare-isolated PRIVATE
      CONSENT_STATE_DIR="${CONSENT_TEST_STATE_DIR}"
      CONSENT_AUTHORITY_DIR="${CONSENT_TEST_AUTHORITY_DIR}"
      CONSENT_SERVICE_USER="${CONSENT_SERVICE_USER}"
      CONSENT_SERVICE_UNIT="consentd-isolated.service")
    INSTALL(TARGETS consent-storage-prepare-isolated
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  ENDIF()
ENDIF()
