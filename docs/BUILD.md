# Building ACAP Quake II

This document describes the build setup and development targets for ACAP Quake II.

For an explanation of how the application works, see [ARCHITECTURE.md](ARCHITECTURE.md).

## Requirements

The host system needs:

- Git
- GNU Make
- Docker, or a container runtime with compatible `build` and `run` commands

Run `source ./setuptarget.sh` from Bash; the script also requires `jq`.
SSH deployment helpers additionally use `sshpass`, `ssh`, and `scp`.
The `make log` helper uses Python 3 with Paramiko. `make checksdk` uses `curl`, and `make openweb` uses `xdg-open`.

Node.js and Yarn are required on the host for `make webdev` and for web checks run by the pre-commit hook.
The build image provides Node.js 22 and Yarn Classic 1.22.22. Use the same versions for host-side development.

The ACAP toolchain and target libraries are provided by the Docker image built from `oci/Dockerfile`.

The Dockerfile pins ACAP Native SDK 12.11.0.
Because `manifest.json` omits `compatibleOsVersions.min`, SDK 12.11 sets AXIS OS 12.11 as the package minimum.
The manifest allows installation through major version 13. This version range does not establish hardware support.
The build targets aarch64 devices and needs access to the GPU and the axoverlay2 API.

References:

- [Axis SDK compatibility](https://developer.axis.com/acap/reference/axis-devices-and-compatibility/)
- [Manifest compatible OS versions](https://developer.axis.com/acap/how-to-guides/upgrade-a-manifest/)

## Complete build

For a fresh clone, the recommended command is:

```sh
make acap
```

The `acap` target:

1. Initializes the pinned Git submodules.
2. Builds the ACAP SDK container image.
3. Builds SDL2, libwebsockets, the Quake client and renderer, and the web UI.
4. Downloads the pinned game data and packages an EAP, the installable Axis application file.

The result is an `.eap` file in the repository root.
Run all commands in this guide from the repository root unless another directory is shown.
To select another runtime, set `CONTAINER_RUNTIME`, for example `make acap CONTAINER_RUNTIME=podman`.

Release builds are the default.
Set `FINAL=n` with `make build`, `make yquake2-client`, or `make eap` to build the client with debug settings.
`make eap FINAL=n` packages that debug build.

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

Builds the application and web UI, then packages the EAP using an already-built SDK image.

During normal development this is usually the fastest complete build command.

### Web UI

Build the production web assets in the ACAP build container:

```sh
make web
```

The output is written to `web/build` and copied into the EAP as the application setting page.

For frontend development, install Node.js and Yarn on the host, follow the
[target setup](#target-helper-commands), and start Vite from the same Bash shell:

```sh
source ./setuptarget.sh
make webdev
```

The development server uses port 8080 when it is available; check the URL Vite prints when it starts.
It forwards requests for video, device configuration, and ACAP input to the configured device.
An installed and running ACAP is still needed for game controls.

### SDL2

```sh
make sdl2
```

Cross-compiles the pinned SDL2 submodule into:

```text
build/sdl2-install
```

SDL2 is built with its PipeWire audio backend and dynamically loads the device's `libpipewire-0.3.so.0`.

### Audio playback

Quake II selects SDL's `pipewire` audio driver at startup. Playback defaults to `AudioDevice0Output0`.
Set `PIPEWIRE_NODE` before launching the executable to select another PipeWire output node.

The manifest requests the `pipewire` group conditionally.
Devices without that group can still install the application, but audio needs access to PipeWire and a usable output.
The Axis PipeWire API was introduced in AXIS OS 12.5.
This project requires AXIS OS 12.11 because it is built with ACAP Native SDK 12.11.0.

References:

- [Axis PipeWire API](https://developer.axis.com/acap/reference/supported-apis/#pipewire)
- [Application user permissions][user-permissions]
- [PipeWire stream options](https://docs.pipewire.org/page_man_pipewire_1.html)

[user-permissions]: https://developer.axis.com/acap/how-to-guides/configure-application-user/#conditional-groups

### libwebsockets

```sh
make libwebsockets
```

Cross-compiles the pinned libwebsockets submodule as a static library into:

```text
build/libwebsockets-install
```

The build disables TLS because the Axis platform terminates browser-facing HTTPS/WSS.
The local libwebsockets server therefore does not handle TLS itself.
zlib support is also disabled because the input path does not depend on WebSocket compression.

### Yamagi Quake II client

```sh
make yquake2-client
```

With the default release settings, this builds:

```text
third_party/yquake2/release/quake2
third_party/yquake2/release/ref_gles3.so
third_party/yquake2/release/baseq2/game.so
```

The ACAP build patches Yamagi at build time from:

```text
patches/yquake2-acap.patch
```

Before applying the patch, the build script runs `git reset --hard HEAD` in the Yamagi submodule.
This discards tracked changes at the submodule's current commit.
A normal `make acap` first checks out the submodule revisions recorded by this repository.
It therefore starts from the pinned Yamagi revision.
When running `make yquake2-client` directly, the submodule must already be checked out at the intended revision.

### Yamagi core

```sh
make yquake2-core
```

Builds the dedicated server and native game library without the full client path.
This target does not use `FINAL`; it builds Yamagi's default release output.

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

Generated files are not committed. Their main locations are:

- `build/` for compiled dependencies, ACAP object files, downloaded data, and package staging
- `third_party/yquake2/release/` or `third_party/yquake2/debug/` for Yamagi binaries
- `web/build/` for the production web UI
- the repository root for the finished `.eap`

The EAP build downloads the Quake II 3.14 demo installer from the mirror documented by Yamagi Quake II.
It verifies the installer before extraction and packages only the demo `baseq2/pak0.pak` and `baseq2/players/` data.
See [THIRD_PARTY_DATA.md](THIRD_PARTY_DATA.md) for the exact source and checksum information.

## Target-side development

After installing the EAP, the normal production path is to start **ACAP Quake II** from the Axis application interface.

For manual development on the device, stop the ACAP first and run:

```sh
cd /usr/local/packages/acap_quake2
./run-quake2.sh
```

When launched from a root shell, the helper switches to the ACAP package user.
It then executes the same `acap_quake2` binary used by the service.

### Target helper commands

Create or update `credentials.json` in the repository root with your device settings:

```json
{
  "TARGET_IP": "192.168.0.90",
  "TARGET_USR": "root",
  "TARGET_PWD": "replace-with-your-device-password",
  "TARGET_PORT": "443",
  "TARGET_SSH_PORT": "22"
}
```

Use a device account with the permissions needed by the command. SSH helpers also require SSH access to the device.
Then load the settings in a Bash shell:

```sh
source ./setuptarget.sh
```

The script exports these settings and enables the repository Git hooks.
It also marks tracked `.vscode` files so Git ignores their local changes.
If the credentials file is absent, it creates one with defaults; edit it and source the script again.

To build and install a complete EAP on the configured target:

```sh
make install
```

For native-code iteration, stop the installed ACAP before replacing its binaries, then choose the matching target:

| Changed code | Command | File copied |
| --- | --- | --- |
| Client or input code | `make deploy` | `acap_quake2` |
| Rendering code, including `src/overlay.c` and `src/gpu_context.c` | `make deployref` | `ref_gles3.so` |
| Default bindings in `config/autoexec.cfg` | `make deployconfig` | `baseq2/autoexec.cfg` |

The binary targets rebuild the client and renderer but copy only the listed file.
Restart the ACAP after copying. Use `make install` when you need to update the complete package.

For frontend iteration without reinstalling the EAP, build and copy only the web assets:

```sh
make deployweb
```

Other target helpers:

| Command | Purpose |
| --- | --- |
| `make logon` | Open an SSH shell in the installed package directory |
| `make log` | Follow device logs over SSH |
| `make kill` | Force-stop the ACAP process with SIGKILL |
| `make checksdk` | Read the device's embedded SDK properties |
| `make openweb` | Open the ACAP web page |
| `make deployprofile` | Replace the SSH user's shell profile with the development profile |

The SSH helpers use `TARGET_SSH_PORT` from `setuptarget.sh`.
`TARGET_DIR` defaults to `/usr/local/packages/acap_quake2` and can be overridden on the make command line.

`make deploy` and `make deployref` copy release outputs, so use them with the default `FINAL=y` setting.

## Cleaning

```sh
make clean
```

Removes generated EAP/package files and the production web build.
Compiled dependencies are kept for faster development rebuilds.

To also remove compiled outputs and downloaded dependencies:

```sh
make distclean
```

This also removes the root `build/` tree, web dependencies, generated web metadata, and Yamagi build outputs.
Source files, credentials, and the pinned submodules themselves are preserved.
