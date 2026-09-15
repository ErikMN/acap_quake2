#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

YQ2_DIR="third_party/yquake2"
CONFIG_FILE="$PWD/yquake2-acap.mk"

echo "Building Yamagi Quake II core"
echo "ACAP SDK: $OECORE_SDK_VERSION"
echo "Compiler: $CC"

make -C "$YQ2_DIR" cleanall

make \
  -C "$YQ2_DIR" \
  -j"$(nproc)" \
  CONFIG_FILE="$CONFIG_FILE" \
  PKG_CONFIG=true \
  game \
  server

echo
echo "Build results:"

file "$YQ2_DIR/release/q2ded"
file "$YQ2_DIR/release/baseq2/game.so"
