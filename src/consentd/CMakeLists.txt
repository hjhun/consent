FILE(GLOB CONSENTD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
SET(REPOSITORY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/repository.cc")
IF(EXISTS "${REPOSITORY_SOURCE}")
  ADD_LIBRARY(consent-storage STATIC "${REPOSITORY_SOURCE}")
  TARGET_COMPILE_DEFINITIONS(consent-storage PRIVATE CONSENT_LOG_TAG="CONSENTD")
  TARGET_INCLUDE_DIRECTORIES(
    consent-storage PUBLIC "${PROJECT_SOURCE_DIR}/src"
                           "${PROJECT_SOURCE_DIR}/src/consent/inc"
  )
  TARGET_LINK_LIBRARIES(
    consent-storage PUBLIC consent-common PkgConfig::SQLITE
                           PkgConfig::CAPI_BASE_COMMON
  )
  LIST(REMOVE_ITEM CONSENTD_SOURCES "${REPOSITORY_SOURCE}")
ENDIF()
IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cc")
  ADD_EXECUTABLE(consentd ${CONSENTD_SOURCES})
  TARGET_INCLUDE_DIRECTORIES(
    consentd PRIVATE "${PROJECT_SOURCE_DIR}/src"
                     "${PROJECT_SOURCE_DIR}/src/consent/inc"
  )
  TARGET_LINK_LIBRARIES(
    consentd
    PRIVATE consent-common
            consent-storage
            PkgConfig::GLIB
            PkgConfig::SQLITE
            PkgConfig::SYSTEMD
            PkgConfig::PKGMGR
            Threads::Threads
  )
  TARGET_COMPILE_DEFINITIONS(
    consentd
    PRIVATE CONSENT_LOG_TAG="CONSENTD"
            CONSENT_SOCKET_PATH="${CONSENT_SOCKET_PATH}"
            CONSENT_STATE_DIR="${CONSENT_STATE_DIR}"
            CONSENT_AUTHORITY_DIR="${CONSENT_AUTHORITY_DIR}"
            CONSENT_SERVICE_UNIT="consentd.service"
            CONSENT_ROLE_CONFIG="${CONSENT_ROLE_CONFIG}"
            CONSENT_INSTALLATIONS="${CONSENT_INSTALLATIONS}"
  )
  SET_PROPERTY(
    TARGET consentd
    APPEND_STRING
    PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections -pie"
  )
  INSTALL(TARGETS consentd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
  IF(CONSENT_BUILD_POC)
    ADD_EXECUTABLE(consentd-poc ${CONSENTD_SOURCES})
    TARGET_INCLUDE_DIRECTORIES(
      consentd-poc PRIVATE "${PROJECT_SOURCE_DIR}/src"
                           "${PROJECT_SOURCE_DIR}/src/consent/inc"
    )
    TARGET_LINK_LIBRARIES(
      consentd-poc
      PRIVATE consent-common
              consent-storage
              PkgConfig::GLIB
              PkgConfig::SQLITE
              PkgConfig::SYSTEMD
              PkgConfig::PKGMGR
              Threads::Threads
    )
    TARGET_COMPILE_DEFINITIONS(
      consentd-poc
      PRIVATE
        CONSENT_LOG_TAG="CONSENTD"
        CONSENT_SOCKET_PATH="${CONSENT_POC_SOCKET_PATH}"
        CONSENT_STATE_DIR="${CONSENT_POC_STATE_DIR}"
        CONSENT_AUTHORITY_DIR="${CONSENT_POC_AUTHORITY_DIR}"
        CONSENT_SERVICE_UNIT="consentd-poc.service"
        CONSENT_ROLE_CONFIG="${CONSENT_POC_ROLE_CONFIG}"
        CONSENT_INSTALLATIONS="${CONSENT_POC_AUTHORITY_DIR}/installations.conf"
    )
    SET_PROPERTY(
      TARGET consentd-poc
      APPEND_STRING
      PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections -pie"
    )
    INSTALL(TARGETS consentd-poc
            RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/poc"
    )
  ENDIF()
  IF(CONSENT_BUILD_TEST_DAEMON)
    SET(TEST_INSTALLATIONS
        "${CONSENT_TEST_AUTHORITY_DIR}/installations.conf"
    )
    FOREACH(TEST_DAEMON consentd-test consentd-shutdown-test)
      ADD_EXECUTABLE(${TEST_DAEMON} ${CONSENTD_SOURCES})
      TARGET_INCLUDE_DIRECTORIES(
        ${TEST_DAEMON} PRIVATE "${PROJECT_SOURCE_DIR}/src"
                               "${PROJECT_SOURCE_DIR}/src/consent/inc"
      )
      TARGET_LINK_LIBRARIES(
        ${TEST_DAEMON}
        PRIVATE consent-common consent-storage PkgConfig::GLIB
                PkgConfig::SQLITE PkgConfig::SYSTEMD Threads::Threads
      )
      TARGET_COMPILE_DEFINITIONS(
        ${TEST_DAEMON}
        PRIVATE
          CONSENT_TEST_BUILD
          CONSENT_LOG_TAG="CONSENTD"
          CONSENT_SOCKET_PATH="${CONSENT_TEST_ROOT}/consent.sock"
          CONSENT_STATE_DIR="${CONSENT_TEST_STATE_DIR}"
          CONSENT_AUTHORITY_DIR="${CONSENT_TEST_AUTHORITY_DIR}"
          CONSENT_SERVICE_UNIT="consentd-isolated.service"
          CONSENT_ROLE_CONFIG="${CONSENT_TEST_ROOT}/roles.conf"
          CONSENT_INSTALLATIONS="${TEST_INSTALLATIONS}"
      )
      IF(TEST_DAEMON STREQUAL "consentd-shutdown-test")
        TARGET_SOURCES(
          ${TEST_DAEMON}
          PRIVATE
            "${PROJECT_SOURCE_DIR}/tests/repository_shutdown_interposer.cc"
        )
        TARGET_LINK_LIBRARIES(${TEST_DAEMON} PRIVATE ${CMAKE_DL_LIBS})
      ENDIF()
      SET_PROPERTY(
        TARGET ${TEST_DAEMON}
        APPEND_STRING
        PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections -pie"
      )
      INSTALL(TARGETS ${TEST_DAEMON}
              RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests"
      )
    ENDFOREACH()
    SET(BOOTSTRAP_AUTHORITY "/opt/var/lib/consent-bootstrap-authority")
    ADD_EXECUTABLE(consentd-bootstrap-test ${CONSENTD_SOURCES})
    TARGET_INCLUDE_DIRECTORIES(
      consentd-bootstrap-test PRIVATE "${PROJECT_SOURCE_DIR}/src"
                                      "${PROJECT_SOURCE_DIR}/src/consent/inc"
    )
    TARGET_LINK_LIBRARIES(
      consentd-bootstrap-test
      PRIVATE consent-common consent-storage PkgConfig::GLIB
              PkgConfig::SQLITE PkgConfig::SYSTEMD Threads::Threads
    )
    TARGET_COMPILE_DEFINITIONS(
      consentd-bootstrap-test
      PRIVATE
        CONSENT_TEST_BUILD
        CONSENT_LOG_TAG="CONSENTD"
        CONSENT_SOCKET_PATH="/tmp/consent-bootstrap-test/consent.sock"
        CONSENT_STATE_DIR="/opt/var/lib/consent-bootstrap-test"
        CONSENT_AUTHORITY_DIR="${BOOTSTRAP_AUTHORITY}"
        CONSENT_ROLE_CONFIG="${CONSENT_ROLE_CONFIG}"
        CONSENT_INSTALLATIONS="${BOOTSTRAP_AUTHORITY}/installations.conf"
        CONSENT_SERVICE_UNIT="consentd-bootstrap-test.service"
    )
    SET_PROPERTY(
      TARGET consentd-bootstrap-test
      APPEND_STRING
      PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections -pie"
    )
    INSTALL(TARGETS consentd-bootstrap-test
            RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests"
    )
    SET(ACTIVATION_ROOT "/tmp/consent-activation-test-12")
    SET(ACTIVATION_INSTALLATIONS
        "/opt/var/lib/consent-activation-authority/installations.conf"
    )
    ADD_EXECUTABLE(consentd-activation-test ${CONSENTD_SOURCES})
    TARGET_INCLUDE_DIRECTORIES(
      consentd-activation-test PRIVATE "${PROJECT_SOURCE_DIR}/src"
                                       "${PROJECT_SOURCE_DIR}/src/consent/inc"
    )
    TARGET_LINK_LIBRARIES(
      consentd-activation-test
      PRIVATE consent-common consent-storage PkgConfig::GLIB
              PkgConfig::SQLITE PkgConfig::SYSTEMD Threads::Threads
    )
    TARGET_COMPILE_DEFINITIONS(
      consentd-activation-test
      PRIVATE
        CONSENT_TEST_BUILD
        CONSENT_LOG_TAG="CONSENTD"
        CONSENT_SOCKET_PATH="${ACTIVATION_ROOT}/consent.sock"
        CONSENT_STATE_DIR="/opt/var/lib/consent-activation-test"
        CONSENT_AUTHORITY_DIR="/opt/var/lib/consent-activation-authority"
        CONSENT_ROLE_CONFIG="${CONSENT_ROLE_CONFIG}"
        CONSENT_INSTALLATIONS="${ACTIVATION_INSTALLATIONS}"
        CONSENT_SERVICE_UNIT="consentd-activation-test.service"
    )
    SET_PROPERTY(
      TARGET consentd-activation-test
      APPEND_STRING
      PROPERTY LINK_FLAGS " -Wl,--as-needed,--gc-sections -pie"
    )
    INSTALL(TARGETS consentd-activation-test
            RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests"
    )
  ENDIF()
ELSE()
  MESSAGE(STATUS "consentd: implementation sources are not present yet")
ENDIF()
