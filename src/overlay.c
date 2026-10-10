/*
 * Connects Quake's finished frames to active video streams through axoverlay2.
 *
 * Quake renders one final image into a stable OpenGL framebuffer owned by the application.
 * Each active VDO stream owns a concrete axoverlay2 overlay with its own rotating output buffers.
 *
 * At most one stream buffer is acquired per Quake frame. When one is available, this file copies the completed
 * shared Quake image into that buffer and submits it to axoverlay2.
 *
 * Each axoverlay2 buffer is imported into OpenGL the first time it is seen and cached per stream.
 * The GPU can then write to it directly without reading the completed frame back through the CPU.
 */
#include "overlay.h"

#include "gpu_context.h"

#include <stdint.h>
#include <stdlib.h>
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

#define OVERLAY_CREATE_RETRY_US 1000000

/*
 * State owned by one VDO stream.
 *
 * Quake itself still renders only once into overlay_context.render_framebuffer.
 * Each stream record owns only the concrete Axoverlay2 overlay and the imported buffers used to present that image.
 */
struct overlay_stream {
  unsigned stream_id;
  unsigned stream_width;
  unsigned stream_height;

  int overlay_id;
  axo_detailed_format *format;
  struct render_surface *surfaces;

  unsigned width;
  unsigned height;
  unsigned full_width;
  unsigned full_height;

  unsigned frame_count;
  int64_t retry_at_us;

  struct overlay_stream *next;
};

static struct overlay_stream *find_overlay_stream(struct overlay_context *overlay, unsigned stream_id);
static bool register_overlay_stream(struct overlay_context *overlay,
                                    unsigned stream_id,
                                    unsigned stream_width,
                                    unsigned stream_height);
static void remove_overlay_stream(struct overlay_context *overlay, unsigned stream_id);
static void remove_all_overlay_streams(struct overlay_context *overlay);

static bool create_overlay(struct overlay_context *overlay, struct overlay_stream *stream);
static void destroy_stream_overlay(struct overlay_stream *stream);
static bool retry_pending_overlays(struct overlay_context *overlay);
static struct overlay_stream *select_output_stream(struct overlay_context *overlay);

static bool create_render_target(struct overlay_context *overlay);
static void destroy_render_target(struct overlay_context *overlay);

static struct render_surface *find_render_surface(struct overlay_stream *stream, unsigned long buffer_id);

static struct render_surface *
create_render_surface(struct overlay_stream *stream, const struct gpu_context *gpu, axo_buffer *buffer);

static void destroy_render_surface(struct render_surface *surface);
static void destroy_render_surfaces(struct overlay_stream *stream);

/*
 * Start axoverlay2 and subscribe to VDO events used to discover video streams.
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
  overlay->current_stream = NULL;
  overlay->current_surface = NULL;
  overlay->current_buffer = NULL;
  overlay->event_fd = -1;
  overlay->width = 0;
  overlay->height = 0;
  overlay->render_texture = 0;
  overlay->render_framebuffer = 0;
  overlay->render_depth_stencil = 0;
  overlay->next_stream_index = 0;
  overlay->frame_count = 0;
  overlay->axo_started = false;

  if (!axo_start(NULL, &axo_error)) {
    syslog(LOG_ERR, "Failed to start axoverlay2: %s", axo_err_get_message(axo_error));
    axo_err_clear(&axo_error);
    return false;
  }

  /*
   * Remember that axoverlay2 was started so shutdown knows that axo_stop() must be called.
   */
  overlay->axo_started = true;

  /*
   * Open VDO stream 0 as the stream used for receiving video stream events.
   * This does not mean Quake always renders into stream 0.
   * It is used to hear about the actual video streams that appear on the device.
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

  /*
   * VDO exposes its events through a file descriptor.
   * The current renderer does not wait on this descriptor.
   * It checks for pending VDO events at the beginning of each frame.
   */
  overlay->event_fd = vdo_stream_get_event_fd(event_stream, &error);
  if (overlay->event_fd < 0) {
    syslog(LOG_ERR, "Failed to get VDO event fd: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
    goto fail;
  }

  g_object_unref(filter);

  syslog(LOG_INFO, "axoverlay2 initialized");
  syslog(LOG_INFO, "VDO event fd: %d", overlay->event_fd);

  return true;

fail:
  /*
   * The filter may still belong to us if initialization failed before it was released above.
   */
  if (filter) {
    g_object_unref(filter);
  }

  /*
   * Use the normal shutdown path to release anything that was successfully created before the failure.
   */
  overlay_context_destroy(overlay);

  return false;
}

