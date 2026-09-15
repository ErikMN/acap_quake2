#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

FINAL=${1:-n}

echo "Building acap_quake2"
echo "ACAP SDK: $OECORE_SDK_VERSION"
echo "Target: $SDKTARGETSYSROOT"

make clean
FINAL="$FINAL" make -j"$(nproc)"

file acap_quake2
