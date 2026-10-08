/*
 * Connects Quake's finished frames to camera video streams through axoverlay2.
 *
 * Quake renders each game frame once into a private OpenGL framebuffer owned by the application.
 * For every active VDO stream, this file keeps a persistent stream record and attempts to maintain one concrete
 * axoverlay2 overlay. When that overlay has an output buffer available, the completed Quake image is copied into it.
 *
 * Each axoverlay2 overlay cycles through several buffers instead of using one permanent image.
 * The first time a buffer is seen for a stream, it is imported into EGL and OpenGL.
 * The GPU can then copy the completed frame directly into that buffer without reading pixels back through the CPU.
 */
#include "overlay.h"

#include "gpu_context.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#include <axoverlay2.h>

/*
 * VDO is used to discover video streams and follow changes when streams appear or disappear while Quake is running.
 */
#include <glib-object.h>
#include <vdo-error.h>
#include <vdo-stream.h>

/*
 * Delay before retrying a concrete overlay which failed to create or needs to be recreated.
 *
 * Stream records remain alive during this delay, so retries do not depend on the browser opening another stream.
 */
#define OVERLAY_CREATE_RETRY_US 1000000

/*
 * axoverlay2 cycles through several overlay buffers instead of using one permanent image.
 *
 * The first time a buffer is received from axoverlay2, it is imported into EGL.
 * This structure keeps the associated OpenGL objects so the same buffer can be reused when it returns later.
 */
struct render_surface {
  /*
   * Unique ID assigned to the axoverlay2 buffer.
   * This is used to recognize the same buffer when it appears again later.
   */
  unsigned long buffer_id;

  /*
   * EGL display that owns the imported image.
   * It is saved here because it is needed again when the image is destroyed.
   */
  EGLDisplay display;

  /*
   * EGL representation of the DMA-BUF backing the axoverlay2 buffer.
   * This connects the axoverlay2 buffer to the graphics system.
   */
  EGLImageKHR image;

  /*
   * OpenGL texture backed by the EGL image.
   * Writing to this texture therefore writes into the axoverlay2 buffer.
   */
  GLuint texture;

  /*
   * OpenGL framebuffer with the imported axoverlay2-backed texture attached as its color image.
   * This lets normal OpenGL commands write into the axoverlay2 buffer.
   */
  GLuint framebuffer;

  GLuint depth_stencil;

  struct render_surface *next;
};

/*
 * State owned by one VDO stream.
 *
 * The VDO record and the concrete axoverlay2 overlay have deliberately separate lifetimes.
 * A record remains present across temporary overlay failures and stream ID reuse so asynchronous SDK results
 * can be checked against the stream generation that produced them.
 *
 * buffer_state is the ownership protocol for the one axoverlay2 buffer which may be outstanding:
 *
 * NONE      The SDK worker may acquire a buffer.
 * READY     The worker acquired a buffer and published its metadata to the render thread.
 * RENDERING The render thread claimed that buffer for the current Quake frame.
 * SUBMIT    GPU work is complete and the SDK worker may submit the rendered buffer.
 * DISCARD   The buffer must not be submitted. Removing the concrete overlay releases it.
 *
 * State changes shared with the SDK worker happen while overlay_worker.mutex is held.
 */
enum overlay_buffer_state {
  OVERLAY_BUFFER_NONE,
  OVERLAY_BUFFER_READY,
  OVERLAY_BUFFER_RENDERING,
  OVERLAY_BUFFER_SUBMIT,
  OVERLAY_BUFFER_DISCARD,
};

struct overlay_stream {
  /*
   * VDO identity and the dimensions most recently reported for the current stream instance.
   */
  unsigned stream_id;
  unsigned stream_width;
  unsigned stream_height;

  /*
   * Generation of the current VDO stream instance.
   *
   * Closing and reopening a stream advances this value. Every blocking SDK operation captures the generation
   * it started for and may change lifecycle state only if that generation is still current when the call returns.
   * This prevents a late result from an old stream instance from deleting a newly reopened stream with the same ID.
   */
  uint64_t generation;
  bool desired_present;

  /*
   * A stream-local failure can request replacement of the concrete overlay without discarding the VDO record.
   */
  bool recreate_requested;
  int64_t retry_at_us;

  /*
   * Concrete axoverlay2 state.
   *
   * All fields in this group are changed by the single SDK worker while holding overlay_worker.mutex.
   * The render thread reads primitive metadata under that mutex but never calls axoverlay2 through these objects.
   */
  int overlay_id;
  uint64_t overlay_generation;
  uint64_t overlay_epoch;
  bool sdk_busy;
  axo_detailed_format *format;

  unsigned width;
  unsigned height;
  unsigned full_width;
  unsigned full_height;
  uint32_t drm_fourcc;
  uint64_t drm_modifier;

  /*
   * Round-robin ordering for blocking acquisition attempts.
   */
  uint64_t acquire_order;

  /*
   * One axoverlay2 buffer may be outstanding for a concrete overlay.
   *
   * The worker owns the axo_buffer pointer. The render thread only receives the ID, DMA-BUF descriptor and
   * format metadata needed for EGL import.
   */
  enum overlay_buffer_state buffer_state;
  axo_buffer *buffer;
  uint64_t buffer_generation;
  unsigned long buffer_id;
  int dma_buf_fd;

  /*
   * Imported OpenGL surfaces are owned exclusively by the render thread.
   * render_overlay_epoch tells the render side when a concrete overlay was replaced and its cache must be rebuilt.
   */
  struct render_surface *surfaces;
  uint64_t render_overlay_epoch;

  /*
   * Metadata for the buffer used by the game frame currently being rendered.
   * These fields are owned exclusively by the render thread.
   */
  struct render_surface *current_surface;
  uint64_t current_generation;
  uint64_t current_overlay_epoch;
  unsigned long current_buffer_id;
  int current_dma_buf_fd;
  unsigned current_width;
  unsigned current_height;
  unsigned current_full_width;
  unsigned current_full_height;
  uint32_t current_drm_fourcc;
  uint64_t current_drm_modifier;

  /*
   * Number of buffers successfully submitted for this stream.
   * Used only for periodic progress logging.
   */
  unsigned frame_count;

  struct overlay_stream *next;
};

/*
 * Axoverlay2 uses shared process-wide request and bookkeeping state.
 *
 * Keep every runtime axoverlay2 operation on one worker thread. This is more restrictive than merely protecting
 * individual stream records: create, remove, acquire and submit are serialized across all streams.
 *
 * axo_get_buffer() is blocking. The worker may therefore spend time inside one stream's acquisition call, but
 * the Yamagi render thread never waits for that call and remains independent of SDK latency.
 */
struct overlay_worker {
  pthread_t thread;

  /*
   * Protects stream lifecycle fields, buffer ownership state and SDK action selection.
   *
   * The mutex is deliberately released before a blocking axoverlay2 call.
   * That lets the render thread process VDO events and completed frames while the SDK worker waits.
   */
  pthread_mutex_t mutex;

  /*
   * Wakes the worker after VDO state changes, render completion or shutdown.
   * The condition does not represent one specific event; the worker always recomputes the highest-priority action.
   */
  pthread_cond_t cond;

  bool started;
  bool stop;

  /*
   * Monotonic tokens used to distinguish stream instances, concrete overlays and acquisition order.
   * Zero is reserved for state that has not yet been assigned.
   */
  uint64_t next_generation;
  uint64_t next_overlay_epoch;
  uint64_t next_acquire_order;

  struct overlay_context *overlay;
};

enum overlay_sdk_action_type {
  OVERLAY_SDK_ACTION_NONE,
  OVERLAY_SDK_ACTION_REMOVE,
  OVERLAY_SDK_ACTION_CREATE,
  OVERLAY_SDK_ACTION_SUBMIT,
  OVERLAY_SDK_ACTION_ACQUIRE,
};

/*
 * Immutable snapshot of one SDK operation selected while the worker mutex is held.
 *
 * The SDK call itself runs after the mutex is released and may block. Generation, epoch, dimensions and object IDs
 * are copied here so the result can be checked against current stream state when the call returns.
 */
struct overlay_sdk_action {
  enum overlay_sdk_action_type type;

  unsigned stream_id;
  unsigned stream_width;
  unsigned stream_height;

  uint64_t generation;
  uint64_t overlay_epoch;
  int overlay_id;

