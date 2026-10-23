/* model_dp.c - double-pendulum ODE model and observables */
#include "model_dp.h"

#include "core.h"

#include "physics.h"

#include <math.h>

static const ModelParamDef dp_params[] = {
    { "theta1",  "theta1",  "rad",   0.5,   -DP_PI, DP_PI, 0.01, 0, 1 },
    { "theta2",  "theta2",  "rad",   0.0,   -DP_PI, DP_PI, 0.01, 0, 1 },
    { "omega1",  "omega1",  "rad/s", 0.0,  -10.0,  10.0,  0.01, 0, 1 },
    { "omega2",  "omega2",  "rad/s", 0.0,  -10.0,  10.0,  0.01, 0, 1 },
    { "m1",      "m1",      "kg",    1.0,    0.2,   3.0,  0.01, 1, 1 },
    { "m2",      "m2",      "kg",    1.0,    0.2,   3.0,  0.01, 1, 1 },
    { "l1",      "l1",      "m",     1.0,    0.2,   2.0,  0.01, 1, 1 },
    { "l2",      "l2",      "m",     1.0,    0.2,   2.0,  0.01, 1, 1 },
    { "g",       "g",       "m/s2",  9.81,   0.0,  20.0,  0.1,  1, 1 },
    { "damping", "damping", "",      0.0,    0.0,   1.0,  0.01, 1, 1 },
};

static const ModelChannelDef dp_channels[] = {
    { "theta1",   "theta1",   "rad",   -DP_PI, DP_PI, 1 },
    { "theta2",   "theta2",   "rad",   -DP_PI, DP_PI, 1 },
    { "energy",   "energy",   "J",     -50.0,  50.0,  0 },
    { "omega_max", "max |omega|", "rad/s", 0.0, 20.0, 0 },
};

static void dp_init(const Model *m, const double *v, double state[MODEL_MAX_STATE])
{
    (void)m;
    state[0] = v[0];
    state[1] = v[1];
    state[2] = v[2];
    state[3] = v[3];
}

static void dp_step(const Model *m, double state[MODEL_MAX_STATE],
                    const double *v, double dt, int steps)
{
    DpParams p;
    int i;
    (void)m;
    p.m1 = v[4]; p.m2 = v[5];
    p.l1 = v[6]; p.l2 = v[7];
    p.g = v[8]; p.damping = v[9];
    p.dt = dt > 0.0 ? dt : 0.01;
    for (i = 0; i < steps; ++i) {
        dp_rk4_step(state, &p);
        if (!(state[0] == state[0]) || !(state[1] == state[1]) ||
            !(state[2] == state[2]) || !(state[3] == state[3])) {
            state[0] = state[1] = state[2] = state[3] = 0.0;
        }
    }
}

static void dp_channels_fn(const Model *m, const double *state,
                           const double *v, float ch[MODEL_MAX_CHANNELS])
{
    DpParams p;
    (void)m;
    p.m1 = v[4]; p.m2 = v[5];
    p.l1 = v[6]; p.l2 = v[7];
    p.g = v[8]; p.damping = v[9]; p.dt = 0.0;
    ch[0] = (float)state[0];
    ch[1] = (float)state[1];
    ch[2] = (float)dp_energy(state, &p);
    ch[3] = (float)(fabs(state[2]) > fabs(state[3]) ? fabs(state[2]) : fabs(state[3]));
}

const Model DP_MODEL = {
    "double_pendulum",
    "Double pendulum",
    4,
    (int)(sizeof(dp_params) / sizeof(dp_params[0])),
    dp_params,
    (int)(sizeof(dp_channels) / sizeof(dp_channels[0])),
    dp_channels,
    { "theta1", "theta2", "omega1", "omega2" },
    dp_init,
    dp_step,
    dp_channels_fn
};