/*
 * Release the overlay resources and VDO event stream, then stop axoverlay2.
 */
void
overlay_context_destroy(struct overlay_context *overlay)
{
  overlay->current_stream = NULL;
  overlay->current_surface = NULL;
  overlay->current_buffer = NULL;

  remove_all_overlay_streams(overlay);
  destroy_render_target(overlay);

  overlay->width = 0;
  overlay->height = 0;
  overlay->next_stream_index = 0;

  if (overlay->event_stream) {
    g_object_unref(overlay->event_stream);
    overlay->event_stream = NULL;
  }

  overlay->event_fd = -1;

  if (overlay->axo_started) {
    axo_stop(NULL);
    overlay->axo_started = false;
  }
}

/*
 * Return the VDO event descriptor, or -1 when no descriptor is available.
 * The current Yamagi integration does not use this accessor.
 */
int
overlay_context_get_event_fd(const struct overlay_context *overlay)
{
  return overlay->event_fd;
}

/*
 * Process pending VDO events to create an overlay for an available stream or remove it when that stream closes.
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
        /*
         * A stream can disappear after its event was queued but before we look it up.
         * Treat that as a local stream race rather than a renderer failure.
         */
        if (error && g_error_matches(error, VDO_ERROR, VDO_ERROR_NOT_FOUND)) {
          g_clear_error(&error);
          g_object_unref(event);
          continue;
        }

        syslog(LOG_ERR, "Failed to get VDO stream %u: %s", stream_id, error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(event);
        continue;
      }

      VdoMap *info = vdo_stream_get_info(stream, NULL);
      if (!info) {
        syslog(LOG_ERR, "Failed to get VDO stream %u information", stream_id);
        g_object_unref(stream);
        g_object_unref(event);
        continue;
      }

      unsigned width = vdo_map_get_uint32(info, "width", 0);
      unsigned height = vdo_map_get_uint32(info, "height", 0);

      g_object_unref(info);
      g_object_unref(stream);
      g_object_unref(event);

      if (!width || !height) {
        syslog(LOG_ERR, "VDO stream %u has invalid size %ux%u", stream_id, width, height);
        continue;
      }

      if (!register_overlay_stream(overlay, stream_id, width, height)) {
        return false;
      }

      continue;
    }

    if (event_type == VDO_STREAM_EVENT_CLOSED) {
      syslog(LOG_INFO, "VDO stream %u closed", stream_id);
      remove_overlay_stream(overlay, stream_id);
    }

    g_object_unref(event);
  }
}

/*
 * Process stream changes, make one round-robin output acquisition attempt and bind the shared Quake framebuffer.
 * Returns true even when no output buffer is available for this frame.
 */