  axo_detailed_format *format;
  axo_buffer *buffer;
};

static struct overlay_worker *get_overlay_worker(const struct overlay_context *overlay);
static struct overlay_stream *find_overlay_stream_locked(struct overlay_context *overlay, unsigned stream_id);
static bool overlay_create_action_is_current(const struct overlay_stream *stream,
                                             const struct overlay_sdk_action *action);

static bool start_overlay_worker(struct overlay_context *overlay);
static void stop_overlay_worker(struct overlay_context *overlay);
static void signal_overlay_worker(struct overlay_context *overlay);
static void *overlay_sdk_worker(void *userdata);
static bool select_overlay_sdk_action_locked(struct overlay_worker *worker, struct overlay_sdk_action *action);
static void perform_overlay_remove(struct overlay_worker *worker, const struct overlay_sdk_action *action);
static void perform_overlay_create(struct overlay_worker *worker, const struct overlay_sdk_action *action);
static void perform_overlay_submit(struct overlay_worker *worker, const struct overlay_sdk_action *action);
static void perform_overlay_acquire(struct overlay_worker *worker, const struct overlay_sdk_action *action);
static void cleanup_overlay_sdk_state(struct overlay_worker *worker);

static bool register_overlay_stream(struct overlay_context *overlay,
                                    unsigned stream_id,
                                    unsigned stream_width,
                                    unsigned stream_height);
static void remove_overlay_stream(struct overlay_context *overlay, unsigned stream_id);
static void mark_overlay_for_recreation(struct overlay_context *overlay,
                                        struct overlay_stream *stream,
                                        uint64_t generation);
static bool sync_render_resources(struct overlay_context *overlay);
static void prune_closed_streams(struct overlay_context *overlay);
static void destroy_overlay_streams(struct overlay_context *overlay);
static void clear_current_render_buffer(struct overlay_stream *stream);

static bool create_render_target(struct overlay_context *overlay);
static void destroy_render_target(struct overlay_context *overlay);

static struct render_surface *find_render_surface(struct overlay_stream *stream, unsigned long buffer_id);
static struct render_surface *create_render_surface(struct overlay_stream *stream, const struct gpu_context *gpu);

static void destroy_render_surface(struct render_surface *surface);
static void destroy_render_surfaces(struct overlay_stream *stream);
static bool has_active_output(const struct overlay_context *overlay);
static bool vdo_stream_disappeared(const GError *error);

/*
 * Start axoverlay2, subscribe to VDO events and start the serialized SDK worker.
 */
bool
overlay_context_init(struct overlay_context *overlay)
{
  axo_err *axo_error = NULL;
  GError *error = NULL;

  VdoMap *filter = NULL;

  /*
   * Start with every resource in its empty state.
   * This makes the normal cleanup path safe even if initialization fails halfway through.
   */
  overlay->event_stream = NULL;
  overlay->streams = NULL;
  overlay->worker_state = NULL;
  overlay->event_fd = -1;
  overlay->width = 0;
  overlay->height = 0;
  overlay->render_texture = 0;
  overlay->render_framebuffer = 0;
  overlay->render_depth_stencil = 0;
  overlay->frame_count = 0;
  overlay->axo_started = false;

  /*
   * axo_start() runs before the SDK worker exists, so no other axoverlay2 call can overlap it.
   */
  if (!axo_start(NULL, &axo_error)) {
    syslog(LOG_ERR, "Failed to start axoverlay2: %s", axo_err_get_message(axo_error));
    axo_err_clear(&axo_error);
    return false;
  }

  overlay->axo_started = true;

  /*
   * Open VDO stream 0 as the stream used for receiving video stream events.
   * This is the event source, not the stream Quake renders into.
   */
  VdoStream *event_stream = vdo_stream_get(0, &error);
  if (!event_stream) {
    syslog(LOG_ERR, "Failed to open VDO stream 0: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
    goto fail;
  }

  overlay->event_stream = event_stream;

  filter = vdo_map_new();
  vdo_map_set_string(filter, "filter", "overlay");

  if (!vdo_stream_attach(event_stream, filter, &error)) {
    syslog(LOG_ERR, "Failed to attach VDO stream filter: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
    goto fail;
  }

  overlay->event_fd = vdo_stream_get_event_fd(event_stream, &error);
  if (overlay->event_fd < 0) {
    syslog(LOG_ERR, "Failed to get VDO event fd: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
    goto fail;
  }

  if (!start_overlay_worker(overlay)) {
    goto fail;
  }

  g_object_unref(filter);

  syslog(LOG_INFO, "axoverlay2 initialized");
  syslog(LOG_INFO, "VDO event fd: %d", overlay->event_fd);

  return true;

fail:
  if (filter) {
    g_object_unref(filter);
  }

  overlay_context_destroy(overlay);
  return false;
}

/*
 * Stop all asynchronous SDK work before releasing render resources or stopping axoverlay2.
 */
void
overlay_context_destroy(struct overlay_context *overlay)
{
  stop_overlay_worker(overlay);

  /*
   * The worker has removed every concrete overlay before it exits.
   * EGL and OpenGL cache objects can now be released without racing SDK buffer ownership.
   */
  destroy_overlay_streams(overlay);
  destroy_render_target(overlay);

  overlay->width = 0;
  overlay->height = 0;

  if (overlay->event_stream) {
    g_object_unref(overlay->event_stream);
    overlay->event_stream = NULL;
  }

  overlay->event_fd = -1;

  /*
   * axo_stop() runs only after the serialized SDK worker has been joined.
   */
  if (overlay->axo_started) {
    axo_stop(NULL);
    overlay->axo_started = false;
  }
}

int
overlay_context_get_event_fd(const struct overlay_context *overlay)
{
  return overlay->event_fd;
}

/*
 * Process VDO lifecycle events.
 *
 * VDO events define which stream instance is current. SDK results are advisory only for the generation they started on.
 * A late result from an older generation is ignored and cannot override a newer CREATED event.
 */
bool
overlay_context_process_events(struct overlay_context *overlay)
{
  VdoStream *event_stream = overlay->event_stream;

  /*
   * Drain every event currently waiting.
   * VDO_ERROR_NO_EVENT is the normal indication that the queue is empty.
   */
  for (;;) {
    GError *error = NULL;
    VdoMap *event = vdo_stream_get_event(event_stream, &error);

    if (!event) {
      if (g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_EVENT)) {
        g_clear_error(&error);
        return true;
      }

      syslog(LOG_ERR, "Failed to get VDO event: %s", error ? error->message : "unknown error");
      g_clear_error(&error);
      return false;
    }

    unsigned event_type = vdo_map_get_uint32(event, "event", 0);
    unsigned stream_id = vdo_map_get_uint32(event, "id", 0);

    /*
     * EXISTING means the stream was already active when monitoring started.
     * CREATED means it appeared later. Both need the same registration path and a fresh size snapshot.
     */
    if (event_type == VDO_STREAM_EVENT_EXISTING || event_type == VDO_STREAM_EVENT_CREATED) {
      VdoStream *stream = vdo_stream_get(stream_id, &error);
      if (!stream) {
        if (vdo_stream_disappeared(error)) {
          syslog(LOG_INFO, "VDO stream %u disappeared during discovery", stream_id);
        } else {
          syslog(LOG_ERR,
                 "Failed to get VDO stream %u during discovery: %s",
                 stream_id,
                 error ? error->message : "unknown error");
        }

        g_clear_error(&error);
        g_object_unref(event);
        continue;
      }

      VdoMap *info = vdo_stream_get_info(stream, &error);
      if (!info) {
        if (vdo_stream_disappeared(error)) {
          syslog(LOG_INFO, "VDO stream %u closed before its information could be read", stream_id);
        } else {
          syslog(LOG_ERR, "Failed to get VDO stream %u info: %s", stream_id, error ? error->message : "unknown error");
        }

        g_clear_error(&error);
        g_object_unref(stream);
        g_object_unref(event);
        continue;
      }

      unsigned width = vdo_map_get_uint32(info, "width", 0);
      unsigned height = vdo_map_get_uint32(info, "height", 0);

      if (!width || !height) {
        syslog(LOG_ERR, "VDO stream %u has invalid size %ux%u", stream_id, width, height);

        g_object_unref(info);
        g_object_unref(stream);
        g_object_unref(event);
        continue;
      }

      bool registered = register_overlay_stream(overlay, stream_id, width, height);

      g_object_unref(info);
      g_object_unref(stream);
      g_object_unref(event);

      if (!registered) {
        return false;
      }

      continue;
    }

    if (event_type == VDO_STREAM_EVENT_CLOSED) {
      remove_overlay_stream(overlay, stream_id);
    }

    g_object_unref(event);
  }
}

/*
 * Prepare one Quake frame without ever waiting for axoverlay2.
 *
 * The SDK worker may currently be blocked in axo_get_buffer(). The render thread only consumes buffers which
 * have already been acquired and published as READY under the worker mutex.
 */
bool
overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu)
{
  if (!overlay_context_process_events(overlay)) {
    return false;
  }

  /*
   * Waking the worker once per game frame also drives delayed creation retries without a timer thread.
   */
  signal_overlay_worker(overlay);

  if (!sync_render_resources(overlay)) {
    return false;
  }

  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return true;
  }

  bool have_output = false;

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    pthread_mutex_lock(&worker->mutex);

    /*
     * sdk_busy is set before the SDK worker releases its mutex for create, remove, acquire or submit work.
     * recreate_requested can be set by a newer VDO update before the worker has selected the corresponding removal.
     *
     * In either case the current concrete overlay is no longer safe for a new render-side claim.
     */
    bool ready = stream->buffer_state == OVERLAY_BUFFER_READY && stream->desired_present &&
                 !stream->sdk_busy && !stream->recreate_requested &&
                 stream->overlay_id >= 0 && stream->overlay_generation == stream->generation &&
                 stream->buffer_generation == stream->generation &&
                 stream->render_overlay_epoch == stream->overlay_epoch;

    if (ready) {
      stream->buffer_state = OVERLAY_BUFFER_RENDERING;

      stream->current_generation = stream->generation;
      stream->current_overlay_epoch = stream->overlay_epoch;
      stream->current_buffer_id = stream->buffer_id;
      stream->current_dma_buf_fd = stream->dma_buf_fd;
      stream->current_width = stream->width;
      stream->current_height = stream->height;
      stream->current_full_width = stream->full_width;
      stream->current_full_height = stream->full_height;
      stream->current_drm_fourcc = stream->drm_fourcc;
      stream->current_drm_modifier = stream->drm_modifier;
    }

    pthread_mutex_unlock(&worker->mutex);

    if (!ready) {
      continue;
    }

    struct render_surface *surface = find_render_surface(stream, stream->current_buffer_id);

    if (!surface) {
      surface = create_render_surface(stream, gpu);
      if (!surface) {
        syslog(LOG_ERR,
               "Failed to import overlay buffer %lu for stream %u; recreating the concrete overlay",
               stream->current_buffer_id,
               stream->stream_id);

        mark_overlay_for_recreation(overlay, stream, stream->current_generation);
        clear_current_render_buffer(stream);
        continue;
      }

      surface->next = stream->surfaces;
      stream->surfaces = surface;

      syslog(LOG_INFO, "Imported overlay buffer %lu for stream %u", stream->current_buffer_id, stream->stream_id);
    }

    stream->current_surface = surface;
    have_output = true;
  }

  if (have_output) {
    if (!overlay->render_framebuffer) {
      syslog(LOG_ERR, "Overlay output is active without a Quake render target");
      return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);
    glViewport(0, 0, (GLsizei)overlay->width, (GLsizei)overlay->height);
  }

  return true;
}

