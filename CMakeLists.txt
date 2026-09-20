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

CMAKE_MINIMUM_REQUIRED(VERSION 3.12)

PROJECT(consent VERSION 0.1.0 LANGUAGES C CXX)

INCLUDE(GNUInstallDirs)
INCLUDE(CTest)
FIND_PACKAGE(PkgConfig REQUIRED)
FIND_PACKAGE(Threads REQUIRED)
FIND_PACKAGE(Python3 REQUIRED COMPONENTS Interpreter)

SET(CMAKE_C_STANDARD 11)
SET(CMAKE_C_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_EXTENSIONS OFF)
SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
SET(CMAKE_CXX_VISIBILITY_PRESET hidden)
SET(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

IF(LIB_INSTALL_DIR)
  SET(CMAKE_INSTALL_LIBDIR "${LIB_INSTALL_DIR}")
ENDIF()
SET(UNITDIR "${CMAKE_INSTALL_LIBDIR}/systemd/system" CACHE PATH
  "Systemd system unit installation directory")
SET(CONSENT_SOCKET_PATH "/run/.consentd.sock" CACHE STRING
  "Systemd-owned consent socket endpoint")
SET(CONSENT_STATE_DIR "/opt/var/lib/consentd" CACHE PATH
  "Consent daemon persistent state directory")
SET(CONSENT_AUTHORITY_DIR "/opt/var/lib/consent-authority" CACHE PATH
  "Root-owned consent installation authority directory")
SET(CONSENT_SERVICE_USER "security_fw" CACHE STRING
  "Dedicated consent daemon account used by storage preparation")
SET(CONSENT_ROLE_CONFIG "/etc/consent/roles.conf" CACHE FILEPATH
  "Root-owned consent identity and role policy")
SET(CONSENT_INSTALLATIONS "${CONSENT_AUTHORITY_DIR}/installations.conf" CACHE FILEPATH
  "Root-owned installation generation registry")
OPTION(CONSENT_BUILD_TOOLS "Build C API exerciser programs" ON)
OPTION(CONSENT_BUILD_TEST_DAEMON "Build isolated integration daemon" OFF)
OPTION(CONSENT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
SET(CONSENT_TEST_ROOT "/tmp/consent-test" CACHE PATH
  "Root-protected isolated integration test directory")
SET(CONSENT_TEST_STATE_DIR "/opt/var/lib/consent-test" CACHE PATH
  "Persistent isolated integration test state directory")
SET(CONSENT_TEST_AUTHORITY_DIR "/opt/var/lib/consent-test-authority" CACHE PATH
  "Root-owned isolated integration installation authority directory")

PKG_CHECK_MODULES(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0)
PKG_CHECK_MODULES(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
PKG_CHECK_MODULES(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
PKG_CHECK_MODULES(PKGMGR REQUIRED IMPORTED_TARGET pkgmgr-info)
PKG_CHECK_MODULES(PARCEL REQUIRED IMPORTED_TARGET parcel)
PKG_CHECK_MODULES(CAPI_BASE_COMMON REQUIRED IMPORTED_TARGET capi-base-common)

ADD_COMPILE_OPTIONS(-Wall -Wextra -Wformat=2 -ffunction-sections -fdata-sections)
IF(CONSENT_WARNINGS_AS_ERRORS)
  ADD_COMPILE_OPTIONS(-Werror)
ENDIF()
ADD_COMPILE_DEFINITIONS(_GNU_SOURCE)

ADD_SUBDIRECTORY(src)

CONFIGURE_FILE(packaging/consentd.service.in consentd.service @ONLY)
INSTALL(FILES "${CMAKE_CURRENT_BINARY_DIR}/consentd.service"
  packaging/consentd.socket DESTINATION "${UNITDIR}")
INSTALL(FILES packaging/roles.conf DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/consent"
  PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
FILE(GLOB CONSENT_DOCUMENTS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/*.md")
INSTALL(FILES ${CONSENT_DOCUMENTS}
  DESTINATION "${CMAKE_INSTALL_DOCDIR}")
