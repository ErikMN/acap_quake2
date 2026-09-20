#pragma once

#include "input_queue.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACAP_INPUT_PROTOCOL_VERSION 1

enum acap_input_message_type {
  ACAP_INPUT_MESSAGE_KEY = 1,
  ACAP_INPUT_MESSAGE_MOUSE = 2,
  ACAP_INPUT_MESSAGE_BUTTON = 3,
  ACAP_INPUT_MESSAGE_WHEEL = 4,
  ACAP_INPUT_MESSAGE_RESET = 5,
};

bool acap_input_protocol_decode(const uint8_t *data, size_t len, struct acap_input_event *event);
