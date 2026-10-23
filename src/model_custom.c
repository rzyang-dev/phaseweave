/* model_custom.c - compile Lua expression strings and expose them as a Model */
#include "model_custom.h"

#include "expr.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void vars_for(const CustomModel *cm, const double state[2], const double *vals, double *vars)
{
    int i;
    vars[0] = state[0];
    vars[1] = state[1];
    for (i = 0; i < cm->nparams; ++i) vars[2 + i] = vals ? vals[i] : 0.0;
}

static void custom_init(const Model *m, const double *vals, double state[MODEL_MAX_STATE])
{
    const CustomModel *cm = (const CustomModel *)m->user;
    double vars[EXPR_MAX_VARS];
    int i;
    for (i = 0; i < cm->nparams; ++i) vars[i] = vals ? vals[i] : 0.0;
    state[0] = expr_eval(&cm->init_x, vars);
    state[1] = expr_eval(&cm->init_y, vars);
    if (!(state[0] == state[0])) state[0] = 0.0;
    if (!(state[1] == state[1])) state[1] = 0.0;
    state[2] = state[3] = 0.0;
}

static void custom_step(const Model *m, double state[MODEL_MAX_STATE],
                        const double *vals, double dt, int steps)
{
    const CustomModel *cm = (const CustomModel *)m->user;
    double vars[EXPR_MAX_VARS];
    int i;
    (void)dt;
    for (i = 0; i < steps; ++i) {
        double x, y;
        vars_for(cm, state, vals, vars);
        x = expr_eval(&cm->step_x, vars);
        vars_for(cm, state, vals, vars);
        y = expr_eval(&cm->step_y, vars);
        if (!(x == x) || !(y == y) || fabs(x) > 1e12 || fabs(y) > 1e12) {
            x = 0.0; y = 0.0;
        }
        state[0] = x;
        state[1] = y;
    }
}

static void custom_observe(const Model *m, const double *state,
                           const double *vals, float ch[MODEL_MAX_CHANNELS])
{
    const CustomModel *cm = (const CustomModel *)m->user;
    double vars[EXPR_MAX_VARS];
    double st[2];
    int i;
    st[0] = state[0]; st[1] = state[1];
    vars_for(cm, st, vals, vars);
    for (i = 0; i < cm->nchannels; ++i) {
        double v = expr_eval(&cm->chan[i], vars);
        if (!(v == v)) v = 0.0;
        ch[i] = (float)v;
    }
}

