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

FILE(GLOB TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
# This helper has no main and belongs only to the dedicated shutdown daemon.
LIST(REMOVE_ITEM TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/repository_shutdown_interposer.cc")
FOREACH(SOURCE ${TEST_SOURCES})
  GET_FILENAME_COMPONENT(TEST_NAME "${SOURCE}" NAME_WE)
  STRING(REPLACE "_" "-" TEST_NAME "${TEST_NAME}")
  ADD_EXECUTABLE(${TEST_NAME} "${SOURCE}")
  IF(TEST_NAME STREQUAL "endpoint-fixture")
    # Explicit emulator-only fixture: it uses the real activation socket path.
    INSTALL(TARGETS ${TEST_NAME}
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
    CONTINUE()
  ENDIF()
  IF(TEST_NAME STREQUAL "wire-scenario")
    # The native wire fixture requires an isolated daemon and explicit roles.
    TARGET_LINK_LIBRARIES(${TEST_NAME} PRIVATE consent-common PkgConfig::GLIB)
    TARGET_COMPILE_DEFINITIONS(${TEST_NAME} PRIVATE
      CONSENT_SOCKET_PATH="${CONSENT_TEST_ROOT}/consent.sock")
    INSTALL(TARGETS ${TEST_NAME}
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
    CONTINUE()
  ENDIF()
  # Assertions in executable tests include fixture setup and must stay active.
  TARGET_COMPILE_OPTIONS(${TEST_NAME} PRIVATE -UNDEBUG)
  TARGET_INCLUDE_DIRECTORIES(${TEST_NAME} PRIVATE
    "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/src/consent/inc")
  IF(TEST_NAME STREQUAL "client-test" AND TARGET consent-test-library)
    TARGET_LINK_LIBRARIES(${TEST_NAME} PRIVATE consent-test-library)
  ELSE()
    TARGET_LINK_LIBRARIES(${TEST_NAME} PRIVATE consent)
  ENDIF()
  TARGET_LINK_LIBRARIES(${TEST_NAME} PRIVATE consent-common
    PkgConfig::GLIB PkgConfig::SQLITE Threads::Threads ${CMAKE_DL_LIBS})
  IF(TARGET consent-storage)
    TARGET_LINK_LIBRARIES(${TEST_NAME} PRIVATE consent-storage)
  ENDIF()
  ADD_TEST(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  # RPM builds disable RPATH. Resolve the just-built C API for tests that use
  # its shared ABI before the runtime package is installed in the build root.
  SET_TESTS_PROPERTIES(${TEST_NAME} PROPERTIES TIMEOUT 30
    ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent>")
  IF(TEST_NAME STREQUAL "storage-prepare-test")
    # Migration validates real root and account transitions; GBS runs as abuild.
    SET_TESTS_PROPERTIES(${TEST_NAME} PROPERTIES SKIP_RETURN_CODE 77)
  ENDIF()
  INSTALL(TARGETS ${TEST_NAME}
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
ENDFOREACH()

ADD_TEST(NAME idl-codegen-test COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/idl_codegen_test.py")
SET_TESTS_PROPERTIES(idl-codegen-test PROPERTIES TIMEOUT 30)
