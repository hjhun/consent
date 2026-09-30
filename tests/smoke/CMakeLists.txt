ADD_LIBRARY(consent-smoke-common STATIC common.c)
TARGET_LINK_LIBRARIES(consent-smoke-common PUBLIC consent-smoke PkgConfig::GLIB)
FOREACH(ROLE installer cm ce argo ui admin)
  SET(SOURCE "${ROLE}.c")
  IF(ROLE STREQUAL "cm" OR ROLE STREQUAL "ce")
    SET(SOURCE check.c)
  ENDIF()
  ADD_EXECUTABLE(consent-smoke-${ROLE} "${SOURCE}")
  TARGET_LINK_LIBRARIES(consent-smoke-${ROLE} PRIVATE consent-smoke-common)
  SET_TARGET_PROPERTIES(consent-smoke-${ROLE} PROPERTIES INSTALL_RPATH
    "${SMOKE_DEST}")
  INSTALL(TARGETS consent-smoke-${ROLE} RUNTIME DESTINATION "${SMOKE_DEST}")
ENDFOREACH()
ADD_EXECUTABLE(consent-smoke-capmgr-preflight capmgr-preflight.c)
TARGET_LINK_LIBRARIES(consent-smoke-capmgr-preflight PRIVATE ${CMAKE_DL_LIBS})
INSTALL(TARGETS consent-smoke-capmgr-preflight RUNTIME DESTINATION
  "${SMOKE_DEST}")

IF(CONSENT_BUILD_SMOKE_TOOLS)
  PKG_CHECK_MODULES(SMOKE_JSON REQUIRED IMPORTED_TARGET json-glib-1.0)
  ADD_LIBRARY(consent-smoke-tool-json STATIC tool-json.c)
  TARGET_LINK_LIBRARIES(consent-smoke-tool-json PUBLIC PkgConfig::SMOKE_JSON)
  IF(BUILD_TESTING)
    ADD_EXECUTABLE(consent-smoke-tool-json-test tool-json-test.c)
    TARGET_LINK_LIBRARIES(consent-smoke-tool-json-test PRIVATE
      consent-smoke-tool-json)
    ADD_TEST(NAME smoke-tool-json-test COMMAND consent-smoke-tool-json-test)
    INSTALL(TARGETS consent-smoke-tool-json-test RUNTIME DESTINATION
      "${SMOKE_DEST}")
  ENDIF()
  ADD_EXECUTABLE(consent-smoke-tool tool.c)
  TARGET_LINK_LIBRARIES(consent-smoke-tool PRIVATE consent-smoke-tool-json)
  INSTALL(TARGETS consent-smoke-tool RUNTIME DESTINATION
    "${SMOKE_DEST}/tool-package/bin")
  INSTALL(FILES cli.json DESTINATION "${SMOKE_DEST}/tool-package")
  IF(IS_ABSOLUTE "${SMOKE_DEST}")
    SET(SMOKE_TOOL_PACKAGE "${SMOKE_DEST}/tool-package")
  ELSE()
    SET(SMOKE_TOOL_PACKAGE "${CMAKE_INSTALL_PREFIX}/${SMOKE_DEST}/tool-package")
  ENDIF()
  FOREACH(ROLE cm ce)
    ADD_EXECUTABLE(consent-smoke-tool-${ROLE}
      tool-check.c tool-process.c tool-gate.c)
    TARGET_LINK_LIBRARIES(consent-smoke-tool-${ROLE} PRIVATE
      consent-smoke-common consent-smoke-tool-json PkgConfig::SQLITE)
    TARGET_COMPILE_DEFINITIONS(consent-smoke-tool-${ROLE} PRIVATE
      SMOKE_TOOL_ROLE="${ROLE}"
      SMOKE_TOOL_PACKAGE="${SMOKE_TOOL_PACKAGE}")
    SET_TARGET_PROPERTIES(consent-smoke-tool-${ROLE} PROPERTIES
      INSTALL_RPATH "$ORIGIN")
    INSTALL(TARGETS consent-smoke-tool-${ROLE} RUNTIME DESTINATION
      "${SMOKE_DEST}")
    ADD_EXECUTABLE(consent-smoke-mock-${ROLE}
      mock-service.c tool-process.c tool-gate.c)
    TARGET_LINK_LIBRARIES(consent-smoke-mock-${ROLE} PRIVATE
      consent-smoke-common consent-smoke-tool-json PkgConfig::SQLITE)
    TARGET_COMPILE_DEFINITIONS(consent-smoke-mock-${ROLE} PRIVATE
      SMOKE_TOOL_ROLE="${ROLE}"
      SMOKE_TOOL_PACKAGE="${SMOKE_TOOL_PACKAGE}")
    SET_TARGET_PROPERTIES(consent-smoke-mock-${ROLE} PROPERTIES
      INSTALL_RPATH "$ORIGIN")
    INSTALL(TARGETS consent-smoke-mock-${ROLE} RUNTIME DESTINATION
      "${SMOKE_DEST}")
  ENDFOREACH()
ENDIF()
