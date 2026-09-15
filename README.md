# ACAP Quake II

Yamagi Quake II running as an ACAP application on Axis devices.

Initial target:

- ARTPEC-8 and newer
- aarch64
- ACAP Native SDK 12.11
- OpenGL ES 3
- axoverlay2

## Current status

The project currently contains:

- A minimal aarch64 ACAP application
- An ACAP Native SDK 12.11 build container
- Yamagi Quake II as a pinned Git submodule
- ACAP-specific Yamagi build configuration
- Cross-compilation of:
  - `q2ded`
  - `baseq2/game.so`

The next milestone is cross-compiling the Yamagi Quake II client and GLES3 renderer.

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

## Build the ACAP application

Build the minimal ACAP application:

```sh
make build
```

Build the EAP package:

```sh
make eap
```

## Build Yamagi Quake II

Cross-compile the Yamagi Quake II core components:

```sh
make yquake2-core
```

This currently builds:

```text
third_party/yquake2/release/q2ded
third_party/yquake2/release/baseq2/game.so
```

Both binaries are built for aarch64 using the ACAP SDK toolchain.

## Yamagi Quake II

Yamagi Quake II is included as a Git submodule under:

```text
third_party/yquake2
```

The project uses a pinned Yamagi revision to keep builds reproducible.

ACAP-specific Yamagi build configuration is kept outside the upstream source tree in:

```text
yquake2-acap.mk
```

The intention is to keep modifications to upstream Yamagi as small and isolated as possible.

## Graphics plan

The target graphics architecture is:

```text
Yamagi Quake II
      |
      v
OpenGL ES 3 renderer
      |
      v
EGL
      |
      v
axoverlay2 GPU buffer
      |
      v
Axis video stream
```

The first graphics milestone is to render a simple GLES3 frame through `axoverlay2`.

After that, the Yamagi GLES3 renderer will be connected to the same framebuffer path.

## Game data

Quake II game data is not included in this repository.

Do not commit Quake II PAK files.

The project ignores:

```text
*.pak
baseq2/
```

Game data must be supplied separately by the user.

## License

This project is licensed under the GNU General Public License version 2.

Yamagi Quake II and its third-party components retain their own copyright and license notices.
