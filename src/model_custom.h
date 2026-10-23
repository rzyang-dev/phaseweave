/* model_custom.h - Lua-described 2D expression map, compiled to CPU bytecode. */
#ifndef DP_MODEL_CUSTOM_H
#define DP_MODEL_CUSTOM_H

#include "model.h"
#include "schema.h"
#include "expr.h"

typedef struct {
    Model model;
    ModelParamDef p[MODEL_MAX_PARAMS];
    ModelChannelDef ch[MODEL_MAX_CHANNELS];
    char pname[MODEL_MAX_PARAMS][24];
    char plabel[MODEL_MAX_PARAMS][32];
    char punit[MODEL_MAX_PARAMS][16];
    char cname[MODEL_MAX_CHANNELS][24];
    char clabel[MODEL_MAX_CHANNELS][32];
    char cunit[MODEL_MAX_CHANNELS][16];
    char state_names[2][24];
    char title[80];
    Expr init_x, init_y;
    Expr step_x, step_y;
    Expr chan[MODEL_MAX_CHANNELS];
    int nparams;
    int nchannels;
} CustomModel;

int  custom_model_build(CustomModel *cm, const CustomSpec *s, char *err, size_t errsz);
void custom_model_rebind(CustomModel *cm);
int  custom_model_glsl(const CustomModel *cm, char *out, size_t outsz,
                       char *err, size_t errsz);

#endif