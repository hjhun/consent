PKG_CHECK_MODULES(APP_CONTROL REQUIRED IMPORTED_TARGET capi-appfw-app-control)
ADD_EXECUTABLE(consent-poc-launch launch.c)
TARGET_LINK_LIBRARIES(consent-poc-launch PRIVATE PkgConfig::APP_CONTROL)
ADD_EXECUTABLE(consent-poc-peer-probe peer_probe.cc)
TARGET_LINK_LIBRARIES(consent-poc-peer-probe PRIVATE PkgConfig::SYSTEMD)
INSTALL(TARGETS consent-poc-launch consent-poc-peer-probe
  RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/consent/poc")
