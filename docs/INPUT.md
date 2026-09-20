# Browser input

ACAP Quake II receives browser control input through the authenticated Axis WebSocket reverse proxy at `/input`.

The WebSocket server accepts binary messages only. Protocol version 1 uses the first byte for the protocol version and the second byte for the message type.
Multi-byte integers are little-endian.

| Message | Type | Bytes |
| --- | ---: | --- |
| Key | 1 | `version, type, key_lo, key_hi, down` |
| Mouse motion | 2 | `version, type, dx_lo, dx_hi, dy_lo, dy_hi` |
| Mouse button | 3 | `version, type, button, down` |
| Wheel | 4 | `version, type, x, y` |
| Reset | 5 | `version, type` |

`down` is either 0 or 1. Mouse deltas are signed 16-bit values. Wheel values are signed 8-bit values.

Key identifiers are defined by `enum acap_key_code` in `src/input/input_queue.h`.

They represent physical browser controls and are translated to Yamagi key values only on the Yamagi client thread.

Mouse button identifiers follow the browser button layout used by the frontend:

- 0: left
- 1: middle
- 2: right
- 3: back
- 4: forward

WebSocket callbacks only validate and enqueue input. Yamagi drains the queue from `IN_Update()`.

A reset event is generated on connect, disconnect, explicit reset, and queue overflow so held keys cannot remain stuck when input state is lost.

The browser implementation lives in `web/src/input.ts`.
It uses `KeyboardEvent.code` so key identifiers represent physical controls rather
than keyboard-layout-dependent characters. Pointer lock is used for relative
mouse motion. Losing pointer lock, browser focus, or the WebSocket connection
resets held input state.
