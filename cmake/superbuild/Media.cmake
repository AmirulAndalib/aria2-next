# Build only the native media client and remuxing libraries. The parent project
# remains CMake-only; dependency builds use their maintained upstream entrypoints.
find_program(ARIA2_POSIX_SHELL NAMES bash sh REQUIRED)
get_filename_component(media_shell_directory "${ARIA2_POSIX_SHELL}" DIRECTORY)
set(media_environment ${CMAKE_COMMAND} -E env
  --modify "PATH=path_list_prepend:${media_shell_directory}")
set(media_tool_args)
set(media_cflags "${CMAKE_C_FLAGS}")
set(media_ldflags "${CMAKE_EXE_LINKER_FLAGS}")
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
  string(APPEND media_cflags " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  string(APPEND media_ldflags " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  foreach(architecture IN LISTS CMAKE_OSX_ARCHITECTURES)
    string(APPEND media_cflags " -arch ${architecture}")
    string(APPEND media_ldflags " -arch ${architecture}")
  endforeach()
endif()
if(CMAKE_C_COMPILER)
  list(APPEND media_tool_args --cc=${CMAKE_C_COMPILER})
endif()
set(ffmpeg_tool_args ${media_tool_args})
if(CMAKE_CXX_COMPILER)
  list(APPEND media_tool_args --cxx=${CMAKE_CXX_COMPILER})
  list(APPEND ffmpeg_tool_args --cxx=${CMAKE_CXX_COMPILER})
endif()
foreach(tool AR RANLIB)
  if(CMAKE_${tool})
    string(TOLOWER "${tool}" tool_name)
    list(APPEND ffmpeg_tool_args --${tool_name}=${CMAKE_${tool}})
    list(APPEND media_environment "${tool}=${CMAKE_${tool}}")
  endif()
endforeach()
if(CMAKE_CROSSCOMPILING OR WIN32)
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" media_os)
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" media_arch)
  if(media_arch MATCHES "^(amd64|x64)$")
    set(media_arch x86_64)
  elseif(media_arch STREQUAL "arm64")
    set(media_arch aarch64)
  endif()
  if(WIN32)
    set(media_os mingw32)
  elseif(ANDROID)
    set(media_os android)
  endif()
  list(APPEND ffmpeg_tool_args --enable-cross-compile
    --target-os=${media_os} --arch=${media_arch})
  list(APPEND media_tool_args --target-os=${media_os}
    --cpu=${media_arch})
endif()

ExternalProject_Add(ffmpeg_project
  SOURCE_DIR "${ARIA2_VENDOR_ROOT}/ffmpeg"
  BINARY_DIR "${CMAKE_BINARY_DIR}/vendor/ffmpeg"
  CONFIGURE_COMMAND ${media_environment} ${ARIA2_POSIX_SHELL} <SOURCE_DIR>/configure
    ${ffmpeg_tool_args} --prefix=${ARIA2_DEPENDENCY_PREFIX}
    "--extra-cflags=${media_cflags}" "--extra-ldflags=${media_ldflags}"
    --enable-pic --enable-static
    --disable-shared --disable-autodetect --disable-everything
    --disable-programs --disable-doc --disable-network --disable-x86asm
    --disable-version-tracking
    --disable-avdevice --disable-avfilter --disable-swscale
    --enable-avformat --enable-avcodec --enable-avutil
    --enable-protocol=file
    --enable-demuxer=mov,mpegts,aac,ac3,eac3,mp3,flac,ogg,matroska,webvtt
    --enable-muxer=mp4,matroska
    --enable-parser=aac,aac_latm,ac3,h264,hevc,av1,vp9,opus,vorbis,flac,mpegaudio
    --enable-decoder=aac,aac_latm,ac3,eac3,mp3,flac,opus,vorbis
    --enable-bsf=aac_adtstoasc,extract_extradata
  BUILD_COMMAND ${media_environment} ${ARIA2_MAKE_EXECUTABLE} -j${ARIA2_BUILD_JOBS}
  INSTALL_COMMAND ${media_environment} ${ARIA2_MAKE_EXECUTABLE} install-libs install-headers
  UPDATE_COMMAND "" TEST_COMMAND "")

set(gpac_packages ssl opensvc openhevc platinum freetype jpeg openjpeg png mad a52
  xvid faad ffmpeg freenect vorbis theora nghttp2 ngtcp2 nghttp3 oss dvb4linux
  alsa pulseaudio jack directfb hid lzma tinygl vtb ogg sdl caption mpeghdec
  libcaca curl)
set(gpac_package_args)
foreach(package IN LISTS gpac_packages)
  list(APPEND gpac_package_args --disable-${package})
endforeach()
ExternalProject_Add(gpac_project
  DEPENDS zlib_project
  SOURCE_DIR "${ARIA2_VENDOR_ROOT}/gpac"
  BINARY_DIR "${CMAKE_BINARY_DIR}/vendor/gpac"
  CONFIGURE_COMMAND ${media_environment} ${ARIA2_POSIX_SHELL} <SOURCE_DIR>/configure
    --prefix=${ARIA2_DEPENDENCY_PREFIX}
    ${media_tool_args} "--extra-cflags=${media_cflags}" "--extra-ldflags=${media_ldflags}"
    --static-build --disable-all --disable-x11 --disable-rmtws
    --enable-dashin --enable-parsers --enable-isoff --enable-isoff-write --enable-isoff-frag
    --enable-threads --enable-network --enable-net-cap --enable-log
    --use-zlib=${ARIA2_DEPENDENCY_PREFIX}
    ${gpac_package_args}
  BUILD_COMMAND ${media_environment} ${ARIA2_MAKE_EXECUTABLE} -C src -j${ARIA2_BUILD_JOBS} lib
  INSTALL_COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR> -DBINARY=<BINARY_DIR>
    -DPREFIX=${ARIA2_DEPENDENCY_PREFIX}
    -P ${CMAKE_SOURCE_DIR}/cmake/scripts/InstallGpac.cmake
  UPDATE_COMMAND "" TEST_COMMAND "")
