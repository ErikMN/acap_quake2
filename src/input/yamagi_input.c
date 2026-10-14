/*
 * Turns queued browser controls into input that Yamagi Quake II understands.
 * This is where keys, mouse movement, buttons, and the wheel enter the game.
 */

#include "yamagi_input.h"

#include "acap_input.h"

#include "client/header/keyboard.h"

/*
 * Track which browser keys and mouse buttons are currently held.
 * This lets reset handling release them if focus is lost or the connection disappears.
 */
static bool acap_keys_down[ACAP_KEY_COUNT];
static bool acap_buttons_down[ACAP_POINTER_BUTTON_FORWARD + 1];

static bool
translate_key(enum acap_key_code code, int *key, bool *special)
{
  *special = false;

  if (code >= ACAP_KEY_A && code <= ACAP_KEY_Z) {
    *key = 'a' + (code - ACAP_KEY_A);
    return true;
  }

  if (code >= ACAP_KEY_DIGIT_0 && code <= ACAP_KEY_DIGIT_9) {
    *key = '0' + (code - ACAP_KEY_DIGIT_0);
    return true;
  }

  *special = true;

  if (code >= ACAP_KEY_F1 && code <= ACAP_KEY_F15) {
    *key = K_F1 + (code - ACAP_KEY_F1);
    return true;
  }

  switch (code) {
  case ACAP_KEY_ESCAPE:
    *key = K_ESCAPE;
    return true;
  case ACAP_KEY_TAB:
    *key = K_TAB;
    return true;
  case ACAP_KEY_ENTER:
    *key = K_ENTER;
    return true;
  case ACAP_KEY_SPACE:
    *key = K_SPACE;
    *special = false;
    return true;
  case ACAP_KEY_BACKSPACE:
    *key = K_BACKSPACE;
    return true;
  case ACAP_KEY_SHIFT_LEFT:
  case ACAP_KEY_SHIFT_RIGHT:
    *key = K_SHIFT;
    return true;
  case ACAP_KEY_CTRL_LEFT:
  case ACAP_KEY_CTRL_RIGHT:
    *key = K_CTRL;
    return true;
  case ACAP_KEY_ALT_LEFT:
  case ACAP_KEY_ALT_RIGHT:
    *key = K_ALT;
    return true;
  case ACAP_KEY_ARROW_UP:
    *key = K_UPARROW;
    return true;
  case ACAP_KEY_ARROW_DOWN:
    *key = K_DOWNARROW;
    return true;
  case ACAP_KEY_ARROW_LEFT:
    *key = K_LEFTARROW;
    return true;
  case ACAP_KEY_ARROW_RIGHT:
    *key = K_RIGHTARROW;
    return true;
  case ACAP_KEY_BACKQUOTE:
    *key = K_CONSOLE;
    return true;
  case ACAP_KEY_INSERT:
    *key = K_INS;
    return true;
  case ACAP_KEY_DELETE:
    *key = K_DEL;
    return true;
  case ACAP_KEY_HOME:
    *key = K_HOME;
    return true;
  case ACAP_KEY_END:
    *key = K_END;
    return true;
  case ACAP_KEY_PAGE_UP:
    *key = K_PGUP;
    return true;
  case ACAP_KEY_PAGE_DOWN:
    *key = K_PGDN;
    return true;
  case ACAP_KEY_CAPS_LOCK:
    *key = K_CAPSLOCK;
    return true;
  case ACAP_KEY_NUM_LOCK:
    *key = K_KP_NUMLOCK;
    return true;
  case ACAP_KEY_SCROLL_LOCK:
    *key = K_SCROLLOCK;
    return true;
  case ACAP_KEY_PAUSE:
    *key = K_PAUSE;
    return true;
  case ACAP_KEY_PRINT_SCREEN:
    *key = K_PRINT;
    return true;
  case ACAP_KEY_META_LEFT:
    *key = K_COMMAND;
    return true;
  case ACAP_KEY_META_RIGHT:
    *key = K_COMMAND;
    return true;
  case ACAP_KEY_CONTEXT_MENU:
    *key = K_MENU;
    return true;
  case ACAP_KEY_NUMPAD_0:
    *key = K_KP_INS;
    return true;
  case ACAP_KEY_NUMPAD_1:
    *key = K_KP_END;
    return true;
  case ACAP_KEY_NUMPAD_2:
    *key = K_KP_DOWNARROW;
    return true;
  case ACAP_KEY_NUMPAD_3:
    *key = K_KP_PGDN;
    return true;
  case ACAP_KEY_NUMPAD_4:
    *key = K_KP_LEFTARROW;
    return true;
  case ACAP_KEY_NUMPAD_5:
    *key = K_KP_5;
    return true;
  case ACAP_KEY_NUMPAD_6:
    *key = K_KP_RIGHTARROW;
    return true;
  case ACAP_KEY_NUMPAD_7:
    *key = K_KP_HOME;
    return true;
  case ACAP_KEY_NUMPAD_8:
    *key = K_KP_UPARROW;
    return true;
  case ACAP_KEY_NUMPAD_9:
    *key = K_KP_PGUP;
    return true;
  case ACAP_KEY_NUMPAD_ENTER:
    *key = K_KP_ENTER;
    return true;
  case ACAP_KEY_NUMPAD_ADD:
    *key = K_KP_PLUS;
    return true;
  case ACAP_KEY_NUMPAD_SUBTRACT:
    *key = K_KP_MINUS;
    return true;
  case ACAP_KEY_NUMPAD_MULTIPLY:
    *key = K_KP_STAR;
    return true;
  case ACAP_KEY_NUMPAD_DIVIDE:
    *key = K_KP_SLASH;
    return true;
  case ACAP_KEY_NUMPAD_DECIMAL:
    *key = K_KP_DEL;
    return true;
  case ACAP_KEY_NUMPAD_EQUAL:
    *key = K_KP_EQUALS;
    return true;
  case ACAP_KEY_INTL_BACKSLASH:
    *key = K_SC_NONUSBACKSLASH;
    return true;
  case ACAP_KEY_INTL_RO:
    *key = K_SC_INTERNATIONAL1;
    return true;
  case ACAP_KEY_INTL_YEN:
    *key = K_SC_INTERNATIONAL3;
    return true;
  case ACAP_KEY_MINUS:
    *key = '-';
    *special = false;
    return true;
  case ACAP_KEY_EQUAL:
    *key = '=';
    *special = false;
    return true;
  case ACAP_KEY_BRACKET_LEFT:
    *key = '[';
    *special = false;
    return true;
  case ACAP_KEY_BRACKET_RIGHT:
    *key = ']';
    *special = false;
    return true;
  case ACAP_KEY_BACKSLASH:
    *key = '\\';
    *special = false;
    return true;
  case ACAP_KEY_SEMICOLON:
    *key = ';';
    *special = false;
    return true;
  case ACAP_KEY_QUOTE:
    *key = '\'';
    *special = false;
    return true;
  case ACAP_KEY_COMMA:
    *key = ',';
    *special = false;
    return true;
  case ACAP_KEY_PERIOD:
    *key = '.';
    *special = false;
    return true;
  case ACAP_KEY_SLASH:
    *key = '/';
    *special = false;
    return true;
  default:
    return false;
  }
}