bool
overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu)
{
  if (!overlay_context_process_events(overlay)) {
    return false;
  }

  if (!retry_pending_overlays(overlay)) {
    return false;
  }

  /*
   * begin_frame and end_frame must remain paired.
   * Only one stream buffer can be owned by the application during a Quake frame.
   */
  if (overlay->current_buffer || overlay->current_stream || overlay->current_surface) {
    syslog(LOG_ERR, "Overlay frame already active");
    return false;
  }

  /*
   * Quake keeps rendering into one stable framebuffer whenever at least one stream has successfully established
   * the render target. Output acquisition is independent of that render target.
   */
  if (!overlay->render_framebuffer) {
    return true;
  }

  struct overlay_stream *stream = select_output_stream(overlay);
  if (stream) {
    axo_err *error = NULL;

    /*
     * Ask axoverlay2 for the next free buffer of the selected concrete overlay.
     *
     * axoverlay2 cycles between several buffers, so the returned buffer can change from frame to frame.
     * Make at most one potentially blocking acquisition call per Quake frame.
     *
     * With multiple streams the selected stream rotates round-robin. This keeps axoverlay2 serialized exactly as
     * on main while preventing the per-frame blocking cost from multiplying with the number of active streams.
     */
    axo_buffer *buffer = axo_get_buffer(stream->overlay_id, NULL, &error);

    if (!buffer) {
      if (error && axo_err_get_code(error) == AXO_ERR_WAIT) {
        /*
         * WAIT is normal pacing and means no buffer is ready for this stream yet.
         * Quake still renders its shared frame; this stream simply receives no new overlay image on this turn.
         */
        axo_err_clear(&error);
      } else if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
        /*
         * Axis documents NO_STREAM as a normal disconnect race.
         * Keep the record intact and let the corresponding VDO CLOSED event own removal.
         */
        axo_err_clear(&error);
      } else {
        syslog(LOG_ERR,
               "Failed to get overlay buffer for stream %u: %s",
               stream->stream_id,
               error ? axo_err_get_message(error) : "unknown error");
        axo_err_clear(&error);
        return false;
      }
    } else {
      unsigned long buffer_id = axo_buffer_get_id(buffer);

      /*
       * Reuse the EGL and OpenGL objects if this axoverlay2 buffer was already imported for this stream.
       */
      struct render_surface *surface = find_render_surface(stream, buffer_id);

      if (!surface) {
        /*
         * This is the first time this stream returned this buffer.
         * Import its DMA-BUF into EGL so the GPU can write into it directly.
         */
        surface = create_render_surface(stream, gpu, buffer);
        if (!surface) {
          /*
           * Do not submit an acquired buffer which was never rendered.
           * Removing the concrete overlay invalidates that buffer before a later retry creates fresh buffers.
           */
          syslog(LOG_ERR,
                 "Failed to import overlay buffer %lu for stream %u; recreating the overlay",
                 buffer_id,
                 stream->stream_id);
          destroy_stream_overlay(stream);
          stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
        } else {
          surface->next = stream->surfaces;
          stream->surfaces = surface;
          syslog(LOG_INFO, "Imported overlay buffer %lu for stream %u", buffer_id, stream->stream_id);
        }
      }

      if (surface) {
        /*
         * Remember exactly which stream, axoverlay2 buffer and imported surface belong to this Quake frame.
         * overlay_context_end_frame() must submit this same buffer back to the same concrete overlay.
         */
        overlay->current_stream = stream;
        overlay->current_buffer = buffer;
        overlay->current_surface = surface;
      }
    }
  }

  glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);
  glViewport(0, 0, (GLsizei)overlay->width, (GLsizei)overlay->height);

  return true;
}

/*
 * Bind the shared Quake framebuffer whenever at least one stream has established the render target.
 */