void
overlay_context_bind_frame(struct overlay_context *overlay)
{
  if (has_active_output(overlay)) {
    glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
}

/*
 * Finish the GPU work for this frame and hand rendered buffers back to the SDK worker for submission.
 *
 * Submission is asynchronous. This function never calls axo_submit_buffer() and never waits for another SDK operation.
 */
bool
overlay_context_end_frame(struct overlay_context *overlay)
{
  if (!has_active_output(overlay)) {
    return true;
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, overlay->render_framebuffer);

  const GLfloat opaque[] = { 0.0f, 0.0f, 0.0f, 1.0f };

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (!stream->current_surface) {
      continue;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, stream->current_surface->framebuffer);

    glBlitFramebuffer(0,
                      0,
                      (GLint)overlay->width,
                      (GLint)overlay->height,
                      0,
                      (GLint)stream->current_height,
                      (GLint)stream->current_width,
                      0,
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    /*
     * Overlay alpha controls how much camera video shows through.
     * Force only the alpha channel to 1.0 so the game image is fully opaque.
     */
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearBufferfv(GL_COLOR, 0, opaque);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  }

  /*
   * Manual DMA synchronization is enabled on every concrete overlay.
   * Finish all GPU writes before the SDK worker is allowed to submit any rendered buffer.
   */
  glFinish();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  struct overlay_worker *worker = get_overlay_worker(overlay);
  bool queued = false;

  if (worker) {
    pthread_mutex_lock(&worker->mutex);

    for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
      if (!stream->current_surface) {
        continue;
      }

      bool current = stream->buffer_state == OVERLAY_BUFFER_RENDERING && stream->desired_present &&
                     stream->generation == stream->current_generation &&
                     stream->overlay_epoch == stream->current_overlay_epoch &&
                     stream->buffer_generation == stream->current_generation;

      if (current) {
        stream->buffer_state = OVERLAY_BUFFER_SUBMIT;
        queued = true;
      } else if (stream->buffer_state == OVERLAY_BUFFER_RENDERING) {
        /*
         * The stream instance changed while the frame was being rendered.
         * Do not submit a frame that belongs to an obsolete generation.
         */
        stream->buffer_state = OVERLAY_BUFFER_DISCARD;
        stream->recreate_requested = stream->desired_present;
      }

      clear_current_render_buffer(stream);
    }

    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);
  }

  if (queued) {
    overlay->frame_count++;
  }

  return true;
}

/*
 * Test helper that exercises the same asynchronous overlay path as the game renderer.
 */
bool
overlay_context_render_frame(struct overlay_context *overlay, const struct gpu_context *gpu)
{
  if (!overlay_context_begin_frame(overlay, gpu)) {
    return false;
  }

  if (!has_active_output(overlay)) {
    return true;
  }

  float phase = (float)(overlay->frame_count % 120) / 119.0f;

  glClearColor(1.0f - phase, phase, 0.0f, 1.0f);
  glClearDepthf(1.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  return overlay_context_end_frame(overlay);
}

/*
 * Access the private worker state without exposing pthread types through overlay.h.
 */
static struct overlay_worker *
get_overlay_worker(const struct overlay_context *overlay)
{
  return (struct overlay_worker *)overlay->worker_state;
}

/*
 * The worker mutex must be held while this helper is used.
 */
static struct overlay_stream *
find_overlay_stream_locked(struct overlay_context *overlay, unsigned stream_id)
{
  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (stream->stream_id == stream_id) {
      return stream;
    }
  }

  return NULL;
}

/*
 * Return true only while an overlay-creation action still describes the stream state which requested it.
 *
 * Stream generation protects close and reopen races.
 * Width and height protect a same-generation settings change which arrives while axo_create_overlay() is running.
 *
 * Keeping this check in one helper ensures success and failure paths apply the same precedence rules.
 */
static bool
overlay_create_action_is_current(const struct overlay_stream *stream, const struct overlay_sdk_action *action)
{
  return stream && stream->sdk_busy && stream->desired_present &&
         stream->generation == action->generation &&
         stream->stream_width == action->stream_width &&
         stream->stream_height == action->stream_height &&
         stream->overlay_id < 0 && !stream->recreate_requested;
}

/*
 * Allocate and start the one thread allowed to make runtime axoverlay2 calls.
 *
 * The worker does not need a separate job queue. Stream records contain the current desired state,
 * and select_overlay_sdk_action_locked() derives the next operation from that state whenever the worker wakes.
 */
