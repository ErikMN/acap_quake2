#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#ifndef APP_NAME
#define APP_NAME "acap_quake2"
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool
get_package_dir(char *path, size_t size)
{
  ssize_t length = readlink("/proc/self/exe", path, size - 1);

  if (length < 0 || (size_t)length >= size - 1) {
    return false;
  }

  path[length] = '\0';

  char *slash = strrchr(path, '/');

  if (!slash) {
    return false;
  }

  *slash = '\0';
  return true;
}

static bool
make_path(char *path, size_t size, const char *dir, const char *name)
{
  int length = snprintf(path, size, "%s/%s", dir, name);
  return length >= 0 && (size_t)length < size;
}

int
main(void)
{
  char package_dir[PATH_MAX];
  char localdata_dir[PATH_MAX];
  char lib_dir[PATH_MAX];
  char quake2_path[PATH_MAX];

  openlog(APP_NAME, LOG_PID | LOG_CONS, LOG_USER);

  if (!get_package_dir(package_dir, sizeof(package_dir))) {
    syslog(LOG_ERR, "Failed to determine package directory: %s", strerror(errno));
    return 1;
  }

  if (!make_path(localdata_dir, sizeof(localdata_dir), package_dir, "localdata") ||
      !make_path(lib_dir, sizeof(lib_dir), package_dir, "lib") ||
      !make_path(quake2_path, sizeof(quake2_path), package_dir, "quake2")) {
    syslog(LOG_ERR, "Package path is too long");
    return 1;
  }

  if (mkdir(localdata_dir, 0755) < 0 && errno != EEXIST) {
    syslog(LOG_ERR, "Failed to create %s: %s", localdata_dir, strerror(errno));
    return 1;
  }

  if (setenv("HOME", localdata_dir, 1) < 0 ||
      setenv("LD_LIBRARY_PATH", lib_dir, 1) < 0 ||
      setenv("SDL_VIDEODRIVER", "dummy", 1) < 0 ||
      setenv("SDL_AUDIODRIVER", "dummy", 1) < 0) {
    syslog(LOG_ERR, "Failed to configure runtime environment: %s", strerror(errno));
    return 1;
  }

  if (chdir(package_dir) < 0) {
    syslog(LOG_ERR, "Failed to change directory to %s: %s", package_dir, strerror(errno));
    return 1;
  }

  char *const args[] = {
    quake2_path,
    "-datadir",
    package_dir,
    "+set",
    "vid_ref",
    "gles3",
    "+set",
    "vid_fullscreen",
    "0",
    "+set",
    "r_msaa_samples",
    "0",
    "+set",
    "r_vsync",
    "0",
    "+map",
    "q2dm1",
    NULL,
  };

  syslog(LOG_INFO, "*** Starting Yamagi Quake II");
  execv(quake2_path, args);

  syslog(LOG_ERR, "Failed to start %s: %s", quake2_path, strerror(errno));
  return 1;
}
