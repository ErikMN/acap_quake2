#pragma once

#include <stdbool.h>

/*
 * gpu_context is defined in gpu_context.h.
 * A forward declaration is enough here because this file only uses pointers to it.
 */
struct gpu_context;

/*
 * Private type for one active video stream.
 */
struct overlay_stream;

/*
 * Private type for one Axoverlay2 buffer imported into OpenGL.
 */
struct render_surface;

/*
 * Connects Quake's shared framebuffer to the active video streams.
 *
 * Each stream has its own Axoverlay2 overlay and imported output buffers.
 * Quake renders once per game frame. At most one stream receives that image.
 *
 * All Axoverlay2 calls run on the render thread.
 */
struct overlay_context {
  /*
   * VDO stream used to receive notifications when video streams appear or disappear.
   *
   * This is stored as void * so overlay.h does not need to include the VDO headers.
   */
  void *event_stream;

  /*
   * File descriptor that signals pending VDO stream events.
   */
  int event_fd;

  /*
   * Active video streams.
   */
  struct overlay_stream *streams;

  /*
   * Stream selected for the current Quake frame.
   */
  struct overlay_stream *current_stream;

  /*
   * OpenGL view of the selected Axoverlay2 buffer.
   */
  struct render_surface *current_surface;

  /*
   * Axoverlay2 buffer held while the current frame is being prepared.
   *
   * This is stored as void * so overlay.h does not need to include the Axoverlay2 headers.
   */
  void *current_buffer;

  /*
   * Size of the shared framebuffer that Quake renders into.
   *
   * The first stream selects this size.
   * The size stays unchanged until all streams close.
   */
  unsigned width;
  unsigned height;

  /*
   * OpenGL objects for Quake's shared framebuffer.
   */
  unsigned render_texture;
  unsigned render_framebuffer;
  unsigned render_depth_stencil;

  /*
   * Position used to choose the next stream in round-robin order.
   */
  unsigned next_stream_index;

  /*
   * True after Axoverlay2 has started successfully.
   */
  bool axo_started;
};

/*
 * Start Axoverlay2 and VDO stream monitoring.
 *
 * Returns true on success.
 */
bool overlay_context_init(struct overlay_context *overlay);

/*
 * Release stream overlays, OpenGL resources and VDO monitoring.
 * Then stop Axoverlay2.
 *
 * This is safe after partial initialization.
 */
void overlay_context_destroy(struct overlay_context *overlay);

/*
 * Process all pending VDO stream events.
 *
 * This is called once during renderer startup so already-active streams are found
 * before Yamagi asks for the drawable size.
 * It is also called at the start of every Quake frame.
 *
 * Returns true on success.
 */
bool overlay_context_process_events(struct overlay_context *overlay);

/*
 * Prepare a Quake frame.
 *
 * Stream events and delayed overlay retries are handled first.
 * Then one active stream is selected and asked for an output buffer.
 *
 * Quake can still render when no output buffer is ready.
 *
 * Returns true when rendering may continue.
 */
bool overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu);

/*
 * Bind the shared framebuffer that Quake uses as its final output.
 */
void overlay_context_bind_frame(struct overlay_context *overlay);

/*
 * Finish a Quake frame.
 *
 * If a stream buffer was selected, copy the Quake image into it, preserve the
 * image aspect ratio, wait for GPU writes to finish and submit the buffer.
 *
 * Returns true on success, including frames where there was nothing to submit.
 */
bool overlay_context_end_frame(struct overlay_context *overlay);
