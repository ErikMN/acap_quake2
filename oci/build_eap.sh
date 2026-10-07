#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

FINAL=${1:-y}

ROOT="$PWD"
STAGE_DIR="$ROOT/build/eap-stage"
DEMO_DIR="$ROOT/build/quake2-demo"
DEMO_ARCHIVE="$ROOT/build/q2-314-demo-x86.exe"
YQ2_DIR="$ROOT/third_party/yquake2"
SDL_PREFIX="$ROOT/build/sdl2-install"
WEB_BUILD_DIR="$ROOT/web/build"

if [ "$FINAL" = "y" ]; then
  YQ2_BUILD_DIR="$YQ2_DIR/release"
else
  YQ2_BUILD_DIR="$YQ2_DIR/debug"
fi

DEMO_URL="https://deponie.yamagi.org/quake2/idstuff/q2-314-demo-x86.exe"
DEMO_SHA256="7ace5a43983f10d6bdc9d9b6e17a1032ba6223118d389bd170df89b945a04a1e"
PAK0_MD5="27d77240466ec4f3253256832b54db8a"

verify_sha256() {
  local file="$1"
  local expected="$2"
  local actual

  actual="$(sha256sum "$file" | awk '{print $1}')"

  if [ "$actual" != "$expected" ]; then
    echo "Unexpected SHA-256 for $file"
    echo "Expected: $expected"
    echo "Actual:   $actual"
    exit 1
  fi
}

verify_md5() {
  local file="$1"
  local expected="$2"
  local actual

  actual="$(md5sum "$file" | awk '{print $1}')"

  if [ "$actual" != "$expected" ]; then
    echo "Unexpected MD5 for $file"
    echo "Expected: $expected"
    echo "Actual:   $actual"
    exit 1
  fi
}

echo "Building ACAP Quake II package"
echo "ACAP SDK: $OECORE_SDK_VERSION"

for file in \
  "$YQ2_BUILD_DIR/quake2" \
  "$YQ2_BUILD_DIR/ref_gles3.so" \
  "$YQ2_BUILD_DIR/baseq2/game.so" \
  "$SDL_PREFIX/lib/libSDL2-2.0.so.0"; do
  if [ ! -e "$file" ]; then
    echo "Missing build artifact: $file"
    echo "Run make yquake2-client first."
    exit 1
  fi
done

if [ ! -e "$WEB_BUILD_DIR/index.html" ]; then
  echo "Missing web build: $WEB_BUILD_DIR/index.html"
  echo "Run make web first."
  exit 1
fi

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/baseq2" "$STAGE_DIR/lib" "$STAGE_DIR/html"

cp "$YQ2_BUILD_DIR/quake2" "$STAGE_DIR/acap_quake2"
cp "$ROOT/manifest.json" "$STAGE_DIR/manifest.json"
cp "$ROOT/LICENSE" "$STAGE_DIR/LICENSE"
cp "$ROOT/scripts/run-quake2.sh" "$STAGE_DIR/run-quake2.sh"
chmod 0755 "$STAGE_DIR/run-quake2.sh"
cp "$YQ2_BUILD_DIR/ref_gles3.so" "$STAGE_DIR/ref_gles3.so"
cp "$YQ2_BUILD_DIR/baseq2/game.so" "$STAGE_DIR/baseq2/game.so"
cp "$ROOT/config/autoexec.cfg" "$STAGE_DIR/baseq2/autoexec.cfg"
cp -L "$SDL_PREFIX/lib/libSDL2-2.0.so.0" "$STAGE_DIR/lib/libSDL2-2.0.so.0"

cp "$YQ2_DIR/LICENSE" "$STAGE_DIR/YAMAGI_LICENSE.txt"
cp "$ROOT/third_party/SDL2/LICENSE.txt" "$STAGE_DIR/SDL2_LICENSE.txt"
cp "$ROOT/third_party/libwebsockets/LICENSE" "$STAGE_DIR/LIBWEBSOCKETS_LICENSE.txt"
cp "$ROOT/docs/THIRD_PARTY_DATA.md" "$STAGE_DIR/THIRD_PARTY_DATA.md"
cp -R "$WEB_BUILD_DIR"/. "$STAGE_DIR/html/"

echo "Fetching Quake II 3.14 demo data"
rm -f "$DEMO_ARCHIVE"
curl --fail --location --silent --show-error \
  "$DEMO_URL" \
  --output "$DEMO_ARCHIVE"
verify_sha256 "$DEMO_ARCHIVE" "$DEMO_SHA256"

rm -rf "$DEMO_DIR"
mkdir -p "$DEMO_DIR"
unzip -q "$DEMO_ARCHIVE" -d "$DEMO_DIR"

DEMO_DATA_DIR="$DEMO_DIR/Install/Data/baseq2"

if [ ! -f "$DEMO_DATA_DIR/pak0.pak" ]; then
  echo "Missing demo data: $DEMO_DATA_DIR/pak0.pak"
  exit 1
fi

if [ ! -d "$DEMO_DATA_DIR/players" ]; then
  echo "Missing demo data: $DEMO_DATA_DIR/players"
  exit 1
fi

verify_md5 "$DEMO_DATA_DIR/pak0.pak" "$PAK0_MD5"

cp "$DEMO_DATA_DIR/pak0.pak" "$STAGE_DIR/baseq2/pak0.pak"
cp -R "$DEMO_DATA_DIR/players" "$STAGE_DIR/baseq2/players"

cat >"$STAGE_DIR/Makefile" <<'EOF'
.PHONY: all
all:
	@true
EOF

(
  cd "$STAGE_DIR"

  acap-build . \
    -a ref_gles3.so \
    -a run-quake2.sh \
    -a baseq2/game.so \
    -a baseq2/autoexec.cfg \
    -a baseq2/pak0.pak \
    -a 'baseq2/players/female/*' \
    -a 'baseq2/players/male/*' \
    -a YAMAGI_LICENSE.txt \
    -a SDL2_LICENSE.txt \
    -a LIBWEBSOCKETS_LICENSE.txt \
    -a THIRD_PARTY_DATA.md
)

cp "$STAGE_DIR"/*.eap "$ROOT/"

echo
echo "Built packages:"
ls -1 "$ROOT"/*.eap
