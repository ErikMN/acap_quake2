#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACAP_INPUT_QUEUE_CAPACITY 256

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
