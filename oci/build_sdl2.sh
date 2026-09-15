#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

ROOT="$PWD"
SDL_DIR="$ROOT/third_party/SDL2"
BUILD_DIR="$ROOT/build/sdl2-build"
PREFIX="$ROOT/build/sdl2-install"

CC_BIN="${CC%% *}"
HOST_TRIPLET="$("$CC_BIN" -dumpmachine)"

echo "Building SDL2"
echo "ACAP SDK: $OECORE_SDK_VERSION"
echo "Compiler: $CC_BIN"
echo "Host triplet: $HOST_TRIPLET"

rm -rf "$BUILD_DIR" "$PREFIX"
mkdir -p "$BUILD_DIR" "$PREFIX"

cd "$BUILD_DIR"

"$SDL_DIR/configure" \
  --host="$HOST_TRIPLET" \
  --prefix="$PREFIX" \
  --disable-static \
  --enable-shared \
  --disable-rpath \
  --disable-video-x11 \
  --disable-video-wayland \
  --disable-video-kmsdrm \
  --disable-video-vulkan \
  --disable-video-opengl \
  --enable-video-opengles \
  --enable-video-opengles2 \
  --enable-video-dummy \
  --disable-alsa \
  --disable-pulseaudio \
  --disable-pipewire \
  --disable-jack \
  --disable-libudev \
  --disable-dbus

make -j"$(nproc)"
make install

echo
echo "SDL2 build results:"

"$PREFIX/bin/sdl2-config" --version
"$PREFIX/bin/sdl2-config" --cflags
"$PREFIX/bin/sdl2-config" --libs

echo
file -L "$PREFIX/lib/libSDL2.so"
