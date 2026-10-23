/* model_dp.c - double-pendulum dynamics and observables (example model) */
#include "model_dp.h"

#include "physics.h"

#include <math.h>

void model_dp_state(const DpModelValues *v, double state[4])
{
    state[0] = v->theta1;
    state[1] = v->theta2;
    state[2] = v->omega1;
    state[3] = v->omega2;
}

void model_dp_step(double state[4], const DpModelValues *v, double dt, int steps)
{
    DpParams p;
    int i;
    p.m1 = v->m1; p.m2 = v->m2;
    p.l1 = v->l1; p.l2 = v->l2;
    p.g = v->g; p.damping = v->damping;
    p.dt = dt > 0.0 ? dt : 0.01;
    for (i = 0; i < steps; ++i) {
        dp_rk4_step(state, &p);
        if (!(state[0] == state[0]) || !(state[1] == state[1]) ||
            !(state[2] == state[2]) || !(state[3] == state[3])) {
            state[0] = state[1] = state[2] = state[3] = 0.0;
        }
    }
}

double model_dp_energy(const double state[4], const DpModelValues *v)
{
    DpParams p;
    p.m1 = v->m1; p.m2 = v->m2;
    p.l1 = v->l1; p.l2 = v->l2;
    p.g = v->g; p.damping = v->damping; p.dt = 0.0;
    return dp_energy(state, &p);
}

void model_dp_channels(const double state[4], const DpModelValues *v, float ch[4])
{
    ch[0] = (float)state[0];
    ch[1] = (float)state[1];
    ch[2] = (float)model_dp_energy(state, v);
    ch[3] = (float)(fabs(state[2]) > fabs(state[3]) ? fabs(state[2]) : fabs(state[3]));
}
