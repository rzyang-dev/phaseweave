/* experiment.c - parameter schema/axes for a selected Model */
#include "experiment.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void exp_init(Experiment *e, const Model *m)
{
    int i, ax = 0, ay = 1;
    if (!e) return;
    memset(e, 0, sizeof(*e));
    e->mdl = m ? m : &DP_MODEL;
    strncpy(e->model, e->mdl->id, sizeof(e->model) - 1);
    strncpy(e->title, e->mdl->title, sizeof(e->title) - 1);
    e->np = e->mdl->nparams;
    if (e->np > EXP_MAX_PARAMS) e->np = EXP_MAX_PARAMS;
    for (i = 0; i < e->np; ++i) {
        const ModelParamDef *d = &e->mdl->params[i];
        ExpParam *p = &e->p[i];
        memset(p, 0, sizeof(*p));
        strncpy(p->name, d->name, sizeof(p->name) - 1);
        strncpy(p->label, d->label, sizeof(p->label) - 1);
        strncpy(p->unit, d->unit, sizeof(p->unit) - 1);
        p->value = d->def;
        p->min = d->min; p->max = d->max; p->step = d->step;
        p->axis_ok = d->axis_ok;
        p->group = d->group;
    }
    e->nch = e->mdl->nchannels;
    if (e->nch > EXP_NCH) e->nch = EXP_NCH;

    for (i = 0; i < e->np; ++i) if (e->p[i].axis_ok) { ax = i; break; }
    for (i = ax + 1; i < e->np; ++i) if (e->p[i].axis_ok) { ay = i; break; }
    if (ay == ax) ay = (ax + 1 < e->np) ? ax + 1 : ax;
    e->axis_x = ax;
    e->axis_y = ay;
    e->xmin = e->p[ax].min; e->xmax = e->p[ax].max;
    e->ymin = e->p[ay].min; e->ymax = e->p[ay].max;

    e->res = 64;
    e->dt = 0.01;
    e->max_steps = 8;
    e->channel = 0;
    e->post = 0;
    e->grid = 1;
    e->periodic = 0;
    e->circle = 1;
    e->selected = -1;
    strcpy(e->palette, "phase");
}

int exp_index(const Experiment *e, const char *name)
{
    int i;
    if (!e || !name) return -1;
    for (i = 0; i < e->np; ++i) if (strcmp(e->p[i].name, name) == 0) return i;
    return -1;
}

double exp_value(const Experiment *e, const char *name)
{
    int i = exp_index(e, name);
    return i >= 0 ? e->p[i].value : 0.0;
}

int exp_set_value(Experiment *e, const char *name, double v)
{
    int i = exp_index(e, name);
    if (i < 0) return 0;
    if (v < e->p[i].min || v > e->p[i].max) return 0;
    e->p[i].value = v;
    return 1;
}

int exp_set_axis(Experiment *e, int which, const char *name)
{
    int i = exp_index(e, name);
    if (i < 0 || !e->p[i].axis_ok) return 0;
    if (which == 0) {
        if (i == e->axis_y) return 0;
        e->axis_x = i;
        e->xmin = e->p[i].min;
        e->xmax = e->p[i].max;
    } else {
        if (i == e->axis_x) return 0;
        e->axis_y = i;
        e->ymin = e->p[i].min;
        e->ymax = e->p[i].max;
    }
    return 1;
}

void exp_set_range(Experiment *e, int which, double lo, double hi)
{
    if (!e || hi <= lo) return;
    if (which == 0) { e->xmin = lo; e->xmax = hi; }
    else { e->ymin = lo; e->ymax = hi; }
}

void exp_axis_values(const Experiment *e, int ix, int iy, double vals[EXP_MAX_PARAMS])
{
    int i;
    double u, v;
    if (!e || !vals) return;
    for (i = 0; i < e->np; ++i) vals[i] = e->p[i].value;
    u = (e->res > 0) ? (((double)ix + 0.5) / (double)e->res) : 0.5;
    v = (e->res > 0) ? (((double)iy + 0.5) / (double)e->res) : 0.5;
    vals[e->axis_x] = e->xmin + u * (e->xmax - e->xmin);
    vals[e->axis_y] = e->ymin + v * (e->ymax - e->ymin);
}

void exp_cell_state(const Experiment *e, int ix, int iy, double state[MODEL_MAX_STATE],
                    double vals[EXP_MAX_PARAMS])
{
    if (!e || !state || !vals) return;
    exp_axis_values(e, ix, iy, vals);
    e->mdl->init(e->mdl, vals, state);
}

const char *exp_channel_name(const Experiment *e, int ch)
{
    if (!e || ch < 0 || ch >= e->nch) ch = 0;
    return e->mdl->channels[ch].name;
}

int exp_channel_info(const Experiment *e, int ch, ExpChannelInfo *out)
{
    const ModelChannelDef *d;
    if (!e || !out) return 0;
    if (ch < 0 || ch >= e->nch) ch = 0;
    d = &e->mdl->channels[ch];
    out->name = d->name;
    out->label = d->label;
    out->unit = d->unit;
    out->min = d->min;
    out->max = d->max;
    out->cyclic = d->cyclic;
    return 1;
}
