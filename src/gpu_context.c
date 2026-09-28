/*
 * Creates the EGL and OpenGL ES context used by Quake on the camera.
 *
 * There is no desktop window, so a small off-screen EGL surface is used only to keep the graphics context active.
 *
 * Frames sent to the video stream use a separate framebuffer managed by overlay.c.
 */
#include "gpu_context.h"

#include <stddef.h>
#include <syslog.h>

#include <GLES3/gl3.h>

bool
gpu_context_init(struct gpu_context *gpu)
{
  /*
   * Quake normally gets its graphics setup from a desktop window created by SDL.
   * On the camera we create that graphics setup ourselves with EGL instead.
   *
   * The small 1x1 pbuffer is only used to give the OpenGL ES context a valid surface.
   * Quake does not render the game into this 1x1 surface.
   * The real game image is rendered into the framebuffer created by overlay.c.
   */

  /*
   * Ask EGL for a configuration that supports an off-screen pbuffer surface and an OpenGL ES 3 graphics context.
   */
  static const EGLint config_attributes[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE,
  };

  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION,
    3,
    EGL_NONE,
  };

  /*
   * The EGL surface itself only needs to exist so the graphics context can be active.
   * A single pixel is enough because the game is rendered somewhere else.
   */
  static const EGLint surface_attributes[] = {
    EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
  };

  EGLConfig config;

  EGLint num_configs;

  /*
   * Get the default EGL display provided by the camera graphics driver.
   * This is the connection EGL uses to access the GPU.
   */
  gpu->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

  /*
   * Start with invalid handles so cleanup is safe if initialization fails partway through.
   */
  gpu->context = EGL_NO_CONTEXT;
  gpu->surface = EGL_NO_SURFACE;

  if (gpu->display == EGL_NO_DISPLAY) {
    syslog(LOG_ERR, "Failed to get EGL display");
    return false;
  }

  /*
   * Initialize EGL before creating any graphics resources.
   * The EGL version numbers are not needed here, so NULL is passed for both outputs.
   */
  if (!eglInitialize(gpu->display, NULL, NULL)) {
    syslog(LOG_ERR, "Failed to initialize EGL");
    goto fail;
  }

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    syslog(LOG_ERR, "Failed to bind OpenGL ES");
    goto fail;
  }

  /*
   * Find an EGL configuration that supports the pbuffer and OpenGL ES 3 requirements above.
   * Only one matching configuration is needed.
   */
  if (!eglChooseConfig(gpu->display, config_attributes, &config, 1, &num_configs) || num_configs < 1) {
    syslog(LOG_ERR, "Failed to choose EGL config");
    goto fail;
  }

  /*
   * Create the OpenGL ES 3 context that Quake will use for all GPU rendering.
   * EGL_NO_CONTEXT means this context does not share resources with another context.
   */
  gpu->context = eglCreateContext(gpu->display, config, EGL_NO_CONTEXT, context_attributes);
  if (gpu->context == EGL_NO_CONTEXT) {
    syslog(LOG_ERR, "Failed to create EGL context");
    goto fail;
  }

  gpu->surface = eglCreatePbufferSurface(gpu->display, config, surface_attributes);
  if (gpu->surface == EGL_NO_SURFACE) {
    syslog(LOG_ERR, "Failed to create EGL surface");
    goto fail;
  }

  /*
   * Make the OpenGL ES context active on the current thread.
   * The same pbuffer is used as both the read surface and the draw surface.
   *
   * From this point on, Quake's OpenGL calls use this context until it is released during shutdown.
   * overlay.c later binds its own framebuffer before Quake starts drawing the actual game frame.
   */
  if (!eglMakeCurrent(gpu->display, gpu->surface, gpu->surface, gpu->context)) {
    syslog(LOG_ERR, "Failed to make EGL context current");
    goto fail;
  }

  /*
   * Log the graphics implementation in use.
   * These values are useful when checking which EGL and GPU driver the camera actually selected.
   */
  syslog(LOG_INFO, "EGL vendor: %s", eglQueryString(gpu->display, EGL_VENDOR));
  syslog(LOG_INFO, "EGL version: %s", eglQueryString(gpu->display, EGL_VERSION));
  syslog(LOG_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
  syslog(LOG_INFO, "GL renderer: %s", glGetString(GL_RENDERER));
  syslog(LOG_INFO, "GL version: %s", glGetString(GL_VERSION));
  syslog(LOG_INFO, "GLSL version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

  return true;

fail:
  /*
   * Use the normal shutdown path so resources created before the failure are cleaned up correctly.
   */
  gpu_context_destroy(gpu);
  return false;
}

void
gpu_context_destroy(struct gpu_context *gpu)
{
  if (gpu->display == EGL_NO_DISPLAY) {
    return;
  }

  /*
   * Release the context from the current thread before destroying the EGL resources.
   */
  eglMakeCurrent(gpu->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  if (gpu->surface != EGL_NO_SURFACE) {
    eglDestroySurface(gpu->display, gpu->surface);
  }

  if (gpu->context != EGL_NO_CONTEXT) {
    eglDestroyContext(gpu->display, gpu->context);
  }

  eglTerminate(gpu->display);

  gpu->display = EGL_NO_DISPLAY;
  gpu->context = EGL_NO_CONTEXT;
  gpu->surface = EGL_NO_SURFACE;
}
