# Building ACAP Quake II

This document describes the build setup and development targets for ACAP Quake II.

## Requirements

The host system needs:

- Git
- GNU Make
- Docker

Target deployment helpers additionally use `sshpass`. The `make log` helper
uses Python 3 with Paramiko.

Node.js and Yarn are only required on the host when running the web development
server. The ACAP build image contains its own Node.js and Yarn installation.

The ACAP toolchain and target libraries are provided by the Docker image built from `oci/Dockerfile`.

## Complete build

For a fresh clone, the recommended command is:

```sh
make acap
```

The `acap` target runs these steps in order:

```text
submodules
    |
    v
ACAP SDK Docker image
    |
    v
SDL2
    |
    v
libwebsockets
    |
    v
Yamagi Quake II
    |
    v
Web UI
    |
    v
EAP packaging
```

The result is an `.eap` file in the repository root.

The submodule step uses:

```sh
git submodule update --init --recursive
```

so a normal Git clone is sufficient.

## Build targets

### Complete ACAP package

```sh
make acap
```

Initializes submodules, builds the SDK image, and builds the final EAP.

### SDK image

```sh
make image
```

Builds the aarch64 ACAP Native SDK container image used by the other build targets.

This normally only needs to be rerun when `oci/Dockerfile` or the SDK configuration changes.

### EAP only

```sh
make eap
```

Builds the application, builds the web UI, and packages the EAP using an
already-built SDK image.

During normal development this is usually the fastest complete build command.

### Web UI

Build the production web assets in the ACAP build container:

```sh
make web
```

The output is written to `web/build` and is copied into the EAP as the
application setting page.

For frontend development, install Node.js and Yarn on the host, configure the
target device, and start the Vite development server:

```sh
source ./setuptarget.sh
make webdev
```

The development server listens on port 8080 and proxies the Axis video,
package-manager, and ACAP input endpoints to the configured target device.

### SDL2

```sh
make sdl2
```

Cross-compiles the pinned SDL2 submodule into:

```text
build/sdl2-install
```

### libwebsockets

```sh
make libwebsockets
```

Cross-compiles the pinned libwebsockets submodule as a static library into:

```text
build/libwebsockets-install
```

The build disables TLS and zlib support because external HTTPS/WSS termination is handled by the Axis platform.

### Yamagi Quake II client

```sh
make yquake2-client
```

Builds:

```text
third_party/yquake2/release/quake2
third_party/yquake2/release/ref_gles3.so
third_party/yquake2/release/baseq2/game.so
```

The ACAP build patches Yamagi at build time from:

```text
patches/yquake2-acap.patch
```

The pinned Yamagi submodule is reset before the patch is applied so repeated builds start from the same upstream revision.

### Yamagi core

```sh
make yquake2-core
```

Builds the dedicated server and native game library without the full client path.

### SDK shell

```sh
make shell
```

Opens an interactive shell in the ACAP SDK container.

## Dependency layout

Pinned source dependencies are stored as Git submodules:

```text
third_party/yquake2
third_party/SDL2
third_party/libwebsockets
```

Generated build artifacts are kept under `build/` and are not committed.

The EAP build also fetches pinned Quake II demo PAK files and verifies their expected Git blob IDs before packaging them.
See `THIRD_PARTY_DATA.md` for the exact source information.

## Target-side development

After installing the EAP, the normal production path is to start **ACAP Quake II** from the Axis application interface.

For manual development on the device, stop the ACAP first and run:

```sh
cd /usr/local/packages/acap_quake2
./run-quake2.sh
```

When launched from a root shell, the helper switches to the ACAP package user and executes the same `acap_quake2` binary used by the service.

### Target helper commands

First configure the target in the current shell:

```sh
source ./setuptarget.sh
```

To build and install a complete EAP on the configured target:

```sh
make install
```

For fast native-code iteration, rebuild and copy only the Quake II executable
into an already installed ACAP:

```sh
make deploy
```

For frontend iteration without reinstalling the EAP, build and copy only the
web assets:

```sh
make deployweb
```

Other target helpers:

```sh
make logon
make log
make kill
make checksdk
make openweb
make deployprofile
```

The deployment helpers use `TARGET_SSH_PORT` from `setuptarget.sh`.
`TARGET_DIR` defaults to `/usr/local/packages/acap_quake2` and can be
overridden on the make command line if needed.

## Cleaning

```sh
make clean
```

Removes generated EAP/package files and the production web build while keeping
compiled dependencies available for fast development rebuilds.

To remove all generated build artifacts:

```sh
make distclean
```

This additionally removes the root `build/` tree, web dependencies and
generated web metadata, and Yamagi Quake II build, debug, and release output.
Source files, credentials, and the pinned submodules themselves are preserved.
