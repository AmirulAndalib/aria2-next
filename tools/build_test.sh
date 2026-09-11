#!/bin/sh
set -eu

GENERATOR=${GENERATOR:-Ninja}
JOBS=${JOBS:-2}

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILDDIR=${BUILDDIR:-"$source_dir/build/option-matrix"}

if [ "$#" -eq 0 ]; then
  set -- default nobt noml nobt_noml nowebsocket noepoll libaria2
fi
for name in "$@"; do
  case "$name" in
    default|nobt|noml|nobt_noml|nowebsocket|noepoll|libaria2) ;;
    *) echo "Unknown matrix configuration: $name" >&2; exit 2 ;;
  esac
done

# CMake reads CMAKE_TOOLCHAIN_FILE, CC, CXX and compile flags from the environment.
# An existing dependency prefix must have been built with the same toolchain.
dependency_root=${ARIA2_DEPENDENCY_ROOT:-}
if [ -z "$dependency_root" ]; then
  dependency_build="$BUILDDIR/dependencies-build"
  cmake --fresh -S "$source_dir" -B "$dependency_build" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Debug
  cmake --build "$dependency_build" --parallel "$JOBS" --target aria2_dependencies
  dependency_root="$dependency_build/dependencies"
fi

build() {
  name=$1
  shift
  dir="$BUILDDIR/$name"
  echo "*** cmake build $name"
  cmake --fresh -S "$source_dir" -B "$dir" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DARIA2_SUPERBUILD=OFF \
    -DARIA2_DEPENDENCY_ROOT="$dependency_root" \
    -DARIA2_BOOST_ROOT="$source_dir/third_party/boost" \
    "$@"
  cmake --build "$dir" --parallel "$JOBS"
  ctest --test-dir "$dir" --output-on-failure
}

for name in "$@"; do
  case "$name" in
    default) build default ;;
    nobt) build nobt -DARIA2_ENABLE_BITTORRENT=OFF ;;
    noml) build noml -DARIA2_ENABLE_METALINK=OFF ;;
    nobt_noml) build nobt_noml -DARIA2_ENABLE_BITTORRENT=OFF -DARIA2_ENABLE_METALINK=OFF ;;
    nowebsocket) build nowebsocket -DARIA2_ENABLE_WEBSOCKET=OFF ;;
    noepoll) build noepoll -DARIA2_ENABLE_EPOLL=OFF ;;
    libaria2) build libaria2 -DARIA2_ENABLE_LIBARIA2=ON ;;
  esac
done
