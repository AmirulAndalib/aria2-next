cmake_minimum_required(VERSION 3.25)
project(aria2_next_create_torrent LANGUAGES CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(Boost_NO_BOOST_CMAKE ON)

find_package(LibtorrentRasterbar CONFIG REQUIRED)

add_executable(aria2_next_create_torrent create_torrent.cc)
target_link_libraries(aria2_next_create_torrent
  PRIVATE LibtorrentRasterbar::torrent-rasterbar)
