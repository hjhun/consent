ADD_LIBRARY(consent-example-common STATIC example_common.c)
TARGET_LINK_LIBRARIES(consent-example-common PUBLIC consent)
TARGET_INCLUDE_DIRECTORIES(consent-example-common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

FOREACH(EXAMPLE check request register offline-register)
  ADD_EXECUTABLE(consent-example-${EXAMPLE} ${EXAMPLE}.c)
  TARGET_LINK_LIBRARIES(consent-example-${EXAMPLE} PRIVATE consent-example-common)
  INSTALL(TARGETS consent-example-${EXAMPLE}
    RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/examples")
ENDFOREACH()
TARGET_LINK_LIBRARIES(consent-example-request PRIVATE PkgConfig::GLIB)

INSTALL(FILES check.c request.c register.c offline-register.c example_common.c
  example_common.h DESTINATION "${CMAKE_INSTALL_DOCDIR}/src/examples")
