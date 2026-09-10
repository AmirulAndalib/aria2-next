file(MAKE_DIRECTORY "${PREFIX}/lib")
file(COPY_FILE "${BINARY}/bin/gcc/libgpac_static.a"
  "${PREFIX}/lib/libgpac_static.a" ONLY_IF_DIFFERENT)
file(INSTALL "${SOURCE}/include/gpac" DESTINATION "${PREFIX}/include"
  PATTERN "configuration.h" EXCLUDE)
file(COPY_FILE "${BINARY}/config.h" "${PREFIX}/include/gpac/configuration.h" ONLY_IF_DIFFERENT)