void
overlay_context_bind_frame(struct overlay_context *overlay)
{
  /*
   * Yamagi sometimes needs to return to its final output framebuffer after using a temporary framebuffer.
   *
   * On a desktop this would normally be framebuffer 0. In this port the final game output is the shared private
   * Quake framebuffer, independent of whether this particular frame acquired an axoverlay2 buffer.
   */
  if (overlay->render_framebuffer) {
    glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
}

/*
 * Copy the completed Quake image into the output buffer and make it opaque.
 * Wait for GPU writes to finish, then submit the buffer to axoverlay2.
 */
bool
overlay_context_end_frame(struct overlay_context *overlay)
{
  struct overlay_stream *stream = overlay->current_stream;
  axo_buffer *buffer = overlay->current_buffer;
  struct render_surface *surface = overlay->current_surface;

  /*
   * Quake may have rendered a valid latest frame even when no stream had a buffer ready.
   * In that case there is simply nothing to present through Axoverlay2 this frame.
   */
  if (!stream || !buffer || !surface) {
    overlay->current_stream = NULL;
    overlay->current_buffer = NULL;
    overlay->current_surface = NULL;
    return true;
  }

  axo_err *error = NULL;

  /*
   * The completed Quake frame is already on the GPU.
   *
   * Use the shared Quake framebuffer as the source and the imported axoverlay2-backed framebuffer as the destination.
   * This keeps the image on the GPU instead of reading all pixels back through the CPU.
   */
  glBindFramebuffer(GL_READ_FRAMEBUFFER, overlay->render_framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, surface->framebuffer);

  /*
   * Copy the completed game image into the selected stream's axoverlay2 buffer.
   *
   * The shared Quake render target is sized by the first successful stream, so the destination may use a different
   * overlay size. glBlitFramebuffer() performs that final scaling on the GPU.
   *
   * Reverse the destination Y coordinates to flip the completed game image vertically for overlay output.
   * GL_NEAREST keeps the same filtering behavior as main and avoids adding interpolation work to this final copy.
   */
  glBlitFramebuffer(0,
                    0,
                    (GLint)overlay->width,
                    (GLint)overlay->height,
                    0,
                    (GLint)stream->height,
                    (GLint)stream->width,
                    0,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);

  /*
   * The completed Quake image can contain non-opaque alpha values.
   * In axoverlay2 output, alpha controls how much of the underlying camera video shows through.
   *
   * Write only the alpha channel and clear it to 1.0.
   * The color channels remain unchanged while the completed overlay becomes fully opaque.
   */
  const GLfloat opaque[] = { 0.0f, 0.0f, 0.0f, 1.0f };

  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
  glClearBufferfv(GL_COLOR, 0, opaque);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  /*
   * Manual DMA synchronization is enabled on every concrete overlay.
   *
   * Wait until the GPU has completely finished writing this axoverlay2 buffer before returning ownership to the SDK.
   * Without this synchronization, the video pipeline could use the image before the GPU finished writing it.
   */
  glFinish();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  overlay->current_stream = NULL;
  overlay->current_buffer = NULL;
  overlay->current_surface = NULL;

  if (!axo_submit_buffer(buffer, NULL, &error)) {
    /*
     * The stream can disappear between acquisition and submission.
     * NO_STREAM is therefore handled as a normal stream lifecycle race rather than a fatal application error.
     */
    if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
      axo_err_clear(&error);
    } else {
      syslog(LOG_ERR,
             "Failed to submit overlay buffer for stream %u: %s; recreating the overlay",
             stream->stream_id,
             error ? axo_err_get_message(error) : "unknown error");
      axo_err_clear(&error);
    }

    /*
     * The ownership state of a failed submission is not useful for future rendering.
     * Recreate only this concrete overlay; other streams continue unaffected.
     */
    destroy_stream_overlay(stream);
    stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
    return true;
  }

  overlay->frame_count++;
  stream->frame_count++;

  if (stream->frame_count % 30 == 0) {
    syslog(LOG_INFO,
           "Rendered %u frames on overlay %d for stream %u",
           stream->frame_count,
           stream->overlay_id,
           stream->stream_id);
  }

  return true;
}

/*
 * Test helper: draw a changing red and green image through the normal overlay output path.
 * The Yamagi renderer does not call this function.
 */
