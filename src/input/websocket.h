#pragma once

#include <stdbool.h>

typedef void (*acap_websocket_connection_callback)(void *user);

struct acap_websocket_callbacks {
  acap_websocket_connection_callback connected;
  acap_websocket_connection_callback disconnected;
};

bool acap_websocket_start(const struct acap_websocket_callbacks *callbacks, void *user);
void acap_websocket_stop(void);
