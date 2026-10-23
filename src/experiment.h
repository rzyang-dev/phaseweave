/* experiment.h - 2D map/experiment description, independent of UI and backend */
#ifndef DP_EXPERIMENT_H
#define DP_EXPERIMENT_H

#include "core.h"
#include "model.h"
#include "schema.h"

#define EXP_MAX_PARAMS MODEL_MAX_PARAMS
#define EXP_NCH        MODEL_MAX_CHANNELS
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
    char name[EXP_NAME];
    char label[EXP_LABEL];
    char unit[16];
    double min, max;
    int cyclic;
} ExpChannel;

typedef struct {
    char title[80];
    char model[32];

    const Model *mdl;

    ExpParam p[EXP_MAX_PARAMS];
    int np;

    int axis_x, axis_y;
    double xmin, xmax, ymin, ymax;

    int res;
    double dt;
    int max_steps;
    int paused;

    int nch;
    ExpChannel ch[EXP_NCH];
    int channel;
    int post;           /* 0 raw, 1 gradient, 2 range, 3 variance, 4 FTLE, 5 escape */
    int export_scale;   /* PNG nearest-neighbour scale: 1/2/4 */
    int export_overlay; /* draw grid/ticks/selection into PNG */

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

void exp_init(Experiment *e, const Model *m);
int  exp_index(const Experiment *e, const char *name);
double exp_value(const Experiment *e, const char *name);
int  exp_set_value(Experiment *e, const char *name, double v);
int  exp_set_axis(Experiment *e, int which, const char *name);
void exp_set_range(Experiment *e, int which, double lo, double hi);
void exp_axis_values(const Experiment *e, int ix, int iy, double vals[EXP_MAX_PARAMS]);
void exp_cell_state(const Experiment *e, int ix, int iy, double state[MODEL_MAX_STATE],
                    double vals[EXP_MAX_PARAMS]);
const char *exp_channel_name(const Experiment *e, int ch);
int exp_channel_info(const Experiment *e, int ch, ExpChannelInfo *out);
int exp_apply_schema(Experiment *e, const Schema *s, int set_defaults);

#endif