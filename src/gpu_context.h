#pragma once

/*
 * EGL provides the display, context and surface types used by gpu_context.
 */
#include <EGL/egl.h>

#include <stdbool.h>

/*
 * Holds the EGL objects used by Quake for GPU rendering.
 *
 * The display represents the connection to the camera graphics system.
 * The context stores the OpenGL ES state used by Quake.
 * The surface is the small off-screen pbuffer used to keep that context active.
 *
 * The actual game image is rendered into a separate framebuffer managed by overlay.c.
 */
struct gpu_context {
  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
};

/*
 * Create the EGL display, OpenGL ES context and small off-screen surface used by Quake.
 *
 * Returns true when the graphics context is ready for use.
 * Returns false if any part of the setup fails.
 */
bool gpu_context_init(struct gpu_context *gpu);

/*
 * Release all EGL resources created by gpu_context_init().
 *
 * It is safe to call this after a partial initialization because gpu_context_init()
 * keeps unused handles in their EGL_NO_* state.
 */
void gpu_context_destroy(struct gpu_context *gpu);
