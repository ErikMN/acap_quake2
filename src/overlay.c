/*
 * Connects Quake's finished frames to the video stream through axoverlay2.
 *
 * Quake renders each frame into a normal OpenGL framebuffer that belongs to the application.
 * When the frame is finished, this file copies that image into a buffer provided by axoverlay2.
 *
 * axoverlay2 provides the final overlay buffers and cycles between several of them.
 *
 * Each buffer is imported into OpenGL the first time it is seen so the GPU can write to it directly
 * without reading the completed frame back to the CPU first.
 */
#include "overlay.h"

#include "gpu_context.h"

#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>

/*
 * EGL is used to import axoverlay2 buffers into the same graphics system used by Quake.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>

/*
 * OpenGL ES is used both by Quake and by this file when creating framebuffers,
 * textures and copying completed frames.
 */
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

/*
 * axoverlay2 provides the image buffers that are inserted into the video stream.
 */
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
 * The first time a buffer is received from axoverlay2, we import it into EGL and create the
 * OpenGL objects needed to draw into it.
 *
 * The same axoverlay2 buffer will return again later, so this structure keeps those objects around for reuse.
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

  /*
   * Depth and stencil storage attached to the framebuffer.
   */
  GLuint depth_stencil;

  /*
   * The imported buffers are kept in a linked list so they can be found by
   * their axoverlay2 buffer ID and reused.
   */
  struct render_surface *next;
};

/*
 * Internal helper functions used to create and destroy the axoverlay2 overlay and its OpenGL resources.
 */
static void remove_overlay(struct overlay_context *overlay);
static void destroy_render_surfaces(struct overlay_context *overlay);

static bool
create_overlay(struct overlay_context *overlay, unsigned stream_id, unsigned stream_width, unsigned stream_height);

static bool create_render_target(struct overlay_context *overlay);
static void destroy_render_target(struct overlay_context *overlay);

/*
 * Find the OpenGL objects that were previously created for an axoverlay2 buffer.
 */
static struct render_surface *find_render_surface(struct overlay_context *overlay, unsigned long buffer_id);

/*
 * Import a new axoverlay2 buffer into EGL and make it usable as an OpenGL framebuffer.
 */
static struct render_surface *
create_render_surface(struct overlay_context *overlay, const struct gpu_context *gpu, axo_buffer *buffer);

static void destroy_render_surface(struct render_surface *surface);

