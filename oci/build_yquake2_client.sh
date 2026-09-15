#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

ROOT="$PWD"
YQ2_DIR="$ROOT/third_party/yquake2"
SDL_PREFIX="$ROOT/build/sdl2-install"
CONFIG_FILE="$ROOT/yquake2-acap.mk"

if [ ! -x "$SDL_PREFIX/bin/sdl2-config" ]; then
  echo "SDL2 has not been built."
  echo "Run make sdl2 first."
  exit 1
fi

export PATH="$SDL_PREFIX/bin:$PATH"

echo "Building Yamagi Quake II client"
echo "ACAP SDK: $OECORE_SDK_VERSION"
echo "Compiler: $CC"
echo "SDL2: $(sdl2-config --version)"
echo "SDL2 cflags: $(sdl2-config --cflags)"
echo "SDL2 libs: $(sdl2-config --libs)"

make -C "$YQ2_DIR" cleanall

make \
  -C "$YQ2_DIR" \
  -j"$(nproc)" \
  CONFIG_FILE="$CONFIG_FILE" \
  INCLUDE="-I$SDL_PREFIX/include" \
  client \
  ref_gles3

echo
echo "Build results:"

file "$YQ2_DIR/release/quake2"
file "$YQ2_DIR/release/ref_gles3.so"

echo
echo "quake2 dependencies:"
readelf -d "$YQ2_DIR/release/quake2" | grep NEEDED || true

echo
echo "ref_gles3.so dependencies:"
readelf -d "$YQ2_DIR/release/ref_gles3.so" | grep NEEDED || true
