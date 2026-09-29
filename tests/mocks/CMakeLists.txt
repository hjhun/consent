SET(MOCK_SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/mocks")
IF(BUILD_TESTING)
  ADD_EXECUTABLE(consent-feature-test feature_test.cc
               "${MOCK_SOURCE_DIR}/feature_common.cc")
  TARGET_INCLUDE_DIRECTORIES(consent-feature-test PRIVATE "${MOCK_SOURCE_DIR}")
  TARGET_COMPILE_DEFINITIONS(
    consent-feature-test PRIVATE CONSENT_FEATURE_UNIT_TEST=1
  )
  TARGET_COMPILE_OPTIONS(consent-feature-test PRIVATE -UNDEBUG)
  TARGET_LINK_LIBRARIES(
    consent-feature-test PRIVATE consent-poc consent-common PkgConfig::SYSTEMD
                                 ${CMAKE_DL_LIBS}
  )
  ADD_TEST(NAME consent-feature-test COMMAND consent-feature-test)
  INSTALL(TARGETS consent-feature-test
          RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests"
  )
  SET_TESTS_PROPERTIES(
    consent-feature-test
    PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent-poc>"
  )
  ADD_EXECUTABLE(consent-mock-runtime-test runtime_test.cc)
  TARGET_INCLUDE_DIRECTORIES(consent-mock-runtime-test
                             PRIVATE "${MOCK_SOURCE_DIR}")
  TARGET_LINK_LIBRARIES(
    consent-mock-runtime-test PRIVATE consent-poc PkgConfig::GLIB
  )
  TARGET_COMPILE_OPTIONS(consent-mock-runtime-test PRIVATE -UNDEBUG)
  ADD_TEST(NAME consent-mock-runtime-test COMMAND consent-mock-runtime-test)
  SET_TESTS_PROPERTIES(
    consent-mock-runtime-test
    PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent-poc>"
  )
ENDIF()

# Separate test-only executable identity: production mocks contain no gate.
IF(BUILD_TESTING)
  SET(FEATURE_GATE_PREFIX
      "${CMAKE_INSTALL_FULL_LIBEXECDIR}/consent/tests/consent-feature-gate-"
  )
  ADD_LIBRARY(consent-feature-gate-common STATIC
              "${MOCK_SOURCE_DIR}/feature_common.cc")
  TARGET_LINK_LIBRARIES(consent-feature-gate-common PUBLIC consent-common)
  TARGET_COMPILE_DEFINITIONS(
    consent-feature-gate-common
    PUBLIC CONSENT_FEATURE_ARGO_PATH="${FEATURE_GATE_PREFIX}argo"
           CONSENT_FEATURE_WORKER_PREFIX="${FEATURE_GATE_PREFIX}"
  )
  FOREACH(ROLE argo cm ce holder)
    ADD_EXECUTABLE(consent-feature-gate-${ROLE}
                   "${MOCK_SOURCE_DIR}/${ROLE}.c"
                   "${MOCK_SOURCE_DIR}/mock_runtime.cc")
    TARGET_LINK_LIBRARIES(
      consent-feature-gate-${ROLE}
      PRIVATE consent-poc consent-feature-gate-common PkgConfig::GLIB
    )
    IF(ROLE STREQUAL "argo")
      TARGET_SOURCES(
        consent-feature-gate-${ROLE}
        PRIVATE "${MOCK_SOURCE_DIR}/feature_coordinator.cc"
                                             feature_gate.cc
      )
      TARGET_INCLUDE_DIRECTORIES(consent-feature-gate-${ROLE}
                                 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
      TARGET_COMPILE_DEFINITIONS(
        consent-feature-gate-${ROLE} PRIVATE CONSENT_FEATURE_GATE_TEST=1
      )
      TARGET_LINK_LIBRARIES(
        consent-feature-gate-${ROLE} PRIVATE PkgConfig::SYSTEMD
      )
    ELSE()
      TARGET_SOURCES(consent-feature-gate-${ROLE}
                     PRIVATE "${MOCK_SOURCE_DIR}/feature_worker.cc")
    ENDIF()
    INSTALL(TARGETS consent-feature-gate-${ROLE}
            RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests"
    )
  ENDFOREACH()
ENDIF()
