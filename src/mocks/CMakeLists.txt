ADD_LIBRARY(consent-feature-common STATIC feature_common.cc)
TARGET_LINK_LIBRARIES(consent-feature-common PUBLIC consent-common)
ADD_LIBRARY(consent-feature-poc SHARED feature_bridge.cc)
TARGET_LINK_LIBRARIES(consent-feature-poc PRIVATE consent-feature-common)
SET_TARGET_PROPERTIES(consent-feature-poc PROPERTIES VERSION 0.1.0 SOVERSION 0)
INSTALL(TARGETS consent-feature-poc LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")

FOREACH(ROLE argo cm ce holder installer)
  ADD_EXECUTABLE(consent-mock-${ROLE} ${ROLE}.c mock_runtime.cc)
  TARGET_LINK_LIBRARIES(consent-mock-${ROLE} PRIVATE consent-poc consent-feature-common PkgConfig::GLIB)
  IF(ROLE STREQUAL "argo")
    TARGET_SOURCES(consent-mock-${ROLE} PRIVATE feature_coordinator.cc)
    TARGET_LINK_LIBRARIES(consent-mock-${ROLE} PRIVATE PkgConfig::SYSTEMD)
  ELSEIF(ROLE STREQUAL "installer")
    TARGET_SOURCES(consent-mock-${ROLE} PRIVATE feature_register.cc)
  ELSE()
    TARGET_SOURCES(consent-mock-${ROLE} PRIVATE feature_worker.cc)
  ENDIF()
  INSTALL(TARGETS consent-mock-${ROLE}
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/poc")
ENDFOREACH()
INSTALL(DIRECTORY fixtures/ DESTINATION "${CMAKE_INSTALL_DATADIR}/consent/poc/fixtures"
  FILES_MATCHING PATTERN "*.ini")

IF(BUILD_TESTING)
  ADD_EXECUTABLE(consent-feature-test feature_test.cc feature_common.cc)
  TARGET_COMPILE_DEFINITIONS(consent-feature-test PRIVATE CONSENT_FEATURE_UNIT_TEST=1)
  TARGET_COMPILE_OPTIONS(consent-feature-test PRIVATE -UNDEBUG)
  TARGET_LINK_LIBRARIES(consent-feature-test PRIVATE consent-poc consent-common PkgConfig::SYSTEMD ${CMAKE_DL_LIBS})
  ADD_TEST(NAME consent-feature-test COMMAND consent-feature-test)
  INSTALL(TARGETS consent-feature-test RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  SET_TESTS_PROPERTIES(consent-feature-test PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent-poc>")
  ADD_EXECUTABLE(consent-mock-runtime-test runtime_test.cc)
  TARGET_LINK_LIBRARIES(consent-mock-runtime-test PRIVATE consent-poc PkgConfig::GLIB)
  TARGET_COMPILE_OPTIONS(consent-mock-runtime-test PRIVATE -UNDEBUG)
  ADD_TEST(NAME consent-mock-runtime-test COMMAND consent-mock-runtime-test)
  SET_TESTS_PROPERTIES(consent-mock-runtime-test PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent-poc>")
ENDIF()

# Separate test-only executable identity: production mocks contain no gate.
IF(BUILD_TESTING)
  SET(FEATURE_GATE_PREFIX "${CMAKE_INSTALL_FULL_LIBEXECDIR}/consent/tests/consent-feature-gate-")
  ADD_LIBRARY(consent-feature-gate-common STATIC feature_common.cc)
  TARGET_LINK_LIBRARIES(consent-feature-gate-common PUBLIC consent-common)
  TARGET_COMPILE_DEFINITIONS(consent-feature-gate-common PUBLIC
    CONSENT_FEATURE_ARGO_PATH="${FEATURE_GATE_PREFIX}argo"
    CONSENT_FEATURE_WORKER_PREFIX="${FEATURE_GATE_PREFIX}")
  FOREACH(ROLE argo cm ce holder)
    ADD_EXECUTABLE(consent-feature-gate-${ROLE} ${ROLE}.c mock_runtime.cc)
    TARGET_LINK_LIBRARIES(consent-feature-gate-${ROLE} PRIVATE
      consent-poc consent-feature-gate-common PkgConfig::GLIB)
    IF(ROLE STREQUAL "argo")
      TARGET_SOURCES(consent-feature-gate-${ROLE} PRIVATE feature_coordinator.cc feature_gate.cc)
      TARGET_COMPILE_DEFINITIONS(consent-feature-gate-${ROLE} PRIVATE CONSENT_FEATURE_GATE_TEST=1)
      TARGET_LINK_LIBRARIES(consent-feature-gate-${ROLE} PRIVATE PkgConfig::SYSTEMD)
    ELSE()
      TARGET_SOURCES(consent-feature-gate-${ROLE} PRIVATE feature_worker.cc)
    ENDIF()
    INSTALL(TARGETS consent-feature-gate-${ROLE}
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  ENDFOREACH()
ENDIF()
