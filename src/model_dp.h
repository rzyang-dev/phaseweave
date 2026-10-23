/* model_dp.h - the built-in double-pendulum example model.
 * Kept separate from the Experiment/Atlas sampling layer so a second model
 * (logistic/Hénon) can be added without touching the UI or the map engine. */
#ifndef DP_MODEL_DP_H
#define DP_MODEL_DP_H

typedef struct {
    double theta1, theta2;
    double omega1, omega2;
    double m1, m2, l1, l2;
    double g, damping;
} DpModelValues;

void   model_dp_state(const DpModelValues *v, double state[4]);
void   model_dp_step(double state[4], const DpModelValues *v, double dt, int steps);
void   model_dp_channels(const double state[4], const DpModelValues *v, float ch[4]);
double model_dp_energy(const double state[4], const DpModelValues *v);

#endif
