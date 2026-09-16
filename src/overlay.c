#include "overlay.h"

#include "gpu_context.h"

#include <stdint.h>
#include <syslog.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>

#include <axoverlay2.h>
#include <glib-object.h>
#include <vdo-error.h>
#include <vdo-stream.h>

static void remove_overlay(struct overlay_context *overlay);

static bool create_test_overlay(struct overlay_context *overlay, const struct gpu_context *gpu, unsigned stream_id);

static bool
render_red_frame(struct overlay_context *overlay, const struct gpu_context *gpu, unsigned width, unsigned height);

bool
overlay_context_init(struct overlay_context *overlay)
{
  axo_err *axo_error = NULL;
  GError *error = NULL;
  VdoMap *filter = NULL;

  overlay->event_stream = NULL;
  overlay->event_fd = -1;
  overlay->axo_started = false;
  overlay->format = NULL;
  overlay->overlay_id = -1;
  overlay->stream_id = 0;

  if (!axo_start(NULL, &axo_error)) {
    syslog(LOG_ERR, "Failed to start axoverlay2: %s", axo_err_get_message(axo_error));
    axo_err_clear(&axo_error);
    return false;
  }

  overlay->axo_started = true;

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
  syslog(LOG_INFO, "VDO event fd: %d", overlay->event_fd);

  return true;

fail:
  if (filter) {
    g_object_unref(filter);
  }

  overlay_context_destroy(overlay);

  return false;
}

