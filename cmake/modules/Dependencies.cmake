function(aria2_import_dependency target header)
  set(options)
  set(one_value_args)
  set(multi_value_args LIBRARIES DEFINITIONS)
  cmake_parse_arguments(DEPENDENCY
    "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  string(MAKE_C_IDENTIFIER "${target}" identifier)
  string(TOUPPER "${identifier}" identifier)
  set(include_variable "ARIA2_${identifier}_INCLUDE_DIR")
  set(library_variable "ARIA2_${identifier}_LIBRARY")
  unset(${include_variable} CACHE)
  unset(${library_variable} CACHE)

  find_path(${include_variable}
    NAMES "${header}"
    PATHS "${ARIA2_DEPENDENCY_ROOT}/include"
    NO_DEFAULT_PATH
    REQUIRED)
  find_library(${library_variable}
    NAMES ${DEPENDENCY_LIBRARIES}
    PATHS "${ARIA2_DEPENDENCY_ROOT}/lib"
    NO_DEFAULT_PATH
    REQUIRED)

  add_library(${target} UNKNOWN IMPORTED)
  set_target_properties(${target} PROPERTIES
    IMPORTED_LOCATION "${${library_variable}}"
    INTERFACE_INCLUDE_DIRECTORIES "${${include_variable}}")
  if(DEPENDENCY_DEFINITIONS)
    set_property(TARGET ${target} PROPERTY
      INTERFACE_COMPILE_DEFINITIONS "${DEPENDENCY_DEFINITIONS}")
  endif()
endfunction()

aria2_import_dependency(aria2::zlib zlib.h
  LIBRARIES z zlibstatic)
aria2_import_dependency(aria2::expat expat.h
  LIBRARIES expat libexpat)
aria2_import_dependency(aria2::sqlite sqlite3.h
  LIBRARIES sqlite3)
aria2_import_dependency(aria2::cares ares.h
  LIBRARIES cares cares_static
  DEFINITIONS $<$<BOOL:${WIN32}>:CARES_STATICLIB>)
aria2_import_dependency(aria2::libssh2 libssh2.h
  LIBRARIES ssh2 libssh2 libssh2_static
  DEFINITIONS $<$<BOOL:${WIN32}>:LIBSSH2_STATIC>)

unset(OpenSSL_DIR CACHE)
set(OpenSSL_DIR "${ARIA2_DEPENDENCY_ROOT}/lib/cmake/OpenSSL")
find_package(OpenSSL ${ARIA2_MIN_OPENSSL_VERSION} CONFIG REQUIRED
  PATHS "${ARIA2_DEPENDENCY_ROOT}/lib/cmake/OpenSSL"
  NO_DEFAULT_PATH)

if(WIN32)
  set_property(TARGET aria2::libssh2 PROPERTY
    INTERFACE_LINK_LIBRARIES "bcrypt;crypt32")
else()
  set_property(TARGET aria2::libssh2 PROPERTY
    INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
endif()

if(ARIA2_ENABLE_BITTORRENT)
  unset(LibtorrentRasterbar_DIR CACHE)
  unset(Boost_DIR CACHE)
  unset(Boost_INCLUDE_DIR CACHE)
  set(Boost_NO_BOOST_CMAKE ON)
  set(Boost_NO_SYSTEM_PATHS ON)
  set(Boost_INCLUDE_DIR "${ARIA2_BOOST_ROOT}")
  set(BOOST_ROOT "${ARIA2_BOOST_ROOT}")
  find_package(LibtorrentRasterbar ${ARIA2_MIN_LIBTORRENT_VERSION}
    CONFIG REQUIRED
    PATHS "${ARIA2_DEPENDENCY_ROOT}/lib/cmake/LibtorrentRasterbar"
    NO_DEFAULT_PATH)
endif()
