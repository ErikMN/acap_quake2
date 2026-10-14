#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACAP_INPUT_QUEUE_CAPACITY 256

/*
 * Protocol key identifiers.
 *
 * These numeric values are part of protocol version 1.
 * Keep them in sync with the frontend AcapKey enum in QuakeInputHandler.tsx.
 */
enum acap_key_code {
  ACAP_KEY_UNKNOWN = 0,

  ACAP_KEY_A = 1,
  ACAP_KEY_B = 2,
  ACAP_KEY_C = 3,
  ACAP_KEY_D = 4,
  ACAP_KEY_E = 5,
  ACAP_KEY_F = 6,
  ACAP_KEY_G = 7,
  ACAP_KEY_H = 8,
  ACAP_KEY_I = 9,
  ACAP_KEY_J = 10,
  ACAP_KEY_K = 11,
  ACAP_KEY_L = 12,
  ACAP_KEY_M = 13,
  ACAP_KEY_N = 14,
  ACAP_KEY_O = 15,
  ACAP_KEY_P = 16,
  ACAP_KEY_Q = 17,
  ACAP_KEY_R = 18,
  ACAP_KEY_S = 19,
  ACAP_KEY_T = 20,
  ACAP_KEY_U = 21,
  ACAP_KEY_V = 22,
  ACAP_KEY_W = 23,
  ACAP_KEY_X = 24,
  ACAP_KEY_Y = 25,
  ACAP_KEY_Z = 26,

  ACAP_KEY_DIGIT_0 = 27,
  ACAP_KEY_DIGIT_1 = 28,
  ACAP_KEY_DIGIT_2 = 29,
  ACAP_KEY_DIGIT_3 = 30,
  ACAP_KEY_DIGIT_4 = 31,
  ACAP_KEY_DIGIT_5 = 32,
  ACAP_KEY_DIGIT_6 = 33,
  ACAP_KEY_DIGIT_7 = 34,
  ACAP_KEY_DIGIT_8 = 35,
  ACAP_KEY_DIGIT_9 = 36,

  ACAP_KEY_ESCAPE = 37,
  ACAP_KEY_TAB = 38,
  ACAP_KEY_ENTER = 39,
  ACAP_KEY_SPACE = 40,
  ACAP_KEY_BACKSPACE = 41,
  ACAP_KEY_SHIFT_LEFT = 42,
  ACAP_KEY_SHIFT_RIGHT = 43,
  ACAP_KEY_CTRL_LEFT = 44,
  ACAP_KEY_CTRL_RIGHT = 45,
  ACAP_KEY_ALT_LEFT = 46,
  ACAP_KEY_ALT_RIGHT = 47,
  ACAP_KEY_ARROW_UP = 48,
  ACAP_KEY_ARROW_DOWN = 49,
  ACAP_KEY_ARROW_LEFT = 50,
  ACAP_KEY_ARROW_RIGHT = 51,

