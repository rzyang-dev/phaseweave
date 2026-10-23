/* experiment.h - 2D map/experiment description, independent of UI and backend */
#ifndef DP_EXPERIMENT_H
#define DP_EXPERIMENT_H

#include "core.h"

#define EXP_MAX_PARAMS 12
#define EXP_NCH 4
#define EXP_NAME 24
#define EXP_LABEL 32

typedef struct {
    char name[EXP_NAME];
    char label[EXP_LABEL];
    char unit[16];
    double value;
    double min, max, step;
    int axis_ok;
    int group;          /* 0 = state, 1 = model */
} ExpParam;

typedef struct {
    char title[80];
    char model[32];

    ExpParam p[EXP_MAX_PARAMS];
    int np;

    int axis_x, axis_y;
    double xmin, xmax, ymin, ymax;

    int res;
    double dt;
    int max_steps;      /* integration steps per frame */
    int paused;

    int channel;        /* 0 theta1, 1 theta2, 2 energy, 3 omega_max */
    int post;           /* 0 raw, 1 gradient, 2 range, 3 variance */

    int grid, periodic, circle;
    int selected;
    int sel_x, sel_y;

    char palette[32];
} Experiment;

typedef struct {
    const char *name;
    const char *label;
    const char *unit;
    double min, max;
    int cyclic;
} ExpChannelInfo;

void exp_init(Experiment *e);
int  exp_index(const Experiment *e, const char *name);
double exp_value(const Experiment *e, const char *name);
int  exp_set_value(Experiment *e, const char *name, double v);
int  exp_set_axis(Experiment *e, int which, const char *name); /* which 0=x,1=y */
void exp_set_range(Experiment *e, int which, double lo, double hi);
void exp_axis_values(const Experiment *e, int ix, int iy, double vals[EXP_MAX_PARAMS]);
void exp_cell_state(const Experiment *e, int ix, int iy, double state[4], double params[6]);
const char *exp_channel_name(int ch);
int exp_channel_info(const Experiment *e, int ch, ExpChannelInfo *out);

#endif
