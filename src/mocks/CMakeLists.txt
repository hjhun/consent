FOREACH(ROLE argo cm ce holder installer)
  ADD_EXECUTABLE(consent-mock-${ROLE} ${ROLE}.c mock_runtime.cc)
  TARGET_LINK_LIBRARIES(consent-mock-${ROLE} PRIVATE consent-poc PkgConfig::GLIB)
  INSTALL(TARGETS consent-mock-${ROLE}
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/poc")
ENDFOREACH()
INSTALL(DIRECTORY fixtures/ DESTINATION "${CMAKE_INSTALL_DATADIR}/consent/poc/fixtures"
  FILES_MATCHING PATTERN "*.ini")

IF(BUILD_TESTING)
  ADD_EXECUTABLE(consent-mock-runtime-test runtime_test.cc)
  TARGET_LINK_LIBRARIES(consent-mock-runtime-test PRIVATE consent-poc PkgConfig::GLIB)
  TARGET_COMPILE_OPTIONS(consent-mock-runtime-test PRIVATE -UNDEBUG)
  ADD_TEST(NAME consent-mock-runtime-test COMMAND consent-mock-runtime-test)
  SET_TESTS_PROPERTIES(consent-mock-runtime-test PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:consent-poc>")
ENDIF()
