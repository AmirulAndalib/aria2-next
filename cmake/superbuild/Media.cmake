# Build only the native media client and remuxing libraries. The parent project
# remains CMake-only; dependency builds use their maintained upstream entrypoints.
set(media_tool_args)
set(media_cflags "${CMAKE_C_FLAGS}")
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
  string(APPEND media_cflags " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
if(CMAKE_C_COMPILER)
  list(APPEND media_tool_args --cc=${CMAKE_C_COMPILER})
endif()
set(ffmpeg_tool_args ${media_tool_args})
foreach(tool AR RANLIB)
  if(CMAKE_${tool})
    string(TOLOWER "${tool}" tool_name)
    list(APPEND ffmpeg_tool_args --${tool_name}=${CMAKE_${tool}})
  endif()
endforeach()
if(CMAKE_CROSSCOMPILING)
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" media_os)
  if(WIN32)
    set(media_os mingw32)
  elseif(ANDROID)
    set(media_os android)
  endif()
  list(APPEND ffmpeg_tool_args --enable-cross-compile
    --target-os=${media_os} --arch=${CMAKE_SYSTEM_PROCESSOR})
  list(APPEND media_tool_args --target-os=${media_os}
    --cpu=${CMAKE_SYSTEM_PROCESSOR})
endif()

ExternalProject_Add(ffmpeg_project
  SOURCE_DIR "${ARIA2_VENDOR_ROOT}/ffmpeg"
  BINARY_DIR "${CMAKE_BINARY_DIR}/vendor/ffmpeg"
  CONFIGURE_COMMAND <SOURCE_DIR>/configure
    ${ffmpeg_tool_args} --prefix=${ARIA2_DEPENDENCY_PREFIX}
    "--extra-cflags=${media_cflags}" --enable-pic --enable-static
    --disable-shared --disable-autodetect --disable-everything
    --disable-programs --disable-doc --disable-network --disable-x86asm
    --disable-version-tracking
    --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample
    --enable-avformat --enable-avcodec --enable-avutil
    --enable-protocol=file,concat
    --enable-demuxer=mov,mpegts,aac,ac3,eac3,mp3,flac,ogg,matroska,webvtt,concat
    --enable-muxer=mp4,matroska,webvtt
    --enable-parser=aac,aac_latm,ac3,h264,hevc,av1,vp9,opus,vorbis,flac,mpegaudio
    --enable-decoder=aac,aac_latm,ac3,eac3,mp3,flac,opus,vorbis
    --enable-bsf=aac_adtstoasc,extract_extradata,h264_mp4toannexb,hevc_mp4toannexb
  BUILD_COMMAND ${ARIA2_MAKE_EXECUTABLE} -j${ARIA2_BUILD_JOBS}
  INSTALL_COMMAND ${ARIA2_MAKE_EXECUTABLE} install-libs install-headers
  UPDATE_COMMAND "" TEST_COMMAND "")

set(gpac_packages opensvc openhevc platinum freetype jpeg openjpeg png mad a52
  xvid faad ffmpeg freenect vorbis theora nghttp2 ngtcp2 nghttp3 oss dvb4linux
  alsa pulseaudio jack directfb hid lzma tinygl vtb ogg sdl caption mpeghdec
  libcaca curl)
set(gpac_package_args)
foreach(package IN LISTS gpac_packages)
  list(APPEND gpac_package_args --disable-${package})
endforeach()
ExternalProject_Add(gpac_project
  DEPENDS openssl_project zlib_project
  SOURCE_DIR "${ARIA2_VENDOR_ROOT}/gpac"
  BINARY_DIR "${CMAKE_BINARY_DIR}/vendor/gpac"
  CONFIGURE_COMMAND <SOURCE_DIR>/configure
    --source-path=<SOURCE_DIR> --prefix=${ARIA2_DEPENDENCY_PREFIX}
    ${media_tool_args} "--extra-cflags=${media_cflags}"
    --static-build --std-allocator --disable-all --disable-x11 --disable-rmtws
    --enable-dashin --enable-parsers --enable-isoff --enable-isoff-write --enable-isoff-frag
    --enable-threads --enable-network --enable-net-cap --enable-crypto --enable-log
    --use-zlib=${ARIA2_DEPENDENCY_PREFIX} --use-ssl=${ARIA2_DEPENDENCY_PREFIX}
    ${gpac_package_args}
  BUILD_COMMAND ${ARIA2_MAKE_EXECUTABLE} -C src -j${ARIA2_BUILD_JOBS} lib
  INSTALL_COMMAND ${CMAKE_COMMAND}
    -DSOURCE=<SOURCE_DIR> -DBINARY=<BINARY_DIR>
    -DPREFIX=${ARIA2_DEPENDENCY_PREFIX}
    -P ${CMAKE_SOURCE_DIR}/cmake/scripts/InstallGpac.cmake
  UPDATE_COMMAND "" TEST_COMMAND "")
