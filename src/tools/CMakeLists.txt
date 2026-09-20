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
  TARGET_LINK_LIBRARIES(consent-installation-authority PRIVATE consent-common PkgConfig::GLIB)
  TARGET_COMPILE_DEFINITIONS(consent-installation-authority PRIVATE
    CONSENT_AUTHORITY_DIR="${CONSENT_AUTHORITY_DIR}"
    CONSENT_INSTALLATIONS="${CONSENT_INSTALLATIONS}")
  INSTALL(TARGETS consent-installation-authority
    RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}")
  IF(CONSENT_BUILD_TEST_DAEMON)
    ADD_EXECUTABLE(consent-installation-authority-isolated installation_authority.cc)
    TARGET_LINK_LIBRARIES(consent-installation-authority-isolated PRIVATE consent-common PkgConfig::GLIB)
    TARGET_COMPILE_DEFINITIONS(consent-installation-authority-isolated PRIVATE
      CONSENT_TEST_BUILD CONSENT_AUTHORITY_DIR="${CONSENT_TEST_AUTHORITY_DIR}"
      CONSENT_INSTALLATIONS="${CONSENT_TEST_AUTHORITY_DIR}/installations.conf")
    INSTALL(TARGETS consent-installation-authority-isolated
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  ENDIF()
ENDIF()

IF(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/storage_prepare.cc")
  ADD_EXECUTABLE(consent-storage-prepare storage_prepare.cc)
  TARGET_INCLUDE_DIRECTORIES(consent-storage-prepare PRIVATE "${PROJECT_SOURCE_DIR}/src")
  TARGET_COMPILE_DEFINITIONS(consent-storage-prepare PRIVATE
    CONSENT_STATE_DIR="${CONSENT_STATE_DIR}"
    CONSENT_AUTHORITY_DIR="${CONSENT_AUTHORITY_DIR}"
    CONSENT_SERVICE_USER="${CONSENT_SERVICE_USER}"
    CONSENT_SERVICE_UNIT="consentd.service")
  INSTALL(TARGETS consent-storage-prepare
    RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}")
  IF(CONSENT_BUILD_TEST_DAEMON)
    ADD_EXECUTABLE(consent-storage-prepare-isolated storage_prepare.cc)
    TARGET_INCLUDE_DIRECTORIES(consent-storage-prepare-isolated PRIVATE "${PROJECT_SOURCE_DIR}/src")
    TARGET_COMPILE_DEFINITIONS(consent-storage-prepare-isolated PRIVATE
      CONSENT_STATE_DIR="${CONSENT_TEST_STATE_DIR}"
      CONSENT_AUTHORITY_DIR="${CONSENT_TEST_AUTHORITY_DIR}"
      CONSENT_SERVICE_USER="${CONSENT_SERVICE_USER}"
      CONSENT_SERVICE_UNIT="consentd-isolated.service")
    INSTALL(TARGETS consent-storage-prepare-isolated
      RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/tests")
  ENDIF()
ENDIF()

IF(CONSENT_BUILD_POC)
  ADD_EXECUTABLE(consent-installation-authority-poc installation_authority.cc)
  TARGET_LINK_LIBRARIES(consent-installation-authority-poc PRIVATE consent-common PkgConfig::GLIB)
  TARGET_COMPILE_DEFINITIONS(consent-installation-authority-poc PRIVATE
    CONSENT_AUTHORITY_DIR="${CONSENT_POC_AUTHORITY_DIR}"
    CONSENT_INSTALLATIONS="${CONSENT_POC_AUTHORITY_DIR}/installations.conf")
  ADD_EXECUTABLE(consent-storage-prepare-poc storage_prepare.cc)
  TARGET_INCLUDE_DIRECTORIES(consent-storage-prepare-poc PRIVATE "${PROJECT_SOURCE_DIR}/src")
  TARGET_COMPILE_DEFINITIONS(consent-storage-prepare-poc PRIVATE
    CONSENT_STATE_DIR="${CONSENT_POC_STATE_DIR}"
    CONSENT_AUTHORITY_DIR="${CONSENT_POC_AUTHORITY_DIR}"
    CONSENT_SERVICE_USER="${CONSENT_SERVICE_USER}"
    CONSENT_SERVICE_UNIT="consentd-poc.service")
  INSTALL(TARGETS consent-installation-authority-poc consent-storage-prepare-poc
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/poc")
ENDIF()
