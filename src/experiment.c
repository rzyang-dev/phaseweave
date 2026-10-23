/* experiment.c - double-pendulum experiment schema and axis mapping */
#include "experiment.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void add_param(Experiment *e, const char *name, const char *label, const char *unit,
                      double value, double min, double max, double step, int group)
{
    ExpParam *p;
    if (e->np >= EXP_MAX_PARAMS) return;
    p = &e->p[e->np++];
    memset(p, 0, sizeof(*p));
    strncpy(p->name, name, sizeof(p->name) - 1);
    strncpy(p->label, label, sizeof(p->label) - 1);
    strncpy(p->unit, unit, sizeof(p->unit) - 1);
    p->value = value;
    p->min = min; p->max = max; p->step = step;
    p->axis_ok = 1;
    p->group = group;
}

void exp_init(Experiment *e)
{
    if (!e) return;
    memset(e, 0, sizeof(*e));
    strcpy(e->title, "Double pendulum");
    strcpy(e->model, "double_pendulum");
    e->np = 0;
    add_param(e, "theta1", "theta1", "rad",  0.5, -DP_PI, DP_PI, 0.01, 0);
    add_param(e, "theta2", "theta2", "rad",  0.0, -DP_PI, DP_PI, 0.01, 0);
    add_param(e, "omega1", "omega1", "rad/s",0.0, -10.0, 10.0, 0.01, 0);
    add_param(e, "omega2", "omega2", "rad/s",0.0, -10.0, 10.0, 0.01, 0);
    add_param(e, "m1", "m1", "kg", 1.0, 0.2, 3.0, 0.01, 1);
    add_param(e, "m2", "m2", "kg", 1.0, 0.2, 3.0, 0.01, 1);
    add_param(e, "l1", "l1", "m",  1.0, 0.2, 2.0, 0.01, 1);
    add_param(e, "l2", "l2", "m",  1.0, 0.2, 2.0, 0.01, 1);
    add_param(e, "g",  "g",  "m/s2",9.81, 0.0, 20.0, 0.1, 1);
    add_param(e, "damping", "damping", "", 0.0, 0.0, 1.0, 0.01, 1);
    e->axis_x = 0;
    e->axis_y = 1;
    e->xmin = e->p[0].min; e->xmax = e->p[0].max;
    e->ymin = e->p[1].min; e->ymax = e->p[1].max;
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

void exp_cell_state(const Experiment *e, int ix, int iy, double state[4], double params[6])
{
    double vals[EXP_MAX_PARAMS];
    if (!e || !state || !params) return;
    exp_axis_values(e, ix, iy, vals);
    state[0] = vals[exp_index(e, "theta1")];
    state[1] = vals[exp_index(e, "theta2")];
    state[2] = vals[exp_index(e, "omega1")];
    state[3] = vals[exp_index(e, "omega2")];
    params[0] = vals[exp_index(e, "m1")];
    params[1] = vals[exp_index(e, "m2")];
    params[2] = vals[exp_index(e, "l1")];
    params[3] = vals[exp_index(e, "l2")];
    params[4] = vals[exp_index(e, "g")];
    params[5] = vals[exp_index(e, "damping")];
}

const char *exp_channel_name(int ch)
{
    switch (ch) {
    case 0: return "theta1";
    case 1: return "theta2";
    case 2: return "energy";
    case 3: return "omega_max";
    default: return "theta1";
    }
}

int exp_channel_info(const Experiment *e, int ch, ExpChannelInfo *out)
{
    if (!out) return 0;
    if (ch < 0 || ch >= EXP_NCH) ch = 0;
    out->cyclic = 0;
    if (ch == 0) { out->name="theta1"; out->label="theta1"; out->unit="rad"; out->min=-DP_PI; out->max=DP_PI; out->cyclic=1; }
    else if (ch == 1) { out->name="theta2"; out->label="theta2"; out->unit="rad"; out->min=-DP_PI; out->max=DP_PI; out->cyclic=1; }
    else if (ch == 2) { out->name="energy"; out->label="energy"; out->unit="J"; out->min=-50.0; out->max=50.0; }
    else { out->name="omega_max"; out->label="max |omega|"; out->unit="rad/s"; out->min=0.0; out->max=20.0; }
    (void)e;
    return 1;
}