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
  Reset = 5,
  Text = 6
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
  ArrowRight = 51,
  F1 = 52,
  F2 = 53,
  F3 = 54,
  F4 = 55,
  F5 = 56,
  F6 = 57,
  F7 = 58,
  F8 = 59,
  F9 = 60,
  F10 = 61,
  F11 = 62,
  F12 = 63,
  F13 = 64,
  F14 = 65,
  F15 = 66,
  Backquote = 67,
  Minus = 68,
  Equal = 69,
  BracketLeft = 70,
  BracketRight = 71,
  Backslash = 72,
  Semicolon = 73,
  Quote = 74,
  Comma = 75,
  Period = 76,
  Slash = 77,
  Insert = 78,
  Delete = 79,
  Home = 80,
  End = 81,
  PageUp = 82,
  PageDown = 83,
  CapsLock = 84,
  NumLock = 85,
  ScrollLock = 86,
  Pause = 87,
  PrintScreen = 88,
  MetaLeft = 89,
  MetaRight = 90,
  ContextMenu = 91,
  Numpad0 = 92,
  Numpad1 = 93,
  Numpad2 = 94,
  Numpad3 = 95,
  Numpad4 = 96,
  Numpad5 = 97,
  Numpad6 = 98,
  Numpad7 = 99,
  Numpad8 = 100,
  Numpad9 = 101,
  NumpadEnter = 102,
  NumpadAdd = 103,
  NumpadSubtract = 104,
  NumpadMultiply = 105,
  NumpadDivide = 106,
  NumpadDecimal = 107,
  NumpadEqual = 108,
  IntlBackslash = 109,
  IntlRo = 110,
  IntlYen = 111
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
  ArrowRight: AcapKey.ArrowRight,
  F1: AcapKey.F1,
  F2: AcapKey.F2,
  F3: AcapKey.F3,
  F4: AcapKey.F4,
  F5: AcapKey.F5,
  F6: AcapKey.F6,
  F7: AcapKey.F7,
  F8: AcapKey.F8,
  F9: AcapKey.F9,
  F10: AcapKey.F10,
  F11: AcapKey.F11,
  F12: AcapKey.F12,
  F13: AcapKey.F13,
  F14: AcapKey.F14,
  F15: AcapKey.F15,
  Backquote: AcapKey.Backquote,
  Minus: AcapKey.Minus,
  Equal: AcapKey.Equal,
  BracketLeft: AcapKey.BracketLeft,
  BracketRight: AcapKey.BracketRight,
  Backslash: AcapKey.Backslash,
  Semicolon: AcapKey.Semicolon,
  Quote: AcapKey.Quote,
  Comma: AcapKey.Comma,
  Period: AcapKey.Period,
  Slash: AcapKey.Slash,
  Insert: AcapKey.Insert,
  Delete: AcapKey.Delete,
  Home: AcapKey.Home,
  End: AcapKey.End,
  PageUp: AcapKey.PageUp,
  PageDown: AcapKey.PageDown,
  CapsLock: AcapKey.CapsLock,
  NumLock: AcapKey.NumLock,
  ScrollLock: AcapKey.ScrollLock,
  Pause: AcapKey.Pause,
  PrintScreen: AcapKey.PrintScreen,
  MetaLeft: AcapKey.MetaLeft,
  MetaRight: AcapKey.MetaRight,
  ContextMenu: AcapKey.ContextMenu,
  Numpad0: AcapKey.Numpad0,
  Numpad1: AcapKey.Numpad1,
  Numpad2: AcapKey.Numpad2,
  Numpad3: AcapKey.Numpad3,
  Numpad4: AcapKey.Numpad4,
  Numpad5: AcapKey.Numpad5,
  Numpad6: AcapKey.Numpad6,
  Numpad7: AcapKey.Numpad7,
  Numpad8: AcapKey.Numpad8,
  Numpad9: AcapKey.Numpad9,
  NumpadEnter: AcapKey.NumpadEnter,
  NumpadAdd: AcapKey.NumpadAdd,
  NumpadSubtract: AcapKey.NumpadSubtract,
  NumpadMultiply: AcapKey.NumpadMultiply,
  NumpadDivide: AcapKey.NumpadDivide,
  NumpadDecimal: AcapKey.NumpadDecimal,
  NumpadEqual: AcapKey.NumpadEqual,
  IntlBackslash: AcapKey.IntlBackslash,
  IntlRo: AcapKey.IntlRo,
  IntlYen: AcapKey.IntlYen
};

const repeatableKeys = new Set<AcapKey>([
  AcapKey.Backspace,
  AcapKey.PageUp,
  AcapKey.PageDown,
  AcapKey.Numpad3,
  AcapKey.Numpad9
]);

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
     * The browser can lose focus without sending normal key-up events.
     * Tell the game to release everything whenever that happens
     * so controls cannot get stuck after switching tabs or leaving mouse capture.
     */
    const resetInput = () => {
      send(new Uint8Array([PROTOCOL_VERSION, MessageType.Reset]).buffer);
      pressedKeysRef.current.clear();
      pressedButtonsRef.current.clear();
    };

    /*
     * Send small fixed-size messages instead of text.
     * The first bytes say what kind of input this is,
     * and the remaining bytes contain only the values needed for that event.
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

    const sendText = (character: number) => {
      send(
        new Uint8Array([PROTOCOL_VERSION, MessageType.Text, character]).buffer
      );
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
          'button, input, select, textarea, a, [role="button"], [role="menuitem"], ' +
            '[role="slider"], [role="combobox"], [role="listbox"]'
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
      if (!inputActive() || event.isComposing) {
        return;
      }

      const key = keyCodes[event.code];
      const character = event.key.length === 1 ? event.key.charCodeAt(0) : 0;
      const printable =
        event.code !== 'Backquote' &&
        character >= 32 &&
        character <= 126 &&
        !event.metaKey &&
        (!event.ctrlKey || event.getModifierState('AltGraph'));

      if (key === undefined && !printable) {
        return;
      }

      event.preventDefault();

      if (key !== undefined) {
        if (!pressedKeysRef.current.has(event.code)) {
          pressedKeysRef.current.add(event.code);
          sendKey(key, true);
        } else if (event.repeat && repeatableKeys.has(key)) {
          sendKey(key, true);
        }
      }

      if (printable && !event.repeat) {
        sendText(character);
      }
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
     * Mouse capture gives movement rather than a screen position.
     * That lets the player keep turning in either direction
     * without the pointer reaching the edge of the browser window.
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
