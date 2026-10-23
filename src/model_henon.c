/* model_henon.c - Hénon map (a,b; x0,y0) */
#include "model.h"

#include <math.h>

static const ModelParamDef henon_params[] = {
    { "a",  "a",  "", 1.4,  0.0,  1.5, 0.001, 1, 1 },
    { "b",  "b",  "", 0.3, -1.0,  1.0, 0.001, 1, 1 },
    { "x0", "x0", "", 0.0, -2.0,  2.0, 0.001, 0, 1 },
    { "y0", "y0", "", 0.0, -2.0,  2.0, 0.001, 0, 1 },
};

static const ModelChannelDef henon_channels[] = {
    { "x",      "x",      "", -2.0, 2.0, 0 },
    { "y",      "y",      "", -2.0, 2.0, 0 },
    { "radius", "radius", "",  0.0, 3.0, 0 },
    { "x_abs",  "|x|",    "",  0.0, 2.0, 0 },
};

static void henon_init(const Model *m, const double *v, double state[MODEL_MAX_STATE])
{
    (void)m;
    state[0] = v[2];
    state[1] = v[3];
    state[2] = 0.0;
    state[3] = 0.0;
}

static void henon_step(const Model *m, double state[MODEL_MAX_STATE],
                       const double *v, double dt, int steps)
{
    double a = v[0], b = v[1];
    int i;
    (void)m; (void)dt;
    for (i = 0; i < steps; ++i) {
        double x = state[0], y = state[1];
        double xn = 1.0 - a * x * x + y;
        double yn = b * x;
        if (!(xn == xn) || !(yn == yn) ||
            fabs(xn) > 1e6 || fabs(yn) > 1e6) {
            xn = 0.0; yn = 0.0;
        }
        state[0] = xn;
        state[1] = yn;
    }
}

static void henon_channels_fn(const Model *m, const double *state,
                              const double *v, float ch[MODEL_MAX_CHANNELS])
{
    (void)m; (void)v;
    ch[0] = (float)state[0];
    ch[1] = (float)state[1];
    ch[2] = (float)sqrt(state[0] * state[0] + state[1] * state[1]);
    ch[3] = (float)fabs(state[0]);
}

const Model HENON_MODEL = {
    "henon",
    "Hénon map",
    2,
    (int)(sizeof(henon_params) / sizeof(henon_params[0])),
    henon_params,
    (int)(sizeof(henon_channels) / sizeof(henon_channels[0])),
    henon_channels,
    henon_init,
    henon_step,
    henon_channels_fn
};
