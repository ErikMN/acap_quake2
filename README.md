# ACAP Quake II

Yamagi Quake II running as an ACAP application on Axis devices.

The project targets aarch64 Axis devices based on ARTPEC-8 and newer. Quake II is rendered with OpenGL ES 3 through axoverlay2 and appears directly in the Axis video stream.

## Status

The game currently runs on Axis hardware with:

- Yamagi Quake II as the ACAP executable
- GPU rendering through EGL and OpenGL ES 3
- axoverlay2 output into the camera video stream
- native `game.so`
- packaged SDL2 runtime
- statically linked libwebsockets
- pinned Quake II demo data fetched during the build
- direct ACAP start and stop support

Keyboard and mouse input through the browser is the next development step.

## Build

Requirements:

- Git
- GNU Make
- Docker

Clone the repository and build the complete ACAP package:

```sh
git clone https://github.com/ErikMN/acap_quake2.git
cd acap_quake2
make acap
```

`make acap` initializes the pinned submodules, builds the ACAP SDK container image, builds all application dependencies, and produces an installable `.eap` file in the repository root.

For build details, individual targets, and development workflows, see [docs/BUILD.md](docs/BUILD.md).

## Install

Install the generated EAP from the Axis device web interface under **Apps**, then start **ACAP Quake II**.

Starting the ACAP launches Quake II directly.

## Game data

Quake II PAK files are not stored in this repository. The build downloads pinned demo data and verifies it before packaging.

See [THIRD_PARTY_DATA.md](THIRD_PARTY_DATA.md) for source and provenance information.

## License

This project is licensed under the GNU General Public License version 2.

Yamagi Quake II, SDL2, libwebsockets, and the Quake II demo data retain their respective copyright and license terms.
