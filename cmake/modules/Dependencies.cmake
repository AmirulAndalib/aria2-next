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
    NO_CMAKE_FIND_ROOT_PATH
    REQUIRED)
  find_library(${library_variable}
    NAMES ${DEPENDENCY_LIBRARIES}
    PATHS "${ARIA2_DEPENDENCY_ROOT}/lib"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
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
  LIBRARIES z zs zlibstatic)
aria2_import_dependency(aria2::expat expat.h
  LIBRARIES expat libexpat)
aria2_import_dependency(aria2::sqlite sqlite3.h
  LIBRARIES sqlite3)
aria2_import_dependency(aria2::cares ares.h
  LIBRARIES cares cares_static
  DEFINITIONS $<$<BOOL:${WIN32}>:CARES_STATICLIB>)
unset(OpenSSL_DIR CACHE)
set(OpenSSL_DIR "${ARIA2_DEPENDENCY_ROOT}/lib/cmake/OpenSSL")
find_package(OpenSSL ${ARIA2_MIN_OPENSSL_VERSION} CONFIG REQUIRED
  PATHS "${ARIA2_DEPENDENCY_ROOT}/lib/cmake/OpenSSL"
  NO_DEFAULT_PATH
  NO_CMAKE_FIND_ROOT_PATH)

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
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH)
endif()

unset(CURL_DIR CACHE)
set(CURL_DIR "${ARIA2_DEPENDENCY_ROOT}/lib/cmake/CURL")
set(CARES_INCLUDE_DIR "${ARIA2_DEPENDENCY_ROOT}/include" CACHE PATH "" FORCE)
set(CARES_LIBRARY "${ARIA2_DEPENDENCY_ROOT}/lib/libcares.a" CACHE FILEPATH "" FORCE)
set(CARES_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(LIBSSH2_INCLUDE_DIR "${ARIA2_DEPENDENCY_ROOT}/include" CACHE PATH "" FORCE)
set(LIBSSH2_LIBRARY "${ARIA2_DEPENDENCY_ROOT}/lib/libssh2.a" CACHE FILEPATH "" FORCE)
set(LIBSSH2_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(NGHTTP2_INCLUDE_DIR "${ARIA2_DEPENDENCY_ROOT}/include" CACHE PATH "" FORCE)
set(NGHTTP2_LIBRARY "${ARIA2_DEPENDENCY_ROOT}/lib/libnghttp2.a" CACHE FILEPATH "" FORCE)
set(NGHTTP2_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(ZLIB_ROOT "${ARIA2_DEPENDENCY_ROOT}" CACHE PATH "" FORCE)
set(ZLIB_INCLUDE_DIR "${ARIA2_DEPENDENCY_ROOT}/include" CACHE PATH "" FORCE)
set(ZLIB_LIBRARY "${ARIA2_DEPENDENCY_ROOT}/lib/libz.a" CACHE FILEPATH "" FORCE)
set(ZLIB_LIBRARY_RELEASE "${ARIA2_DEPENDENCY_ROOT}/lib/libz.a" CACHE FILEPATH "" FORCE)
set(ZLIB_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
find_package(CURL CONFIG REQUIRED
  PATHS "${ARIA2_DEPENDENCY_ROOT}/lib/cmake/CURL"
  NO_DEFAULT_PATH
  NO_CMAKE_FIND_ROOT_PATH)
get_target_property(aria2_curl_link_libraries CURL::libcurl_static
  INTERFACE_LINK_LIBRARIES)
list(FILTER aria2_curl_link_libraries EXCLUDE REGEX
  ".*(CURL::cares|ZLIB::ZLIB).*")
set_property(TARGET CURL::libcurl_static PROPERTY INTERFACE_LINK_LIBRARIES
  "${aria2_curl_link_libraries}")