int custom_model_build(CustomModel *cm, const CustomSpec *s, char *err, size_t errsz)
{
    const char *vinit[EXPR_MAX_VARS];
    const char *vstep[EXPR_MAX_VARS];
    char vbuf[EXPR_MAX_VARS][24];
    int i, nv;
    if (err && errsz) err[0] = 0;
    if (!cm || !s) return 0;
    memset(cm, 0, sizeof(*cm));
    if (s->schema.nparams < 1 || s->schema.nparams > MODEL_MAX_PARAMS) {
        snprintf(err, errsz, "custom model: bad parameter count");
        return 0;
    }
    if (s->schema.nchannels < 1 || s->schema.nchannels > MODEL_MAX_CHANNELS) {
        snprintf(err, errsz, "custom model: bad channel count");
        return 0;
    }
    cm->nparams = s->schema.nparams;
    cm->nchannels = s->schema.nchannels;
    strncpy(cm->title, s->schema.title[0] ? s->schema.title : "Lua expression map",
            sizeof(cm->title) - 1);
    strncpy(cm->state_names[0], s->state_names[0], sizeof(cm->state_names[0]) - 1);
    strncpy(cm->state_names[1], s->state_names[1], sizeof(cm->state_names[1]) - 1);
    for (i = 0; i < cm->nparams; ++i) {
        const SchemaParam *sp = &s->schema.params[i];
        strncpy(cm->pname[i], sp->name, sizeof(cm->pname[i]) - 1);
        strncpy(cm->plabel[i], sp->label, sizeof(cm->plabel[i]) - 1);
        strncpy(cm->punit[i], sp->unit, sizeof(cm->punit[i]) - 1);
        cm->p[i].name = cm->pname[i];
        cm->p[i].label = cm->plabel[i];
        cm->p[i].unit = cm->punit[i];
        cm->p[i].def = sp->def;
        cm->p[i].min = sp->min;
        cm->p[i].max = sp->max;
        cm->p[i].step = sp->step;
        cm->p[i].group = 0;
        cm->p[i].axis_ok = sp->axis;
    }
    for (i = 0; i < cm->nchannels; ++i) {
        const SchemaChannel *sc = &s->schema.channels[i];
        strncpy(cm->cname[i], sc->name, sizeof(cm->cname[i]) - 1);
        strncpy(cm->clabel[i], sc->label, sizeof(cm->clabel[i]) - 1);
        strncpy(cm->cunit[i], sc->unit, sizeof(cm->cunit[i]) - 1);
        cm->ch[i].name = cm->cname[i];
        cm->ch[i].label = cm->clabel[i];
        cm->ch[i].unit = cm->cunit[i];
        cm->ch[i].min = sc->min;
        cm->ch[i].max = sc->max;
        cm->ch[i].cyclic = sc->cyclic;
    }
    cm->model.id = "custom";
    cm->model.title = cm->title;
    cm->model.state_dim = 2;
    cm->model.nparams = cm->nparams;
    cm->model.params = cm->p;
    cm->model.nchannels = cm->nchannels;
    cm->model.channels = cm->ch;
    cm->model.state_names[0] = cm->state_names[0];
    cm->model.state_names[1] = cm->state_names[1];
    for (i = 2; i < MODEL_MAX_STATE; ++i) cm->model.state_names[i] = NULL;
    cm->model.init = custom_init;
    cm->model.step = custom_step;
    cm->model.observe = custom_observe;
    cm->model.user = cm;

    nv = 0;
    for (i = 0; i < cm->nparams; ++i) vinit[nv++] = cm->pname[i];
    if (!expr_compile(&cm->init_x, s->init_x, vinit, nv, err, errsz)) return 0;
    if (!expr_compile(&cm->init_y, s->init_y, vinit, nv, err, errsz)) return 0;

    nv = 0;
    vstep[nv++] = cm->state_names[0];
    vstep[nv++] = cm->state_names[1];
    for (i = 0; i < cm->nparams; ++i) vstep[nv++] = cm->pname[i];
    if (!expr_compile(&cm->step_x, s->step_x, vstep, nv, err, errsz)) return 0;
    if (!expr_compile(&cm->step_y, s->step_y, vstep, nv, err, errsz)) return 0;
    for (i = 0; i < cm->nchannels; ++i) {
        char fallback[32];
        const char *src = s->chan_expr[i][0] ? s->chan_expr[i] : NULL;
        if (!src) {
            snprintf(fallback, sizeof(fallback), "%s", i == 0 ? cm->state_names[0] :
                     (i == 1 ? cm->state_names[1] : "0"));
            src = fallback;
        }
        if (!expr_compile(&cm->chan[i], src, vstep, nv, err, errsz)) return 0;
    }
    (void)vbuf;
    return 1;
}
void custom_model_rebind(CustomModel *cm)
{
    int i;
    if (!cm) return;
    for (i = 0; i < cm->nparams; ++i) {
        cm->p[i].name = cm->pname[i];
        cm->p[i].label = cm->plabel[i];
        cm->p[i].unit = cm->punit[i];
    }
    for (i = 0; i < cm->nchannels; ++i) {
        cm->ch[i].name = cm->cname[i];
        cm->ch[i].label = cm->clabel[i];
        cm->ch[i].unit = cm->cunit[i];
    }
    cm->model.title = cm->title;
    cm->model.params = cm->p;
    cm->model.channels = cm->ch;
    cm->model.state_names[0] = cm->state_names[0];
    cm->model.state_names[1] = cm->state_names[1];
    for (i = 2; i < MODEL_MAX_STATE; ++i) cm->model.state_names[i] = NULL;
    cm->model.user = cm;
}
static int glsl_append(char *out, size_t outsz, size_t *pos, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (!out || !pos || *pos >= outsz) return 0;
    va_start(ap, fmt);
    n = vsnprintf(out + *pos, outsz - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= outsz - *pos) return 0;
    *pos += (size_t)n;
    return 1;
}

