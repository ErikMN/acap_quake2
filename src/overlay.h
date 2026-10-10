#pragma once

#include <stdbool.h>

/*
 * gpu_context is defined in gpu_context.h.
 * A forward declaration is enough here because this file only uses pointers to it.
 */
struct gpu_context;

/*
 * overlay_stream and render_surface are defined privately in overlay.c.
 *
 * Each overlay_stream represents one active VDO stream and its concrete axoverlay2 overlay.
 * Each render_surface represents one axoverlay2 buffer from that stream which has been imported into EGL and OpenGL.
 */
struct overlay_stream;
struct render_surface;

/*
 * Holds the state that connects one Quake render target to every active VDO stream.
 *
 * VDO discovers and monitors video streams.
 * axoverlay2 creates one concrete overlay per active stream and provides the buffers displayed on that stream.
 * OpenGL renders Quake once into a private framebuffer and copies the completed image into at most one stream buffer
 * per game frame.
 *
 * All axoverlay2 calls remain synchronous on the render thread. Supporting more streams therefore does not introduce
 * worker threads or multiply the number of potentially blocking buffer acquisitions performed in one game frame.
 */
struct overlay_context {
  /*
   * VDO stream used to receive notifications when video streams appear or disappear.
   *
   * Stored as void * so overlay.h does not need to include the VDO headers.
   */
  void *event_stream;

  /*
   * Linked list of active VDO stream records.
   *
   * Each record owns its concrete axoverlay2 overlay, detailed buffer format and cache of imported output buffers.
   */
  struct overlay_stream *streams;

  /*
   * VDO stream selected for presentation during the current Quake frame.
   *
   * Only one stream is selected because the renderer intentionally performs at most one axoverlay2 acquisition
   * attempt per game frame.
   */
  struct overlay_stream *current_stream;

  /*
   * OpenGL representation of the selected axoverlay2 buffer for the current frame.
   *
   * This points into the selected stream's render-surface cache.
   */
  struct render_surface *current_surface;

  /*
   * axoverlay2 buffer currently owned by the application while the selected output frame is being prepared.
   *
   * Stored as void * so overlay.h does not need to include the axoverlay2 headers.
   */
  void *current_buffer;

  /*
   * File descriptor that can signal new VDO stream events.
   *
   * A caller could wait on this descriptor, but the current Yamagi integration checks pending events at the
   * beginning of each frame instead.
   */
  int event_fd;

  /*
   * Width and height of the shared framebuffer that Quake renders into.
   *
   * The first successfully created stream selects this size. It can be smaller than that stream's video resolution
   * when axoverlay2 2x upscaling is enabled, and it remains stable until the final VDO stream record closes.
   */
  unsigned width;
  unsigned height;

  /*
   * OpenGL texture that stores Quake's completed color image before it is copied into a selected stream buffer.
   */
  unsigned render_texture;

  /*
   * Private OpenGL framebuffer that Quake treats as its final output framebuffer.
   */
  unsigned render_framebuffer;

  /*
   * Depth and stencil storage attached to Quake's private framebuffer.
   */
  unsigned render_depth_stencil;

  /*
   * Round-robin position used to select one active stream for the next output acquisition attempt.
   *
   * This keeps the number of blocking axo_get_buffer() calls bounded to one per Quake frame.
   */
  unsigned next_stream_index;

  /*
   * Total number of successfully submitted overlay frames across all streams.
   *
   * This is used by the test-frame helper and as general presentation progress state.
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
 * Concrete overlays are created later as available video streams are discovered.
 *
 * Returns true when initialization succeeds.
 * Returns false if VDO or axoverlay2 setup fails.
 */
bool overlay_context_init(struct overlay_context *overlay);

/*
 * Release every stream overlay and its graphics resources, release the shared Quake render target,
 * release the VDO event stream and stop axoverlay2.
 *
 * This can also be used to clean up after a partial initialization failure.
 */
void overlay_context_destroy(struct overlay_context *overlay);

/*
 * Return the file descriptor used to receive VDO stream events.
 *
 * The caller can wait on this descriptor for stream changes. The current Yamagi integration does not use it.
 */
int overlay_context_get_event_fd(const struct overlay_context *overlay);

/*
 * Process all currently pending VDO stream events.
 *
 * EXISTING and CREATED events register one concrete overlay per stream.
 * CLOSED removes only the matching stream and its resources while leaving other active streams untouched.
 *
 * Returns true when all pending events were handled successfully.
 * Returns false if an unexpected VDO or overlay error occurs.
 */
bool overlay_context_process_events(struct overlay_context *overlay);

/*
 * Prepare the output path for a new Quake frame.
 *
 * This processes pending VDO events, retries delayed stream-local overlay creation and selects one active stream
 * round-robin for at most one axoverlay2 buffer acquisition attempt.
 *
 * Quake renders into the shared private framebuffer whenever it exists, even if no overlay buffer is available for
 * the selected stream.
 *
 * Returns true when rendering may continue.
 * Returns false if an unexpected VDO, overlay or buffer error occurs.
 */
bool overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu);

/*
 * Bind the framebuffer that Quake should treat as its final output framebuffer.
 *
 * When at least one stream has established the shared private render target, this binds that framebuffer instead of
 * framebuffer 0. It does not depend on whether the current frame acquired an axoverlay2 output buffer.
 */
void overlay_context_bind_frame(struct overlay_context *overlay);

/*
 * Finish the current Quake frame and, when available, submit it to the selected stream.
 *
 * The completed game image is copied from the shared Quake framebuffer into the selected stream's axoverlay2 buffer.
 * The final alpha channel is made fully opaque, the code waits for GPU writes to finish and then submits that buffer.
 *
 * Only one stream is submitted per Quake frame. Multiple active streams share presentation opportunities round-robin
 * without causing multiple blocking acquisitions in one game frame.
 *
 * Returns true when the frame was handled without an unexpected error, including when there was nothing to submit.
 * Returns false if buffer submission fails for an unexpected reason.
 */
bool overlay_context_end_frame(struct overlay_context *overlay);

/*
 * Test helper: render a simple changing color through the same overlay path used by Quake.
 *
 * This tests the overlay output without depending on the game renderer. Yamagi does not call this function.
 *
 * Returns true when the test frame was handled successfully.
 * Returns false if preparing or submitting the frame fails.
 */
bool overlay_context_render_frame(struct overlay_context *overlay, const struct gpu_context *gpu);
