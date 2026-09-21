/*
 * Captures keyboard and mouse controls in the browser.
 * It sends small input messages to the ACAP while the game has focus.
 */

import { RefObject, useEffect, useRef } from 'react';

interface QuakeInputHandlerProps {
  socketRef: RefObject<WebSocket | null>;
  targetRef: RefObject<HTMLDivElement | null>;
  onCaptureChange: (captured: boolean) => void;
}

const PROTOCOL_VERSION = 1;

enum MessageType {
  Key = 1,
  Mouse = 2,
  Button = 3,
  Wheel = 4,
  Reset = 5
}

enum AcapKey {
  A = 1,
  B = 2,
  C = 3,
  D = 4,
  E = 5,
  F = 6,
  G = 7,
  H = 8,
  I = 9,
  J = 10,
  K = 11,
  L = 12,
  M = 13,
  N = 14,
  O = 15,
  P = 16,
  Q = 17,
  R = 18,
  S = 19,
  T = 20,
  U = 21,
  V = 22,
  W = 23,
  X = 24,
  Y = 25,
  Z = 26,
  Digit0 = 27,
  Digit1 = 28,
  Digit2 = 29,
  Digit3 = 30,
  Digit4 = 31,
  Digit5 = 32,
  Digit6 = 33,
  Digit7 = 34,
  Digit8 = 35,
  Digit9 = 36,
  Escape = 37,
  Tab = 38,
  Enter = 39,
  Space = 40,
  Backspace = 41,
  ShiftLeft = 42,
  ShiftRight = 43,
  ControlLeft = 44,
  ControlRight = 45,
  AltLeft = 46,
  AltRight = 47,
  ArrowUp = 48,
  ArrowDown = 49,
  ArrowLeft = 50,
  ArrowRight = 51
}

const keyCodes: Record<string, AcapKey> = {
  KeyA: AcapKey.A,
  KeyB: AcapKey.B,
  KeyC: AcapKey.C,
  KeyD: AcapKey.D,
  KeyE: AcapKey.E,
  KeyF: AcapKey.F,
  KeyG: AcapKey.G,
  KeyH: AcapKey.H,
  KeyI: AcapKey.I,
  KeyJ: AcapKey.J,
  KeyK: AcapKey.K,
  KeyL: AcapKey.L,
  KeyM: AcapKey.M,
  KeyN: AcapKey.N,
  KeyO: AcapKey.O,
  KeyP: AcapKey.P,
  KeyQ: AcapKey.Q,
  KeyR: AcapKey.R,
  KeyS: AcapKey.S,
  KeyT: AcapKey.T,
  KeyU: AcapKey.U,
  KeyV: AcapKey.V,
  KeyW: AcapKey.W,
  KeyX: AcapKey.X,
  KeyY: AcapKey.Y,
  KeyZ: AcapKey.Z,
  Digit0: AcapKey.Digit0,
  Digit1: AcapKey.Digit1,
  Digit2: AcapKey.Digit2,
  Digit3: AcapKey.Digit3,
  Digit4: AcapKey.Digit4,
  Digit5: AcapKey.Digit5,
  Digit6: AcapKey.Digit6,
  Digit7: AcapKey.Digit7,
  Digit8: AcapKey.Digit8,
  Digit9: AcapKey.Digit9,
  Escape: AcapKey.Escape,
  Tab: AcapKey.Tab,
  Enter: AcapKey.Enter,
  Space: AcapKey.Space,
  Backspace: AcapKey.Backspace,
  ShiftLeft: AcapKey.ShiftLeft,
  ShiftRight: AcapKey.ShiftRight,
  ControlLeft: AcapKey.ControlLeft,
  ControlRight: AcapKey.ControlRight,
  AltLeft: AcapKey.AltLeft,
  AltRight: AcapKey.AltRight,
  ArrowUp: AcapKey.ArrowUp,
  ArrowDown: AcapKey.ArrowDown,
  ArrowLeft: AcapKey.ArrowLeft,
  ArrowRight: AcapKey.ArrowRight
};

const clampInt16 = (value: number): number => {
  return Math.max(-32768, Math.min(32767, Math.trunc(value)));
};

const wheelDirection = (value: number): number => {
  if (value < 0) {
    return 1;
  }

  if (value > 0) {
    return -1;
  }

  return 0;
};