bool
overlay_context_render_frame(struct overlay_context *overlay, const struct gpu_context *gpu)
{
  /*
   * This helper renders a simple changing color instead of a Quake frame.
   * It is useful for testing the axoverlay2 path without depending on the game renderer.
   */
  if (!overlay_context_begin_frame(overlay, gpu)) {
    return false;
  }

  if (!overlay->render_framebuffer) {
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
 * Find the record for one VDO stream.
 */
static struct overlay_stream *
find_overlay_stream(struct overlay_context *overlay, unsigned stream_id)
{
  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (stream->stream_id == stream_id) {
      return stream;
    }
  }

  return NULL;
}

/*
 * Create the concrete Axoverlay2 overlay for one VDO stream.
 *
 * SDK creation failures are isolated to this stream and retried later. A failure to create Quake's shared OpenGL
 * render target is different: without that target the renderer cannot produce its final image, so that remains fatal.
 */
static bool
create_overlay(struct overlay_context *overlay, struct overlay_stream *stream)
{
  if (stream->overlay_id >= 0) {
    return true;
  }

  /*
   * When both stream dimensions divide cleanly by two, render this overlay at half width and half height.
   *
   * axoverlay2 scales the overlay back to the full video size. Halving both dimensions reduces the destination
   * pixel count for this stream to one quarter of the full stream resolution.
   */
  bool use_upscale = stream->stream_width % 2 == 0 && stream->stream_height % 2 == 0;

  unsigned used_width = use_upscale ? stream->stream_width / 2 : stream->stream_width;
  unsigned used_height = use_upscale ? stream->stream_height / 2 : stream->stream_height;

  axo_err *error = NULL;

  /*
   * Ask axoverlay2 for an ARGB32 format suitable for direct GPU rendering.
   *
   * ARGB32 provides four 8-bit components including alpha.
   * The GPU flag requests a layout compatible with GPU access.
   * The compressed flag requests a compressed layout when the platform supports one.
   */
  axo_detailed_format *format =
      axo_suggest_detailed_format(AXO_FORMAT_ARGB32, AXO_FORMAT_FLAGS_COMPRESSED | AXO_FORMAT_FLAGS_GPU, &error);
  if (!format) {
    syslog(LOG_ERR,
           "Failed to get GPU overlay format for stream %u: %s; retrying later",
           stream->stream_id,
           error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
    return true;
  }

  /*
   * Hardware format requirements can make the allocated buffer larger than the visible overlay image.
   * Ask axoverlay2 for the required size, then keep the existing 16-pixel allocation alignment used by this port.
   */
  unsigned full_width;
  unsigned full_height;

  axo_detailed_format_get_aligned_size(format, used_width, used_height, &full_width, &full_height);

  full_width = (full_width + 15) & ~15u;
  full_height = (full_height + 15) & ~15u;

  /*
   * props describes the concrete overlay buffers and scaling behavior.
   * match binds this concrete overlay to exactly one VDO stream.
   */
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
    syslog(LOG_ERR, "Failed to allocate overlay creation objects for stream %u; retrying later", stream->stream_id);
    stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
    return true;
  }

  axo_props_set_detailed_format(props, format);
  axo_props_set_size(props, full_width, full_height);

  /*
   * When half-size rendering is enabled, tell axoverlay2 to scale the overlay by two before compositing it.
   */
  axo_props_set_upscale_x2(props, use_upscale);

  /*
   * Let the application handle synchronization before submission.
   * overlay_context_end_frame() uses glFinish() before returning an acquired buffer to axoverlay2.
   */
  axo_props_set_manual_dma_sync(props, true);

  axo_match_stream_id(match, stream->stream_id);

  int overlay_id = axo_create_overlay(props, match, &error);

  axo_props_free(props);
  axo_match_free(match);

  if (overlay_id < 0) {
    if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
      /*
       * The stream disappeared between discovery and overlay creation.
       * VDO will provide the CLOSED event which removes this record, so do not create a retry loop for it.
       */
      syslog(LOG_INFO, "VDO stream %u disappeared while creating its overlay", stream->stream_id);
      stream->retry_at_us = 0;
    } else {
      syslog(LOG_ERR,
             "Failed to create overlay for stream %u: %s; retrying later",
             stream->stream_id,
             error ? axo_err_get_message(error) : "unknown error");
      stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
    }

    axo_err_clear(&error);
    axo_detailed_format_free(format);
    return true;
  }

  stream->overlay_id = overlay_id;
  stream->format = format;

  /*
   * width and height describe the visible overlay image used for the final GPU copy.
   * full_width and full_height describe the actual axoverlay2 buffer allocation after format alignment.
   */
  stream->width = used_width;
  stream->height = used_height;
  stream->full_width = full_width;
  stream->full_height = full_height;
  stream->frame_count = 0;
  stream->retry_at_us = 0;

  /*
   * The first concrete overlay selects Quake's shared render size.
   * Keep that target stable while any VDO stream record remains, even if one overlay is temporarily recreated.
   */
  if (!overlay->render_framebuffer) {
    overlay->width = used_width;
    overlay->height = used_height;

    if (!create_render_target(overlay)) {
      destroy_stream_overlay(stream);
      overlay->width = 0;
      overlay->height = 0;
      return false;
    }
  }

  syslog(LOG_INFO,
         "Created GPU overlay %d on stream %u: stream %ux%u, overlay %ux%u, buffer %ux%u%s",
         overlay_id,
         stream->stream_id,
         stream->stream_width,
         stream->stream_height,
         used_width,
         used_height,
         full_width,
         full_height,
         use_upscale ? ", 2x upscale" : "");

  return true;
}

