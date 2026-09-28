#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The local libwebsockets server runs behind the Axis ACAP reverse proxy.
 *
 * All callbacks execute on the WebSocket service thread.
 * They must pass work to the game thread instead of modifying Yamagi state directly.
 */
typedef void (*acap_websocket_connection_callback)(void *user);
typedef void (*acap_websocket_message_callback)(const uint8_t *data, size_t len, void *user);

struct acap_websocket_callbacks {
  acap_websocket_connection_callback connected;
  acap_websocket_connection_callback disconnected;
  acap_websocket_message_callback message;
};

/*
 * Start the WebSocket service thread and wait for server startup to complete.
 *
 * The callback table is copied and user is passed unchanged to each callback.
 * Returns true when the server is running.
 */
bool acap_websocket_start(const struct acap_websocket_callbacks *callbacks, void *user);

/*
 * Request server shutdown and join the WebSocket service thread.
 */
void acap_websocket_stop(void);
