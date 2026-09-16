#pragma once

#include <stdbool.h>

struct gpu_context;

struct overlay_context {
  void *event_stream;
  void *format;
  int event_fd;
  int overlay_id;
  unsigned stream_id;
  bool axo_started;
};

bool overlay_context_init(struct overlay_context *overlay);
void overlay_context_destroy(struct overlay_context *overlay);
int overlay_context_get_event_fd(const struct overlay_context *overlay);
bool overlay_context_process_events(struct overlay_context *overlay, const struct gpu_context *gpu);
