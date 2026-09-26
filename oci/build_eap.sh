#!/usr/bin/env bash
set -euo pipefail

. /opt/axis/acapsdk/environment-setup*

FINAL=${1:-y}

ROOT="$PWD"
STAGE_DIR="$ROOT/build/eap-stage"
DEMO_REPO_DIR="$ROOT/build/quake2-demo"
YQ2_DIR="$ROOT/third_party/yquake2"
SDL_PREFIX="$ROOT/build/sdl2-install"
WEB_BUILD_DIR="$ROOT/web/build"

if [ "$FINAL" = "y" ]; then
  YQ2_BUILD_DIR="$YQ2_DIR/release"
else
  YQ2_BUILD_DIR="$YQ2_DIR/debug"
fi

DEMO_REPO="https://github.com/drags/docker-quake2.git"
DEMO_COMMIT="c8b00cbc4bce0c7bcad8d490af4cd1d45cb4cd7c"
PAK0_BLOB="1b5d5e28410cf6d90f6e1e3fae4c590a811629cf"
PAK1_BLOB="8189343fc45aaa784fab54578405ec88d883642a"
PAK2_BLOB="462bb0d42eba1eeacee45e52a3443f5c5a141574"
DEMO_README_BLOB="94ddd3ecbc15dc185e417cabb38e67eb65ca6ca7"

fetch_commit() {
  local directory="$1"
  local repository="$2"
  local commit="$3"

  rm -rf "$directory"
  mkdir -p "$directory"

  git -C "$directory" init -q
  git -C "$directory" remote add origin "$repository"
  git -C "$directory" -c protocol.version=2 fetch -q --depth 1 --filter=blob:none origin "$commit"
}

extract_file() {
  local directory="$1"
  local path="$2"
  local destination="$3"

  git -C "$directory" show "FETCH_HEAD:$path" >"$destination"
}

verify_blob() {
  local file="$1"
  local expected="$2"
  local actual

  actual="$(git hash-object "$file")"

  if [ "$actual" != "$expected" ]; then
    echo "Unexpected Git blob for $file"
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

echo "Fetching pinned Quake II demo data"
fetch_commit "$DEMO_REPO_DIR" "$DEMO_REPO" "$DEMO_COMMIT"

extract_file "$DEMO_REPO_DIR" "baseq2/pak0.pak" "$STAGE_DIR/baseq2/pak0.pak"
extract_file "$DEMO_REPO_DIR" "baseq2/pak1.pak" "$STAGE_DIR/baseq2/pak1.pak"
extract_file "$DEMO_REPO_DIR" "baseq2/pak2.pak" "$STAGE_DIR/baseq2/pak2.pak"

verify_blob "$STAGE_DIR/baseq2/pak0.pak" "$PAK0_BLOB"
verify_blob "$STAGE_DIR/baseq2/pak1.pak" "$PAK1_BLOB"
verify_blob "$STAGE_DIR/baseq2/pak2.pak" "$PAK2_BLOB"

extract_file "$DEMO_REPO_DIR" "README.md" "$STAGE_DIR/DEMO_DATA_SOURCE.md"
verify_blob "$STAGE_DIR/DEMO_DATA_SOURCE.md" "$DEMO_README_BLOB"

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
    -a baseq2/pak1.pak \
    -a baseq2/pak2.pak \
    -a YAMAGI_LICENSE.txt \
    -a SDL2_LICENSE.txt \
    -a LIBWEBSOCKETS_LICENSE.txt \
    -a DEMO_DATA_SOURCE.md \
    -a THIRD_PARTY_DATA.md
)

cp "$STAGE_DIR"/*.eap "$ROOT/"

echo
echo "Built packages:"
ls -1 "$ROOT"/*.eap