static bool
start_overlay_worker(struct overlay_context *overlay)
{
  struct overlay_worker *worker = calloc(1, sizeof(*worker));
  if (!worker) {
    syslog(LOG_ERR, "Failed to allocate axoverlay2 worker state");
    return false;
  }

  int mutex_error = pthread_mutex_init(&worker->mutex, NULL);
  if (mutex_error != 0) {
    syslog(LOG_ERR, "Failed to initialize axoverlay2 worker mutex: %s", strerror(mutex_error));
    free(worker);
    return false;
  }

  int cond_error = pthread_cond_init(&worker->cond, NULL);
  if (cond_error != 0) {
    syslog(LOG_ERR, "Failed to initialize axoverlay2 worker condition variable: %s", strerror(cond_error));
    pthread_mutex_destroy(&worker->mutex);
    free(worker);
    return false;
  }

  worker->overlay = overlay;
  overlay->worker_state = worker;

  int thread_error = pthread_create(&worker->thread, NULL, overlay_sdk_worker, worker);
  if (thread_error != 0) {
    syslog(LOG_ERR, "Failed to start axoverlay2 worker: %s", strerror(thread_error));
    overlay->worker_state = NULL;
    pthread_cond_destroy(&worker->cond);
    pthread_mutex_destroy(&worker->mutex);
    free(worker);
    return false;
  }

  worker->started = true;
  return true;
}

/*
 * Stop and join the SDK worker during renderer shutdown.
 *
 * Joining can wait for a currently blocking axo_get_buffer() call. That is acceptable here because normal rendering
 * has already stopped; the game thread never performs this wait during stream maintenance.
 */
static void
stop_overlay_worker(struct overlay_context *overlay)
{
  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return;
  }

  pthread_mutex_lock(&worker->mutex);
  worker->stop = true;
  pthread_cond_broadcast(&worker->cond);
  pthread_mutex_unlock(&worker->mutex);

  if (worker->started) {
    int error = pthread_join(worker->thread, NULL);
    if (error != 0) {
      syslog(LOG_ERR, "Failed to join axoverlay2 worker: %s", strerror(error));
    }
  }

  overlay->worker_state = NULL;

  pthread_cond_destroy(&worker->cond);
  pthread_mutex_destroy(&worker->mutex);
  free(worker);
}

/*
 * Wake the SDK worker after shared state changes.
 *
 * Signals may be coalesced. The worker does not rely on signal count because it re-evaluates all stream state
 * under the mutex before selecting its next action.
 */
static void
signal_overlay_worker(struct overlay_context *overlay)
{
  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return;
  }

  pthread_mutex_lock(&worker->mutex);
  pthread_cond_signal(&worker->cond);
  pthread_mutex_unlock(&worker->mutex);
}

/*
 * Choose one SDK operation while holding the global worker mutex.
 *
 * Priority is deliberate:
 * 1. remove obsolete concrete overlays
 * 2. submit already rendered buffers
 * 3. create missing overlays
 * 4. perform one blocking acquisition attempt
 *
 * This prevents new acquisition attempts from jumping ahead of teardown or completed game frames.
 */
static bool
select_overlay_sdk_action_locked(struct overlay_worker *worker, struct overlay_sdk_action *action)
{
  struct overlay_context *overlay = worker->overlay;
  int64_t now = g_get_monotonic_time();

  memset(action, 0, sizeof(*action));

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    bool obsolete = !stream->desired_present || stream->recreate_requested ||
                    stream->overlay_generation != stream->generation;

    if (stream->overlay_id >= 0 && !stream->sdk_busy &&
        stream->buffer_state != OVERLAY_BUFFER_RENDERING && obsolete) {
      action->type = OVERLAY_SDK_ACTION_REMOVE;
      action->stream_id = stream->stream_id;
      action->generation = stream->overlay_generation;
      action->overlay_epoch = stream->overlay_epoch;
      action->overlay_id = stream->overlay_id;
      action->format = stream->format;

      stream->sdk_busy = true;
      return true;
    }
  }

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    bool current = stream->desired_present && !stream->recreate_requested &&
                   stream->overlay_id >= 0 && stream->overlay_generation == stream->generation;

    if (current && !stream->sdk_busy && stream->buffer_state == OVERLAY_BUFFER_SUBMIT) {
      action->type = OVERLAY_SDK_ACTION_SUBMIT;
      action->stream_id = stream->stream_id;
      action->generation = stream->buffer_generation;
      action->overlay_epoch = stream->overlay_epoch;
      action->overlay_id = stream->overlay_id;
      action->buffer = stream->buffer;

      stream->sdk_busy = true;
      return true;
    }
  }

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (stream->desired_present && !stream->recreate_requested &&
        stream->overlay_id < 0 && !stream->sdk_busy && stream->retry_at_us <= now) {
      action->type = OVERLAY_SDK_ACTION_CREATE;
      action->stream_id = stream->stream_id;
      action->stream_width = stream->stream_width;
      action->stream_height = stream->stream_height;
      action->generation = stream->generation;

      stream->sdk_busy = true;
      return true;
    }
  }

  struct overlay_stream *candidate = NULL;

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    bool current = stream->desired_present && !stream->recreate_requested &&
                   stream->overlay_id >= 0 && stream->overlay_generation == stream->generation;

    if (!current || stream->sdk_busy || stream->buffer_state != OVERLAY_BUFFER_NONE) {
      continue;
    }

    if (!candidate || stream->acquire_order < candidate->acquire_order) {
      candidate = stream;
    }
  }

  if (candidate) {
    action->type = OVERLAY_SDK_ACTION_ACQUIRE;
    action->stream_id = candidate->stream_id;
    action->generation = candidate->generation;
    action->overlay_epoch = candidate->overlay_epoch;
    action->overlay_id = candidate->overlay_id;

    candidate->sdk_busy = true;
    candidate->acquire_order = ++worker->next_acquire_order;
    return true;
  }

  action->type = OVERLAY_SDK_ACTION_NONE;
  return false;
}

/*
 * Execute all runtime axoverlay2 operations on one serialized thread.
 *
 * Action selection happens under the worker mutex and marks the chosen stream sdk_busy.
 * The mutex is then released before the SDK call so VDO events and render-thread state changes can continue.
 * Each action handler rechecks generation and overlay identity before applying a result.
 */
static void *
overlay_sdk_worker(void *userdata)
{
  struct overlay_worker *worker = userdata;

  for (;;) {
    struct overlay_sdk_action action;

    pthread_mutex_lock(&worker->mutex);

    while (!worker->stop && !select_overlay_sdk_action_locked(worker, &action)) {
      pthread_cond_wait(&worker->cond, &worker->mutex);
    }

    bool stop = worker->stop;
    pthread_mutex_unlock(&worker->mutex);

    if (stop) {
      break;
    }

    switch (action.type) {
      case OVERLAY_SDK_ACTION_REMOVE:
        perform_overlay_remove(worker, &action);
        break;

      case OVERLAY_SDK_ACTION_CREATE:
        perform_overlay_create(worker, &action);
        break;

      case OVERLAY_SDK_ACTION_SUBMIT:
        perform_overlay_submit(worker, &action);
        break;

      case OVERLAY_SDK_ACTION_ACQUIRE:
        perform_overlay_acquire(worker, &action);
        break;

      case OVERLAY_SDK_ACTION_NONE:
      default:
        break;
    }
  }

  /*
   * Shutdown may wait for a currently blocking acquisition, but once this loop reaches cleanup there are no
   * concurrent SDK callers and every remaining concrete overlay can be removed safely.
   */
  cleanup_overlay_sdk_state(worker);
  return NULL;
}

/*
 * Remove one obsolete concrete overlay and retire its SDK-owned state.
 *
 * The action carries the concrete overlay ID and epoch selected under the mutex. After the SDK call, local state is
 * cleared only if the stream record still refers to that same concrete overlay.
 */
static void
perform_overlay_remove(struct overlay_worker *worker, const struct overlay_sdk_action *action)
{
  axo_err *error = NULL;

  if (!axo_remove_overlay(action->overlay_id, &error)) {
    syslog(LOG_ERR,
           "Failed to remove overlay %d from stream %u: %s",
           action->overlay_id,
           action->stream_id,
           error ? axo_err_get_message(error) : "unknown error");
  }

  axo_err_clear(&error);

  if (action->format) {
    axo_detailed_format_free(action->format);
  }

  pthread_mutex_lock(&worker->mutex);

  struct overlay_stream *stream = find_overlay_stream_locked(worker->overlay, action->stream_id);

  if (stream && stream->overlay_id == action->overlay_id && stream->overlay_epoch == action->overlay_epoch) {
    stream->overlay_id = -1;
    stream->overlay_generation = 0;
    stream->overlay_epoch = 0;
    stream->format = NULL;
    stream->width = 0;
    stream->height = 0;
    stream->full_width = 0;
    stream->full_height = 0;
    stream->drm_fourcc = 0;
    stream->drm_modifier = 0;
    stream->buffer = NULL;
    stream->buffer_state = OVERLAY_BUFFER_NONE;
    stream->buffer_generation = 0;
    stream->buffer_id = 0;
    stream->dma_buf_fd = -1;
    stream->sdk_busy = false;

    /*
     * The old concrete overlay is gone. A still-present stream may now create a replacement when retry_at_us allows it.
     */
    stream->recreate_requested = false;
  }

  pthread_cond_signal(&worker->cond);
  pthread_mutex_unlock(&worker->mutex);
}

