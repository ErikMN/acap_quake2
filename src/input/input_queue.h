#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACAP_INPUT_QUEUE_CAPACITY 256

enum acap_key_code {
  ACAP_KEY_UNKNOWN = 0,

  ACAP_KEY_A,
  ACAP_KEY_B,
  ACAP_KEY_C,
  ACAP_KEY_D,
  ACAP_KEY_E,
  ACAP_KEY_F,
  ACAP_KEY_G,
  ACAP_KEY_H,
  ACAP_KEY_I,
  ACAP_KEY_J,
  ACAP_KEY_K,
  ACAP_KEY_L,
  ACAP_KEY_M,
  ACAP_KEY_N,
  ACAP_KEY_O,
  ACAP_KEY_P,
  ACAP_KEY_Q,
  ACAP_KEY_R,
  ACAP_KEY_S,
  ACAP_KEY_T,
  ACAP_KEY_U,
  ACAP_KEY_V,
  ACAP_KEY_W,
  ACAP_KEY_X,
  ACAP_KEY_Y,
  ACAP_KEY_Z,

  ACAP_KEY_DIGIT_0,
  ACAP_KEY_DIGIT_1,
  ACAP_KEY_DIGIT_2,
  ACAP_KEY_DIGIT_3,
  ACAP_KEY_DIGIT_4,
  ACAP_KEY_DIGIT_5,
  ACAP_KEY_DIGIT_6,
  ACAP_KEY_DIGIT_7,
  ACAP_KEY_DIGIT_8,
  ACAP_KEY_DIGIT_9,

  ACAP_KEY_ESCAPE,
  ACAP_KEY_TAB,
  ACAP_KEY_ENTER,
  ACAP_KEY_SPACE,
  ACAP_KEY_BACKSPACE,
  ACAP_KEY_SHIFT_LEFT,
  ACAP_KEY_SHIFT_RIGHT,
  ACAP_KEY_CTRL_LEFT,
  ACAP_KEY_CTRL_RIGHT,
  ACAP_KEY_ALT_LEFT,
  ACAP_KEY_ALT_RIGHT,
  ACAP_KEY_ARROW_UP,
  ACAP_KEY_ARROW_DOWN,
  ACAP_KEY_ARROW_LEFT,
  ACAP_KEY_ARROW_RIGHT,

  ACAP_KEY_COUNT,
};

enum acap_pointer_button {
  ACAP_POINTER_BUTTON_LEFT = 0,
  ACAP_POINTER_BUTTON_MIDDLE,
  ACAP_POINTER_BUTTON_RIGHT,
  ACAP_POINTER_BUTTON_BACK,
  ACAP_POINTER_BUTTON_FORWARD,
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

struct acap_input_queue {
  pthread_mutex_t mutex;
  struct acap_input_event events[ACAP_INPUT_QUEUE_CAPACITY];
  size_t head;
  size_t count;
  bool initialized;
};

bool acap_input_queue_init(struct acap_input_queue *queue);
void acap_input_queue_destroy(struct acap_input_queue *queue);
bool acap_input_queue_push(struct acap_input_queue *queue, const struct acap_input_event *event);
bool acap_input_queue_pop(struct acap_input_queue *queue, struct acap_input_event *event);
void acap_input_queue_clear(struct acap_input_queue *queue);
