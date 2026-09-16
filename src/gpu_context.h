#pragma once

#include <EGL/egl.h>
#include <stdbool.h>

struct gpu_context {
  EGLDisplay display;
  EGLContext context;
  EGLSurface surface;
};

bool gpu_context_init(struct gpu_context *gpu);
void gpu_context_destroy(struct gpu_context *gpu);