/*
 * Create a concrete axoverlay2 overlay from the stream snapshot captured in the action.
 *
 * Creation may take long enough for VDO state or dimensions to change. Both success and failure paths therefore
 * validate the captured generation and dimensions before changing the persistent stream record.
 */
static void
perform_overlay_create(struct overlay_worker *worker, const struct overlay_sdk_action *action)
{
  unsigned used_width = action->stream_width;
  unsigned used_height = action->stream_height;
  /*
   * When both dimensions divide cleanly by two, use a half-size overlay and let axoverlay2 upscale it by two.
   * This reduces the number of destination pixels written by the game frame to one quarter of the full stream.
   */
  bool use_upscale = action->stream_width % 2 == 0 && action->stream_height % 2 == 0;

  if (use_upscale) {
    used_width /= 2;
    used_height /= 2;
  }

  axo_err *error = NULL;

  /*
   * Request ARGB32 so the output has an alpha channel, plus a layout suitable for direct GPU access.
   * Compression is requested when the platform can provide a compatible compressed format.
   */
  axo_detailed_format *format =
      axo_suggest_detailed_format(AXO_FORMAT_ARGB32, AXO_FORMAT_FLAGS_COMPRESSED | AXO_FORMAT_FLAGS_GPU, &error);

  if (!format) {
    pthread_mutex_lock(&worker->mutex);

    struct overlay_stream *stream = find_overlay_stream_locked(worker->overlay, action->stream_id);

    if (overlay_create_action_is_current(stream, action)) {
      syslog(LOG_ERR,
             "Failed to get GPU overlay format for stream %u: %s; retrying later",
             action->stream_id,
             error ? axo_err_get_message(error) : "unknown error");
      stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
      stream->sdk_busy = false;
    } else if (stream && stream->sdk_busy) {
      /*
       * The VDO stream changed while this creation action was running.
       * Drop only the old action state so the worker can immediately create from the newest dimensions.
       */
      stream->sdk_busy = false;
    }

    pthread_mutex_unlock(&worker->mutex);
    axo_err_clear(&error);
    return;
  }

  unsigned full_width;
  unsigned full_height;

  /*
   * The visible overlay size and the allocated buffer size are not necessarily equal.
   * Ask axoverlay2 for the format alignment, then keep the existing 16-pixel alignment required by this import path.
   */
  axo_detailed_format_get_aligned_size(format, used_width, used_height, &full_width, &full_height);

  full_width = (full_width + 15) & ~15u;
  full_height = (full_height + 15) & ~15u;

  axo_props *props = axo_props_new();
  axo_match *match = axo_match_new();

  if (!props || !match) {
    if (props) {
      axo_props_free(props);
    }
    if (match) {
      axo_match_free(match);
    }

    axo_detailed_format_free(format);

    pthread_mutex_lock(&worker->mutex);

    struct overlay_stream *stream = find_overlay_stream_locked(worker->overlay, action->stream_id);

    if (overlay_create_action_is_current(stream, action)) {
      syslog(LOG_ERR, "Failed to allocate axoverlay2 creation objects for stream %u; retrying later", action->stream_id);
      stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
      stream->sdk_busy = false;
    } else if (stream && stream->sdk_busy) {
      stream->sdk_busy = false;
    }

    pthread_mutex_unlock(&worker->mutex);
    return;
  }

  /*
   * props describes the overlay buffers and scaling behavior.
   * match binds this concrete overlay to exactly one VDO stream.
   *
   * Manual DMA synchronization is enabled because overlay_context_end_frame() completes GPU writes with glFinish()
   * before the SDK worker is allowed to submit the rendered buffer.
   */
  axo_props_set_detailed_format(props, format);
  axo_props_set_size(props, full_width, full_height);
  axo_props_set_upscale_x2(props, use_upscale);
  axo_props_set_manual_dma_sync(props, true);
  axo_match_stream_id(match, action->stream_id);

  int overlay_id = axo_create_overlay(props, match, &error);

  axo_props_free(props);
  axo_match_free(match);

  if (overlay_id < 0) {
    pthread_mutex_lock(&worker->mutex);

    struct overlay_stream *stream = find_overlay_stream_locked(worker->overlay, action->stream_id);

    /*
     * A creation result is authoritative only while both the VDO generation and requested dimensions still match.
     */
    bool current = overlay_create_action_is_current(stream, action);

    if (current && error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
      syslog(LOG_INFO, "VDO stream %u disappeared while creating its overlay", action->stream_id);
      stream->desired_present = false;
      stream->generation = ++worker->next_generation;
      stream->retry_at_us = 0;
      stream->sdk_busy = false;
    } else if (current) {
      syslog(LOG_ERR,
             "Failed to create overlay on stream %u: %s; retrying later",
             action->stream_id,
             error ? axo_err_get_message(error) : "unknown error");
      stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
      stream->sdk_busy = false;
    } else if (stream && stream->sdk_busy) {
      /*
       * A newer VDO event won the race. Do not let this old SDK result change the new stream instance.
       */
      stream->sdk_busy = false;
    }

    pthread_mutex_unlock(&worker->mutex);

    axo_err_clear(&error);
    axo_detailed_format_free(format);
    return;
  }

  uint32_t fourcc = axo_detailed_format_get_drm_fourcc(format);
  uint64_t modifier = axo_detailed_format_get_drm_modifier(format, 0);

  pthread_mutex_lock(&worker->mutex);

  struct overlay_stream *stream = find_overlay_stream_locked(worker->overlay, action->stream_id);

  bool current = overlay_create_action_is_current(stream, action);

  if (current) {
    stream->overlay_id = overlay_id;
    stream->overlay_generation = action->generation;
    stream->overlay_epoch = ++worker->next_overlay_epoch;
    stream->format = format;
    stream->width = used_width;
    stream->height = used_height;
    stream->full_width = full_width;
    stream->full_height = full_height;
    stream->drm_fourcc = fourcc;
    stream->drm_modifier = modifier;
    stream->buffer_state = OVERLAY_BUFFER_NONE;
    stream->buffer = NULL;
    stream->buffer_generation = 0;
    stream->dma_buf_fd = -1;
    stream->retry_at_us = 0;
    stream->acquire_order = ++worker->next_acquire_order;
    stream->sdk_busy = false;

    syslog(LOG_INFO,
           "Created GPU overlay %d on stream %u: stream %ux%u, overlay %ux%u, buffer %ux%u%s",
           overlay_id,
           action->stream_id,
           action->stream_width,
           action->stream_height,
           used_width,
           used_height,
           full_width,
           full_height,
           use_upscale ? ", 2x upscale" : "");

    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);
    axo_err_clear(&error);
    return;
  }

  if (stream && stream->sdk_busy) {
    stream->sdk_busy = false;
  }

  pthread_mutex_unlock(&worker->mutex);

  /*
   * The VDO generation or requested dimensions changed while creation was in progress.
   * Remove the stale overlay immediately on this same serialized SDK thread.
   * The next worker pass can then create an overlay from the newest stream state.
   */
  axo_err *remove_error = NULL;
  if (!axo_remove_overlay(overlay_id, &remove_error)) {
    syslog(LOG_ERR,
           "Failed to remove stale overlay %d from stream %u: %s",
           overlay_id,
           action->stream_id,
           remove_error ? axo_err_get_message(remove_error) : "unknown error");
  }
  axo_err_clear(&remove_error);
  axo_detailed_format_free(format);
  axo_err_clear(&error);
}

/*
 * Submit one buffer whose GPU writes were completed by the render thread.
 *
 * axo_submit_buffer() normally presents the image, so only buffers which reached OVERLAY_BUFFER_SUBMIT are selected.
 * A late result is applied only when the stream generation, concrete overlay and buffer still match the action.
 */
