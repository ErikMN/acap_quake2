#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

FINAL=${1:-n}

echo "Building ACAP Quake II package"
echo "ACAP SDK: $OECORE_SDK_VERSION"

make clean
FINAL="$FINAL" make -j"$(nproc)"

acap-build .

echo
echo "Built packages:"
ls -1 ./*.eap