const QuakeInputHandler = ({
  socketRef,
  targetRef,
  onCaptureChange
}: QuakeInputHandlerProps) => {
  const pressedKeysRef = useRef(new Set<string>());
  const pressedButtonsRef = useRef(new Set<number>());
  const capturedRef = useRef(false);

  useEffect(() => {
    const target = targetRef.current;

    if (!target) {
      return;
    }

    const send = (packet: ArrayBuffer) => {
      const socket = socketRef.current;

      if (!socket || socket.readyState !== WebSocket.OPEN) {
        return;
      }

      socket.send(packet);
    };

    /*
     * The browser can lose focus without sending normal key-up events. Tell the
     * game to release everything whenever that happens so controls cannot get
     * stuck after switching tabs or leaving mouse capture.
     */
    const resetInput = () => {
      send(new Uint8Array([PROTOCOL_VERSION, MessageType.Reset]).buffer);
      pressedKeysRef.current.clear();
      pressedButtonsRef.current.clear();
    };

    /*
     * Send small fixed-size messages instead of text. The first bytes say what
     * kind of input this is, and the remaining bytes contain only the values
     * needed for that event.
     */
    const sendKey = (key: AcapKey, down: boolean) => {
      const packet = new ArrayBuffer(5);
      const view = new DataView(packet);

      view.setUint8(0, PROTOCOL_VERSION);
      view.setUint8(1, MessageType.Key);
      view.setUint16(2, key, true);
      view.setUint8(4, down ? 1 : 0);
      send(packet);
    };

    const sendMouse = (dx: number, dy: number) => {
      if (dx === 0 && dy === 0) {
        return;
      }

      const packet = new ArrayBuffer(6);
      const view = new DataView(packet);

      view.setUint8(0, PROTOCOL_VERSION);
      view.setUint8(1, MessageType.Mouse);
      view.setInt16(2, clampInt16(dx), true);
      view.setInt16(4, clampInt16(dy), true);
      send(packet);
    };

    const sendButton = (button: number, down: boolean) => {
      if (button < 0 || button > 4) {
        return;
      }

      send(
        new Uint8Array([
          PROTOCOL_VERSION,
          MessageType.Button,
          button,
          down ? 1 : 0
        ]).buffer
      );
    };

    const sendWheel = (x: number, y: number) => {
      if (x === 0 && y === 0) {
        return;
      }

      const packet = new ArrayBuffer(4);
      const view = new DataView(packet);

      view.setUint8(0, PROTOCOL_VERSION);
      view.setUint8(1, MessageType.Wheel);
      view.setInt8(2, x);
      view.setInt8(3, y);
      send(packet);
    };

    const inputActive = () => {
      return (
        document.pointerLockElement === target ||
        document.activeElement === target
      );
    };

    const handleClick = (event: MouseEvent) => {
      const eventTarget = event.target as HTMLElement | null;

      if (
        eventTarget?.closest(
          'button, input, select, textarea, a, [role="button"], [role="menuitem"], [role="slider"], [role="combobox"], [role="listbox"]'
        )
      ) {
        return;
      }

      target.focus({ preventScroll: true });

      if (document.pointerLockElement !== target) {
        target.requestPointerLock().catch((error) => {
          console.error('Failed to capture mouse:', error);
        });
      }
    };

    const handleKeyDown = (event: KeyboardEvent) => {
      if (!inputActive() || event.repeat) {
        return;
      }

      const key = keyCodes[event.code];
      if (key === undefined) {
        return;
      }

      event.preventDefault();

      if (pressedKeysRef.current.has(event.code)) {
        return;
      }

      pressedKeysRef.current.add(event.code);
      sendKey(key, true);
    };

    const handleKeyUp = (event: KeyboardEvent) => {
      if (!inputActive()) {
        return;
      }

      const key = keyCodes[event.code];
      if (key === undefined) {
        return;
      }

      event.preventDefault();

      if (!pressedKeysRef.current.delete(event.code)) {
        return;
      }

      sendKey(key, false);
    };

    /*
     * Mouse capture gives movement rather than a screen position. That lets the
     * player keep turning in either direction without the pointer reaching the
     * edge of the browser window.
     */
    const handleMouseMove = (event: MouseEvent) => {
      if (document.pointerLockElement !== target) {
        return;
      }

      sendMouse(event.movementX, event.movementY);
    };

    const handleMouseDown = (event: MouseEvent) => {
      if (document.pointerLockElement !== target) {
        return;
      }

      if (event.button < 0 || event.button > 4) {
        return;
      }

      event.preventDefault();

      if (pressedButtonsRef.current.has(event.button)) {
        return;
      }

      pressedButtonsRef.current.add(event.button);
      sendButton(event.button, true);
    };

    const handleMouseUp = (event: MouseEvent) => {
      if (document.pointerLockElement !== target) {
        return;
      }

      if (event.button < 0 || event.button > 4) {
        return;
      }

      event.preventDefault();

      if (!pressedButtonsRef.current.delete(event.button)) {
        return;
      }

      sendButton(event.button, false);
    };

    const handleWheel = (event: WheelEvent) => {
      if (document.pointerLockElement !== target) {
        return;
      }

      event.preventDefault();
      sendWheel(wheelDirection(event.deltaX), wheelDirection(event.deltaY));
    };

    const handleContextMenu = (event: MouseEvent) => {
      if (document.pointerLockElement === target) {
        event.preventDefault();
      }
    };

    const handlePointerLockChange = () => {
      const captured = document.pointerLockElement === target;

      if (capturedRef.current && !captured) {
        resetInput();
      }

      capturedRef.current = captured;
      onCaptureChange(captured);
    };

    const handleBlur = () => {
      resetInput();
    };

    const handleVisibilityChange = () => {
      if (document.visibilityState === 'hidden') {
        resetInput();
      }
    };

    target.addEventListener('click', handleClick);
    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    window.addEventListener('mousemove', handleMouseMove);
    window.addEventListener('mousedown', handleMouseDown);
    window.addEventListener('mouseup', handleMouseUp);
    window.addEventListener('wheel', handleWheel, { passive: false });
    window.addEventListener('contextmenu', handleContextMenu);
    window.addEventListener('blur', handleBlur);
    document.addEventListener('pointerlockchange', handlePointerLockChange);
    document.addEventListener('visibilitychange', handleVisibilityChange);

    handlePointerLockChange();

    return () => {
      target.removeEventListener('click', handleClick);
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('mousedown', handleMouseDown);
      window.removeEventListener('mouseup', handleMouseUp);
      window.removeEventListener('wheel', handleWheel);
      window.removeEventListener('contextmenu', handleContextMenu);
      window.removeEventListener('blur', handleBlur);
      document.removeEventListener(
        'pointerlockchange',
        handlePointerLockChange
      );
      document.removeEventListener('visibilitychange', handleVisibilityChange);

      if (capturedRef.current) {
        resetInput();
      }

      capturedRef.current = false;
      onCaptureChange(false);
    };
  }, [onCaptureChange, socketRef, targetRef]);

  return null;
};

export default QuakeInputHandler;
