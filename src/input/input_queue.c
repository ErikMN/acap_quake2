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