bool
overlay_context_init(struct overlay_context *overlay)
{
  /*
   * axoverlay2 and VDO use different error types, so each API gets its own error pointer.
   */
  axo_err *axo_error = NULL;
  GError *error = NULL;

  /*
   * The VDO filter is created during initialization and released once it has been attached to the event stream.
   */
  VdoMap *filter = NULL;

  /*
   * Start with every resource in its empty state.
   * This makes the normal cleanup path safe even if initialization fails halfway through.
   */
  overlay->event_stream = NULL;
  overlay->format = NULL;
  overlay->surfaces = NULL;
  overlay->current_surface = NULL;
  overlay->current_buffer = NULL;
  overlay->event_fd = -1;
  overlay->overlay_id = -1;
  overlay->stream_id = 0;
  overlay->width = 0;
  overlay->height = 0;
  overlay->full_width = 0;
  overlay->full_height = 0;
  overlay->render_texture = 0;
  overlay->render_framebuffer = 0;
  overlay->render_depth_stencil = 0;
  overlay->frame_count = 0;
  overlay->axo_started = false;

  /*
   * Start axoverlay2 before trying to create or receive overlay buffers.
   */
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

  /*
   * Ask VDO to report overlay-related stream events.
   */
  filter = vdo_map_new();
  vdo_map_set_string(filter, "filter", "overlay");

  if (!vdo_stream_attach(event_stream, filter, &error)) {
    syslog(LOG_ERR, "Failed to attach VDO stream filter: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
    goto fail;
  }

  /*
   * VDO exposes its events through a file descriptor.
   * This allows the application to wait for stream changes like normal Linux
   * input instead of repeatedly asking VDO whether something changed.
   */
  overlay->event_fd = vdo_stream_get_event_fd(event_stream, &error);
  if (overlay->event_fd < 0) {
    syslog(LOG_ERR, "Failed to get VDO event fd: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
    goto fail;
  }

  /*
   * VDO has taken the information it needs from the filter, so our reference can now be released.
   */
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

void
overlay_context_destroy(struct overlay_context *overlay)
{
  /*
   * Remove the active axoverlay2 overlay and all GPU resources that belong to it.
   */
  remove_overlay(overlay);

  /*
   * Release the VDO event stream used to discover video streams.
   */
  if (overlay->event_stream) {
    g_object_unref(overlay->event_stream);
    overlay->event_stream = NULL;
  }

  overlay->event_fd = -1;

  /*
   * Stop axoverlay2 only if initialization successfully started it.
   */
  if (overlay->axo_started) {
    axo_stop(NULL);
    overlay->axo_started = false;
  }
}

int
overlay_context_get_event_fd(const struct overlay_context *overlay)
{
  /*
   * The caller can wait on this file descriptor to know when VDO has new stream events available.
   */
  return overlay->event_fd;
}

bool
overlay_context_process_events(struct overlay_context *overlay)
{
  /*
   * Video streams can appear and disappear while the application is running.
   * Follow those changes so the overlay stays attached to an available stream
   * and clean up when that stream goes away.
   */
  VdoStream *event_stream = overlay->event_stream;

  /*
   * Process every event that is currently waiting.
   * VDO_ERROR_NO_EVENT is used to tell us that the queue is empty.
   */
  for (;;) {
    GError *error = NULL;
    VdoMap *event = vdo_stream_get_event(event_stream, &error);
    if (!event) {
      /*
       * Having no more events is normal and means all pending work has been handled.
       */
      if (g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_EVENT)) {
        g_clear_error(&error);
        return true;
      }
      syslog(LOG_ERR, "Failed to get VDO event: %s", error ? error->message : "unknown error");
      g_clear_error(&error);

      return false;
    }

    /*
     * Each event tells us what happened and which video stream it belongs to.
     */
    unsigned event_type = vdo_map_get_uint32(event, "event", 0);
    unsigned stream_id = vdo_map_get_uint32(event, "id", 0);

    /*
     * Existing means the stream was already present when we started listening.
     * Created means the stream appeared while the application was running.
     *
     * Both cases require the same setup.
     */
    if (event_type == VDO_STREAM_EVENT_EXISTING || event_type == VDO_STREAM_EVENT_CREATED) {
      VdoStream *stream = vdo_stream_get(stream_id, &error);
      if (!stream) {
        syslog(LOG_ERR, "Failed to get VDO stream %u: %s", stream_id, error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(event);
        continue;
      }

      /*
       * Read the stream information so the overlay can be created at a size that matches the video stream.
       */
      VdoMap *info = vdo_stream_get_info(stream, NULL);
      if (info) {
        unsigned width = vdo_map_get_uint32(info, "width", 0);
        unsigned height = vdo_map_get_uint32(info, "height", 0);

        syslog(LOG_INFO, "VDO stream %u available: %ux%u", stream_id, width, height);
        /*
         * Create the axoverlay2 overlay and the private framebuffer Quake will use for rendering.
         */
        if (!create_overlay(overlay, stream_id, width, height)) {
          g_object_unref(info);
          g_object_unref(stream);
          g_object_unref(event);
          return false;
        }

        g_object_unref(info);
      }

      g_object_unref(stream);
    } else if (event_type == VDO_STREAM_EVENT_CLOSED) {
      syslog(LOG_INFO, "VDO stream %u closed", stream_id);
      /*
       * Only remove our overlay if the stream that disappeared is the one we are currently attached to.
       */
      if (overlay->stream_id == stream_id) {
        remove_overlay(overlay);
      }
    }
    g_object_unref(event);
  }
}

bool
overlay_context_begin_frame(struct overlay_context *overlay, const struct gpu_context *gpu)
{
  /*
   * Handle stream changes before starting another Quake frame.
   */
  if (!overlay_context_process_events(overlay)) {
    return false;
  }

  /*
   * A current buffer means the previous frame was never submitted.
   * Starting another frame in that state would lose track of which axoverlay2 buffer is being written.
   */
  if (overlay->current_buffer) {
    syslog(LOG_ERR, "Overlay frame already active");
    return false;
  }

  /*
   * There may temporarily be no video stream and therefore no overlay.
   * That is not treated as an application error.
   */
  if (overlay->overlay_id < 0) {
    return true;
  }

  /*
   * Ask axoverlay2 for the next overlay buffer that is free to use.
   *
   * axoverlay2 manages several buffers for the overlay and cycles between them, so the
   * buffer received here can change from frame to frame.
   */
  axo_err *error = NULL;
  axo_buffer *buffer = axo_get_buffer(overlay->overlay_id, NULL, &error);
  if (!buffer) {
    /*
     * WAIT means no overlay buffer is ready yet.
     * NO_STREAM means the video stream disappeared before the event was processed.
     *
     * Neither condition means the application itself is broken, so the frame is simply skipped.
     */
    if (error && (axo_err_get_code(error) == AXO_ERR_WAIT || axo_err_get_code(error) == AXO_ERR_NO_STREAM)) {
      axo_err_clear(&error);
      return true;
    }

    syslog(LOG_ERR, "Failed to get overlay buffer: %s", error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    return false;
  }

  /*
   * Every axoverlay2 buffer has an ID that remains the same when that buffer is returned again later.
   */
  unsigned long buffer_id = axo_buffer_get_id(buffer);

  /*
   * Reuse the EGL and OpenGL objects if this axoverlay2 buffer was already imported
   * during an earlier frame.
   */
  struct render_surface *surface = find_render_surface(overlay, buffer_id);
  if (!surface) {
    /*
     * This is the first time this axoverlay2 buffer has been seen.
     * Import it into EGL so the GPU can write into it.
     */
    surface = create_render_surface(overlay, gpu, buffer);
    if (!surface) {
      return false;
    }

    /*
     * Add the newly imported buffer to the front of our linked list so it can be found and reused later.
     */
    surface->next = overlay->surfaces;
    overlay->surfaces = surface;
    syslog(LOG_INFO, "Imported overlay buffer %lu", buffer_id);
  }

  /*
   * Remember which axoverlay2 buffer belongs to the frame currently being rendered.
   * overlay_context_end_frame() will submit this same buffer back to axoverlay2.
   */
  overlay->current_buffer = buffer;
  overlay->current_surface = surface;

  /*
   * Quake draws into one stable image owned by the application.
   *
   * This keeps the game renderer simple even though axoverlay2 gives us a different
   * output buffer over time. Quake therefore sees the same framebuffer every frame.
   */
  glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);

  /*
   * Match OpenGL's drawing area to the resolution selected for the game framebuffer.
   */
  glViewport(0, 0, (GLsizei)overlay->width, (GLsizei)overlay->height);

  return true;
}

void
overlay_context_bind_frame(struct overlay_context *overlay)
{
  /*
   * Yamagi sometimes needs to return to what it considers the final output
   * framebuffer after using one of its own temporary framebuffers.
   *
   * On a desktop this would normally be framebuffer 0.
   * In this port the real game output is our private framebuffer instead.
   */
  if (overlay->current_surface) {
    glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);
  } else {
    /*
     * When no axoverlay2 output buffer is active, fall back to the normal OpenGL framebuffer.
     */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
}

bool
overlay_context_end_frame(struct overlay_context *overlay)
{
  /*
   * Retrieve the axoverlay2 buffer and imported OpenGL surface selected when this frame began.
   */
  axo_buffer *buffer = overlay->current_buffer;
  struct render_surface *surface = overlay->current_surface;

  /*
   * A missing buffer means no axoverlay2 buffer was available when rendering began.
   * There is therefore nothing to submit.
   */
  if (!buffer || !surface) {
    return true;
  }

  axo_err *error = NULL;

  /*
   * The completed Quake frame is already on the GPU.
   *
   * Use the private Quake framebuffer as the source and the OpenGL framebuffer
   * backed by the axoverlay2 buffer as the destination.
   * This keeps the image on the GPU instead of reading all pixels back through the CPU.
   */
  glBindFramebuffer(GL_READ_FRAMEBUFFER, overlay->render_framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, surface->framebuffer);

  /*
   * Copy the completed game image into the axoverlay2 buffer.
   *
   * The source covers the complete Quake framebuffer.
   *
   * The destination Y coordinates are reversed because the Quake render target and
   * axoverlay2 buffer use opposite vertical directions.
   * Reversing these coordinates flips the image while it is copied.
   *
   * GL_NEAREST is enough here because the source and destination use the same render size.
   * If 2x overlay scaling is enabled, axoverlay2 performs that scaling later in the video pipeline.
   */
  glBlitFramebuffer(0,
                    0,
                    (GLint)overlay->width,
                    (GLint)overlay->height,
                    0,
                    (GLint)overlay->height,
                    (GLint)overlay->width,
                    0,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);

  /*
   * Quake uses alpha while rendering effects such as flashes and transparent surfaces.
   * That alpha is useful while Quake builds the image, but it has a different meaning
   * after the image reaches the axoverlay2 overlay.
   *
   * Alpha in the final axoverlay2 overlay controls whether the video stream shows
   * through the game. Leaving Quake's final alpha values untouched would
   * therefore make some game effects accidentally reveal the video stream.
   *
   * Only enable writes to the alpha channel and clear it to 1.0.
   * The finished red, green and blue parts of the Quake image remain unchanged while the
   * complete axoverlay2 overlay becomes fully opaque.
   */
  const GLfloat opaque[] = { 0.0f, 0.0f, 0.0f, 1.0f };

  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
  glClearBufferfv(GL_COLOR, 0, opaque);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  /*
   * Wait until the GPU has completely finished writing the axoverlay2 buffer before
   * returning ownership of that buffer to axoverlay2.
   *
   * Without this synchronization, axoverlay2 could start using the image while the
   * GPU was still changing it.
   */
  glFinish();

  /*
   * Stop using either of the application framebuffers before handing the
   * completed image back to axoverlay2.
   */
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  /*
   * The application no longer owns an active output buffer after this point.
   */
  overlay->current_buffer = NULL;
  overlay->current_surface = NULL;

  /*
   * Return the completed buffer to axoverlay2 so it can be shown as the next overlay frame.
   */
  if (!axo_submit_buffer(buffer, NULL, &error)) {
    /*
     * The stream can disappear between acquiring the buffer and submitting it.
     * That is handled like a normal stream change rather than a fatal error.
     */
    if (error && axo_err_get_code(error) == AXO_ERR_NO_STREAM) {
      axo_err_clear(&error);
      return true;
    }
    syslog(LOG_ERR, "Failed to submit overlay buffer: %s", error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);

    return false;
  }

  overlay->frame_count++;

  /*
   * Log occasional progress without writing a message for every rendered frame.
   */
  if (overlay->frame_count % 30 == 0) {
    syslog(LOG_INFO, "Rendered %u frames on overlay %d", overlay->frame_count, overlay->overlay_id);
  }

  return true;
}

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

  /*
   * No surface means there is currently no axoverlay2 buffer available.
   */
  if (!overlay->current_surface) {
    return true;
  }

  /*
   * Move gradually through a repeating range from 0.0 to 1.0 so the test image visibly changes over time.
   */
  float phase = (float)(overlay->frame_count % 120) / 119.0f;

  /*
   * Clear the test frame with a color that changes between red and green.
   */
  glClearColor(1.0f - phase, phase, 0.0f, 1.0f);

  /*
   * Reset depth and stencil values as well because the private framebuffer contains those buffers.
   */
  glClearDepthf(1.0f);
  glClearStencil(0);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  /*
   * Send the finished test frame through the same path used by Quake.
   */
  return overlay_context_end_frame(overlay);
}

static bool
create_overlay(struct overlay_context *overlay, unsigned stream_id, unsigned stream_width, unsigned stream_height)
{
  /*
   * When both stream dimensions can be divided cleanly by two, Quake renders at half width and half height.
   *
   * axoverlay2 then scales the image back to the full video size.
   * Rendering one quarter as many pixels greatly reduces the amount of work
   * the GPU must do for each Quake frame.
   */
  bool use_upscale = stream_width % 2 == 0 && stream_height % 2 == 0;

  /*
   * These are the dimensions Quake itself will render.
   */
  unsigned used_width = use_upscale ? stream_width / 2 : stream_width;
  unsigned used_height = use_upscale ? stream_height / 2 : stream_height;

  /*
   * axoverlay2 may require the actual allocated buffer dimensions to be larger than
   * the visible image because of hardware alignment requirements.
   */
  unsigned full_width;
  unsigned full_height;

  axo_err *error = NULL;
  axo_props *props = NULL;
  axo_match *match = NULL;

  /*
   * Only one axoverlay2 overlay is used at a time.
   */
  if (overlay->overlay_id >= 0) {
    return true;
  }

  /*
   * Ask axoverlay2 for a GPU-friendly ARGB buffer format.
   *
   * ARGB32 provides four color components including alpha.
   * The GPU flag tells axoverlay2 that the buffer will be used directly by the GPU.
   * The compressed flag lets axoverlay2 choose its GPU-friendly image layout.
   */
  axo_detailed_format *format =
      axo_suggest_detailed_format(AXO_FORMAT_ARGB32, AXO_FORMAT_FLAGS_COMPRESSED | AXO_FORMAT_FLAGS_GPU, &error);
  if (!format) {
    syslog(LOG_ERR, "Failed to get GPU overlay format: %s", error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    return false;
  }

  /*
   * Ask axoverlay2 how large the allocated image must be for the chosen format.
   * These dimensions can be larger than the visible render size.
   */
  axo_detailed_format_get_aligned_size(format, used_width, used_height, &full_width, &full_height);

  /*
   * Keep both allocated dimensions aligned to 16 pixels.
   */
  full_width = (full_width + 15) & ~15u;
  full_height = (full_height + 15) & ~15u;

  /*
   * props describes how the overlay buffers should be created.
   * match describes which video stream the overlay belongs to.
   */
  props = axo_props_new();
  match = axo_match_new();

  /*
   * Configure the overlay with the GPU format and the fully aligned buffer dimensions calculated above.
   */
  axo_props_set_detailed_format(props, format);
  axo_props_set_size(props, full_width, full_height);

  /*
   * When half-size rendering is enabled, tell axoverlay2 to scale the overlay by two
   * before placing it into the video stream.
   */
  axo_props_set_upscale_x2(props, use_upscale);

  /*
   * Let the application handle synchronization before an overlay buffer is submitted.
   * The glFinish() call at the end of each frame waits for the GPU to finish writing the buffer.
   */
  axo_props_set_manual_dma_sync(props, true);

  /*
   * Attach this overlay specifically to the video stream that generated the VDO event.
   */
  axo_match_stream_id(match, stream_id);

  /*
   * Create the axoverlay2 overlay using the selected properties and stream.
   */
  int overlay_id = axo_create_overlay(props, match, &error);

  /*
   * axo_create_overlay() has already read these temporary configuration objects, so they are no longer needed.
   */
  axo_props_free(props);
  axo_match_free(match);

  if (overlay_id < 0) {
    syslog(LOG_ERR, "Failed to create overlay: %s", error ? axo_err_get_message(error) : "unknown error");

    axo_err_clear(&error);
    axo_detailed_format_free(format);
    return false;
  }

  /*
   * Save everything needed while this video stream and overlay remain active.
   */
  overlay->overlay_id = overlay_id;
  overlay->stream_id = stream_id;
  overlay->format = format;

  /*
   * width and height describe the visible image Quake renders.
   *
   * full_width and full_height describe the actual memory size required by the axoverlay2 buffer format.
   */
  overlay->width = used_width;
  overlay->height = used_height;
  overlay->full_width = full_width;
  overlay->full_height = full_height;
  overlay->frame_count = 0;

  /*
   * Create the stable private framebuffer that Quake will render into.
   */
  if (!create_render_target(overlay)) {
    remove_overlay(overlay);
    return false;
  }

  syslog(LOG_INFO,
         "Created GPU overlay %d on stream %u: stream %ux%u, render %ux%u, buffer %ux%u%s",
         overlay_id,
         stream_id,
         stream_width,
         stream_height,
         used_width,
         used_height,
         full_width,
         full_height,
         use_upscale ? ", 2x upscale" : "");

  return true;
}

static bool
create_render_target(struct overlay_context *overlay)
{
  /*
   * Keep one private game image at a fixed size.
   *
   * Quake always renders into this framebuffer first.
   * When the frame is finished, overlay_context_end_frame() copies the image into whichever axoverlay2
   * buffer is available for that video frame.
   */

  /*
   * Create the texture that stores the finished color image produced by Quake.
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

  /*
   * No texture filtering is needed for the render target itself.
   */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  /*
   * Create Quake's private framebuffer and attach the color texture to it.
   */
  glGenFramebuffers(1, &overlay->render_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, overlay->render_framebuffer);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, overlay->render_texture, 0);

  /*
   * Quake also needs depth and stencil information while drawing the 3D scene.
   * Store both in one combined renderbuffer.
   */
  glGenRenderbuffers(1, &overlay->render_depth_stencil);
  glBindRenderbuffer(GL_RENDERBUFFER, overlay->render_depth_stencil);

  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)overlay->width, (GLsizei)overlay->height);

  glFramebufferRenderbuffer(
      GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, overlay->render_depth_stencil);

  /*
   * A framebuffer is only usable if all attached images form a valid OpenGL render target.
   */
  GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  /*
   * Leave OpenGL in a neutral state after creating the resources.
   */
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

static void
destroy_render_target(struct overlay_context *overlay)
{
  /*
   * Delete the private depth and stencil buffer if it was created.
   */
  if (overlay->render_depth_stencil) {
    glDeleteRenderbuffers(1, &overlay->render_depth_stencil);
    overlay->render_depth_stencil = 0;
  }

  /*
   * Delete Quake's private framebuffer.
   */
  if (overlay->render_framebuffer) {
    glDeleteFramebuffers(1, &overlay->render_framebuffer);
    overlay->render_framebuffer = 0;
  }

  /*
   * Delete the texture that stored Quake's completed color image.
   */
  if (overlay->render_texture) {
    glDeleteTextures(1, &overlay->render_texture);
    overlay->render_texture = 0;
  }
}

static struct render_surface *
find_render_surface(struct overlay_context *overlay, unsigned long buffer_id)
{
  /*
   * Walk through every axoverlay2 buffer that has already been imported into EGL.
   */
  struct render_surface *surface = overlay->surfaces;

  while (surface) {
    /*
     * Buffer IDs remain stable, so an equal ID means this is the same axoverlay2 buffer we prepared earlier.
     */
    if (surface->buffer_id == buffer_id) {
      return surface;
    }
    surface = surface->next;
  }

  /*
   * NULL tells the caller that this axoverlay2 buffer still needs to be imported.
   */
  return NULL;
}

static struct render_surface *
create_render_surface(struct overlay_context *overlay, const struct gpu_context *gpu, axo_buffer *buffer)
{
  /*
   * These functions are extensions rather than basic EGL and OpenGL ES
   * functions, so their addresses are requested from the graphics driver at runtime.
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

  /*
   * Use the exact image format selected when the axoverlay2 overlay was created.
   */
  axo_detailed_format *format = overlay->format;

  /*
   * DRM FourCC describes the pixel format of the shared image so EGL knows how
   * the color data should be interpreted.
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
   * EGL needs the allocated dimensions, pixel format, DMA-BUF handle, position
   * of the first pixel, row size and GPU memory layout before it can import the image.
   */
  EGLint attributes[] = {
    EGL_WIDTH,
    (EGLint)overlay->full_width,

    EGL_HEIGHT,
    (EGLint)overlay->full_height,

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
    (EGLint)(overlay->full_width * 4),

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
   * Allocate our bookkeeping structure before creating the EGL and OpenGL
   * objects for this axoverlay2 buffer.
   *
   * calloc() also starts every handle at zero, which makes cleanup safe after a partial failure.
   */
  struct render_surface *surface = calloc(1, sizeof(*surface));
  if (!surface) {
    syslog(LOG_ERR, "Failed to allocate render surface");
    return NULL;
  }

  /*
   * Remember which axoverlay2 buffer this surface belongs to and which EGL display owns the imported image.
   */
  surface->buffer_id = axo_buffer_get_id(buffer);
  surface->display = gpu->display;

  /*
   * EGL uses its own special invalid value for images, so set it explicitly
   * instead of relying on the zero-filled allocation.
   */
  surface->image = EGL_NO_IMAGE_KHR;

  /*
   * Import the DMA-BUF backing the axoverlay2 buffer as an EGL image.
   *
   * EGL_LINUX_DMA_BUF_EXT tells EGL that the image is backed by the Linux
   * DMA-BUF described by the attributes above.
   */
  surface->image = create_image(gpu->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attributes);

  if (surface->image == EGL_NO_IMAGE_KHR) {
    syslog(LOG_ERR, "Failed to import overlay DMA-BUF as EGLImage: 0x%x", eglGetError());
    destroy_render_surface(surface);
    return NULL;
  }

  /*
   * Create a normal OpenGL texture object that will refer to the imported EGL image.
   */
  glGenTextures(1, &surface->texture);
  glBindTexture(GL_TEXTURE_2D, surface->texture);

  /*
   * Connect the imported EGL image to the texture.
   *
   * After this call, writes made through the OpenGL texture affect the same
   * image that belongs to the axoverlay2 buffer.
   */
  image_target_texture(GL_TEXTURE_2D, surface->image);

  /*
   * Binding an external EGL image to an OpenGL texture can fail even though the
   * EGL import itself succeeded, so check the OpenGL error separately.
   */
  GLenum gl_error = glGetError();
  if (gl_error != GL_NO_ERROR) {
    syslog(LOG_ERR, "Failed to bind EGLImage as texture: 0x%x", gl_error);
    glBindTexture(GL_TEXTURE_2D, 0);
    destroy_render_surface(surface);
    return NULL;
  }

  /*
   * Create an OpenGL framebuffer and use the imported axoverlay2-backed texture as its color output.
   * This turns the axoverlay2 buffer into something normal OpenGL framebuffer commands can write into.
   */
  glGenFramebuffers(1, &surface->framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, surface->framebuffer);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface->texture, 0);

  /*
   * Create depth and stencil storage matching the full allocated axoverlay2 buffer.
   */
  glGenRenderbuffers(1, &surface->depth_stencil);
  glBindRenderbuffer(GL_RENDERBUFFER, surface->depth_stencil);

  glRenderbufferStorage(
      GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)overlay->full_width, (GLsizei)overlay->full_height);

  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, surface->depth_stencil);

  /*
   * The renderbuffer no longer needs to remain bound after it has been attached to the framebuffer.
   */
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  /*
   * Make sure the imported EGL image and the attached OpenGL resources form a
   * complete framebuffer before trying to use it.
   */
  GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  /*
   * Leave the imported surface unbound after setup.
   */
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
    syslog(LOG_ERR, "Overlay framebuffer incomplete: 0x%x", framebuffer_status);
    destroy_render_surface(surface);
    return NULL;
  }

  return surface;
}

