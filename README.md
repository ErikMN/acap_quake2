# ACAP Quake II

<table border="2" cellpadding="10" cellspacing="0" width="100%">
  <tr>
    <td align="center">
      <strong>⚠️ IMPORTANT ⚠️</strong><br/>
      This application is <strong>NOT affiliated</strong> with id Software LLC or Axis Communications AB.<br/>
      <strong>Please read and respect the LICENSE</strong> to ensure compliance.<br/>
      <strong>All assets, libraries, and tools are the properties of their respective owners.</strong><br/>
      <strong>FOR DEMO AND EDUCATIONAL PURPOSE ONLY</strong>
    </td>
  </tr>
</table>

<table border="2" cellpadding="10" cellspacing="0" width="100%">
  <tr>
    <td align="center">
      <strong>⚠️ UNOFFICIAL APP ⚠️</strong><br/>
      Requires unsigned ACAP packages to be enabled on the device (no longer an option with AXIS OS 13).
    </td>
  </tr>
</table>

<table border="2" cellpadding="10" cellspacing="0" width="100%">
  <tr>
    <td align="center">
      <strong>⚠️ IMPORTANT ⚠️</strong><br/>
      <strong>This application is NOT actively maintained.</strong><br/>
      It may not work on all devices or firmware versions.<br/>
      Requires AXIS OS firmware version <strong>12.5</strong> or later, and is validated up to AXIS OS <strong>13</strong>.
    </td>
  </tr>
</table>

## What is this?

[Yamagi Quake II](https://www.yamagi.org/quake2/) running as an ACAP application on Axis devices.

The project targets aarch64 Axis devices based on **ARTPEC-8 and newer**.

Quake II is rendered with OpenGL ES 3 through axoverlay2 and appears directly in the Axis video stream.

## Status

The game currently runs on Axis hardware with:

- Yamagi Quake II as the ACAP executable
- GPU rendering through EGL and OpenGL ES 3
- axoverlay2 output into the camera video stream
- Pinned Quake II demo data fetched during the build
- Included web interface with live video and browser controls
- Authenticated WebSocket keyboard and mouse input

The web interface can start and stop the application, display the Axis video
stream, and capture keyboard and pointer-lock mouse input for Quake II.

## Build

Requirements:

- Git
- GNU Make
- Docker (or alternative container runtime)

Clone the repository and build the complete ACAP package:

```sh
git clone https://github.com/ErikMN/acap_quake2.git
cd acap_quake2
make acap
```

`make acap` initializes the pinned submodules, builds the ACAP SDK container image, builds all application dependencies,
and produces an installable `.eap` file in the repository root.

For build details, individual targets, and development workflows, see [docs/BUILD.md](docs/BUILD.md).

## Install

Install the generated ACAP package from the Axis device web interface under **Apps**, then start **ACAP Quake II**.

Starting the ACAP launches Quake II directly.

## Game data

Quake II PAK files are not stored in this repository. The build downloads pinned demo data and verifies it before packaging.

See [docs/THIRD_PARTY_DATA.md](docs/THIRD_PARTY_DATA.md) for source and provenance information.

## FAQ

### Q: What is "ACAP"?

**A:** AXIS Camera Application Platform: An open application platform for software-based solutions built around Axis devices.
More info [here](https://www.axis.com/developer-community/open-source/acap).

### Q: Why can't I control the game?

**A:** Controls only work within the ACAP Quake II webpage.

### Q: Why are the controls slow or unresponsive?

**A:** There are a few steps you can take to improve the responsiveness of your game input:

1. **Try turning off other running ACAP applications.**
2. **Reload the video stream or the application UI.**
3. **Lower the stream resolution.**
4. **Ensure you are on the same network as your device for optimal performance.**

### Q: Why does it not work on my device?

**A:** Only tested on a limited set of ARTPEC-8 and ARTPEC-9 devices.

### Q: Which browsers are supported?

**A:** Tested on latest stable versions of Chrome and Firefox. Other browsers are unverified.

### Q: Why does it not install?

**A:** You will need either [sign the ACAP package](https://www.axis.com/support/acap-signing) or enable unsigned ACAP
packages *(no longer an option with AXIS OS 13 unless using a developer mode device)* on the Apps page:
**`Allow unsigned apps`**
Also ensure that the application matches the device architecture.

### Q: Why am I not hearing any sounds?

**A:** You need to plug speakers or headphones to the audio output jack of your device.

### Q: Does it only work on devices with an ARTPEC chip?

**A:** Yes, it currently only works on devices based on ARTPEC-8 or newer ARTPEC SoCs.

### Q: What is the purpose of this?

**A:** This project is primarily a technical experiment demonstrating what is possible with the ACAP SDK.

## Screenshots

<img src="images/screenshot.png" width="800" alt="screenshot"/>

## License

This project is licensed under the GNU General Public License version 2.

Yamagi Quake II, SDL2, libwebsockets, and the Quake II demo data retain their respective copyright and license terms.
