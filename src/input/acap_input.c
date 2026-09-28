/*
 * Connects incoming browser messages to the game's input queue.
 * It validates each message through the protocol decoder and queues a reset event when the connection changes.
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
   * If input arrives faster than the game can consume it, queued press and release state is no longer trustworthy.
   * Clear the queue and ask the game to release all held controls instead of risking a stuck input.
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
   * Start every browser connection from a known state.
   * Reset again on disconnect because a released key may otherwise never reach the game.
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
