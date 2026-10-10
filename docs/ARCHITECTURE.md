# ACAP Quake II Architecture

## Table of contents

- [ACAP Quake II Architecture](#acap-quake-ii-architecture)
  - [Table of contents](#table-of-contents)
  - [Purpose](#purpose)
  - [1. High-level architecture](#1-high-level-architecture)
    - [Browser video playback](#browser-video-playback)
  - [2. Yamagi integration](#2-yamagi-integration)
    - [Renderer hooks](#renderer-hooks)
  - [3. Graphics context](#3-graphics-context)
  - [4. Video stream and overlay setup](#4-video-stream-and-overlay-setup)
    - [Render size](#render-size)
  - [5. Private Quake framebuffer](#5-private-quake-framebuffer)
  - [6. Importing axoverlay2 buffers](#6-importing-axoverlay2-buffers)
  - [7. Frame lifecycle](#7-frame-lifecycle)
    - [Beginning the frame](#beginning-the-frame)
    - [Completing the frame](#completing-the-frame)
    - [Final alpha](#final-alpha)
    - [Synchronization](#synchronization)
  - [8. Input architecture](#8-input-architecture)
    - [Browser input handler](#browser-input-handler)
    - [WebSocket server](#websocket-server)
    - [Protocol decoder](#protocol-decoder)
      - [Wire format](#wire-format)
    - [Input queue](#input-queue)
    - [Yamagi input adapter](#yamagi-input-adapter)
  - [9. Runtime setup and audio](#9-runtime-setup-and-audio)
  - [10. Resource and thread ownership](#10-resource-and-thread-ownership)
  - [11. Where to look when debugging](#11-where-to-look-when-debugging)
  - [12. Recommended reading order](#12-recommended-reading-order)

## Purpose

ACAP (AXIS Camera Application Platform) lets applications run on Axis devices.
This guide follows browser controls into Quake II and rendered game frames back to the browser.

Start with the overview below. For rendering details, continue through sections 2 to 7.
For the control connection, jump to [Input architecture](#8-input-architecture).
The [reading order](#12-recommended-reading-order) points to the implementation when you want to explore the code.

Related documentation:

- [BUILD.md](BUILD.md) covers build, packaging, and target-side development.
- [THIRD_PARTY_DATA.md](THIRD_PARTY_DATA.md) explains where the packaged demo data comes from.

Yamagi Quake II remains the game engine.
It provides the game loop, game logic, input handling, audio handling, and GLES3 renderer.

The ACAP-specific code adapts the parts that do not map directly to the camera environment.
It provides an EGL and OpenGL ES context without a desktop window.
It sends rendered frames to a camera video stream through axoverlay2.
It receives keyboard and mouse input from the browser through the Axis ACAP WebSocket reverse proxy.

The port keeps these platform-specific changes outside Yamagi where practical.
The Yamagi patch contains the integration points that connect the engine to the ACAP-specific code.

## 1. High-level architecture

There are four main parts:

1. The browser plays the camera video stream and captures keyboard and mouse input.
2. The ACAP input layer receives browser input and converts it to events Yamagi understands.
3. Yamagi Quake II runs the game and renders each frame.
4. The ACAP graphics layer provides the GPU context and sends completed frames to the camera video stream.

```mermaid
flowchart TD
    Browser[Browser]
    Proxy[Axis ACAP reverse proxy]
    Input[ACAP input layer<br/>src/input/]
    Yamagi[Yamagi Quake II]
    Overlay[ACAP graphics output<br/>src/overlay.c]
    Stream[Camera video stream]
    GPU[GPU context<br/>src/gpu_context.c]

    Browser -->|Authenticated WebSocket| Proxy
    Proxy -->|Loopback WebSocket| Input
    Input -->|Key and mouse events| Yamagi
    GPU -.->|EGL and OpenGL ES context| Yamagi
    Yamagi -->|Rendered frame| Overlay
    Overlay -->|axoverlay2| Stream
    Stream -->|Video playback| Browser
```

[`gpu_context.c`](../src/gpu_context.c) sets up the graphics context before rendering starts.
[`overlay.c`](../src/overlay.c) manages the output buffers and submits completed frames to the video stream.

### Browser video playback

The Axis device serves the camera video stream containing the Quake overlay to the browser.
[`VideoPlayer.tsx`](../web/src/components/VideoPlayer.tsx) creates the player using the current page's host
and the saved video parameters. It passes these settings to
[`CustomPlayer.tsx`](../web/src/components/player/CustomPlayer.tsx), which uses `PlaybackArea`
from `media-stream-player` to display the stream.

Video playback and game input use separate connections.
The browser sends controls through the ACAP `input` WebSocket endpoint
while the player receives the camera video stream.

## 2. Yamagi integration

The ACAP changes to Yamagi are stored in [yquake2-acap.patch](../patches/yquake2-acap.patch).
The client executable runs the game.
`ref_gles3.so` is the renderer library it loads.

The patch performs these main tasks:

1. Links the ACAP input objects into the Quake client.
2. Links `gpu_context.o` and `overlay.o` into `ref_gles3.so`.
3. Replaces SDL OpenGL context creation with `gpu_context_init()` for ACAP builds.
4. Redirects final framebuffer handling and frame presentation to `overlay.c`.
5. Starts and stops the ACAP input system from Yamagi's normal input lifecycle.
6. Adds ACAP-specific runtime defaults during startup.

The patch does not replace the Yamagi renderer.
It changes how the renderer obtains its graphics context and where its completed frame is presented.

### Renderer hooks

A framebuffer tells OpenGL which images to draw into.
The private Quake framebuffer holds the finished game image before it is copied into an overlay buffer.

The patch adds `GL3_BeginOutputFrame()`.
On ACAP it calls `overlay_context_begin_frame()`.

The patch also adds `GL3_BindOutputFramebuffer()`.
On ACAP it calls `overlay_context_bind_frame()`.
When an axoverlay2 output buffer is active, that function binds the private framebuffer owned by `overlay.c`.
Otherwise it binds framebuffer 0, the default framebuffer provided by the EGL surface.
A normal desktop build uses framebuffer 0 for the window.

Yamagi uses temporary framebuffers during rendering.
During an active ACAP output frame, Yamagi must return to the private Quake framebuffer instead of framebuffer 0.

The patch also changes `GL3_SwapWindow()`.
A normal build calls `SDL_GL_SwapWindow()`.
The ACAP build calls `overlay_context_end_frame()`.

## 3. Graphics context

[`src/gpu_context.c`](../src/gpu_context.c) creates the graphics context used by the renderer.
OpenGL ES provides drawing commands. The context stores the graphics state and objects those commands use.
EGL creates the context and connects it to the device graphics system.

A normal Yamagi desktop build lets SDL create an OpenGL context for a window.
The ACAP build creates its own graphics context with EGL.

It creates:

- an EGL display, which is a connection to the graphics system
- an OpenGL ES 3 context
- a 1x1 EGL pbuffer, a small drawing surface in memory

The 1x1 pbuffer is not the game image.
It gives EGL a valid surface so the OpenGL ES context can be made current, meaning active on the calling thread.
The image sent to the video stream is rendered into a framebuffer created by `overlay.c`.

```mermaid
flowchart TD
    Display[eglGetDisplay]
    Init[eglInitialize]
    API[eglBindAPI]
    Config[eglChooseConfig]
    Context[eglCreateContext]
    Pbuffer[eglCreatePbufferSurface]
    Current[eglMakeCurrent]

    Display --> Init --> API --> Config --> Context --> Pbuffer --> Current
```

`gpu_context_destroy()` releases the current context and destroys the pbuffer and context.
It then terminates the EGL display connection.

## 4. Video stream and overlay setup

The renderer listens to VDO stream lifecycle events through stream 0 with the `overlay` filter.

Unlike the original single-stream implementation, the context keeps one small `overlay_stream` record for every
active VDO stream. Each record owns:

- the VDO stream ID and reported dimensions,
- one concrete Axoverlay2 overlay,
- the detailed Axoverlay2 format,
- the EGL/OpenGL imports for that overlay's rotating buffers,
- retry timing for stream-local creation failures.

The records are intentionally synchronous. There is no overlay worker thread and no asynchronous SDK result which can
outlive the VDO event that caused it.

### Stream lifecycle

`EXISTING` and `CREATED` register or refresh the matching stream record.
If an already known stream reports different dimensions, only that stream's concrete overlay is recreated.

`CLOSED` removes only the matching record and concrete overlay. Other streams remain active.

Temporary creation failures are retried once per second from the normal render loop. A failure on one stream does not
tear down the other stream overlays.

### Shared Quake render target

Quake still renders exactly once.

The first successfully created concrete overlay selects the private Quake framebuffer size. That shared render target
remains stable while any VDO stream record exists. If another stream uses a different overlay size, the completed
Quake image is scaled by the final GPU blit into that stream's output buffer.

When the final VDO stream closes, the shared render target is released. A later stream may then choose a new render
size.

## 5. Private Quake framebuffer

Quake renders its final image into one application-owned OpenGL framebuffer.

This framebuffer is independent of Axoverlay2 buffer availability. A frame can therefore complete normally even when
the selected video stream has no output buffer ready.

No CPU readback is used. When an Axoverlay2 output buffer is available, the completed image stays on the GPU and is
copied with `glBlitFramebuffer()`.

## 6. Importing Axoverlay2 buffers

Axoverlay2 rotates through several output buffers for each concrete overlay.

The first time a buffer ID is returned for a stream, its DMA-BUF is imported into EGL and attached to an OpenGL
texture and framebuffer. The resulting `render_surface` is cached on that stream record and reused when the same
buffer returns later.

Caches are per stream, so identical buffer IDs from two different concrete overlays cannot be confused.

When a concrete overlay is removed or recreated, all imported surfaces belonging to that stream are destroyed first.

## 7. Frame lifecycle

The renderer deliberately keeps the same synchronous Axoverlay2 execution model as `main`.

There are no Axoverlay2 worker threads and no concurrent SDK calls.

### Beginning the frame

`overlay_context_begin_frame()`:

1. drains pending VDO events,
2. retries any stream-local overlay creation whose retry delay expired,
3. selects one active stream round-robin,
4. performs at most one `axo_get_buffer()` call,
5. binds the shared Quake framebuffer.

Only one potentially blocking acquisition is attempted per game frame regardless of the number of active streams.
This is the central CPU and complexity constraint of this design.

With two active streams the acquisition order is approximately:

```text
frame 1 -> stream A
frame 2 -> stream B
frame 3 -> stream A
frame 4 -> stream B
```

A stream returning `AXO_ERR_WAIT` simply receives no presentation for that turn. The next game frame advances to the
next stream instead of polling the same stream in a background loop.

`AXO_ERR_NO_STREAM` is treated as the documented disconnect race: the acquisition is skipped and the stream record
is left intact until the corresponding VDO `CLOSED` event removes it.

### Completing the frame

Quake renders once into the shared framebuffer.

If begin-frame acquired an output buffer, `overlay_context_end_frame()`:

1. blits the completed Quake image into that stream's imported framebuffer,
2. forces the output alpha channel to 1.0,
3. calls `glFinish()` because manual DMA synchronization is enabled,
4. submits that one buffer with `axo_submit_buffer()`.

If no buffer was acquired, the Quake frame still completes but there is nothing to submit.

A failed submission is isolated to that stream. Its concrete overlay is removed and recreated later instead of
terminating the game or disturbing other streams.

### Performance tradeoff

This design intentionally favors a small synchronous implementation over maximum per-stream presentation frequency.

Supporting N streams does not perform N blocking acquisitions per Quake frame and does not create background polling
threads. Instead, the streams share output opportunities round-robin.

That should keep CPU behavior much closer to the original single-stream implementation while fixing the lifecycle bug
where only one stream could own Quake.

The tradeoff is that with multiple active streams each stream receives a fraction of the available presentation
opportunities. This is preferable to adding an autonomous SDK worker until real device measurements demonstrate that
the synchronous Axoverlay2 wait time is unacceptable.

## 8. Input architecture

A WebSocket keeps a connection open so the browser can send controls as they happen.
Inside the ACAP process, two threads share this work: one handles the connection, and one runs the game.

The WebSocket server runs on its own thread.
Yamagi consumes the queued input from its normal game thread during `IN_Update()`.

```mermaid
flowchart TD
    Browser[Browser input handler]
    Proxy[Axis ACAP reverse proxy]

    subgraph WS[WebSocket thread]
        Socket[libwebsockets server]
        Decode[input_protocol.c]
    end

    subgraph Game[Yamagi game thread]
        Adapter[yamagi_input.c]
        Yamagi[Yamagi input system]
    end

    Queue[Shared input queue<br/>input_queue.c]

    Browser -->|Binary WebSocket messages| Proxy
    Proxy -->|Loopback WebSocket| Socket
    Socket --> Decode
    Decode --> Queue
    Queue -->|acap_input_next_event| Adapter
    Adapter -->|Key_Event and mouse deltas| Yamagi
```

The input queue is the handoff between the two threads.
The WebSocket thread does not call Yamagi input functions directly.

### Browser input handler

[`App.tsx`](../web/src/components/App.tsx) opens the control connection and retries two seconds after it closes.
[`getBackendWebSocketUrl.ts`](../web/src/components/getBackendWebSocketUrl.ts) builds `/local/acap_quake2/input`
on the current page's host. It uses `wss` for an HTTPS page and `ws` for an HTTP page.

[`QuakeInputHandler.tsx`](../web/src/components/QuakeInputHandler.tsx) captures keys, mouse movement,
buttons, and wheel input. It sends the small binary messages described in the wire format below.

Clicking the game area focuses it and requests pointer lock, which captures the mouse for relative movement.
This lets the player keep turning without the cursor reaching the edge of the screen.
Mouse movement, buttons, and wheel input are sent while pointer lock is active.
Keyboard input is accepted while the game area has pointer lock or keyboard focus.
The handler uses `KeyboardEvent.code`, so keys represent physical positions rather than typed characters.
It sends messages only while the WebSocket is open.
Controls used while disconnected are not saved for later.

A reset asks the game to release held controls, for example when a key release is missed after switching tabs.
The browser sends it when pointer lock is lost after capture, the window loses focus, or the page becomes hidden.

### WebSocket server

The Axis web server authenticates the browser and forwards its control connection to the app.
This forwarding is the reverse proxy: the browser uses the device web address while the app listens locally.
[`manifest.json`](../manifest.json) maps the `input` path to `ws://127.0.0.1:9000` and requires `admin` access.
[`websocket.c`](../src/input/websocket.c) runs libwebsockets on its own thread and listens on loopback,
which is reachable only from within the device.

The callback accepts complete binary messages.
Non-binary messages and fragmented messages are ignored.

Connection, disconnection, and message events are passed to callbacks registered by `acap_input.c`.

### Protocol decoder

[`input_protocol.c`](../src/input/input_protocol.c) checks each packet and produces an `acap_input_event`
for valid input. Invalid packets are discarded.

It checks:

- protocol version
- message type and packet length
- key code range
- button range
- pressed or released values where applicable

#### Wire format

Protocol version 1 uses byte 0 for the protocol version and byte 1 for the message type.
Each table entry lists the bytes of one complete message.
Multi-byte integers use little-endian byte order: the low byte comes before the high byte.

| Message | Type | Bytes |
| --- | ---: | --- |
| Key | 1 | `version, type, key_lo, key_hi, down` |
| Mouse motion | 2 | `version, type, dx_lo, dx_hi, dy_lo, dy_hi` |
| Mouse button | 3 | `version, type, button, down` |
| Wheel | 4 | `version, type, x, y` |
| Reset | 5 | `version, type` |

`down` must be 0 or 1. Mouse deltas are signed 16-bit integers. Wheel values are signed 8-bit integers.
The browser normalizes each wheel axis to -1, 0, or 1 before sending it.
The Yamagi adapter currently uses only vertical wheel value `y`.
Horizontal `x` is decoded but ignored.

For example, pressing physical key `KeyW` sends the decimal bytes `1, 1, 23, 0, 1`.
These mean protocol version 1, key message, key ID 23, and pressed. Releasing it changes the last byte to 0.

Key identifiers are defined by `enum acap_key_code` in [`input_queue.h`](../src/input/input_queue.h).
They represent physical browser controls and are translated to Yamagi key values on the game thread.

Mouse button identifiers match the `MouseEvent.button` values supported by the frontend:

- 0: left
- 1: middle
- 2: right
- 3: back
- 4: forward

### Input queue

[`input_queue.c`](../src/input/input_queue.c) holds up to 256 events in arrival order.
A mutex, a lock shared by both threads, prevents them from changing the queue at the same time.
The WebSocket thread adds events, and the game thread removes them in the same order.

If the queue is full, [`acap_input.c`](../src/input/acap_input.c) discards the queued events
and the event that could not fit, then inserts a reset.
It also queues a reset when a client connects or disconnects.
There is one shared queue and one set of held controls: all connected browsers control the same game.

### Yamagi input adapter

[`yamagi_input.c`](../src/input/yamagi_input.c) translates queued ACAP events into Yamagi input.

It maps:

- letters and digits to their character values
- special keys to Yamagi key constants
- mouse buttons to `K_MOUSE1` through `K_MOUSE5`
- vertical wheel movement to `K_MWHEELUP` or `K_MWHEELDOWN`

Relative mouse movement is added to Yamagi's `mouse_x` and `mouse_y` values
only while input is directed to gameplay and the game is not paused.
Mouse movement is discarded while input is directed to menus or the console.

A wheel event is sent to Yamagi as an immediate key press followed by a key release.

`config/autoexec.cfg` binds the wheel keys to Yamagi's normal weapon commands:

```cfg
bind MWHEELDOWN weapprev
bind MWHEELUP weapnext
```

The same binding system turns the `KeyW` example into forward movement: the adapter produces a `w` key event,
and `config/autoexec.cfg` binds `w` to `+forward`.
The input protocol sends controls, and Yamagi decides their actions.

For reset events, `yamagi_input.c` releases the keys and mouse buttons it tracks.
It then calls `Key_MarkAllUp()` and clears accumulated mouse movement.

## 9. Runtime setup and audio

The patch adds ACAP-specific startup defaults in Yamagi's `src/backends/unix/main.c`.

It adds these command-line arguments before any user-supplied arguments:

- `-datadir` with the package binary directory
- `+set vid_ref gles3`
- `+set vid_fullscreen 0`
- `+set r_msaa_samples 0`
- `+set r_vsync 0`
- `+map q2dm1`

It creates `localdata` inside the package directory and assigns that path to `HOME`.

It also sets:

- `SDL_VIDEODRIVER=dummy`
- `SDL_AUDIODRIVER=pipewire`
- `PIPEWIRE_NODE=AudioDevice0Output0` if `PIPEWIRE_NODE` is not already set

The process then changes its working directory to the package binary directory.

SDL remains part of Yamagi.
For the ACAP build, EGL replaces SDL as the source of the OpenGL context.
SDL audio uses PipeWire to send game sound to an output on the device.
See [Audio playback](BUILD.md#audio-playback) for output selection and permissions.

## 10. Resource and thread ownership

Here, ownership means responsibility for keeping a resource valid and releasing it when finished.

- `gpu_context` owns the EGL display, OpenGL ES context, and 1x1 pbuffer.
- `overlay_context` owns the VDO event stream and the resources associated with the active overlay.
- `render_surface` owns the EGL and OpenGL objects created for one imported axoverlay2 buffer.
- The WebSocket thread owns network servicing and produces decoded input events.
- The input queue transfers events from the WebSocket thread to the Yamagi game thread.
- The Yamagi game thread consumes ACAP input and runs the renderer calls that use the ACAP graphics path.

Shutdown stops the WebSocket thread before destroying the input queue.
The renderer releases overlay graphics resources before destroying the EGL context they depend on.

## 11. Where to look when debugging

- For EGL or OpenGL context setup, start with `src/gpu_context.c`.
- For VDO stream discovery or overlay creation, start with `src/overlay.c`.
- For buffer import, frame copy, alpha handling, or submission, start with `src/overlay.c`.
- For WebSocket reception, start with `src/input/websocket.c`.
- For rejected input packets, start with `src/input/input_protocol.c`.
- For queue ordering or overflow behavior, start with `src/input/input_queue.c` and `src/input/acap_input.c`.
- For incorrect Quake key or mouse behavior, start with `src/input/yamagi_input.c`.
- For mouse wheel weapon bindings, start with `config/autoexec.cfg`.

`overlay_context_render_frame()` is a test helper that draws a changing red and green image through the overlay path.
The Yamagi renderer does not call it. It is compiled into the renderer but is not a selectable game mode.

## 12. Recommended reading order

For the graphics path:

1. `src/gpu_context.h`
2. `src/gpu_context.c`
3. `src/overlay.h`
4. `overlay_context_init()`
5. `create_overlay()`
6. `create_render_target()`
7. `overlay_context_begin_frame()`
8. `create_render_surface()`
9. `overlay_context_end_frame()`
10. The graphics-related parts of `patches/yquake2-acap.patch`

For the input path:

1. `manifest.json` for the reverse-proxy endpoint
2. `web/src/components/getBackendWebSocketUrl.ts`
3. `web/src/components/QuakeInputHandler.tsx`
4. `src/input/input_protocol.h` and `src/input/input_protocol.c`
5. `src/input/websocket.c`
6. `src/input/acap_input.c`
7. `src/input/input_queue.h` and `src/input/input_queue.c`
8. `src/input/yamagi_input.c`
9. The input-related parts of `patches/yquake2-acap.patch`

Read the patch after the custom source files.
Most patch changes are easier to understand once the functions they call are already familiar.

[axoverlay2-api]: https://developer.axis.com/acap/acap-native-sdk-version-12/api/src/api/axoverlay_v2/html/index.html