  ACAP_KEY_F1 = 52,
  ACAP_KEY_F2 = 53,
  ACAP_KEY_F3 = 54,
  ACAP_KEY_F4 = 55,
  ACAP_KEY_F5 = 56,
  ACAP_KEY_F6 = 57,
  ACAP_KEY_F7 = 58,
  ACAP_KEY_F8 = 59,
  ACAP_KEY_F9 = 60,
  ACAP_KEY_F10 = 61,
  ACAP_KEY_F11 = 62,
  ACAP_KEY_F12 = 63,
  ACAP_KEY_F13 = 64,
  ACAP_KEY_F14 = 65,
  ACAP_KEY_F15 = 66,
  ACAP_KEY_BACKQUOTE = 67,
  ACAP_KEY_MINUS = 68,
  ACAP_KEY_EQUAL = 69,
  ACAP_KEY_BRACKET_LEFT = 70,
  ACAP_KEY_BRACKET_RIGHT = 71,
  ACAP_KEY_BACKSLASH = 72,
  ACAP_KEY_SEMICOLON = 73,
  ACAP_KEY_QUOTE = 74,
  ACAP_KEY_COMMA = 75,
  ACAP_KEY_PERIOD = 76,
  ACAP_KEY_SLASH = 77,
  ACAP_KEY_INSERT = 78,
  ACAP_KEY_DELETE = 79,
  ACAP_KEY_HOME = 80,
  ACAP_KEY_END = 81,
  ACAP_KEY_PAGE_UP = 82,
  ACAP_KEY_PAGE_DOWN = 83,
  ACAP_KEY_CAPS_LOCK = 84,
  ACAP_KEY_NUM_LOCK = 85,
  ACAP_KEY_SCROLL_LOCK = 86,
  ACAP_KEY_PAUSE = 87,
  ACAP_KEY_PRINT_SCREEN = 88,
  ACAP_KEY_META_LEFT = 89,
  ACAP_KEY_META_RIGHT = 90,
  ACAP_KEY_CONTEXT_MENU = 91,
  ACAP_KEY_NUMPAD_0 = 92,
  ACAP_KEY_NUMPAD_1 = 93,
  ACAP_KEY_NUMPAD_2 = 94,
  ACAP_KEY_NUMPAD_3 = 95,
  ACAP_KEY_NUMPAD_4 = 96,
  ACAP_KEY_NUMPAD_5 = 97,
  ACAP_KEY_NUMPAD_6 = 98,
  ACAP_KEY_NUMPAD_7 = 99,
  ACAP_KEY_NUMPAD_8 = 100,
  ACAP_KEY_NUMPAD_9 = 101,
  ACAP_KEY_NUMPAD_ENTER = 102,
  ACAP_KEY_NUMPAD_ADD = 103,
  ACAP_KEY_NUMPAD_SUBTRACT = 104,
  ACAP_KEY_NUMPAD_MULTIPLY = 105,
  ACAP_KEY_NUMPAD_DIVIDE = 106,
  ACAP_KEY_NUMPAD_DECIMAL = 107,
  ACAP_KEY_NUMPAD_EQUAL = 108,
  ACAP_KEY_INTL_BACKSLASH = 109,
  ACAP_KEY_INTL_RO = 110,
  ACAP_KEY_INTL_YEN = 111,

  ACAP_KEY_COUNT = 112,
};

/*
 * Pointer button identifiers use the browser MouseEvent.button numbering supported by the frontend.
 */
enum acap_pointer_button {
  ACAP_POINTER_BUTTON_LEFT = 0,
  ACAP_POINTER_BUTTON_MIDDLE = 1,
  ACAP_POINTER_BUTTON_RIGHT = 2,
  ACAP_POINTER_BUTTON_BACK = 3,
  ACAP_POINTER_BUTTON_FORWARD = 4,
};

enum acap_input_event_type {
  ACAP_INPUT_EVENT_KEY,
  ACAP_INPUT_EVENT_MOUSE,
  ACAP_INPUT_EVENT_BUTTON,
  ACAP_INPUT_EVENT_WHEEL,

  ACAP_INPUT_EVENT_TEXT,
  ACAP_INPUT_EVENT_RESET,
};

struct acap_input_event {
  enum acap_input_event_type type;

  union {
    struct {
      uint16_t key;
      bool down;
    } key;

    struct {
      int16_t dx;
      int16_t dy;
    } mouse;

    struct {
      uint8_t button;
      bool down;
    } button;

    struct {
      int8_t x;
      int8_t y;
    } wheel;

    uint32_t text;
  } data;
};

/*
 * Fixed-size queue that keeps events in arrival order.
 * A mutex protects it while the WebSocket thread adds events and the Yamagi thread removes them.
 *
 * head identifies the oldest event and count is the number of queued events.
 */
struct acap_input_queue {
  pthread_mutex_t mutex;
  struct acap_input_event events[ACAP_INPUT_QUEUE_CAPACITY];
  size_t head;
  size_t count;
  bool initialized;
};

/*
 * Initialize an empty queue and its mutex.
 */
bool acap_input_queue_init(struct acap_input_queue *queue);

/*
 * Destroy the queue mutex and reset the queue state.
 */
void acap_input_queue_destroy(struct acap_input_queue *queue);

/*
 * Append an event to the queue.
 *
 * Returns false when the queue is full.
 */
bool acap_input_queue_push(struct acap_input_queue *queue, const struct acap_input_event *event);

/*
 * Remove the oldest event from the queue.
 *
 * Returns false when the queue is empty.
 */
bool acap_input_queue_pop(struct acap_input_queue *queue, struct acap_input_event *event);

/*
 * Discard every queued event.
 */
void acap_input_queue_clear(struct acap_input_queue *queue);
