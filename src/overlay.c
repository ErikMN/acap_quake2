/*
 * Sends Quake's rendered image to the active video streams.
 *
 * Quake renders once into a shared OpenGL framebuffer.
 * Each video stream has its own Axoverlay2 overlay and output buffers.
 * At most one stream receives a new image during each Quake frame.
 *
 * An output buffer is imported into OpenGL the first time it is seen.
 * The imported OpenGL objects are then reused when that buffer returns.
 */
#include "overlay.h"

#include "gpu_context.h"

#include <poll.h>
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
 * VDO reports when video streams appear and disappear.
 */
#include <glib-object.h>
#include <vdo-error.h>
#include <vdo-stream.h>

/*
 * OpenGL objects created for one Axoverlay2 output buffer.
 *
 * Axoverlay2 reuses a small set of buffers.
 * Keeping these objects avoids importing the same buffer again every frame.
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
   * EGL image created from the shared Axoverlay2 buffer.
   */
  EGLImageKHR image;

  /*
   * OpenGL texture that refers to the EGL image.
   */
  GLuint texture;

  /*
   * Framebuffer used as the destination when the Quake image is copied.
   */
  GLuint framebuffer;

  struct render_surface *next;
};

#define OVERLAY_CREATE_RETRY_US 1000000
#define NO_OUTPUT_WAIT_MS 33

/*
 * State for one active video stream.
 *
 * The stream owns one Axoverlay2 overlay and the OpenGL objects created for that overlay's output buffers.
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
static bool remove_overlay_stream(struct overlay_context *overlay, unsigned stream_id);
static void remove_all_overlay_streams(struct overlay_context *overlay);

static bool create_overlay(struct overlay_context *overlay, struct overlay_stream *stream);
static bool destroy_stream_overlay(struct overlay_stream *stream);
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
  overlay->event_fd = -1;
  overlay->streams = NULL;
  overlay->current_stream = NULL;
  overlay->current_surface = NULL;
  overlay->current_buffer = NULL;
  overlay->width = 0;
  overlay->height = 0;
  overlay->render_texture = 0;
  overlay->render_framebuffer = 0;
  overlay->render_depth_stencil = 0;
  overlay->next_stream_index = 0;
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

  overlay->event_fd = vdo_stream_get_event_fd(event_stream, &error);
  if (overlay->event_fd < 0) {
    syslog(LOG_ERR, "Failed to get VDO event fd: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
    goto fail;
  }

  g_object_unref(filter);

  syslog(LOG_INFO, "axoverlay2 initialized");

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
 * Process all pending video stream events.
 */
bool
overlay_context_process_events(struct overlay_context *overlay)
{
  VdoStream *event_stream = overlay->event_stream;

  /*
   * Drain the queue. VDO_ERROR_NO_EVENT means there are no more events.
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
     * EXISTING and CREATED both mean that the stream should have an overlay.
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

      if (!remove_overlay_stream(overlay, stream_id)) {
        g_object_unref(event);
        return false;
      }
    }

    g_object_unref(event);
  }
}

/*
 * Prepare one Quake frame.
 *
 * Stream events are handled first.
 * Then one active stream is selected for an output buffer.
 * Quake can still render when no output buffer is ready.
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
   * A previous frame must be finished before another one starts.
   */
  if (overlay->current_buffer || overlay->current_stream || overlay->current_surface) {
    syslog(LOG_ERR, "Overlay frame already active");
    return false;
  }

  struct overlay_stream *stream = select_output_stream(overlay);

  /*
   * Avoid spinning at full speed when there is no active overlay to render to.
   * Waiting on the VDO event fd wakes immediately when a stream appears.
   */
  if (!stream) {
    struct pollfd event_poll = {
      .fd = overlay->event_fd,
      .events = POLLIN,
    };

    (void)poll(&event_poll, 1, NO_OUTPUT_WAIT_MS);
    return true;
  }

  if (stream) {
    axo_err *error = NULL;
    /*
     * Ask only the selected stream for a buffer.
     * The selected stream changes each frame, so there is at most one blocking
     * Axoverlay2 call per Quake frame.
     */
    axo_buffer *buffer = axo_get_buffer(stream->overlay_id, NULL, &error);
    if (!buffer) {
      if (error && axo_err_get_code(error) == AXO_ERR_WAIT) {
        /*
         * No buffer is ready yet. Quake can still render this frame.
         */
        axo_err_clear(&error);
      } else if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
        /*
         * The stream closed before its VDO close event arrived.
         * Keep the record until that event is processed.
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
       * Reuse the OpenGL objects if this buffer was imported earlier.
       */
      struct render_surface *surface = find_render_surface(stream, buffer_id);
      if (!surface) {
        /*
         * Import a new buffer so OpenGL can write to it.
         */
        surface = create_render_surface(stream, gpu, buffer);
        if (!surface) {
          /*
           * Do not submit a buffer that was never rendered.
           * Removing the stream overlay discards that buffer before a later retry creates new ones.
           */
          syslog(LOG_ERR,
                 "Failed to import overlay buffer %lu for stream %u; recreating the overlay",
                 buffer_id,
                 stream->stream_id);
          if (!destroy_stream_overlay(stream)) {
            return false;
          }

          stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
        } else {
          surface->next = stream->surfaces;
          stream->surfaces = surface;
          syslog(LOG_INFO, "Imported overlay buffer %lu for stream %u", buffer_id, stream->stream_id);
        }
      }

      if (surface) {
        /*
         * Keep the stream, buffer and imported surface together until end_frame().
         */
        overlay->current_stream = stream;
        overlay->current_buffer = buffer;
        overlay->current_surface = surface;
      }
    }
  }

  /*
   * Quake always renders into the same shared framebuffer.
   */
  glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);
  glViewport(0, 0, (GLsizei)overlay->width, (GLsizei)overlay->height);

  return true;
}