static void
perform_overlay_submit(struct overlay_worker *worker, const struct overlay_sdk_action *action)
{
  axo_err *error = NULL;
  bool submitted = axo_submit_buffer(action->buffer, NULL, &error);

  pthread_mutex_lock(&worker->mutex);

  struct overlay_stream *stream = find_overlay_stream_locked(worker->overlay, action->stream_id);

  bool current = stream && stream->sdk_busy && stream->overlay_id == action->overlay_id &&
                 stream->overlay_epoch == action->overlay_epoch &&
                 stream->buffer_state == OVERLAY_BUFFER_SUBMIT &&
                 stream->buffer == action->buffer &&
                 stream->buffer_generation == action->generation &&
                 stream->generation == action->generation;

  if (!current) {
    /*
     * A newer VDO generation arrived while submission was in progress.
     * The old operation may finish, but its result is not allowed to change the newer stream instance.
     */
    if (stream && stream->sdk_busy) {
      stream->sdk_busy = false;
      stream->buffer_state = OVERLAY_BUFFER_DISCARD;
    }

    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);
    axo_err_clear(&error);
    return;
  }

  if (submitted) {
    stream->buffer = NULL;
    stream->buffer_state = OVERLAY_BUFFER_NONE;
    stream->buffer_generation = 0;
    stream->buffer_id = 0;
    stream->dma_buf_fd = -1;
    stream->sdk_busy = false;
    stream->frame_count++;
  } else if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
    syslog(LOG_INFO, "VDO stream %u disappeared while submitting its overlay buffer", action->stream_id);

    stream->desired_present = false;
    stream->generation = ++worker->next_generation;
    stream->buffer_state = OVERLAY_BUFFER_DISCARD;
    stream->sdk_busy = false;
  } else {
    syslog(LOG_ERR,
           "Failed to submit overlay buffer for stream %u: %s; recreating the concrete overlay",
           action->stream_id,
           error ? axo_err_get_message(error) : "unknown error");

    stream->recreate_requested = true;
    stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
    stream->buffer_state = OVERLAY_BUFFER_DISCARD;
    stream->sdk_busy = false;
  }

  pthread_cond_signal(&worker->cond);
  pthread_mutex_unlock(&worker->mutex);
  axo_err_clear(&error);
}

/*
 * Perform the blocking buffer acquisition for one concrete overlay.
 *
 * This is the only axo_get_buffer() call in the application. It runs on the serialized SDK worker so the game thread
 * remains responsive and no second thread can enter axoverlay2 while the shared request path is in use.
 *
 * On success, only primitive buffer metadata is published to the render thread. The axo_buffer pointer remains owned
 * by the SDK worker until submission or concrete-overlay removal.
 */
static void
perform_overlay_acquire(struct overlay_worker *worker, const struct overlay_sdk_action *action)
{
  axo_err *error = NULL;
  axo_buffer *buffer = axo_get_buffer(action->overlay_id, NULL, &error);

  unsigned long buffer_id = 0;
  int dma_buf_fd = -1;

  if (buffer) {
    /*
     * Keep all axoverlay2 buffer access on the serialized worker as well.
     * The render thread receives only primitive metadata and the shared DMA-BUF descriptor.
     */
    buffer_id = axo_buffer_get_id(buffer);
    dma_buf_fd = axo_buffer_get_dma_buf_fd(buffer);
  }

  pthread_mutex_lock(&worker->mutex);

  struct overlay_stream *stream = find_overlay_stream_locked(worker->overlay, action->stream_id);

  bool current = stream && stream->sdk_busy && stream->desired_present &&
                 stream->generation == action->generation &&
                 stream->overlay_id == action->overlay_id &&
                 stream->overlay_epoch == action->overlay_epoch &&
                 stream->overlay_generation == action->generation &&
                 stream->buffer_state == OVERLAY_BUFFER_NONE &&
                 !stream->recreate_requested;

  if (!current) {
    /*
     * This result belongs to an obsolete stream generation or concrete overlay.
     * In particular, a late NO_STREAM from an old instance must not override a newer CREATED event.
     */
    if (stream && stream->sdk_busy) {
      stream->sdk_busy = false;
    }

    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);
    axo_err_clear(&error);
    return;
  }

  if (buffer && dma_buf_fd >= 0) {
    stream->buffer = buffer;
    stream->buffer_state = OVERLAY_BUFFER_READY;
    stream->buffer_generation = action->generation;
    stream->buffer_id = buffer_id;
    stream->dma_buf_fd = dma_buf_fd;
    stream->sdk_busy = false;
  } else if (buffer) {
    /*
     * The buffer cannot be imported safely, and submitting it would present undrawn contents.
     * Leave it owned by the old concrete overlay and remove that overlay instead.
     */
    syslog(LOG_ERR,
           "Overlay buffer %lu for stream %u has no DMA-BUF; recreating the concrete overlay",
           buffer_id,
           action->stream_id);

    stream->buffer = buffer;
    stream->buffer_state = OVERLAY_BUFFER_DISCARD;
    stream->buffer_generation = action->generation;
    stream->recreate_requested = true;
    stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
    stream->sdk_busy = false;
  } else if (error && axo_err_get_code(error) == AXO_ERR_WAIT) {
    /*
     * WAIT is normal pacing. Move this stream to the back of the round-robin order and try another stream next.
     */
    stream->sdk_busy = false;
  } else if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
    /*
     * This can change presence only because the generation still matches the operation which produced the result.
     */
    stream->desired_present = false;
    stream->generation = ++worker->next_generation;
    stream->sdk_busy = false;
  } else {
    syslog(LOG_ERR,
           "Failed to acquire overlay buffer for stream %u: %s; recreating the concrete overlay",
           action->stream_id,
           error ? axo_err_get_message(error) : "unknown error");

    stream->recreate_requested = true;
    stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
    stream->sdk_busy = false;
  }

  pthread_cond_signal(&worker->cond);
  pthread_mutex_unlock(&worker->mutex);
  axo_err_clear(&error);
}

/*
 * Remove every concrete overlay before the worker exits.
 *
 * The worker is already the only axoverlay2 caller, so shutdown does not need additional SDK synchronization.
 */
static void
cleanup_overlay_sdk_state(struct overlay_worker *worker)
{
  for (;;) {
    struct overlay_sdk_action action;
    memset(&action, 0, sizeof(action));

    pthread_mutex_lock(&worker->mutex);

    struct overlay_stream *stream_with_overlay = NULL;

    for (struct overlay_stream *stream = worker->overlay->streams; stream; stream = stream->next) {
      if (stream->overlay_id >= 0) {
        stream_with_overlay = stream;
        break;
      }
    }

    if (!stream_with_overlay) {
      pthread_mutex_unlock(&worker->mutex);
      return;
    }

    action.type = OVERLAY_SDK_ACTION_REMOVE;
    action.stream_id = stream_with_overlay->stream_id;
    action.generation = stream_with_overlay->overlay_generation;
    action.overlay_epoch = stream_with_overlay->overlay_epoch;
    action.overlay_id = stream_with_overlay->overlay_id;
    action.format = stream_with_overlay->format;

    stream_with_overlay->sdk_busy = true;

    pthread_mutex_unlock(&worker->mutex);

    perform_overlay_remove(worker, &action);
  }
}

/*
 * Register a VDO stream instance.
 *
 * Duplicate EXISTING or CREATED events for a currently present stream do not advance the generation.
 * A CREATED event after CLOSED starts a new generation and therefore invalidates every outstanding SDK result
 * from the older instance.
 */
