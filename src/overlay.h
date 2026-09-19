#pragma once

#include <stdbool.h>

struct gpu_context;
struct render_surface;

struct overlay_context {
  void *event_stream;
  void *format;
  struct render_surface *surfaces;
  struct render_surface *current_surface;
  void *current_buffer;
  int event_fd;
  int overlay_id;
  unsigned stream_id;
  unsigned width;
  unsigned height;
  unsigned frame_count;
  bool axo_started;
};

bool overlay_context_init(struct overlay_context *overlay);
void overlay_context_destroy(struct overlay_context *overlay);
int overlay_context_get_event_fd(const struct overlay_context *overlay);
bool overlay_context_process_events(struct overlay_context *overlay);
bool overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu);
void overlay_context_bind_frame(struct overlay_context *overlay);
bool overlay_context_end_frame(struct overlay_context *overlay);
bool overlay_context_render_frame(struct overlay_context *overlay, const struct gpu_context *gpu);
