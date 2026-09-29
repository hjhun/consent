ADD_LIBRARY(consent-feature-common STATIC feature_common.cc)
TARGET_LINK_LIBRARIES(consent-feature-common PUBLIC consent-common)
ADD_LIBRARY(consent-feature-poc SHARED feature_bridge.cc)
TARGET_LINK_LIBRARIES(consent-feature-poc PRIVATE consent-feature-common)
SET_TARGET_PROPERTIES(consent-feature-poc PROPERTIES VERSION 0.1.0 SOVERSION 0)
INSTALL(TARGETS consent-feature-poc
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

FOREACH(ROLE argo cm ce holder installer)
  ADD_EXECUTABLE(consent-mock-${ROLE} ${ROLE}.c mock_runtime.cc)
  TARGET_LINK_LIBRARIES(
    consent-mock-${ROLE} PRIVATE consent-poc consent-feature-common
                                 PkgConfig::GLIB
  )
  IF(ROLE STREQUAL "argo")
    TARGET_SOURCES(consent-mock-${ROLE} PRIVATE feature_coordinator.cc)
    TARGET_LINK_LIBRARIES(consent-mock-${ROLE} PRIVATE PkgConfig::SYSTEMD)
  ELSEIF(ROLE STREQUAL "installer")
    TARGET_SOURCES(consent-mock-${ROLE} PRIVATE feature_register.cc)
  ELSE()
    TARGET_SOURCES(consent-mock-${ROLE} PRIVATE feature_worker.cc)
  ENDIF()
  INSTALL(TARGETS consent-mock-${ROLE}
          RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/poc"
  )
ENDFOREACH()
INSTALL(
  DIRECTORY fixtures/
  DESTINATION "${CMAKE_INSTALL_DATADIR}/consent/poc/fixtures"
  FILES_MATCHING
  PATTERN "*.ini"
)