static bool
register_overlay_stream(struct overlay_context *overlay,
                        unsigned stream_id,
                        unsigned stream_width,
                        unsigned stream_height)
{
  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return false;
  }

  pthread_mutex_lock(&worker->mutex);

  struct overlay_stream *stream = find_overlay_stream_locked(overlay, stream_id);

  if (!stream) {
    stream = calloc(1, sizeof(*stream));
    if (!stream) {
      pthread_mutex_unlock(&worker->mutex);
      syslog(LOG_ERR, "Failed to allocate overlay stream record");
      return false;
    }

    stream->stream_id = stream_id;
    stream->generation = ++worker->next_generation;
    stream->desired_present = true;
    stream->overlay_id = -1;
    stream->dma_buf_fd = -1;
    stream->acquire_order = ++worker->next_acquire_order;

    stream->next = overlay->streams;
    overlay->streams = stream;
  } else if (!stream->desired_present) {
    stream->generation = ++worker->next_generation;
    stream->desired_present = true;
    stream->recreate_requested = stream->overlay_id >= 0;
    stream->retry_at_us = 0;
  }

  bool dimensions_changed =
      stream->stream_width != stream_width || stream->stream_height != stream_height;

  stream->stream_width = stream_width;
  stream->stream_height = stream_height;

  if (dimensions_changed && stream->overlay_id >= 0) {
    /*
     * An existing concrete overlay must be replaced.
     *
     * If creation is already running, overlay_id is still negative. In that case the action carries the old
     * dimensions and perform_overlay_create() rejects its result through overlay_create_action_is_current().
     */
    stream->recreate_requested = true;
    stream->retry_at_us = 0;
  }

  pthread_cond_signal(&worker->cond);
  pthread_mutex_unlock(&worker->mutex);

  syslog(LOG_INFO,
         "VDO stream %u available: %ux%u",
         stream_id,
         stream_width,
         stream_height);

  return true;
}

/*
 * End the current VDO stream generation.
 *
 * The record is retained while SDK or render-side state can still refer to it.
 * A later CREATED event can therefore start a newer generation on the same record before cleanup finishes.
 * prune_closed_streams() frees the record once it is completely unused.
 */
static void
remove_overlay_stream(struct overlay_context *overlay, unsigned stream_id)
{
  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return;
  }

  pthread_mutex_lock(&worker->mutex);

  struct overlay_stream *stream = find_overlay_stream_locked(overlay, stream_id);

  if (stream && stream->desired_present) {
    stream->desired_present = false;
    stream->generation = ++worker->next_generation;
    stream->recreate_requested = false;
    stream->retry_at_us = 0;

    pthread_cond_signal(&worker->cond);
  }

  pthread_mutex_unlock(&worker->mutex);
}

/*
 * Request replacement of the concrete overlay after a render-side failure.
 *
 * generation prevents a failed buffer from an older stream instance from changing the current one.
 */
static void
mark_overlay_for_recreation(struct overlay_context *overlay,
                            struct overlay_stream *stream,
                            uint64_t generation)
{
  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return;
  }

  pthread_mutex_lock(&worker->mutex);

  if (stream->desired_present && stream->generation == generation &&
      stream->buffer_state == OVERLAY_BUFFER_RENDERING) {
    stream->buffer_state = OVERLAY_BUFFER_DISCARD;
    stream->recreate_requested = true;
    stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;

    pthread_cond_signal(&worker->cond);
  }

  pthread_mutex_unlock(&worker->mutex);
}

/*
 * Synchronize render-thread caches with concrete overlay epochs and maintain the shared Quake render target.
 *
 * The render target is intentionally retained while any VDO stream is still considered present, even when every
 * concrete overlay is temporarily being recreated. This keeps Quake's output size stable across recovery.
 */
static bool
sync_render_resources(struct overlay_context *overlay)
{
  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return true;
  }

  bool any_desired = false;
  bool have_size = false;
  unsigned render_width = 0;
  unsigned render_height = 0;

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    pthread_mutex_lock(&worker->mutex);

    uint64_t overlay_epoch = stream->overlay_epoch;
    bool desired_present = stream->desired_present;
    bool active_overlay = stream->overlay_id >= 0 &&
                          stream->overlay_generation == stream->generation &&
                          desired_present;

    unsigned width = stream->width;
    unsigned height = stream->height;

    pthread_mutex_unlock(&worker->mutex);

    if (stream->render_overlay_epoch != overlay_epoch) {
      if (stream->current_surface) {
        syslog(LOG_ERR, "Overlay epoch changed while stream %u still had an active render buffer", stream->stream_id);
        return false;
      }

      destroy_render_surfaces(stream);
      stream->render_overlay_epoch = overlay_epoch;
    }

    any_desired = any_desired || desired_present;

    if (!have_size && active_overlay && width && height) {
      render_width = width;
      render_height = height;
      have_size = true;
    }
  }

  if (!any_desired) {
    if (overlay->render_framebuffer) {
      destroy_render_target(overlay);
      overlay->width = 0;
      overlay->height = 0;
      overlay->frame_count = 0;
    }
  } else if (!overlay->render_framebuffer && have_size) {
    overlay->width = render_width;
    overlay->height = render_height;

    if (!create_render_target(overlay)) {
      overlay->width = 0;
      overlay->height = 0;
      return false;
    }
  }

  /*
   * Records are kept while any old SDK operation or render cache can still refer to them.
   * Once a closed stream is completely quiescent, it can be removed without weakening the generation rule.
   */
  prune_closed_streams(overlay);

  return true;
}

/*
 * Free closed stream records only after both SDK and render-side state are gone.
 *
 * A record with sdk_busy set cannot be removed because the worker has an active SDK action which will look it up
 * again by stream ID when the blocking SDK call returns. A record with a concrete overlay, outstanding buffer,
 * current render surface or imported surface cache is also still in use.
 */
static void
prune_closed_streams(struct overlay_context *overlay)
{
  struct overlay_worker *worker = get_overlay_worker(overlay);
  if (!worker) {
    return;
  }

  pthread_mutex_lock(&worker->mutex);

  struct overlay_stream **link = &overlay->streams;

  while (*link) {
    struct overlay_stream *stream = *link;

    bool unused = !stream->desired_present && !stream->sdk_busy &&
                  stream->overlay_id < 0 && stream->buffer_state == OVERLAY_BUFFER_NONE &&
                  stream->render_overlay_epoch == 0 && !stream->current_surface && !stream->surfaces;

    if (!unused) {
      link = &stream->next;
      continue;
    }

    *link = stream->next;
    free(stream);
  }

  pthread_mutex_unlock(&worker->mutex);
}

/*
 * Release render-thread state after the SDK worker has stopped and removed all concrete overlays.
 */
static void
destroy_overlay_streams(struct overlay_context *overlay)
{
  struct overlay_stream *stream = overlay->streams;
  overlay->streams = NULL;

  while (stream) {
    struct overlay_stream *next = stream->next;

    clear_current_render_buffer(stream);
    destroy_render_surfaces(stream);
    free(stream);

    stream = next;
  }
}

/*
 * Clear only the render thread's snapshot of the current output buffer.
 *
 * This does not change buffer_state or the SDK-owned axo_buffer pointer. Those transitions happen under the worker
 * mutex before this helper is called.
 */
static void
clear_current_render_buffer(struct overlay_stream *stream)
{
  stream->current_surface = NULL;
  stream->current_generation = 0;
  stream->current_overlay_epoch = 0;
  stream->current_buffer_id = 0;
  stream->current_dma_buf_fd = -1;
  stream->current_width = 0;
  stream->current_height = 0;
  stream->current_full_width = 0;
  stream->current_full_height = 0;
  stream->current_drm_fourcc = 0;
  stream->current_drm_modifier = 0;
}

/*
 * Return true for expected discovery races where a VDO stream disappears between its lifecycle event and lookup.
 */
static bool
vdo_stream_disappeared(const GError *error)
{
  return error &&
      (g_error_matches(error, VDO_ERROR, VDO_ERROR_CLOSED) ||
       g_error_matches(error, VDO_ERROR, VDO_ERROR_NOT_FOUND));
}

/*
 * Create the private framebuffer with color, depth and stencil storage at the Quake render size.
 */
static bool
create_render_target(struct overlay_context *overlay)
{
  /*
   * Keep one private game image at a fixed size.
   *
   * During an active output frame, this framebuffer receives Quake's final image.
   * At the end of the frame, overlay_context_end_frame() copies the same image into every stream buffer
   * that was acquired for that game frame.
   */

  glGenTextures(1, &overlay->render_texture);
  glBindTexture(GL_TEXTURE_2D, overlay->render_texture);

  /*
   * Allocate an RGBA image matching the resolution selected for Quake.
   *
   * No initial pixel data is supplied because Quake will completely render the contents itself.
   */
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA8,
               (GLsizei)overlay->width,
               (GLsizei)overlay->height,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               NULL);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glGenFramebuffers(1, &overlay->render_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, overlay->render_texture, 0);

  /*
   * Quake's renderer expects depth and stencil attachments on its final render target.
   */
  glGenRenderbuffers(1, &overlay->render_depth_stencil);
  glBindRenderbuffer(GL_RENDERBUFFER, overlay->render_depth_stencil);

  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)overlay->width, (GLsizei)overlay->height);

  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, overlay->render_depth_stencil);

  GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  glBindRenderbuffer(GL_RENDERBUFFER, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
    syslog(LOG_ERR, "Render framebuffer incomplete: 0x%x", framebuffer_status);
    destroy_render_target(overlay);
    return false;
  }

  return true;
}