static void
destroy_render_surface(struct render_surface *surface)
{
  /*
   * This helper can safely be called after partial setup or with no surface.
   */
  if (!surface) {
    return;
  }

  /*
   * Delete the OpenGL resources created around this axoverlay2 buffer.
   */
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
     * eglDestroyImageKHR is an EGL extension, so obtain its address from the
     * graphics driver at runtime just like eglCreateImageKHR.
     */
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

    if (destroy_image) {
      destroy_image(surface->display, surface->image);
    }
  }
  /*
   * Finally release the bookkeeping structure itself.
   */
  free(surface);
}

static void
destroy_render_surfaces(struct overlay_context *overlay)
{
  /*
   * Walk through every axoverlay2 buffer that was imported during the lifetime of this overlay.
   */
  struct render_surface *surface = overlay->surfaces;

  while (surface) {
    /*
     * Save the next pointer before destroying the current entry because the
     * current structure becomes invalid after free().
     */
    struct render_surface *next = surface->next;

    destroy_render_surface(surface);
    surface = next;
  }
  /*
   * The list is empty after every imported surface has been destroyed.
   */
  overlay->surfaces = NULL;
}

static void
remove_overlay(struct overlay_context *overlay)
{
  axo_err *error = NULL;

  /*
   * Forget any frame that was active when the video stream disappeared.
   */
  overlay->current_surface = NULL;
  overlay->current_buffer = NULL;

  /*
   * Destroy Quake's private render target and all imported axoverlay2 buffers before
   * removing the axoverlay2 overlay they belong to.
   */
  destroy_render_target(overlay);
  destroy_render_surfaces(overlay);

  /*
   * Remove the overlay from axoverlay2 if one currently exists.
   */
  if (overlay->overlay_id >= 0) {
    if (!axo_remove_overlay(overlay->overlay_id, &error)) {
      syslog(LOG_ERR,
             "Failed to remove overlay %d: %s",
             overlay->overlay_id,
             error ? axo_err_get_message(error) : "unknown error");
    }
    axo_err_clear(&error);
    overlay->overlay_id = -1;
  }

  /*
   * Release the detailed axoverlay2 buffer format saved when the overlay was created.
   */
  if (overlay->format) {
    axo_detailed_format_free(overlay->format);
    overlay->format = NULL;
  }

  /*
   * Reset all stream-specific values so the same overlay_context can be used
   * again if another video stream appears later.
   */
  overlay->stream_id = 0;
  overlay->width = 0;
  overlay->height = 0;
  overlay->full_width = 0;
  overlay->full_height = 0;
  overlay->frame_count = 0;
}
