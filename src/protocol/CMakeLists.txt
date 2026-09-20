SET(CONSENT_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
SET(CONSENT_WIRE_HEADER "${CONSENT_GENERATED_DIR}/consent_wire.hh")

ADD_CUSTOM_COMMAND(OUTPUT "${CONSENT_WIRE_HEADER}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/tools/parcel_codegen.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/consent.idl.json" "${CONSENT_WIRE_HEADER}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/consent.idl.json"
    "${PROJECT_SOURCE_DIR}/src/tools/parcel_codegen.py"
  COMMENT "Generating native Tizen Parcelable message classes"
  VERBATIM)
ADD_CUSTOM_TARGET(consent-wire DEPENDS "${CONSENT_WIRE_HEADER}")
