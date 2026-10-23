/* schema.h - experiment metadata exchanged between Lua and Experiment.
 * Names are matched against the selected built-in Model; this is a metadata/
 * axis override, not a replacement for the model's dynamics callbacks. */
#ifndef DP_SCHEMA_H
#define DP_SCHEMA_H

#include "model.h"

typedef struct {
    char name[24];
    char label[32];
    char unit[16];
    double def, min, max, step;
    int axis;
} SchemaParam;

typedef struct {
    char name[24];
    char label[32];
    char unit[16];
    double min, max;
    int cyclic;
} SchemaChannel;

typedef struct {
    char model[32];
    char title[80];
    char axis_x[24];
    char axis_y[24];
    int nparams;
    int nchannels;
    SchemaParam params[MODEL_MAX_PARAMS];
    SchemaChannel channels[MODEL_MAX_CHANNELS];
} Schema;

#endif
