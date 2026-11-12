// Replaces source/Graphics/InitGL.h on the host: an off-screen 1280x720 pbuffer on Mesa's surfaceless EGL.
#pragma once
#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>
#include "Log.h"

static EGLDisplay s_display;
static EGLContext s_context;
static EGLSurface s_surface;

bool HostInitEgl(EGLDisplay* display, EGLContext* context, EGLSurface* surface);
void HostDeinitEgl(EGLDisplay display, EGLContext context, EGLSurface surface);
void HostSwapBuffers(void);

static bool initEgl(NWindow*) { return HostInitEgl(&s_display, &s_context, &s_surface); }
static void deinitEgl() { HostDeinitEgl(s_display, s_context, s_surface); }
#define eglSwapBuffers(display, surface) HostSwapBuffers()
