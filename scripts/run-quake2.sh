#!/bin/sh
set -eu

PACKAGE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

if [ "$(id -u)" -eq 0 ]; then
  exec su -s /bin/sh -c "cd '$PACKAGE_DIR' && exec ./acap_quake2" acap-acap_quake2
fi

cd "$PACKAGE_DIR"
exec ./acap_quake2
