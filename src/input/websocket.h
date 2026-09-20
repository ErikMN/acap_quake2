#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*acap_websocket_connection_callback)(void *user);
typedef void (*acap_websocket_message_callback)(const uint8_t *data, size_t len, void *user);

struct acap_websocket_callbacks {
  acap_websocket_connection_callback connected;
  acap_websocket_connection_callback disconnected;
  acap_websocket_message_callback message;
};

bool acap_websocket_start(const struct acap_websocket_callbacks *callbacks, void *user);
void acap_websocket_stop(void);
