#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

ROOT="$PWD"
LWS_DIR="$ROOT/third_party/libwebsockets"
BUILD_DIR="$ROOT/build/libwebsockets"
PREFIX="$ROOT/build/libwebsockets-install"

if [ ! -f "$LWS_DIR/CMakeLists.txt" ]; then
  echo "libwebsockets submodule is not initialized."
  echo "Run git submodule update --init --recursive."
  exit 1
fi

rm -rf "$BUILD_DIR" "$PREFIX"
mkdir -p "$BUILD_DIR" "$PREFIX"

read -r -a CC_CMD <<<"$CC"

cmake \
  -S "$LWS_DIR" \
  -B "$BUILD_DIR" \
  -G Ninja \
  -D CMAKE_BUILD_TYPE=Release \
  -D CMAKE_INSTALL_PREFIX="$PREFIX" \
  -D CMAKE_SYSTEM_NAME=Linux \
  -D CMAKE_SYSTEM_PROCESSOR=aarch64 \
  -D CMAKE_C_COMPILER="${CC_CMD[0]}" \
  -D CMAKE_C_FLAGS="${CFLAGS:-}" \
  -D CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -D CMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON \
  -D CMAKE_FIND_ROOT_PATH="$SDKTARGETSYSROOT" \
  -D CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
  -D CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
  -D CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
  -D LWS_WITH_STATIC=ON \
  -D LWS_WITH_SHARED=OFF \
  -D LWS_WITH_SSL=OFF \
  -D LWS_WITH_ZLIB=OFF \
  -D LWS_WITHOUT_TESTAPPS=ON \
  -D LWS_HAVE_LIBCAP=OFF

cmake --build "$BUILD_DIR"
cmake --install "$BUILD_DIR"

echo
echo "libwebsockets build results:"
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --modversion libwebsockets
file "$PREFIX/lib/libwebsockets.a"