/*
 * Register a VDO stream or refresh the dimensions of an existing record.
 *
 * Because all Axoverlay2 calls are synchronous on the render thread, there is no older worker result which can race
 * this update. A dimension change can therefore recreate the concrete overlay directly without generation counters.
 */
static bool
register_overlay_stream(struct overlay_context *overlay,
                        unsigned stream_id,
                        unsigned stream_width,
                        unsigned stream_height)
{
  struct overlay_stream *stream = find_overlay_stream(overlay, stream_id);

  if (!stream) {
    stream = calloc(1, sizeof(*stream));
    if (!stream) {
      syslog(LOG_ERR, "Failed to allocate overlay stream record");
      return false;
    }

    stream->stream_id = stream_id;
    stream->overlay_id = -1;
    stream->next = overlay->streams;
    overlay->streams = stream;
  }

  bool dimensions_changed = stream->stream_width != stream_width || stream->stream_height != stream_height;

  stream->stream_width = stream_width;
  stream->stream_height = stream_height;

  if (dimensions_changed && stream->overlay_id >= 0) {
    syslog(LOG_INFO,
           "VDO stream %u changed size to %ux%u; recreating its overlay",
           stream_id,
           stream_width,
           stream_height);
    destroy_stream_overlay(stream);
  }

  stream->retry_at_us = 0;

  syslog(LOG_INFO, "VDO stream %u available: %ux%u", stream_id, stream_width, stream_height);

  return create_overlay(overlay, stream);
}

/*
 * Retry stream-local creation failures at a low rate.
 *
 * This runs on the render thread and makes no background progress of its own, so it cannot create an independent
 * polling loop or consume CPU while the game is idle between frames.
 */
static bool
retry_pending_overlays(struct overlay_context *overlay)
{
  int64_t now = g_get_monotonic_time();

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (stream->overlay_id < 0 && stream->retry_at_us > 0 && stream->retry_at_us <= now) {
      if (!create_overlay(overlay, stream)) {
        return false;
      }
    }
  }

  return true;
}

/*
 * Select one concrete overlay for this Quake frame.
 *
 * At most one Axoverlay2 buffer is acquired per game frame. With N active streams each stream therefore receives
 * roughly one out of every N presentation opportunities, while Quake itself still renders every game frame.
 */
static struct overlay_stream *
select_output_stream(struct overlay_context *overlay)
{
  unsigned active_count = 0;

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (stream->overlay_id >= 0) {
      active_count++;
    }
  }

  if (!active_count) {
    return NULL;
  }

  unsigned target = overlay->next_stream_index % active_count;
  overlay->next_stream_index = (target + 1) % active_count;

  unsigned current = 0;

  for (struct overlay_stream *stream = overlay->streams; stream; stream = stream->next) {
    if (stream->overlay_id < 0) {
      continue;
    }

    if (current == target) {
      return stream;
    }

    current++;
  }

  return NULL;
}

/*
 * Release the concrete overlay and graphics resources for one stream while keeping its VDO record available for retry.
 */
