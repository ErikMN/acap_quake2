#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

ROOT="$PWD"
YQ2_DIR="$ROOT/third_party/yquake2"
SDL_PREFIX="$ROOT/build/sdl2-install"
LWS_PREFIX="$ROOT/build/libwebsockets-install"
CONFIG_FILE="$ROOT/yquake2-acap.mk"
PATCH_FILE="$ROOT/patches/yquake2-acap.patch"
ACAP_BUILD_DIR="$ROOT/build/yquake2-acap"
ACAP_PKGS="egl glesv2 vdostream axoverlay2"

if [ ! -x "$SDL_PREFIX/bin/sdl2-config" ]; then
  echo "SDL2 has not been built."
  echo "Run make sdl2 first."
  exit 1
fi

if [ ! -f "$LWS_PREFIX/lib/libwebsockets.a" ]; then
  echo "libwebsockets has not been built."
  echo "Run make libwebsockets first."
  exit 1
fi

export PATH="$SDL_PREFIX/bin:$PATH"

# The ACAP changes are maintained as a patch outside the Yamagi submodule.
# Restore the pinned revision before applying it so repeated builds start clean.
git -C "$YQ2_DIR" reset --hard HEAD

if ! git -C "$YQ2_DIR" apply --check "$PATCH_FILE"; then
  echo "Yamagi ACAP patch does not apply cleanly."
  exit 1
fi

git -C "$YQ2_DIR" apply "$PATCH_FILE"

echo "Building Yamagi Quake II client"
echo "ACAP SDK: $OECORE_SDK_VERSION"
echo "Compiler: $CC"
echo "SDL2: $(sdl2-config --version)"
echo "SDL2 cflags: $(sdl2-config --cflags)"
echo "SDL2 libs: $(sdl2-config --libs)"
echo "libwebsockets: $(PKG_CONFIG_PATH="$LWS_PREFIX/lib/pkgconfig" pkg-config --modversion libwebsockets)"

make -C "$YQ2_DIR" cleanall

rm -rf "$ACAP_BUILD_DIR"
mkdir -p "$ACAP_BUILD_DIR"

read -r -a CC_CMD <<< "$CC"
read -r -a SDK_CFLAGS <<< "${CFLAGS:-}"
read -r -a ACAP_CFLAGS <<< "$(pkg-config --cflags $ACAP_PKGS)"

"${CC_CMD[@]}" "${SDK_CFLAGS[@]}" -O2 -Wall -fPIC "${ACAP_CFLAGS[@]}" -I"$ROOT/src" \
  -c "$ROOT/src/gpu_context.c" -o "$ACAP_BUILD_DIR/gpu_context.o"

"${CC_CMD[@]}" "${SDK_CFLAGS[@]}" -O2 -Wall -fPIC "${ACAP_CFLAGS[@]}" -I"$ROOT/src" \
  -c "$ROOT/src/overlay.c" -o "$ACAP_BUILD_DIR/overlay.o"

"${CC_CMD[@]}" "${SDK_CFLAGS[@]}" -O2 -Wall -fPIC -pthread \
  -I"$ROOT/src" -I"$LWS_PREFIX/include" \
  -c "$ROOT/src/input/acap_input.c" -o "$ACAP_BUILD_DIR/acap_input.o"

"${CC_CMD[@]}" "${SDK_CFLAGS[@]}" -O2 -Wall -fPIC -pthread \
  -I"$ROOT/src" -I"$LWS_PREFIX/include" \
  -c "$ROOT/src/input/websocket.c" -o "$ACAP_BUILD_DIR/websocket.o"

ACAP_GLES3_OBJS="$ACAP_BUILD_DIR/gpu_context.o $ACAP_BUILD_DIR/overlay.o"
ACAP_GLES3_LDLIBS="$(pkg-config --libs $ACAP_PKGS)"
ACAP_CLIENT_OBJS="$ACAP_BUILD_DIR/acap_input.o $ACAP_BUILD_DIR/websocket.o"

LWS_LDLIBS="$(PKG_CONFIG_PATH="$LWS_PREFIX/lib/pkgconfig" pkg-config --static --libs libwebsockets)"
LWS_ARCHIVE="$LWS_PREFIX/lib/libwebsockets.a"
ACAP_CLIENT_LDLIBS="${LWS_LDLIBS/-lwebsockets/$LWS_ARCHIVE} -pthread"

echo "libwebsockets link flags: $ACAP_CLIENT_LDLIBS"

make \
  -C "$YQ2_DIR" \
  -j"$(nproc)" \
  CONFIG_FILE="$CONFIG_FILE" \
  INCLUDE="-I$SDL_PREFIX/include -I$ROOT/src -DYQ2_ACAP" \
  ACAP_GLES3_OBJS="$ACAP_GLES3_OBJS" \
  ACAP_GLES3_LDLIBS="$ACAP_GLES3_LDLIBS" \
  ACAP_CLIENT_OBJS="$ACAP_CLIENT_OBJS" \
  ACAP_CLIENT_LDLIBS="$ACAP_CLIENT_LDLIBS" \
  client \
  ref_gles3 \
  game

echo
echo "Build results:"

file "$YQ2_DIR/release/quake2"
file "$YQ2_DIR/release/ref_gles3.so"
file "$YQ2_DIR/release/baseq2/game.so"

echo
echo "quake2 dependencies:"
readelf -d "$YQ2_DIR/release/quake2" | grep -E 'NEEDED|RPATH|RUNPATH' || true

echo
echo "ref_gles3.so dependencies:"
readelf -d "$YQ2_DIR/release/ref_gles3.so" | grep -E 'NEEDED|RPATH|RUNPATH' || true
