/* model_glsl.h - built-in model compute shaders for the GPU atlas. */
#ifndef DP_MODEL_GLSL_H
#define DP_MODEL_GLSL_H

#include "experiment.h"
#include <stddef.h>

int model_builtin_glsl(const Experiment *e, char *out, size_t outsz,
                       char *err, size_t errsz);

#endif
