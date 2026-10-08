#pragma once

#include <stdbool.h>

/*
 * gpu_context is defined in gpu_context.h.
 * A forward declaration is enough here because this file only uses pointers to it.
 */
struct gpu_context;

/*
 * overlay_stream is defined privately in overlay.c.
 * Each instance is a persistent VDO stream record which may own a concrete axoverlay2 overlay.
 */
struct overlay_stream;

/*
 * Holds the state that connects Quake's rendered frames to active camera video streams.
 *
 * VDO discovers and monitors streams.
 * axoverlay2 is used to maintain one concrete overlay per active stream when overlay resources are available.
 *
 * All runtime axoverlay2 calls are serialized through one private SDK worker because the library has shared
 * process-wide state and axo_get_buffer() is blocking. The Yamagi render thread never waits for that worker.
 *
 * OpenGL remains on the Yamagi render thread: Quake renders once into a shared private framebuffer,
 * then the completed image is copied into buffers which the SDK worker has already acquired.
 */
struct overlay_context {
  /*
   * VDO stream used to receive notifications when video streams appear or disappear.
   *
   * This is stored as void * so overlay.h does not need to include the VDO headers.
   */
  void *event_stream;

  /*
   * Linked list of persistent VDO stream records.
   *
   * Records survive temporary concrete-overlay failures and stream ID reuse so asynchronous SDK results can be
   * matched to the VDO stream generation which started them.
   */
  struct overlay_stream *streams;

  /*
   * Private serialized axoverlay2 worker state.
   *
   * Stored as void * so pthread and axoverlay2 implementation details stay out of this public header.
   */
  void *worker_state;

  /*
   * File descriptor that can signal new VDO stream events.
   * The current renderer checks events at the start of each frame instead of waiting on it.
   */
  int event_fd;

  /*
   * Width and height of the shared framebuffer that Quake renders into.
   *
   * The first concrete overlay selects this size. Once created, it remains stable while VDO still reports at least
   * one stream as present, including periods where concrete overlays are being recreated after a failure.
   */
  unsigned width;
  unsigned height;

  /*
   * OpenGL texture that stores Quake's completed color image before it is copied into stream overlays.
   */
  unsigned render_texture;

  /*
   * Shared private OpenGL framebuffer that receives Quake's final image.
   */
  unsigned render_framebuffer;

  /*
   * Depth and stencil storage attached to the shared Quake framebuffer.
   */
  unsigned render_depth_stencil;

  /*
   * Number of game frames handed to the SDK worker for at least one stream.
   *
   * Submission itself is asynchronous. This counter is used only by the test-frame helper.
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
 * Concrete overlays are created later as video streams are discovered.
 *
 * Returns true when initialization succeeds.
 * Returns false if VDO or axoverlay2 setup fails.
 */
bool overlay_context_init(struct overlay_context *overlay);

/*
 * Release every stream overlay, the shared graphics resources, the VDO event stream and axoverlay2.
 *
 * This is also the common cleanup path after partial initialization failure.
 */
void overlay_context_destroy(struct overlay_context *overlay);

/*
 * Return the file descriptor used to receive VDO stream events.
 *
 * The current Yamagi integration polls events at the start of each frame instead of waiting on this descriptor.
 */
int overlay_context_get_event_fd(const struct overlay_context *overlay);

/*
 * Process all pending VDO stream events.
 *
 * Existing and newly created streams get persistent stream records.
 * A close and later reopen advances that record's stream generation.
 * Blocking SDK results are allowed to change lifecycle state only when their captured generation is still current.
 *
 * Concrete overlay creation is retried after temporary failures while the stream remains open.
 * Discovery and concrete-overlay failures that belong to one stream are isolated from the remaining streams.
 *
 * Returns false only when the VDO event feed itself or process-level resource allocation fails.
 */
bool overlay_context_process_events(struct overlay_context *overlay);

/*
 * Prepare the output path for a new Quake frame.
 *
 * This processes VDO events, synchronizes render-side caches with concrete-overlay generations, and consumes buffers
 * which the serialized SDK worker has already acquired. It never calls axo_get_buffer() on the game thread.
 *
 * Streams with no ready buffer are skipped for the current game frame.
 * If importing a buffer fails, that buffer is never submitted; the worker removes and retries the concrete overlay.
 *
 * Returns true when the game may continue rendering.
 * Returns false only for a failure in shared renderer state that prevents safe rendering.
 */
bool overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu);

/*
 * Bind the framebuffer Yamagi should treat as its final output framebuffer.
 *
 * While at least one stream has a buffer for the current frame, this binds the shared Quake framebuffer.
 * Otherwise it falls back to framebuffer 0.
 */
void overlay_context_bind_frame(struct overlay_context *overlay);

/*
 * Finish the current Quake frame.
 *
 * The completed image is copied into every ready stream buffer selected by overlay_context_begin_frame().
 * The final alpha is forced opaque and one GPU synchronization waits for all copies.
 *
 * Rendered buffers are then handed to the serialized SDK worker for asynchronous submission.
 * This function never calls axo_submit_buffer() and never waits for another SDK operation.
 */
bool overlay_context_end_frame(struct overlay_context *overlay);

/*
 * Test helper that renders a changing color through the same multi-stream overlay path used by Quake.
 *
 * Yamagi does not call this function.
 */
bool overlay_context_render_frame(struct overlay_context *overlay, const struct gpu_context *gpu);
