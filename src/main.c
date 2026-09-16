#include <signal.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>

#include "gpu_context.h"

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

  syslog(LOG_INFO, "Starting %s", APP_NAME);

  struct gpu_context gpu;

  if (!gpu_context_init(&gpu)) {
    syslog(LOG_ERR, "Failed to initialize GPU");
    closelog();
    return 1;
  }

  while (running) {
    pause();
  }

  gpu_context_destroy(&gpu);

  syslog(LOG_INFO, "Stopping %s", APP_NAME);
  closelog();

  return 0;
}