/*
 * Bind Quake's shared output framebuffer.
 */
void
overlay_context_bind_frame(struct overlay_context *overlay)
{
  /*
   * Yamagi calls this after using temporary framebuffers.
   * The ACAP build returns to the shared Quake framebuffer instead of framebuffer 0.
   */
  if (overlay->render_framebuffer) {
    glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
}

/*
 * Copy the completed Quake image to the selected stream and submit it.
 */
bool
overlay_context_end_frame(struct overlay_context *overlay)
{
  struct overlay_stream *stream = overlay->current_stream;
  axo_buffer *buffer = overlay->current_buffer;
  struct render_surface *surface = overlay->current_surface;

  /*
   * No selected buffer means there is nothing to submit for this frame.
   */
  if (!stream || !buffer || !surface) {
    overlay->current_stream = NULL;
    overlay->current_buffer = NULL;
    overlay->current_surface = NULL;
    return true;
  }

  axo_err *error = NULL;

  /*
   * Copy directly between OpenGL framebuffers so the image stays on the GPU.
   */
  glBindFramebuffer(GL_READ_FRAMEBUFFER, overlay->render_framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, surface->framebuffer);

  /*
   * Preserve the Quake aspect ratio.
   * Any unused area becomes an opaque black border.
   */
  unsigned dst_width = stream->width;
  unsigned dst_height = stream->height;

  uint64_t dst_aspect = (uint64_t)stream->width * overlay->height;
  uint64_t src_aspect = (uint64_t)overlay->width * stream->height;

  if (dst_aspect > src_aspect) {
    dst_width = (unsigned)(((uint64_t)overlay->width * stream->height) / overlay->height);
    if (!dst_width) {
      dst_width = 1;
    }
  } else if (dst_aspect < src_aspect) {
    dst_height = (unsigned)(((uint64_t)overlay->height * stream->width) / overlay->width);
    if (!dst_height) {
      dst_height = 1;
    }
  }

  unsigned dst_x = (stream->width - dst_width) / 2;
  unsigned dst_y = (stream->height - dst_height) / 2;

  const GLfloat opaque_black[] = { 0.0f, 0.0f, 0.0f, 1.0f };

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearBufferfv(GL_COLOR, 0, opaque_black);

  /*
   * Flip the image vertically while copying it.
   * Nearest filtering keeps this final copy simple and cheap.
   */
  glBlitFramebuffer(0,
                    0,
                    (GLint)overlay->width,
                    (GLint)overlay->height,
                    (GLint)dst_x,
                    (GLint)(dst_y + dst_height),
                    (GLint)(dst_x + dst_width),
                    (GLint)dst_y,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);

  /*
   * Force alpha to fully opaque so the camera image does not show through.
   */
  const GLfloat opaque[] = { 0.0f, 0.0f, 0.0f, 1.0f };

  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
  glClearBufferfv(GL_COLOR, 0, opaque);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  /*
   * Finish GPU writes before returning the buffer to Axoverlay2.
   */
  glFinish();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  overlay->current_stream = NULL;
  overlay->current_buffer = NULL;
  overlay->current_surface = NULL;

  if (!axo_submit_buffer(buffer, NULL, &error)) {
    /*
     * A stream can close between buffer acquisition and submission.
     */
    if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
      axo_err_clear(&error);
      return true;
    }

    syslog(LOG_ERR,
           "Failed to submit overlay buffer for stream %u: %s",
           stream->stream_id,
           error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    return false;
  }

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
 * Create the Axoverlay2 overlay for one video stream.
 */
static bool
create_overlay(struct overlay_context *overlay, struct overlay_stream *stream)
{
  if (stream->overlay_id >= 0) {
    return true;
  }

  /*
   * Use half-size buffers when both dimensions are even.
   * Axoverlay2 scales them back to the video size.
   * This reduces the buffer area to one quarter of the stream size.
   */
  bool use_upscale = stream->stream_width % 2 == 0 && stream->stream_height % 2 == 0;

  unsigned used_width = use_upscale ? stream->stream_width / 2 : stream->stream_width;
  unsigned used_height = use_upscale ? stream->stream_height / 2 : stream->stream_height;

  axo_err *error = NULL;

  /*
   * Request an ARGB32 format that supports GPU rendering and compression.
   */
  axo_detailed_format *format =
      axo_suggest_detailed_format(AXO_FORMAT_ARGB32, AXO_FORMAT_FLAGS_COMPRESSED | AXO_FORMAT_FLAGS_GPU, &error);
  if (!format) {
    if (error && axo_err_get_code(error) == AXO_ERR_NOT_AVAILABLE) {
      syslog(LOG_WARNING,
             "GPU overlay format is temporarily unavailable for stream %u: %s; retrying later",
             stream->stream_id,
             axo_err_get_message(error));
      axo_err_clear(&error);
      stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
      return true;
    }

    syslog(LOG_ERR,
           "Failed to get GPU overlay format for stream %u: %s",
           stream->stream_id,
           error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    return false;
  }

  /*
   * The allocated buffer may be larger than the visible image.
   */
  unsigned full_width;
  unsigned full_height;

  /*
   * Ask Axoverlay2 for the size required by the selected format.
   */
  axo_detailed_format_get_aligned_size(format, used_width, used_height, &full_width, &full_height);

  /*
   * Keep both allocated dimensions aligned to 16 pixels.
   */
  full_width = (full_width + 15) & ~15u;
  full_height = (full_height + 15) & ~15u;

  /*
   * props describes the overlay. match selects its video stream.
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
    syslog(LOG_ERR, "Failed to allocate overlay creation objects for stream %u", stream->stream_id);
    return false;
  }

  axo_props_set_detailed_format(props, format);
  axo_props_set_size(props, full_width, full_height);

  /*
   * Scale half-size buffers back to the video size.
   */
  axo_props_set_upscale_x2(props, use_upscale);

  /*
   * The application waits for GPU writes before submitting each buffer.
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
       * VDO will provide the CLOSED event which removes this record.
       */
      syslog(LOG_INFO, "VDO stream %u disappeared while creating its overlay", stream->stream_id);
      stream->retry_at_us = 0;
      axo_err_clear(&error);
      axo_detailed_format_free(format);
      return true;
    }

    if (error && axo_err_get_code(error) == AXO_ERR_NOT_AVAILABLE) {
      syslog(LOG_WARNING,
             "Overlay service is temporarily unavailable for stream %u: %s; retrying later",
             stream->stream_id,
             axo_err_get_message(error));
      stream->retry_at_us = g_get_monotonic_time() + OVERLAY_CREATE_RETRY_US;
      axo_err_clear(&error);
      axo_detailed_format_free(format);
      return true;
    }

    syslog(LOG_ERR,
           "Failed to create overlay for stream %u: %s",
           stream->stream_id,
           error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    axo_detailed_format_free(format);
    return false;
  }

  stream->overlay_id = overlay_id;
  stream->format = format;

  /*
   * width and height are visible.
   * full_width and full_height include any required padding.
   */
  stream->width = used_width;
  stream->height = used_height;
  stream->full_width = full_width;
  stream->full_height = full_height;
  stream->frame_count = 0;
  stream->retry_at_us = 0;

  /*
   * The first stream selects the shared Quake render size.
   * Keep that size until all streams close.
   */
  if (!overlay->render_framebuffer) {
    overlay->width = used_width;
    overlay->height = used_height;

    if (!create_render_target(overlay)) {
      (void)destroy_stream_overlay(stream);
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
 * Add a video stream or update its size.
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
    if (!destroy_stream_overlay(stream)) {
      return false;
    }
  }

  stream->retry_at_us = 0;

  syslog(LOG_INFO, "VDO stream %u available: %ux%u", stream_id, stream_width, stream_height);

  return create_overlay(overlay, stream);
}

/*
 * Retry temporary overlay creation failures once per second.
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
 * Select the next stream in round-robin order.
 *
 * Only one stream is selected per Quake frame.
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
 * Remove one stream's Axoverlay2 overlay and imported buffers.
 *
 * The stream record itself is kept so the overlay can be created again later.
 */
static bool
destroy_stream_overlay(struct overlay_stream *stream)
{
  axo_err *error = NULL;

  if (stream->overlay_id >= 0 && !axo_remove_overlay(stream->overlay_id, &error)) {
    /*
     * NO_STREAM means the video stream already disappeared.
     * For any other error, keep the overlay ID so cleanup can be tried again.
     */
    if (!error || axo_err_get_code(error) != AXO_ERR_NO_STREAM) {
      syslog(LOG_ERR,
             "Failed to remove overlay %d from stream %u: %s",
             stream->overlay_id,
             stream->stream_id,
             error ? axo_err_get_message(error) : "unknown error");
      axo_err_clear(&error);
      return false;
    }
  }

  axo_err_clear(&error);

  /*
   * The overlay is gone, so its imported buffers can now be released.
   */
  destroy_render_surfaces(stream);

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

  return true;
}

/*
 * Remove one stream and the overlay attached to it.
 */
static bool
remove_overlay_stream(struct overlay_context *overlay, unsigned stream_id)
{
  struct overlay_stream **link = &overlay->streams;

  while (*link && (*link)->stream_id != stream_id) {
    link = &(*link)->next;
  }

  if (!*link) {
    return true;
  }

  struct overlay_stream *stream = *link;

  /*
   * Keep the stream record if Axoverlay2 cannot remove the overlay.
   * The overlay ID is needed if cleanup has to be tried again during shutdown.
   */
  if (!destroy_stream_overlay(stream)) {
    return false;
  }

  if (overlay->current_stream == stream) {
    overlay->current_stream = NULL;
    overlay->current_buffer = NULL;
    overlay->current_surface = NULL;
  }

  *link = stream->next;
  free(stream);

  if (!overlay->streams) {
    /*
     * The next stream may use a different size, so discard the shared Quake image
     * when the last stream closes.
     */
    destroy_render_target(overlay);
    overlay->width = 0;
    overlay->height = 0;
    overlay->next_stream_index = 0;
  }

  return true;
}

/*
 * Release all stream records during shutdown.
 *
 * Shutdown is best effort. If Axoverlay2 refuses to remove an overlay, local
 * graphics objects are still released before the application stops Axoverlay2.
 */
static void
remove_all_overlay_streams(struct overlay_context *overlay)
{
  while (overlay->streams) {
    struct overlay_stream *stream = overlay->streams;
    overlay->streams = stream->next;

    if (!destroy_stream_overlay(stream)) {
      destroy_render_surfaces(stream);

      if (stream->format) {
        axo_detailed_format_free(stream->format);
      }
    }

    free(stream);
  }
}

/*
 * Create the shared framebuffer that Quake renders into.
 */
static bool
create_render_target(struct overlay_context *overlay)
{
  /*
   * This framebuffer exists independently of the Axoverlay2 output buffers.
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
 * Destroy the shared Quake framebuffer and its attachments.
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
 * Find the OpenGL objects already created for an Axoverlay2 buffer.
 */
static struct render_surface *
find_render_surface(struct overlay_stream *stream, unsigned long buffer_id)
{
  struct render_surface *surface = stream->surfaces;

  while (surface) {
    /*
     * Buffer IDs remain stable for the lifetime of this overlay.
     */
    if (surface->buffer_id == buffer_id) {
      return surface;
    }
    surface = surface->next;
  }

  return NULL;
}

/*
 * Import one Axoverlay2 output buffer into OpenGL.
 */
static struct render_surface *
create_render_surface(struct overlay_stream *stream, const struct gpu_context *gpu, axo_buffer *buffer)
{
  /*
   * These extension functions connect the shared Axoverlay2 buffer to OpenGL.
   */
  PFNEGLCREATEIMAGEKHRPROC create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (!create_image || !image_target_texture) {
    syslog(LOG_ERR, "Required EGL image extensions are unavailable");
    return NULL;
  }

  /*
   * DMA-BUF is the shared image buffer used by Axoverlay2 and the GPU.
   */
  int dma_buf_fd = axo_buffer_get_dma_buf_fd(buffer);

  if (dma_buf_fd < 0) {
    syslog(LOG_ERR, "Overlay buffer has no DMA-BUF");
    return NULL;
  }

  axo_detailed_format *format = stream->format;

  /*
   * FourCC tells EGL how the pixels are stored.
   */
  uint32_t fourcc = axo_detailed_format_get_drm_fourcc(format);

  /*
   * The modifier tells EGL how the buffer is laid out in memory.
   */
  uint64_t modifier = axo_detailed_format_get_drm_modifier(format, 0);

  /*
   * Describe the shared buffer so EGL can import it.
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
     * ARGB32 uses four bytes per pixel.
     */
    EGL_DMA_BUF_PLANE0_PITCH_EXT,
    (EGLint)(stream->full_width * 4),

    /*
     * EGL expects the 64-bit modifier as two 32-bit values.
     */
    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
    (EGLint)(modifier & 0xffffffff),

    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
    (EGLint)(modifier >> 32),

    EGL_NONE,
  };

  /*
   * Zeroed handles make partial cleanup safe.
   */
  struct render_surface *surface = calloc(1, sizeof(*surface));
  if (!surface) {
    syslog(LOG_ERR, "Failed to allocate render surface");
    return NULL;
  }

  surface->buffer_id = axo_buffer_get_id(buffer);
  surface->display = gpu->display;

  /*
   * EGL uses a specific value for an invalid image.
   */
  surface->image = EGL_NO_IMAGE_KHR;

  /*
   * Import the shared buffer as an EGL image.
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
   * Connect the EGL image to the OpenGL texture.
   */
  image_target_texture(GL_TEXTURE_2D, surface->image);

  /*
   * Check the OpenGL step separately from the EGL import.
   */
  GLenum gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    syslog(LOG_ERR, "Failed to bind EGLImage as texture: 0x%x", gl_error);
    glBindTexture(GL_TEXTURE_2D, 0);
    destroy_render_surface(surface);
    return NULL;
  }

  /*
   * Attach the texture to a framebuffer so it can receive the final copy.
   */
  glGenFramebuffers(1, &surface->framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, surface->framebuffer);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface->texture, 0);

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
 * Release the OpenGL objects created for one output buffer.
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

  if (surface->framebuffer) {
    glDeleteFramebuffers(1, &surface->framebuffer);
  }

  if (surface->texture) {
    glDeleteTextures(1, &surface->texture);
  }

  /*
   * The EGL image must be released separately from the texture.
   */
  if (surface->image != EGL_NO_IMAGE_KHR) {
    /*
     * eglDestroyImageKHR is loaded from the graphics driver at runtime.
     */
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    if (destroy_image) {
      destroy_image(surface->display, surface->image);
    }
  }
  free(surface);
}

/*
 * Release every imported buffer for one stream.
 */
static void
destroy_render_surfaces(struct overlay_stream *stream)
{
  struct render_surface *surface = stream->surfaces;

  while (surface) {

    struct render_surface *next = surface->next;
    destroy_render_surface(surface);
    surface = next;
  }

  stream->surfaces = NULL;
}
