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

FILE(GLOB CONSENT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
IF(CONSENT_SOURCES)
  ADD_LIBRARY(consent SHARED ${CONSENT_SOURCES})
  TARGET_INCLUDE_DIRECTORIES(consent PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/consent>")
  TARGET_LINK_LIBRARIES(consent PRIVATE consent-common PkgConfig::GLIB Threads::Threads)
  TARGET_COMPILE_DEFINITIONS(consent PRIVATE
    CONSENT_SOCKET_PATH="${CONSENT_SOCKET_PATH}")
  SET_TARGET_PROPERTIES(consent PROPERTIES VERSION "${PROJECT_VERSION}" SOVERSION 0)
  SET_PROPERTY(TARGET consent APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections,--no-undefined")
  SET_PROPERTY(TARGET consent APPEND_STRING PROPERTY LINK_FLAGS
    " -Wl,--version-script=${PROJECT_SOURCE_DIR}/packaging/consent.map")
  SET_PROPERTY(TARGET consent APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/packaging/consent.map")
  INSTALL(TARGETS consent LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  INSTALL(FILES consent.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/consent")
  IF(BUILD_TESTING OR CONSENT_BUILD_TEST_DAEMON)
    ADD_LIBRARY(consent-test-library STATIC ${CONSENT_SOURCES})
    TARGET_INCLUDE_DIRECTORIES(consent-test-library PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
    TARGET_LINK_LIBRARIES(consent-test-library PUBLIC consent-common
      PkgConfig::GLIB Threads::Threads)
    TARGET_COMPILE_DEFINITIONS(consent-test-library PRIVATE CONSENT_TESTING=1
      CONSENT_SOCKET_PATH="${CONSENT_TEST_ROOT}/consent.sock")
  ENDIF()
ELSE()
  MESSAGE(STATUS "consent: implementation sources are not present yet")
ENDIF()

SET(CONSENT_PC_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
IF(NOT IS_ABSOLUTE "${CONSENT_PC_LIBDIR}")
  SET(CONSENT_PC_LIBDIR "\${prefix}/${CONSENT_PC_LIBDIR}")
ENDIF()
SET(CONSENT_PC_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}")
IF(NOT IS_ABSOLUTE "${CONSENT_PC_INCLUDEDIR}")
  SET(CONSENT_PC_INCLUDEDIR "\${prefix}/${CONSENT_PC_INCLUDEDIR}")
ENDIF()
CONFIGURE_FILE(consent.pc.in consent.pc @ONLY)
INSTALL(FILES "${CMAKE_CURRENT_BINARY_DIR}/consent.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
