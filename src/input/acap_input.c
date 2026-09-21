/*
 * Connects incoming browser messages to the game's input queue.
 * It checks each message and resets held controls when the connection changes.
 */

#include "acap_input.h"

#include "input_protocol.h"
#include "websocket.h"

#include <syslog.h>

static struct acap_input_queue input_queue;

static void
enqueue_event(struct acap_input_queue *queue, const struct acap_input_event *event)
{
  if (acap_input_queue_push(queue, event)) {
    return;
  }

  /*
   * If input arrives faster than the game can consume it, old key presses are
   * no longer trustworthy. Clear them and ask the game to release everything
   * instead of risking a movement key staying held forever.
   */
  const struct acap_input_event reset = {
    .type = ACAP_INPUT_EVENT_RESET,
  };

  syslog(LOG_WARNING, "ACAP input: input queue full, resetting queued input");
  acap_input_queue_clear(queue);
  acap_input_queue_push(queue, &reset);
}

static void
enqueue_reset(void *user)
{
  struct acap_input_queue *queue = user;
  const struct acap_input_event event = {
    .type = ACAP_INPUT_EVENT_RESET,
  };

  enqueue_event(queue, &event);
}

static void
receive_message(const uint8_t *data, size_t len, void *user)
{
  struct acap_input_queue *queue = user;
  struct acap_input_event event;

  if (!acap_input_protocol_decode(data, len, &event)) {
    syslog(LOG_WARNING, "ACAP input: invalid input packet");
    return;
  }

  enqueue_event(queue, &event);
}

bool
acap_input_init(void)
{
  /*
   * Start every browser connection from a known state. We also reset when the
   * browser disconnects because a released key may otherwise never reach us.
   */
  const struct acap_websocket_callbacks callbacks = {
    .connected = enqueue_reset,
    .disconnected = enqueue_reset,
    .message = receive_message,
  };

  if (!acap_input_queue_init(&input_queue)) {
    syslog(LOG_ERR, "ACAP input: failed to initialize input queue");
    return false;
  }

  if (!acap_websocket_start(&callbacks, &input_queue)) {
    acap_input_queue_destroy(&input_queue);
    return false;
  }

  return true;
}

void
acap_input_shutdown(void)
{
  acap_websocket_stop();
  acap_input_queue_destroy(&input_queue);
}

bool
acap_input_next_event(struct acap_input_event *event)
{
  return acap_input_queue_pop(&input_queue, event);
}
