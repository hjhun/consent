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

FILE(GLOB CONSENTD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
SET(REPOSITORY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/repository.cc")
IF(EXISTS "${REPOSITORY_SOURCE}")
  ADD_LIBRARY(consent-storage STATIC "${REPOSITORY_SOURCE}")
  TARGET_INCLUDE_DIRECTORIES(consent-storage PUBLIC
    "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/consent")
  TARGET_LINK_LIBRARIES(consent-storage PUBLIC consent-common PkgConfig::SQLITE)
  LIST(REMOVE_ITEM CONSENTD_SOURCES "${REPOSITORY_SOURCE}")
ENDIF()
IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cc")
  ADD_EXECUTABLE(consentd ${CONSENTD_SOURCES})
  TARGET_INCLUDE_DIRECTORIES(consentd PRIVATE
    "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/consent")
  TARGET_LINK_LIBRARIES(consentd PRIVATE consent-common consent-storage PkgConfig::GLIB
    PkgConfig::SQLITE PkgConfig::SYSTEMD PkgConfig::PKGMGR Threads::Threads)
  TARGET_COMPILE_DEFINITIONS(consentd PRIVATE
    CONSENT_SOCKET_PATH="${CONSENT_SOCKET_PATH}"
    CONSENT_STATE_DIR="${CONSENT_STATE_DIR}"
    CONSENT_ROLE_CONFIG="${CONSENT_ROLE_CONFIG}"
    CONSENT_INSTALLATIONS="${CONSENT_INSTALLATIONS}")
  SET_PROPERTY(TARGET consentd APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections -pie")
  INSTALL(TARGETS consentd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  IF(CONSENT_BUILD_TEST_DAEMON)
    ADD_EXECUTABLE(consentd-test ${CONSENTD_SOURCES})
    TARGET_INCLUDE_DIRECTORIES(consentd-test PRIVATE
      "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/consent")
    TARGET_LINK_LIBRARIES(consentd-test PRIVATE consent-common consent-storage
      PkgConfig::GLIB PkgConfig::SQLITE PkgConfig::SYSTEMD Threads::Threads)
    TARGET_COMPILE_DEFINITIONS(consentd-test PRIVATE CONSENT_TEST_BUILD
      CONSENT_SOCKET_PATH="${CONSENT_TEST_ROOT}/consent.sock"
      CONSENT_STATE_DIR="${CONSENT_TEST_STATE_DIR}"
      CONSENT_ROLE_CONFIG="${CONSENT_TEST_ROOT}/roles.conf"
      CONSENT_INSTALLATIONS="${CONSENT_TEST_STATE_DIR}/installations.conf")
    SET_PROPERTY(TARGET consentd-test APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections -pie")
    INSTALL(TARGETS consentd-test
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  ENDIF()
ELSE()
  MESSAGE(STATUS "consentd: implementation sources are not present yet")
ENDIF()
