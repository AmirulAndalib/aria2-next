#!/bin/sh
set -e

GENERATOR=${GENERATOR:-Ninja}
JOBS=${JOBS:-2}

cleanup_build_dir=false
if [ -z "${BUILDDIR:-}" ]; then
  BUILDDIR=$(mktemp -d "${TMPDIR:-/tmp}/aria2buildtest.XXXXXX")
  cleanup_build_dir=true
fi

cleanup() {
  if [ "$cleanup_build_dir" = true ]; then
    cmake -E remove_directory "$BUILDDIR"
  fi
}
trap cleanup EXIT HUP INT TERM

dependency_build="$BUILDDIR/dependencies-build"
cmake -E remove_directory "$dependency_build"
cmake --fresh -S . -B "$dependency_build" -G "$GENERATOR" \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build "$dependency_build" -j"$JOBS" --target \
  zlib_project expat_project sqlite_project cares_project \
  openssl_project libssh2_project libtorrent_project

dependency_root="$dependency_build/dependencies"

build() {
  name=$1
  shift
  dir="$BUILDDIR/$name"
  echo "*** cmake build $name"
  cmake -E remove_directory "$dir"
  cmake --fresh -S . -B "$dir" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DARIA2_SUPERBUILD=OFF \
    -DARIA2_DEPENDENCY_ROOT="$dependency_root" \
    -DARIA2_BOOST_ROOT="$PWD/third_party/boost" \
    "$@"
  cmake --build "$dir" -j"$JOBS"
  ctest --test-dir "$dir" --output-on-failure
}

case "$1" in
  *)
    build default
    build nossl -DARIA2_ENABLE_SSL=OFF
    build nobt -DARIA2_ENABLE_BITTORRENT=OFF
    build noml -DARIA2_ENABLE_METALINK=OFF
    build nobt_noml -DARIA2_ENABLE_BITTORRENT=OFF -DARIA2_ENABLE_METALINK=OFF
    build nowebsocket -DARIA2_ENABLE_WEBSOCKET=OFF
    build noepoll -DARIA2_ENABLE_EPOLL=OFF
    build libaria2 -DARIA2_ENABLE_LIBARIA2=ON
    ;;
esac