/*
 * Delete the private Quake framebuffer and its color, depth and stencil storage, then clear their handles.
 */
static void
destroy_render_target(struct overlay_context *overlay)
{
  if (overlay->render_depth_stencil) {
    glDeleteRenderbuffers(1, &overlay->render_depth_stencil);
    overlay->render_depth_stencil = 0;
  }

  if (overlay->render_framebuffer) {
    glDeleteFramebuffers(1, &overlay->render_framebuffer);
    overlay->render_framebuffer = 0;
  }

  if (overlay->render_texture) {
    glDeleteTextures(1, &overlay->render_texture);
    overlay->render_texture = 0;
  }
}

/*
 * Find the cached OpenGL surface for one buffer ID on the current concrete overlay.
 *
 * The complete cache is destroyed whenever overlay_epoch changes, so a reused buffer ID from a later concrete overlay
 * cannot be confused with a buffer imported for an older overlay instance.
 */
static struct render_surface *
find_render_surface(struct overlay_stream *stream, unsigned long buffer_id)
{
  struct render_surface *surface = stream->surfaces;

  while (surface) {
    if (surface->buffer_id == buffer_id) {
      return surface;
    }

    surface = surface->next;
  }

  return NULL;
}

/*
 * Import the DMA-BUF metadata captured by the serialized SDK worker.
 *
 * No axoverlay2 function is called here. EGL and OpenGL remain owned by the Yamagi render thread, while the worker
 * is the only thread allowed to touch axoverlay2 runtime state.
 */
static struct render_surface *
create_render_surface(struct overlay_stream *stream, const struct gpu_context *gpu)
{
  /*
   * These entry points are EGL and OpenGL ES extensions, so their addresses come from the graphics driver.
   *
   * eglCreateImageKHR imports the shared DMA-BUF into EGL.
   * glEGLImageTargetTexture2DOES attaches that EGL image to an OpenGL texture.
   */
  PFNEGLCREATEIMAGEKHRPROC create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (!create_image || !image_target_texture) {
    syslog(LOG_ERR, "Required EGL image extensions are unavailable");
    return NULL;
  }

  /*
   * The SDK worker queried the DMA-BUF descriptor while it exclusively owned axoverlay2 state.
   * DMA-BUF lets axoverlay2 and the GPU refer to the same image without copying the completed frame through the CPU.
   */
  if (stream->current_dma_buf_fd < 0) {
    syslog(LOG_ERR, "Overlay buffer has no DMA-BUF");
    return NULL;
  }

  /*
   * Describe the shared image to EGL.
   *
   * full_width and full_height are the allocated buffer dimensions, not just the visible overlay dimensions.
   * FourCC describes the pixel format. The DRM modifier describes the GPU memory layout and is split into
   * low and high 32-bit values because that is how the EGL DMA-BUF extension receives it.
   */
  EGLint attributes[] = {
    EGL_WIDTH,
    (EGLint)stream->current_full_width,

    EGL_HEIGHT,
    (EGLint)stream->current_full_height,

    EGL_LINUX_DRM_FOURCC_EXT,
    (EGLint)stream->current_drm_fourcc,

    EGL_DMA_BUF_PLANE0_FD_EXT,
    stream->current_dma_buf_fd,

    EGL_DMA_BUF_PLANE0_OFFSET_EXT,
    0,

    /*
     * The first image plane starts at the beginning of this DMA-BUF.
     * ARGB32 uses four bytes for each pixel in the logical image row.
     */
    EGL_DMA_BUF_PLANE0_PITCH_EXT,
    (EGLint)(stream->current_full_width * 4),

    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
    (EGLint)(stream->current_drm_modifier & 0xffffffff),

    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
    (EGLint)(stream->current_drm_modifier >> 32),

    EGL_NONE,
  };

  /*
   * Zero-initialize OpenGL handles so destroy_render_surface() is safe after any partial setup failure.
   * EGL has its own invalid image sentinel, so set that field explicitly.
   */
  struct render_surface *surface = calloc(1, sizeof(*surface));
  if (!surface) {
    syslog(LOG_ERR, "Failed to allocate render surface");
    return NULL;
  }

  surface->buffer_id = stream->current_buffer_id;
  surface->display = gpu->display;
  surface->image = EGL_NO_IMAGE_KHR;

  /*
   * EGL_LINUX_DMA_BUF_EXT tells EGL that the image is backed by the DMA-BUF described above.
   */
  surface->image = create_image(gpu->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attributes);

  if (surface->image == EGL_NO_IMAGE_KHR) {
    syslog(LOG_ERR, "Failed to import overlay DMA-BUF as EGLImage: 0x%x", eglGetError());
    destroy_render_surface(surface);
    return NULL;
  }

  glGenTextures(1, &surface->texture);
  glBindTexture(GL_TEXTURE_2D, surface->texture);

  /*
   * Attach the imported EGL image to an OpenGL texture.
   * Writes through this texture then modify the image owned by the axoverlay2 buffer.
   */
  image_target_texture(GL_TEXTURE_2D, surface->image);

  /*
   * Texture attachment can fail even when the EGL import succeeded, so check the OpenGL error separately.
   */
  GLenum gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    syslog(LOG_ERR, "Failed to bind EGLImage as texture: 0x%x", gl_error);
    glBindTexture(GL_TEXTURE_2D, 0);
    destroy_render_surface(surface);
    return NULL;
  }

  /*
   * Attach the imported texture to a framebuffer so the final GPU blit and alpha clear can write directly
   * into the axoverlay2-owned image.
   */
  glGenFramebuffers(1, &surface->framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, surface->framebuffer);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface->texture, 0);

  glGenRenderbuffers(1, &surface->depth_stencil);
  glBindRenderbuffer(GL_RENDERBUFFER, surface->depth_stencil);

  glRenderbufferStorage(GL_RENDERBUFFER,
                        GL_DEPTH24_STENCIL8,
                        (GLsizei)stream->current_full_width,
                        (GLsizei)stream->current_full_height);

  glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                            GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER,
                            surface->depth_stencil);

  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
    syslog(LOG_ERR, "Overlay framebuffer incomplete: 0x%x", framebuffer_status);
    destroy_render_surface(surface);
    return NULL;
  }

  return surface;
}

/*
 * Release the EGL and OpenGL objects for one imported buffer and free its render_surface structure.
 */
static void
destroy_render_surface(struct render_surface *surface)
{
  /*
   * This helper can safely be called after partial setup or with no surface.
   */
  if (!surface) {
    return;
  }

  if (surface->depth_stencil) {
    glDeleteRenderbuffers(1, &surface->depth_stencil);
  }

  if (surface->framebuffer) {
    glDeleteFramebuffers(1, &surface->framebuffer);
  }

  if (surface->texture) {
    glDeleteTextures(1, &surface->texture);
  }

  /*
   * The EGL image is a separate object from the OpenGL texture and must also be released.
   */
  if (surface->image != EGL_NO_IMAGE_KHR) {
    /*
     * eglDestroyImageKHR is an EGL extension, so obtain its address from the graphics driver at runtime.
     */
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    if (destroy_image) {
      destroy_image(surface->display, surface->image);
    }
  }
  free(surface);
}

/*
 * Release every cached render surface for one stream overlay.
 */
static void
destroy_render_surfaces(struct overlay_stream *stream)
{
  struct render_surface *surface = stream->surfaces;

  while (surface) {
    /*
     * Save the next pointer before freeing the current entry because the current structure then becomes invalid.
     */
    struct render_surface *next = surface->next;
    destroy_render_surface(surface);
    surface = next;
  }

  stream->surfaces = NULL;
}

/*
 * Return true while the render thread owns at least one valid output buffer for the current game frame.
 */
static bool
has_active_output(const struct overlay_context *overlay)
{
  for (const struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (stream->current_surface) {
      return true;
    }
  }

  return false;
}
