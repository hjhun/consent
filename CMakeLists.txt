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
OPTION(CONSENT_BUILD_POC "Build isolated .NET approval UI and participant mocks" OFF)
SET(CONSENT_POC_SOCKET_PATH "/opt/var/lib/consent-poc-runtime/consent.sock")
SET(CONSENT_POC_STATE_DIR "/opt/var/lib/consent-poc-state")
SET(CONSENT_POC_AUTHORITY_DIR "/opt/var/lib/consent-poc-authority")
SET(CONSENT_POC_ROLE_CONFIG "/etc/consent-poc/roles.conf")
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
INSTALL(FILES README.md AGENTS.md LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
INSTALL(DIRECTORY docs/ DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
  FILES_MATCHING PATTERN "*.md")

IF(CONSENT_BUILD_POC)
  CONFIGURE_FILE(packaging/consentd-poc.service.in consentd-poc.service @ONLY)
  INSTALL(FILES "${CMAKE_CURRENT_BINARY_DIR}/consentd-poc.service"
    packaging/consentd-poc.socket packaging/consent-poc-register.service
    DESTINATION "${UNITDIR}")
  INSTALL(FILES packaging/consent-poc-roles.conf
    DESTINATION "${CMAKE_INSTALL_SYSCONFDIR}/consent-poc" RENAME roles.conf)
  INSTALL(PROGRAMS packaging/consent-poc-register.sh
    DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/poc" RENAME consent-poc-register)
  FILE(GLOB_RECURSE CONSENT_UI_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/consent-ui/*.cs"
    "${PROJECT_SOURCE_DIR}/src/consent-ui/*.csproj"
    "${PROJECT_SOURCE_DIR}/src/consent-ui/*.xml")
  LIST(FILTER CONSENT_UI_SOURCES EXCLUDE REGEX "/(obj|bin)/")
  SET(CONSENT_UI_TPK "${PROJECT_BINARY_DIR}/org.tizen.consentui-0.1.0.tpk")
  ADD_CUSTOM_COMMAND(OUTPUT "${CONSENT_UI_TPK}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/tools/build_poc_tpk.py"
      --source-dir "${PROJECT_SOURCE_DIR}/src/consent-ui"
      --work-dir "${PROJECT_BINARY_DIR}/poc-ui-build"
      --output "${CONSENT_UI_TPK}" --tizen-net-version 14.0.0.19364
    DEPENDS ${CONSENT_UI_SOURCES} "${PROJECT_SOURCE_DIR}/src/tools/build_poc_tpk.py"
    VERBATIM)
  ADD_CUSTOM_TARGET(consent-ui-tpk ALL DEPENDS "${CONSENT_UI_TPK}")
  INSTALL(FILES "${CONSENT_UI_TPK}" DESTINATION "${CMAKE_INSTALL_DATADIR}/consent/poc")
  SET(CONSENT_NEGATIVE_TPK "${PROJECT_BINARY_DIR}/org.tizen.consentui.negative-0.1.0.tpk")
  ADD_CUSTOM_COMMAND(OUTPUT "${CONSENT_NEGATIVE_TPK}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/tools/build_poc_tpk.py"
      --source-dir "${PROJECT_SOURCE_DIR}/src/consent-ui"
      --work-dir "${PROJECT_BINARY_DIR}/poc-negative-build"
      --output "${CONSENT_NEGATIVE_TPK}" --tizen-net-version 14.0.0.19364
      --project negative/ConsentUINegative.csproj
      --package org.tizen.consentui.negative --assembly ConsentUINegative
    DEPENDS ${CONSENT_UI_SOURCES} "${PROJECT_SOURCE_DIR}/src/tools/build_poc_tpk.py"
    VERBATIM)
  ADD_CUSTOM_TARGET(consent-ui-negative-tpk ALL DEPENDS "${CONSENT_NEGATIVE_TPK}")
  INSTALL(FILES "${CONSENT_NEGATIVE_TPK}" DESTINATION "${CMAKE_INSTALL_DATADIR}/consent/poc")
ENDIF()
