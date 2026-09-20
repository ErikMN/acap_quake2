#include "yamagi_input.h"

#include "acap_input.h"

#include "client/header/keyboard.h"

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

void
acap_yamagi_input_update(float *mouse_x, float *mouse_y, bool mouse_active)
{
  struct acap_input_event event;

  while (acap_input_next_event(&event)) {
    switch (event.type) {
      case ACAP_INPUT_EVENT_KEY: {
        int key;
        bool special;

        if (translate_key(event.data.key.key, &key, &special)) {
          Key_Event(key, event.data.key.down, special);
        }
        break;
      }

      case ACAP_INPUT_EVENT_MOUSE:
        if (mouse_active) {
          *mouse_x += event.data.mouse.dx;
          *mouse_y += event.data.mouse.dy;
        }
        break;

      case ACAP_INPUT_EVENT_BUTTON: {
        int key;

        if (translate_button(event.data.button.button, &key)) {
          Key_Event(key, event.data.button.down, true);
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
        Key_MarkAllUp();
        *mouse_x = 0;
        *mouse_y = 0;
        break;

      case ACAP_INPUT_EVENT_TEXT:
        break;
    }
  }
}
