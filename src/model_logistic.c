/* model_logistic.c - logistic map x <- r*x*(1-x), with finite-time Lyapunov */
#include "model.h"

#include <stddef.h>

#include <math.h>

static const ModelParamDef logistic_params[] = {
    { "r",  "r",  "",  3.7, 0.0, 4.0, 0.001, 1, 1 },
    { "x0", "x0", "",  0.2, 0.0, 1.0, 0.001, 0, 1 },
};

static const ModelChannelDef logistic_channels[] = {
    { "x",        "x",        "", 0.0, 1.0, 0 },
    { "lyapunov", "lyapunov", "", -2.0, 2.0, 0 },
    { "mean",     "mean x",   "", 0.0, 1.0, 0 },
    { "x_abs",    "|x|",      "", 0.0, 1.0, 0 },
};

static void logistic_init(const Model *m, const double *v, double state[MODEL_MAX_STATE])
{
    (void)m;
    state[0] = v[1];
    state[1] = 0.0;   /* sum log|f'| */
    state[2] = 0.0;   /* sum x */
    state[3] = 0.0;   /* count */
}

static void logistic_step(const Model *m, double state[MODEL_MAX_STATE],
                          const double *v, double dt, int steps)
{
    double r = v[0];
    int i;
    (void)m; (void)dt;
    for (i = 0; i < steps; ++i) {
        double x = state[0];
        double d = fabs(r * (1.0 - 2.0 * x));
        if (d > 1e-12) state[1] += log(d);
        state[2] += x;
        state[3] += 1.0;
        x = r * x * (1.0 - x);
        if (!(x == x) || x < 0.0 || x > 1.0) x = 0.0;
        state[0] = x;
    }
}

static void logistic_channels_fn(const Model *m, const double *state,
                                 const double *v, float ch[MODEL_MAX_CHANNELS])
{
    double n = state[3] > 0.0 ? state[3] : 1.0;
    (void)m; (void)v;
    ch[0] = (float)state[0];
    ch[1] = (float)(state[1] / n);
    ch[2] = (float)(state[2] / n);
    ch[3] = (float)fabs(state[0]);
}

const Model LOGISTIC_MODEL = {
    "logistic",
    "Logistic map",
    1,
    (int)(sizeof(logistic_params) / sizeof(logistic_params[0])),
    logistic_params,
    (int)(sizeof(logistic_channels) / sizeof(logistic_channels[0])),
    logistic_channels,
    { "x", NULL, NULL, NULL },
    logistic_init,
    logistic_step,
    logistic_channels_fn
};