static bool
translate_button(uint8_t button, int *key)
{
  switch (button) {
  case ACAP_POINTER_BUTTON_LEFT:
    *key = K_MOUSE1;
    return true;
  case ACAP_POINTER_BUTTON_MIDDLE:
    *key = K_MOUSE3;
    return true;
  case ACAP_POINTER_BUTTON_RIGHT:
    *key = K_MOUSE2;
    return true;
  case ACAP_POINTER_BUTTON_BACK:
    *key = K_MOUSE4;
    return true;
  case ACAP_POINTER_BUTTON_FORWARD:
    *key = K_MOUSE5;
    return true;
  default:
    return false;
  }
}

static void
release_pressed_input(void)
{
  for (enum acap_key_code code = ACAP_KEY_UNKNOWN + 1; code < ACAP_KEY_COUNT; code++) {
    if (!acap_keys_down[code]) {
      continue;
    }

    int key;
    bool special;

    if (translate_key(code, &key, &special)) {
      Key_Event(key, false, special);
    }

    acap_keys_down[code] = false;
  }

  for (uint8_t button = ACAP_POINTER_BUTTON_LEFT; button <= ACAP_POINTER_BUTTON_FORWARD; button++) {
    if (!acap_buttons_down[button]) {
      continue;
    }

    int key;

    if (translate_button(button, &key)) {
      Key_Event(key, false, true);
    }

    acap_buttons_down[button] = false;
  }
}

void
acap_yamagi_input_update(float *mouse_x, float *mouse_y, bool mouse_active)
{
  /*
   * This runs from Quake's normal input update. The WebSocket thread only queues events.
   * Translating them here keeps all changes to Quake on the game thread.
   */
  struct acap_input_event event;

  while (acap_input_next_event(&event)) {
    switch (event.type) {
    case ACAP_INPUT_EVENT_KEY: {
      int key;
      bool special;

      if (translate_key(event.data.key.key, &key, &special)) {
        Key_Event(key, event.data.key.down, special);
        acap_keys_down[event.data.key.key] = event.data.key.down;
      }
      break;
    }

    case ACAP_INPUT_EVENT_MOUSE:
      /*
       * Mouse messages contain movement since the previous browser event.
       * Accumulate those deltas so Quake receives the complete movement for this frame.
       */
      if (mouse_active) {
        *mouse_x += event.data.mouse.dx;
        *mouse_y += event.data.mouse.dy;
      }
      break;

    case ACAP_INPUT_EVENT_BUTTON: {
      int key;

      if (translate_button(event.data.button.button, &key)) {
        Key_Event(key, event.data.button.down, true);
        acap_buttons_down[event.data.button.button] = event.data.button.down;
      }
      break;
    }

    case ACAP_INPUT_EVENT_WHEEL:
      if (event.data.wheel.y != 0) {
        int key = event.data.wheel.y > 0 ? K_MWHEELUP : K_MWHEELDOWN;
        Key_Event(key, true, true);
        Key_Event(key, false, true);
      }
      break;

    case ACAP_INPUT_EVENT_RESET:
      /*
       * A reset means we can no longer trust which controls are still held.
       * Release everything and discard any old mouse movement before continuing.
       */
      release_pressed_input();
      Key_MarkAllUp();
      *mouse_x = 0;
      *mouse_y = 0;
      break;

    case ACAP_INPUT_EVENT_TEXT:
      Char_Event(event.data.text);
      break;
    }
  }
}
