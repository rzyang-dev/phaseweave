/* model_standard.c - Chirikov standard map:
 *   p' = p + k sin(theta)
 *   theta' = theta + p'  (mod 2*pi)
 */
#include "model_standard.h"
#include "physics.h"

#include <math.h>
#include <stddef.h>

#define STD_PI 3.14159265358979323846

static const ModelParamDef standard_params[] = {
    { "k",      "k",      "", 0.97, 0.0, 4.0, 0.01, 1, 0 },
    { "theta0", "theta0", "", 0.0, -STD_PI, STD_PI, 0.01, 0, 0 },
    { "p0",     "p0",     "", 0.0, -STD_PI, STD_PI, 0.01, 1, 0 }
};

static const ModelChannelDef standard_channels[] = {
    { "theta", "theta", "", -STD_PI, STD_PI, 1 },
    { "p",     "p",     "", -5.0, 5.0, 0 },
    { "abs_p", "|p|",   "", 0.0, 5.0, 0 },
    { "cos_t", "cos(theta)", "", -1.0, 1.0, 0 }
};

static void standard_init(const Model *m, const double *vals, double state[MODEL_MAX_STATE])
{
    (void)m;
    state[0] = vals[1]; /* theta0 */
    state[1] = vals[2]; /* p0 */
}

static void standard_step(const Model *m, double state[MODEL_MAX_STATE],
                          const double *vals, double dt, int steps)
{
    double k = vals[0];
    int i;
    (void)m; (void)dt;
    for (i = 0; i < steps; ++i) {
        double th = state[0];
        double p = state[1] + k * sin(th);
        th = th + p;
        dp_normalize_angle(&th);
        if (!(th == th) || !(p == p) || fabs(p) > 1e6) {
            th = 0.0; p = 0.0;
        }
        state[0] = th;
        state[1] = p;
    }
}

static void standard_observe(const Model *m, const double *state,
                             const double *vals, float ch[MODEL_MAX_CHANNELS])
{
    (void)m; (void)vals;
    ch[0] = (float)state[0];
    ch[1] = (float)state[1];
    ch[2] = (float)fabs(state[1]);
    ch[3] = (float)cos(state[0]);
}

const Model STANDARD_MODEL = {
    "standard",
    "Standard map",
    2,
    (int)(sizeof(standard_params) / sizeof(standard_params[0])),
    standard_params,
    (int)(sizeof(standard_channels) / sizeof(standard_channels[0])),
    standard_channels,
    { "theta", "p", NULL, NULL },
    standard_init,
    standard_step,
    standard_observe,
    NULL
};
