#pragma once

#include <stdbool.h>

struct overlay_context {
  void *event_stream;
  int event_fd;
  bool axo_started;
};

bool overlay_context_init(struct overlay_context *overlay);
void overlay_context_destroy(struct overlay_context *overlay);
int overlay_context_get_event_fd(const struct overlay_context *overlay);
bool overlay_context_process_events(struct overlay_context *overlay);