static void
destroy_stream_overlay(struct overlay_stream *stream)
{
  axo_err *error = NULL;

  /*
   * Destroy EGL and OpenGL objects backed by this overlay's buffers before removing the concrete overlay itself.
   */
  destroy_render_surfaces(stream);

  if (stream->overlay_id >= 0) {
    if (!axo_remove_overlay(stream->overlay_id, &error)) {
      /*
       * NO_STREAM is expected when the VDO stream disappeared before its close event was processed.
       */
      if (!error || axo_err_get_code(error) != AXO_ERR_NO_STREAM) {
        syslog(LOG_ERR,
               "Failed to remove overlay %d from stream %u: %s",
               stream->overlay_id,
               stream->stream_id,
               error ? axo_err_get_message(error) : "unknown error");
      }
    }

    axo_err_clear(&error);
  }

  if (stream->format) {
    axo_detailed_format_free(stream->format);
  }

  stream->overlay_id = -1;
  stream->format = NULL;
  stream->width = 0;
  stream->height = 0;
  stream->full_width = 0;
  stream->full_height = 0;
  stream->frame_count = 0;
}

/*
 * Remove one VDO stream record and only the concrete overlay which belongs to it.
 */
static void
remove_overlay_stream(struct overlay_context *overlay, unsigned stream_id)
{
  struct overlay_stream **link = &overlay->streams;

  while (*link && (*link)->stream_id != stream_id) {
    link = &(*link)->next;
  }

  if (!*link) {
    return;
  }

  struct overlay_stream *stream = *link;

  if (overlay->current_stream == stream) {
    overlay->current_stream = NULL;
    overlay->current_buffer = NULL;
    overlay->current_surface = NULL;
  }

  *link = stream->next;

  destroy_stream_overlay(stream);
  free(stream);

  if (!overlay->streams) {
    /*
     * No VDO stream remains, so a future stream may choose a new shared Quake render size.
     */
    destroy_render_target(overlay);
    overlay->width = 0;
    overlay->height = 0;
    overlay->next_stream_index = 0;
  }
}

/*
 * Release all stream records during renderer shutdown.
 */
