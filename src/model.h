/* model.h - generic dynamical-system model interface.
 *
 * A model owns its parameter definitions, state-vector size and observables;
 * the Experiment/Atlas/UI layers only see this table plus callbacks. */
#ifndef DP_MODEL_H
#define DP_MODEL_H

#define MODEL_MAX_PARAMS   12
#define MODEL_MAX_CHANNELS 4
#define MODEL_MAX_STATE    4

typedef struct {
    const char *name;
    const char *label;
    const char *unit;
    double def;
    double min, max, step;
    int group;      /* 0 = state/initial, 1 = model parameter */
    int axis_ok;
} ModelParamDef;

typedef struct {
    const char *name;
    const char *label;
    const char *unit;
    double min, max;
    int cyclic;
} ModelChannelDef;

typedef struct Model {
    const char *id;
    const char *title;
    int state_dim;
    int nparams;
    const ModelParamDef *params;
    int nchannels;
    const ModelChannelDef *channels;
    const char *state_names[MODEL_MAX_STATE];

    void (*init)(const struct Model *m, const double *vals, double state[MODEL_MAX_STATE]);
    void (*step)(const struct Model *m, double state[MODEL_MAX_STATE],
                 const double *vals, double dt, int steps);
    void (*observe)(const struct Model *m, const double *state,
                    const double *vals, float ch[MODEL_MAX_CHANNELS]);
    void *user;   /* model-private data (custom/expression model) */
} Model;

extern const Model DP_MODEL;
extern const Model LOGISTIC_MODEL;
extern const Model HENON_MODEL;

const Model *model_by_id(const char *id);
int          model_count(void);
const Model *model_at(int index);

#endif