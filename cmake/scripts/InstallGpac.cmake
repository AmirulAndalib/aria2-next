file(INSTALL "${BINARY}/bin/gcc/libgpac_static.a" DESTINATION "${PREFIX}/lib")
file(INSTALL "${SOURCE}/include/gpac" DESTINATION "${PREFIX}/include")
file(COPY_FILE "${BINARY}/config.h" "${PREFIX}/include/gpac/configuration.h")
