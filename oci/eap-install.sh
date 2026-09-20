#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

echo "Installing ACAP package using ACAP SDK $OECORE_SDK_VERSION"

export axis_device_ip="$TARGET_IP"
export user="$TARGET_USR"
export password="$TARGET_PWD"

eap-install.sh
