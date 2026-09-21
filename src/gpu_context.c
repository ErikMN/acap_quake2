/*
 * Sets up the graphics connection Quake uses on the camera.
 * This lets the game use the GPU even though there is no desktop window.
 */

#include "gpu_context.h"

#include <stddef.h>
#include <syslog.h>

#include <GLES3/gl3.h>

bool
gpu_context_init(struct gpu_context *gpu)
{
  /*
   * Quake normally draws into a real window. There is no desktop window on the
   * camera, but the graphics driver still needs a small surface while the game
   * is running. The finished game image is sent to the video stream separately.
   */
  static const EGLint config_attributes[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE,
  };

  static const EGLint context_attributes[] = {
    EGL_CONTEXT_CLIENT_VERSION,
    3,
    EGL_NONE,
  };

  static const EGLint surface_attributes[] = {
    EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
  };

  EGLConfig config;
  EGLint num_configs;

  gpu->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  gpu->context = EGL_NO_CONTEXT;
  gpu->surface = EGL_NO_SURFACE;

  if (gpu->display == EGL_NO_DISPLAY) {
    syslog(LOG_ERR, "Failed to get EGL display");
    return false;
  }

  if (!eglInitialize(gpu->display, NULL, NULL)) {
    syslog(LOG_ERR, "Failed to initialize EGL");
    goto fail;
  }

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    syslog(LOG_ERR, "Failed to bind OpenGL ES");
    goto fail;
  }

  if (!eglChooseConfig(gpu->display, config_attributes, &config, 1, &num_configs) || num_configs < 1) {
    syslog(LOG_ERR, "Failed to choose EGL config");
    goto fail;
  }

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
   * Make this graphics setup active on the game thread. All later Quake drawing
   * uses this same setup until the application shuts down.
   */
  if (!eglMakeCurrent(gpu->display, gpu->surface, gpu->surface, gpu->context)) {
    syslog(LOG_ERR, "Failed to make EGL context current");
    goto fail;
  }

  syslog(LOG_INFO, "EGL vendor: %s", eglQueryString(gpu->display, EGL_VENDOR));
  syslog(LOG_INFO, "EGL version: %s", eglQueryString(gpu->display, EGL_VERSION));
  syslog(LOG_INFO, "GL vendor: %s", glGetString(GL_VENDOR));
  syslog(LOG_INFO, "GL renderer: %s", glGetString(GL_RENDERER));
  syslog(LOG_INFO, "GL version: %s", glGetString(GL_VERSION));
  syslog(LOG_INFO, "GLSL version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

  return true;

fail:
  gpu_context_destroy(gpu);
  return false;
}

void
gpu_context_destroy(struct gpu_context *gpu)
{
  if (gpu->display == EGL_NO_DISPLAY) {
    return;
  }

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
