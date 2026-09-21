/*
 * Stores browser input until the game thread is ready to handle it.
 * The queue keeps events in order and protects them while two threads use it.
 */

#include "input_queue.h"

#include <string.h>

bool
acap_input_queue_init(struct acap_input_queue *queue)
{
  memset(queue, 0, sizeof(*queue));

  if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
    return false;
  }

  queue->initialized = true;
  return true;
}

void
acap_input_queue_destroy(struct acap_input_queue *queue)
{
  if (!queue->initialized) {
    return;
  }

  pthread_mutex_destroy(&queue->mutex);
  queue->initialized = false;
  queue->head = 0;
  queue->count = 0;
}

bool
acap_input_queue_push(struct acap_input_queue *queue, const struct acap_input_event *event)
{
  /*
   * Browser input arrives on one thread while Quake reads it on another. The
   * lock makes each change to the queue happen as one complete operation, so
   * the game never sees an event while it is only partly written.
   */
  bool pushed = false;

  pthread_mutex_lock(&queue->mutex);

  if (queue->count < ACAP_INPUT_QUEUE_CAPACITY) {
    size_t index = (queue->head + queue->count) % ACAP_INPUT_QUEUE_CAPACITY;
    queue->events[index] = *event;
    queue->count++;
    pushed = true;
  }

  pthread_mutex_unlock(&queue->mutex);

  return pushed;
}

bool
acap_input_queue_pop(struct acap_input_queue *queue, struct acap_input_event *event)
{
  /*
   * Events leave the queue in the same order they arrived. This matters for
   * pairs such as key down followed by key up, where reversing them could leave
   * a control stuck in the wrong state.
   */
  bool popped = false;

  pthread_mutex_lock(&queue->mutex);

  if (queue->count > 0) {
    *event = queue->events[queue->head];
    queue->head = (queue->head + 1) % ACAP_INPUT_QUEUE_CAPACITY;
    queue->count--;
    popped = true;
  }

  pthread_mutex_unlock(&queue->mutex);

  return popped;
}

void
acap_input_queue_clear(struct acap_input_queue *queue)
{
  pthread_mutex_lock(&queue->mutex);
  queue->head = 0;
  queue->count = 0;
  pthread_mutex_unlock(&queue->mutex);
}
