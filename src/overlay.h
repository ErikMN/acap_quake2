#pragma once

#include <stdbool.h>

/*
 * gpu_context is defined in gpu_context.h.
 * A forward declaration is enough here because this file only uses pointers to it.
 */
struct gpu_context;

/*
 * render_surface is defined privately in overlay.c.
 * Each render_surface represents one axoverlay2 buffer that has been imported into EGL and OpenGL.
 */
struct render_surface;

/*
 * Holds all state needed to connect Quake's rendered frames to an axoverlay2 overlay.
 *
 * VDO is used to discover and monitor video streams.
 * axoverlay2 is used to create the overlay and provide the buffers that will be displayed.
 * OpenGL is used to render Quake into a private framebuffer and copy the finished image into those buffers.
 */
struct overlay_context {
  /*
   * VDO stream used to receive notifications when video streams appear or disappear.
   *
   * This is stored as void * so overlay.h does not need to include the VDO headers.
   */
  void *event_stream;

  /*
   * Detailed axoverlay2 buffer format selected when the overlay is created.
   *
   * This is stored as void * so overlay.h does not need to include the axoverlay2 headers.
   */
  void *format;

  /*
   * Linked list of axoverlay2 buffers that have already been imported into EGL and OpenGL.
   *
   * Keeping these objects allows the same buffer to be reused without importing it again every frame.
   */
  struct render_surface *surfaces;

  /*
   * OpenGL representation of the axoverlay2 buffer being used for the current frame.
   */
  struct render_surface *current_surface;

  /*
   * axoverlay2 buffer currently owned by the application while a frame is being prepared.
   *
   * This is stored as void * so overlay.h does not need to include the axoverlay2 headers.
   */
  void *current_buffer;

  /*
   * File descriptor used to wait for new VDO stream events.
   */
  int event_fd;

  /*
   * ID of the active axoverlay2 overlay.
   *
   * A negative value means no overlay is currently active.
   */
  int overlay_id;

  /*
   * ID of the VDO video stream that the current axoverlay2 overlay is attached to.
   */
  unsigned stream_id;

  /*
   * Width of the framebuffer that Quake renders into.
   *
   * This can be smaller than the video stream when axoverlay2 2x upscaling is enabled.
   */
  unsigned width;

  /*
   * Height of the framebuffer that Quake renders into.
   *
   * This can be smaller than the video stream when axoverlay2 2x upscaling is enabled.
   */
  unsigned height;

  /*
   * Full width of the allocated axoverlay2 buffer after format and alignment requirements are applied.
   */
  unsigned full_width;

  /*
   * Full height of the allocated axoverlay2 buffer after format and alignment requirements are applied.
   */
  unsigned full_height;

  /*
   * OpenGL texture that stores Quake's completed color image before it is copied into an axoverlay2 buffer.
   */
  unsigned render_texture;

  /*
   * Private OpenGL framebuffer that Quake renders into each frame.
   */
  unsigned render_framebuffer;

  /*
   * Depth and stencil storage attached to Quake's private framebuffer.
   */
  unsigned render_depth_stencil;

  /*
   * Number of successfully submitted overlay frames.
   *
   * This is used for progress logging and by the built-in test renderer.
   */
  unsigned frame_count;

  /*
   * Records whether axoverlay2 was successfully started so shutdown knows whether axo_stop() must be called.
   */
  bool axo_started;
};

/*
 * Initialize VDO stream monitoring and start axoverlay2.
 *
 * The actual overlay is created later when an available video stream is discovered.
 *
 * Returns true when initialization succeeds.
 * Returns false if VDO or axoverlay2 setup fails.
 */
bool overlay_context_init(struct overlay_context *overlay);

/*
 * Remove the active axoverlay2 overlay, release imported buffers and stop VDO and axoverlay2 resources.
 *
 * This can also be used to clean up after a partial initialization failure.
 */
void overlay_context_destroy(struct overlay_context *overlay);

/*
 * Return the file descriptor used to receive VDO stream events.
 *
 * The caller can wait on this descriptor instead of repeatedly checking for stream changes.
 */
int overlay_context_get_event_fd(const struct overlay_context *overlay);

/*
 * Process all currently pending VDO stream events.
 *
 * New streams can cause an axoverlay2 overlay to be created.
 * Closed streams cause the matching overlay and its resources to be removed.
 *
 * Returns true when all pending events were handled successfully.
 * Returns false if an unexpected VDO or overlay error occurs.
 */
bool overlay_context_process_events(struct overlay_context *overlay);

/*
 * Prepare the output path for a new Quake frame.
 *
 * This processes pending VDO events, requests the next available axoverlay2 buffer and binds
 * Quake's private framebuffer so the game can render into it.
 *
 * Returns true when rendering may continue.
 * A true result can also mean that no overlay buffer is currently available and the frame should be skipped.
 * Returns false if an unexpected error occurs.
 */
bool overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu);

/*
 * Bind the framebuffer that Quake should treat as its final output framebuffer.
 *
 * During an active overlay frame this binds Quake's private framebuffer instead of framebuffer 0.
 */
void overlay_context_bind_frame(struct overlay_context *overlay);

/*
 * Finish the current Quake frame and submit it to axoverlay2.
 *
 * The completed game image is copied from Quake's private framebuffer into the current axoverlay2 buffer.
 * The final alpha channel is made fully opaque, the GPU is allowed to finish its work and the buffer is submitted.
 *
 * Returns true when the frame was completed or there was no frame to submit.
 * Returns false if the buffer could not be submitted because of an unexpected error.
 */
bool overlay_context_end_frame(struct overlay_context *overlay);

/*
 * Render a simple changing color through the same overlay path used by Quake.
 *
 * This is useful for testing VDO, axoverlay2, EGL and OpenGL integration without depending on the game renderer.
 *
 * Returns true when the test frame was handled successfully.
 * Returns false if preparing or submitting the frame fails.
 */
bool overlay_context_render_frame(struct overlay_context *overlay, const struct gpu_context *gpu);
