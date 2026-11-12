// Host shim: Mesa exports every core entry point, so no loader is needed.
#pragma once
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
static inline int gladLoadGL(void) { return 1; }