int custom_model_glsl(const CustomModel *cm, char *out, size_t outsz,
                      char *err, size_t errsz)
{
    char vbuf[2 + MODEL_MAX_PARAMS][64];
    const char *vars[2 + MODEL_MAX_PARAMS];
    char step_x[2048], step_y[2048];
    char chan[MODEL_MAX_CHANNELS][2048];
    size_t pos = 0;
    int i;
    if (err && errsz) err[0] = 0;
    if (!cm || !out || outsz == 0) return 0;
    if (cm->nparams < 0 || cm->nparams > MODEL_MAX_PARAMS) return 0;
    snprintf(vbuf[0], sizeof(vbuf[0]), "sx");
    snprintf(vbuf[1], sizeof(vbuf[1]), "sy");
    for (i = 0; i < cm->nparams; ++i)
        snprintf(vbuf[2 + i], sizeof(vbuf[2 + i]), "cells[i*16+%d]", 4 + i);
    vars[0] = vbuf[0]; vars[1] = vbuf[1];
    for (i = 0; i < cm->nparams; ++i) vars[2 + i] = vbuf[2 + i];
    if (!expr_to_glsl(&cm->step_x, vars, 2 + cm->nparams, step_x, sizeof(step_x), err, errsz)) return 0;
    if (!expr_to_glsl(&cm->step_y, vars, 2 + cm->nparams, step_y, sizeof(step_y), err, errsz)) return 0;
    for (i = 0; i < cm->nchannels; ++i) {
        if (!expr_to_glsl(&cm->chan[i], vars, 2 + cm->nparams, chan[i], sizeof(chan[i]), err, errsz))
            return 0;
    }
    if (!glsl_append(out, outsz, &pos, "#version 430\n"))
        return 0;
    glsl_append(out, outsz, &pos,
        "layout(local_size_x=1, local_size_y=1) in;\n"
        "layout(std430, binding=0) buffer Cells { float cells[]; };\n"
        "layout(std430, binding=1) buffer Chans { float chans[]; };\n"
        "uniform int uRes;\nuniform int uSteps;\n"
        "void main() {\n"
        "  int i = int(gl_GlobalInvocationID.x) + int(gl_GlobalInvocationID.y) * uRes;\n"
        "  float sx = cells[i*16+0];\n"
        "  float sy = cells[i*16+1];\n"
        "  for (int k=0; k<uSteps; ++k) {\n");
    glsl_append(out, outsz, &pos, "    float nx = %s;\n", step_x);
    glsl_append(out, outsz, &pos, "    float ny = %s;\n", step_y);
    glsl_append(out, outsz, &pos,
        "    sx = nx; sy = ny;\n"
        "    if (isnan(sx) || isnan(sy) || isinf(sx) || isinf(sy)) { sx = 0.0; sy = 0.0; }\n"
        "  }\n"
        "  cells[i*16+0] = sx; cells[i*16+1] = sy;\n");
    for (i = 0; i < cm->nchannels; ++i)
        glsl_append(out, outsz, &pos, "  chans[i*4+%d] = %s;\n", i, chan[i]);
    for (i = cm->nchannels; i < 4; ++i)
        glsl_append(out, outsz, &pos, "  chans[i*4+%d] = 0.0;\n", i);
    glsl_append(out, outsz, &pos, "}\n");
    if (pos >= outsz) {
        if (err) snprintf(err, errsz, "GLSL shader too long");
        return 0;
    }
    return 1;
}