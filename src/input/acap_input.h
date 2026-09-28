#pragma once

#include "input_queue.h"

#include <stdbool.h>

/*
 * Coordinates the shared input queue and the local WebSocket server.
 *
 * WebSocket callbacks enqueue decoded events on the WebSocket thread.
 * Yamagi consumes those events from its game thread through acap_input_next_event().
 */

/*
 * Initialize the input queue and start the WebSocket server.
 *
 * Returns true when both are ready.
 */
bool acap_input_init(void);

/*
 * Stop the WebSocket server and destroy the input queue.
 */
void acap_input_shutdown(void);

/*
 * Remove the oldest queued input event.
 *
 * Returns true when an event was available.
 */
bool acap_input_next_event(struct acap_input_event *event);
