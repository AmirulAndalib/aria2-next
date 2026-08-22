cmake_minimum_required(VERSION 3.25)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h" SQLITE_VERSION_LINE
  REGEX "^#define SQLITE_VERSION +\"[0-9]+\\.[0-9]+\\.[0-9]+\"$")
string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
  SQLITE_SOURCE_VERSION "${SQLITE_VERSION_LINE}")
project(sqlite3 VERSION ${SQLITE_SOURCE_VERSION} LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(sqlite3 STATIC sqlite3.c)
target_include_directories(sqlite3
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_ENABLE_COLUMN_METADATA
  SQLITE_ENABLE_FTS5
  SQLITE_ENABLE_RTREE
  SQLITE_THREADSAFE=1)
set_target_properties(sqlite3 PROPERTIES
  EXPORT_NAME SQLite3
  OUTPUT_NAME sqlite3
  POSITION_INDEPENDENT_CODE ON)

install(TARGETS sqlite3
  EXPORT SQLite3Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES sqlite3.h sqlite3ext.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT SQLite3Targets
  FILE SQLite3Targets.cmake
  NAMESPACE SQLite3::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SQLite3)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/SQLite3ConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SQLite3Config.cmake"
  "include(\"\${CMAKE_CURRENT_LIST_DIR}/SQLite3Targets.cmake\")\n")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/SQLite3Config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/SQLite3ConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SQLite3)
