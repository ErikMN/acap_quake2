#include <signal.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>

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

  while (running) {
    pause();
  }

  syslog(LOG_INFO, "Stopping %s", APP_NAME);

  closelog();

  return 0;
}
