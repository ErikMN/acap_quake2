#include <signal.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string.h>

#include "gpu_context.h"
#include "overlay.h"

#ifndef APP_NAME
#define APP_NAME "acap_quake2"
#endif

static volatile sig_atomic_t running = 1;

static void
handle_signal(int signal_number)
{
  (void)signal_number;
  running = 0;
}

int
main(void)
{
  openlog(APP_NAME, LOG_PID | LOG_CONS, LOG_USER);

  signal(SIGINT, handle_signal);
  signal(SIGTERM, handle_signal);

  syslog(LOG_INFO, "*** Starting %s", APP_NAME);

  struct gpu_context gpu;

  if (!gpu_context_init(&gpu)) {
    syslog(LOG_ERR, "Failed to initialize GPU");
    closelog();
    return 1;
  }

  struct overlay_context overlay;

  if (!overlay_context_init(&overlay)) {
    syslog(LOG_ERR, "Failed to initialize overlay system");
    gpu_context_destroy(&gpu);
    closelog();
    return 1;
  }

  struct pollfd poll_fd = {
    .fd = overlay_context_get_event_fd(&overlay),
    .events = POLLIN | POLLPRI,
  };

  while (running) {
    int ret = poll(&poll_fd, 1, 33);

    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }

      syslog(LOG_ERR, "poll failed: %s", strerror(errno));
      break;
    }

    if (ret > 0 && (poll_fd.revents & (POLLIN | POLLPRI))) {
      if (!overlay_context_process_events(&overlay)) {
        break;
      }
    }

    if (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
      syslog(LOG_ERR, "VDO event connection closed");
      break;
    }

    if (!overlay_context_render_frame(&overlay, &gpu)) {
      break;
    }
  }

  overlay_context_destroy(&overlay);
  gpu_context_destroy(&gpu);

  syslog(LOG_INFO, "*** Stopping %s", APP_NAME);
  closelog();

  return 0;
}
