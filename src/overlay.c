#include "overlay.h"

#include <syslog.h>

#include <axoverlay2.h>
#include <glib-object.h>
#include <vdo-error.h>
#include <vdo-stream.h>

bool
overlay_context_init(struct overlay_context *overlay)
{
  axo_err *axo_error = NULL;
  GError *error = NULL;
  VdoMap *filter = NULL;

  overlay->event_stream = NULL;
  overlay->event_fd = -1;
  overlay->axo_started = false;

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
  if (overlay->event_stream) {
    g_object_unref(overlay->event_stream);
    overlay->event_stream = NULL;
  }

  overlay->event_fd = -1;

  if (overlay->axo_started) {
    axo_err *error = NULL;

    if (!axo_stop(&error)) {
      syslog(LOG_ERR, "Failed to stop axoverlay2: %s", error ? axo_err_get_message(error) : "unknown error");
    }

    axo_err_clear(&error);
    overlay->axo_started = false;
  }
}

int
overlay_context_get_event_fd(const struct overlay_context *overlay)
{
  return overlay->event_fd;
}

bool
overlay_context_process_events(struct overlay_context *overlay)
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

        g_object_unref(info);
      }

      g_object_unref(stream);
    } else if (event_type == VDO_STREAM_EVENT_CLOSED) {
      syslog(LOG_INFO, "VDO stream %u closed", stream_id);
    }

    g_object_unref(event);
  }
}