static void
remove_all_overlay_streams(struct overlay_context *overlay)
{
  while (overlay->streams) {
    remove_overlay_stream(overlay, overlay->streams->stream_id);
  }
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
   * This framebuffer receives Quake's final image independently of Axoverlay2 buffer availability.
   * When a stream buffer was acquired for the frame, overlay_context_end_frame() copies the image into it.
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
 * Find the cached OpenGL surface for an axoverlay2 buffer ID, or return NULL if it has not been imported.
 */
static struct render_surface *
find_render_surface(struct overlay_stream *stream, unsigned long buffer_id)
{
  struct render_surface *surface = stream->surfaces;

  while (surface) {
    /*
     * Buffer IDs remain stable, so an equal ID means this is the same axoverlay2 buffer we prepared earlier.
     */
    if (surface->buffer_id == buffer_id) {
      return surface;
    }
    surface = surface->next;
  }

  return NULL;
}

/*
 * Import an axoverlay2 buffer into EGL and create the OpenGL objects needed to use it as an output framebuffer.
 */
static struct render_surface *
create_render_surface(struct overlay_stream *stream, const struct gpu_context *gpu, axo_buffer *buffer)
{
  /*
   * These are EGL and OpenGL ES extension functions, so their addresses are requested from the graphics driver.
   *
   * eglCreateImageKHR imports the DMA-BUF backing the axoverlay2 buffer into EGL.
   * glEGLImageTargetTexture2DOES connects that EGL image to an OpenGL texture.
   */
  PFNEGLCREATEIMAGEKHRPROC create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (!create_image || !image_target_texture) {
    syslog(LOG_ERR, "Required EGL image extensions are unavailable");
    return NULL;
  }

  /*
   * Get the DMA-BUF file descriptor for the axoverlay2 buffer.
   *
   * DMA-BUF allows axoverlay2 and the GPU to access the same image buffer.
   * This avoids reading the completed image back to the CPU and copying it into another buffer.
   */
  int dma_buf_fd = axo_buffer_get_dma_buf_fd(buffer);

  if (dma_buf_fd < 0) {
    syslog(LOG_ERR, "Overlay buffer has no DMA-BUF");
    return NULL;
  }

  axo_detailed_format *format = stream->format;

  /*
   * DRM FourCC describes the shared image's pixel format so EGL knows how to interpret the color data.
   */
  uint32_t fourcc = axo_detailed_format_get_drm_fourcc(format);

  /*
   * The DRM modifier describes the memory layout selected for this GPU buffer.
   * The value is 64 bits, while EGL receives it as separate low and high 32-bit values below.
   */
  uint64_t modifier = axo_detailed_format_get_drm_modifier(format, 0);

  /*
   * Describe the DMA-BUF backing the axoverlay2 buffer to EGL.
   *
   * EGL needs the allocated dimensions, pixel format and DMA-BUF handle before it can import the image.
   * It also needs the first-plane offset, row pitch and GPU memory layout.
   */
  EGLint attributes[] = {
    EGL_WIDTH,
    (EGLint)stream->full_width,

    EGL_HEIGHT,
    (EGLint)stream->full_height,

    EGL_LINUX_DRM_FOURCC_EXT,
    (EGLint)fourcc,

    EGL_DMA_BUF_PLANE0_FD_EXT,
    dma_buf_fd,

    /*
     * The first image plane starts at the beginning of this DMA buffer.
     */
    EGL_DMA_BUF_PLANE0_OFFSET_EXT,
    0,

    /*
     * ARGB32 uses four bytes for each pixel in the logical image row.
     */
    EGL_DMA_BUF_PLANE0_PITCH_EXT,
    (EGLint)(stream->full_width * 4),

    /*
     * Split the 64-bit DRM modifier into the two 32-bit values expected by EGL.
     */
    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
    (EGLint)(modifier & 0xffffffff),

    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
    (EGLint)(modifier >> 32),

    EGL_NONE,
  };

  /*
   * Zero-initialize the OpenGL handles so cleanup can safely handle partial initialization.
   */
  struct render_surface *surface = calloc(1, sizeof(*surface));
  if (!surface) {
    syslog(LOG_ERR, "Failed to allocate render surface");
    return NULL;
  }

  surface->buffer_id = axo_buffer_get_id(buffer);
  surface->display = gpu->display;

  /*
   * EGL uses its own invalid image value, so set it explicitly instead of relying on the zero-filled allocation.
   */
  surface->image = EGL_NO_IMAGE_KHR;

  /*
   * Import the DMA-BUF backing the axoverlay2 buffer as an EGL image.
   *
   * EGL_LINUX_DMA_BUF_EXT tells EGL that the image is backed by the Linux DMA-BUF described above.
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
   * Connect the imported EGL image to the texture.
   *
   * After this call, writes through the OpenGL texture affect the image owned by the axoverlay2 buffer.
   */
  image_target_texture(GL_TEXTURE_2D, surface->image);

  /*
   * Binding the EGL image to an OpenGL texture can fail even when the EGL import succeeded.
   * Check the OpenGL error separately.
   */
  GLenum gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    syslog(LOG_ERR, "Failed to bind EGLImage as texture: 0x%x", gl_error);
    glBindTexture(GL_TEXTURE_2D, 0);
    destroy_render_surface(surface);
    return NULL;
  }

  /*
   * Attaching the imported texture to a framebuffer lets OpenGL write directly into the axoverlay2 buffer.
   */
  glGenFramebuffers(1, &surface->framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, surface->framebuffer);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface->texture, 0);

  glGenRenderbuffers(1, &surface->depth_stencil);
  glBindRenderbuffer(GL_RENDERBUFFER, surface->depth_stencil);

  glRenderbufferStorage(
      GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)stream->full_width, (GLsizei)stream->full_height);

  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, surface->depth_stencil);

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
 * Release every cached render surface and clear the list.
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
