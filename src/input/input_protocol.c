#include "input_protocol.h"

static int16_t
read_i16_le(const uint8_t *data)
{
  return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

bool
acap_input_protocol_decode(const uint8_t *data, size_t len, struct acap_input_event *event)
{
  if (!data || !event || len < 2 || data[0] != ACAP_INPUT_PROTOCOL_VERSION) {
    return false;
  }

  switch (data[1]) {
  case ACAP_INPUT_MESSAGE_KEY:
    if (len != 5 || data[4] > 1) {
      return false;
    }

    event->type = ACAP_INPUT_EVENT_KEY;
    event->data.key.key = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
    event->data.key.down = data[4] != 0;

    return event->data.key.key > ACAP_KEY_UNKNOWN && event->data.key.key < ACAP_KEY_COUNT;

  case ACAP_INPUT_MESSAGE_MOUSE:
    if (len != 6) {
      return false;
    }

    event->type = ACAP_INPUT_EVENT_MOUSE;
    event->data.mouse.dx = read_i16_le(&data[2]);
    event->data.mouse.dy = read_i16_le(&data[4]);
    return true;

  case ACAP_INPUT_MESSAGE_BUTTON:
    if (len != 4 || data[2] > ACAP_POINTER_BUTTON_FORWARD || data[3] > 1) {
      return false;
    }

    event->type = ACAP_INPUT_EVENT_BUTTON;
    event->data.button.button = data[2];
    event->data.button.down = data[3] != 0;
    return true;

  case ACAP_INPUT_MESSAGE_WHEEL:
    if (len != 4) {
      return false;
    }

    event->type = ACAP_INPUT_EVENT_WHEEL;
    event->data.wheel.x = (int8_t)data[2];
    event->data.wheel.y = (int8_t)data[3];
    return true;

  case ACAP_INPUT_MESSAGE_RESET:
    if (len != 2) {
      return false;
    }

    event->type = ACAP_INPUT_EVENT_RESET;
    return true;

  default:
    return false;
  }
}
