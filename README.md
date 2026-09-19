# ACAP Quake II

Yamagi Quake II running as an ACAP application on Axis devices.

Initial target:

- ARTPEC-8 and newer
- aarch64
- ACAP Native SDK 12.11
- OpenGL ES 3
- axoverlay2

## Current status

The Yamagi Quake II client is running on Axis hardware using:

```text
Yamagi Quake II
      |
      v
OpenGL ES 3
      |
      v
EGL
      |
      v
axoverlay2 DMA-BUF
      |
      v
Axis video stream
```

The current renderer runs at half the stream resolution and uses axoverlay2
2x upscaling. A 1920x1080 stream therefore renders Quake II at 960x540.

The EAP packages Yamagi as the ACAP executable, together with the GLES3
renderer, SDL2 runtime, native `game.so`, and pinned Quake II demo game
data. Starting the ACAP launches `q2dm1` directly.

## Clone

Clone the repository including submodules:

```sh
git clone --recurse-submodules <repository-url>
```

If the repository was cloned without submodules:

```sh
git submodule update --init --recursive
```

## Build environment

Build the ACAP SDK container image:

```sh
make image
```

Open a shell inside the ACAP SDK container:

```sh
make shell
```

## Build the EAP

Build the complete application package:

```sh
make eap
```

This builds SDL2 and Yamagi Quake II, downloads the pinned demo game data,
and creates the EAP in the repository root.

Install the EAP on the Axis device and start `ACAP Quake II`. No manual
copying of binaries, libraries, or PAK files is required.

The packaged `acap_quake2` executable is Yamagi itself. The ACAP-specific
build uses `$ORIGIN/lib` for SDL2, selects the dummy SDL video and audio
backends, uses the ACAP `localdata` directory as Yamagi's writable home,
and starts `q2dm1`.

For development on the target, stop the ACAP first and run:

```sh
cd /usr/local/packages/acap_quake2
./run-quake2.sh
```

When run from a root shell, the script switches to the
`acap-acap_quake2` package user and executes the same `acap_quake2`
binary used by the ACAP service.

## Build Yamagi Quake II manually

Build SDL2 for the ACAP target:

```sh
make sdl2
```

Cross-compile the Yamagi Quake II core components:

```sh
make yquake2-core
```

This builds:

```text
third_party/yquake2/release/q2ded
third_party/yquake2/release/baseq2/game.so
```

Cross-compile the Yamagi Quake II client, GLES3 renderer, and game library:

```sh
make yquake2-client
```

This builds:

```text
third_party/yquake2/release/quake2
third_party/yquake2/release/ref_gles3.so
third_party/yquake2/release/baseq2/game.so
```

## Third-party sources

Yamagi Quake II is included as a pinned Git submodule under:

```text
third_party/yquake2
```

SDL2 is included as a pinned Git submodule under:

```text
third_party/SDL2
```

ACAP-specific Yamagi changes are kept outside the upstream source tree in:

```text
patches/yquake2-acap.patch
yquake2-acap.mk
```

## Game data

Quake II PAK files are not committed to this repository.

The EAP build fetches the pinned game data from `drags/docker-quake2`
and verifies the expected Git blob IDs before packaging it. See
`THIRD_PARTY_DATA.md` for the exact source revisions and provenance.

The game data remains copyrighted by id Software and is not covered by the
GPL license of this project.

## License

This project is licensed under the GNU General Public License version 2.

Yamagi Quake II, SDL2, and the Quake II demo data retain their respective
copyright and license terms. The EAP includes the Yamagi and SDL2 licenses,
plus the pinned demo-data source README and provenance information.