void
overlay_context_destroy(struct overlay_context *overlay)
{
  remove_overlay(overlay);

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

int
overlay_context_get_event_fd(const struct overlay_context *overlay)
{
  return overlay->event_fd;
}

bool
overlay_context_process_events(struct overlay_context *overlay, const struct gpu_context *gpu)
{
  VdoStream *event_stream = overlay->event_stream;

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

    if (event_type == VDO_STREAM_EVENT_EXISTING || event_type == VDO_STREAM_EVENT_CREATED) {
      VdoStream *stream = vdo_stream_get(stream_id, &error);

      if (!stream) {
        syslog(LOG_ERR, "Failed to get VDO stream %u: %s", stream_id, error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(event);
        continue;
      }

      VdoMap *info = vdo_stream_get_info(stream, NULL);

      if (info) {
        unsigned width = vdo_map_get_uint32(info, "width", 0);
        unsigned height = vdo_map_get_uint32(info, "height", 0);

        syslog(LOG_INFO, "VDO stream %u available: %ux%u", stream_id, width, height);

        if (!create_test_overlay(overlay, gpu, stream_id)) {
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

      if (overlay->stream_id == stream_id) {
        remove_overlay(overlay);
      }
    }

    g_object_unref(event);
  }
}

static void
remove_overlay(struct overlay_context *overlay)
{
  axo_err *error = NULL;

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

  if (overlay->format) {
    axo_detailed_format_free(overlay->format);
    overlay->format = NULL;
  }

  overlay->stream_id = 0;
}

static bool
create_test_overlay(struct overlay_context *overlay, const struct gpu_context *gpu, unsigned stream_id)
{
  const unsigned used_width = 320;
  const unsigned used_height = 180;

  unsigned full_width;
  unsigned full_height;

  axo_err *error = NULL;
  axo_props *props = NULL;
  axo_match *match = NULL;

  if (overlay->overlay_id >= 0) {
    return true;
  }

  axo_detailed_format *format =
      axo_suggest_detailed_format(AXO_FORMAT_ARGB32, AXO_FORMAT_FLAGS_COMPRESSED | AXO_FORMAT_FLAGS_GPU, &error);

  if (!format) {
    syslog(LOG_ERR, "Failed to get GPU overlay format: %s", error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    return false;
  }

  axo_detailed_format_get_aligned_size(format, used_width, used_height, &full_width, &full_height);

  full_width = (full_width + 15) & ~15u;
  full_height = (full_height + 15) & ~15u;

  props = axo_props_new();
  match = axo_match_new();

  axo_props_set_detailed_format(props, format);
  axo_props_set_size(props, full_width, full_height);
  axo_props_set_manual_dma_sync(props, true);

  axo_match_stream_id(match, stream_id);

  int overlay_id = axo_create_overlay(props, match, &error);

  axo_props_free(props);
  axo_match_free(match);

  if (overlay_id < 0) {
    syslog(LOG_ERR, "Failed to create overlay: %s", error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    axo_detailed_format_free(format);
    return false;
  }

  overlay->overlay_id = overlay_id;
  overlay->stream_id = stream_id;
  overlay->format = format;

  syslog(LOG_INFO, "Created GPU overlay %d on stream %u: %ux%u", overlay_id, stream_id, full_width, full_height);

  if (!render_red_frame(overlay, gpu, full_width, full_height)) {
    remove_overlay(overlay);
    return false;
  }

  return true;
}

static bool
render_red_frame(struct overlay_context *overlay, const struct gpu_context *gpu, unsigned width, unsigned height)
{
  axo_err *error = NULL;
  axo_buffer *buffer = NULL;
  EGLImageKHR image = EGL_NO_IMAGE_KHR;
  GLuint texture = 0;
  GLuint framebuffer = 0;
  bool success = false;

  PFNEGLCREATEIMAGEKHRPROC create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");

  PFNEGLDESTROYIMAGEKHRPROC destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (!create_image || !destroy_image || !image_target_texture) {
    syslog(LOG_ERR, "Required EGL image extensions are unavailable");
    return false;
  }

  buffer = axo_get_buffer(overlay->overlay_id, NULL, &error);

  if (!buffer) {
    if (error && (axo_err_get_code(error) == AXO_ERR_WAIT || axo_err_get_code(error) == AXO_ERR_NO_STREAM)) {
      axo_err_clear(&error);
      return true;
    }

    syslog(LOG_ERR, "Failed to get overlay buffer: %s", error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    return false;
  }

  int dma_buf_fd = axo_buffer_get_dma_buf_fd(buffer);

  if (dma_buf_fd < 0) {
    syslog(LOG_ERR, "Overlay buffer has no DMA-BUF");
    return false;
  }

  axo_detailed_format *format = overlay->format;

  uint32_t fourcc = axo_detailed_format_get_drm_fourcc(format);
  uint64_t modifier = axo_detailed_format_get_drm_modifier(format, 0);

  EGLint attributes[] = {
    EGL_WIDTH,
    (EGLint)width,
    EGL_HEIGHT,
    (EGLint)height,
    EGL_LINUX_DRM_FOURCC_EXT,
    (EGLint)fourcc,
    EGL_DMA_BUF_PLANE0_FD_EXT,
    dma_buf_fd,
    EGL_DMA_BUF_PLANE0_OFFSET_EXT,
    0,
    EGL_DMA_BUF_PLANE0_PITCH_EXT,
    (EGLint)(width * 4),
    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
    (EGLint)(modifier & 0xffffffff),
    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
    (EGLint)(modifier >> 32),
    EGL_NONE,
  };

  image = create_image(gpu->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attributes);

  if (image == EGL_NO_IMAGE_KHR) {
    syslog(LOG_ERR, "Failed to import overlay DMA-BUF as EGLImage: 0x%x", eglGetError());
    goto out;
  }

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);

  image_target_texture(GL_TEXTURE_2D, image);

  GLenum gl_error = glGetError();

  if (gl_error != GL_NO_ERROR) {
    syslog(LOG_ERR, "Failed to bind EGLImage as texture: 0x%x", gl_error);
    goto out;
  }

  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

  GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
    syslog(LOG_ERR, "Overlay framebuffer incomplete: 0x%x", framebuffer_status);
    goto out;
  }

  glViewport(0, 0, (GLsizei)width, (GLsizei)height);

  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glFinish();

  if (!axo_submit_buffer(buffer, NULL, &error)) {
    syslog(LOG_ERR, "Failed to submit overlay buffer: %s", error ? axo_err_get_message(error) : "unknown error");
    axo_err_clear(&error);
    goto out;
  }

  syslog(LOG_INFO, "Submitted red GPU frame to overlay %d", overlay->overlay_id);

  success = true;

out:
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (framebuffer) {
    glDeleteFramebuffers(1, &framebuffer);
  }

  if (texture) {
    glDeleteTextures(1, &texture);
  }

  if (image != EGL_NO_IMAGE_KHR) {
    destroy_image(gpu->display, image);
  }

  return success;
}
