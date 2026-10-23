/* model_gingerbread.c - Gingerbreadman map:
 *   x' = 1 - y + |x|
 *   y' = x
 */
#include "model_gingerbread.h"

#include <math.h>
#include <stddef.h>

static const ModelParamDef ginger_params[] = {
    { "x0", "x0", "", 0.0, -5.0, 5.0, 0.01, 1, 0 },
    { "y0", "y0", "", 0.0, -5.0, 5.0, 0.01, 1, 0 }
};

static const ModelChannelDef ginger_channels[] = {
    { "x",      "x",      "", -5.0, 5.0, 0 },
    { "y",      "y",      "", -5.0, 5.0, 0 },
    { "radius", "radius", "", 0.0, 8.0, 0 },
    { "abs_x",  "|x|",    "", 0.0, 5.0, 0 }
};

static void ginger_init(const Model *m, const double *vals, double state[MODEL_MAX_STATE])
{
    (void)m;
    state[0] = vals[0];
    state[1] = vals[1];
}

static void ginger_step(const Model *m, double state[MODEL_MAX_STATE],
                        const double *vals, double dt, int steps)
{
    int i;
    (void)m; (void)vals; (void)dt;
    for (i = 0; i < steps; ++i) {
        double x = 1.0 - state[1] + fabs(state[0]);
        double y = state[0];
        if (!(x == x) || !(y == y) || fabs(x) > 1e6 || fabs(y) > 1e6) {
            x = 0.0; y = 0.0;
        }
        state[0] = x;
        state[1] = y;
    }
}

static void ginger_observe(const Model *m, const double *state,
                           const double *vals, float ch[MODEL_MAX_CHANNELS])
{
    (void)m; (void)vals;
    ch[0] = (float)state[0];
    ch[1] = (float)state[1];
    ch[2] = (float)sqrt(state[0] * state[0] + state[1] * state[1]);
    ch[3] = (float)fabs(state[0]);
}

const Model GINGERBREAD_MODEL = {
    "gingerbread",
    "Gingerbreadman map",
    2,
    (int)(sizeof(ginger_params) / sizeof(ginger_params[0])),
    ginger_params,
    (int)(sizeof(ginger_channels) / sizeof(ginger_channels[0])),
    ginger_channels,
    { "x", "y", NULL, NULL },
    ginger_init,
    ginger_step,
    ginger_observe,
    NULL
};
