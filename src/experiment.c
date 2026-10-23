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
    for (i = 0; i < e->nch; ++i) {
        const ModelChannelDef *d = &e->mdl->channels[i];
        strncpy(e->ch[i].name, d->name, sizeof(e->ch[i].name) - 1);
        strncpy(e->ch[i].label, d->label, sizeof(e->ch[i].label) - 1);
        strncpy(e->ch[i].unit, d->unit, sizeof(e->ch[i].unit) - 1);
        e->ch[i].min = d->min;
        e->ch[i].max = d->max;
        e->ch[i].cyclic = d->cyclic;
    }

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
    e->export_scale = 1;
    e->export_overlay = 1;
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
    if (!e || e->nch <= 0) return "";
    if (ch < 0 || ch >= e->nch) ch = 0;
    return e->ch[ch].name;
}

int exp_channel_info(const Experiment *e, int ch, ExpChannelInfo *out)
{
    const ExpChannel *d;
    if (!e || !out || e->nch <= 0) return 0;
    if (ch < 0 || ch >= e->nch) ch = 0;
    d = &e->ch[ch];
    out->name = d->name;
    out->label = d->label;
    out->unit = d->unit;
    out->min = d->min;
    out->max = d->max;
    out->cyclic = d->cyclic;
    return 1;
}

static void clamp_value(ExpParam *p)
{
    if (p->value < p->min) p->value = p->min;
    if (p->value > p->max) p->value = p->max;
}

int exp_apply_schema(Experiment *e, const Schema *s, int set_defaults)
{
    int i, k, applied = 0;
    if (!e || !s) return 0;
    if (s->title[0]) {
        strncpy(e->title, s->title, sizeof(e->title) - 1);
        e->title[sizeof(e->title) - 1] = 0;
    }
    for (i = 0; i < s->nparams; ++i) {
        const SchemaParam *sp = &s->params[i];
        int idx = exp_index(e, sp->name);
        ExpParam *p;
        if (idx < 0) continue;
        p = &e->p[idx];
        if (sp->label[0]) strncpy(p->label, sp->label, sizeof(p->label) - 1);
        if (sp->unit[0]) strncpy(p->unit, sp->unit, sizeof(p->unit) - 1);
        if (sp->max > sp->min) {
            p->min = sp->min;
            p->max = sp->max;
        }
        if (sp->step > 0.0) p->step = sp->step;
        p->axis_ok = sp->axis;
        if (set_defaults) p->value = sp->def;
        clamp_value(p);
        applied++;
    }
    for (i = 0; i < s->nchannels; ++i) {
        const SchemaChannel *sc = &s->channels[i];
        for (k = 0; k < e->nch; ++k) {
            if (strcmp(e->ch[k].name, sc->name) != 0) continue;
            if (sc->label[0]) strncpy(e->ch[k].label, sc->label, sizeof(e->ch[k].label) - 1);
            if (sc->unit[0]) strncpy(e->ch[k].unit, sc->unit, sizeof(e->ch[k].unit) - 1);
            if (sc->max > sc->min) {
                e->ch[k].min = sc->min;
                e->ch[k].max = sc->max;
            }
            e->ch[k].cyclic = sc->cyclic;
            applied++;
            break;
        }
    }
    if (set_defaults) {
        if (s->axis_x[0] && s->axis_y[0]) {
            int ax = exp_index(e, s->axis_x);
            int ay = exp_index(e, s->axis_y);
            if (ax >= 0 && ay >= 0 && ax != ay &&
                e->p[ax].axis_ok && e->p[ay].axis_ok) {
                e->axis_x = ax; e->axis_y = ay;
                e->xmin = e->p[ax].min; e->xmax = e->p[ax].max;
                e->ymin = e->p[ay].min; e->ymax = e->p[ay].max;
            }
        }
    } else {
        int ax = e->axis_x, ay = e->axis_y;
        if (!e->p[ax].axis_ok) {
            for (i = 0; i < e->np; ++i) if (e->p[i].axis_ok) { ax = i; break; }
        }
        if (!e->p[ay].axis_ok || ay == ax) {
            ay = -1;
            for (i = 0; i < e->np; ++i)
                if (e->p[i].axis_ok && i != ax) { ay = i; break; }
            if (ay < 0) ay = ax;
        }
        e->axis_x = ax; e->axis_y = ay;
        if (e->xmin < e->p[ax].min) e->xmin = e->p[ax].min;
        if (e->xmax > e->p[ax].max) e->xmax = e->p[ax].max;
        if (e->ymin < e->p[ay].min) e->ymin = e->p[ay].min;
        if (e->ymax > e->p[ay].max) e->ymax = e->p[ay].max;
    }
    return applied;
}