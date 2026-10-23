/* physics.c - 与原始程序逐式对应的 RK4 参考实现，以及 CPU 侧颜色映射 */
#include "physics.h"

#include <math.h>

void dp_config_defaults(DpConfig *c)
{
    if (!c) return;
    c->physics.m1 = 1.0;
    c->physics.m2 = 1.0;
    c->physics.l1 = 1.0;
    c->physics.l2 = 1.0;
    c->physics.g = 9.81;
    c->physics.damping = 0.1;
    c->grid.res = 256;
    c->grid.range = 4.0 * DP_PI;
    c->grid.omega1 = 10.0;
    c->grid.omega2 = 10.0;
    c->sim.dt = 0.1;
    c->sim.max_steps = 8;
    c->sim.threads = 0;
}

const char *dp_kernel_name(int kernel)
{
    return kernel == DP_KERNEL_GPU ? "gpu" : "cpu";
}

void dp_normalize_angle(double *angle)
{
    double a = *angle;
    a = fmod(a + DP_PI, DP_TWO_PI);
    if (a < 0.0) a += DP_TWO_PI;
    a -= DP_PI;
    *angle = a;
}

/* 单步 RK4。与原始 C++ 的 computeDerivatives/Update 完全同式，只是改用 double。 */
void dp_rk4_step(double state[4], const DpParams *p)
{
    double t1 = state[0], t2 = state[1], w1 = state[2], w2 = state[3];
    double k1[4], k2[4], k3[4], k4[4];
    double dt = p->dt;
    int i;

#define DERIV(T1, T2, W1, W2, K)                                                         \
    do {                                                                                 \
        double d  = (T1) - (T2);                                                         \
        double sd = sin(d), cd = cos(d);                                                 \
        double den = p->m1 + p->m2 * sd * sd;                                            \
        (K)[0] = (W1);                                                                   \
        (K)[1] = (W2);                                                                   \
        (K)[2] = (p->m2 * p->g * sin(T2) * cd                                          \
                  - p->m2 * sd * (p->l1 * (W1) * (W1) * cd + p->l2 * (W2) * (W2))      \
                  - (p->m1 + p->m2) * p->g * sin(T1)) / (p->l1 * den)                  \
                 - p->damping * (W1);                                                    \
        (K)[3] = ((p->m1 + p->m2) * (p->l1 * (W1) * (W1) * sd - p->g * sin(T2)          \
                                     + p->g * sin(T1) * cd)                             \
                  + p->m2 * p->l2 * (W2) * (W2) * sd * cd) / (p->l2 * den)              \
                 - p->damping * (W2);                                                    \
    } while (0)

    DERIV(t1, t2, w1, w2, k1);
    DERIV(t1 + 0.5 * dt * k1[0], t2 + 0.5 * dt * k1[1],
          w1 + 0.5 * dt * k1[2], w2 + 0.5 * dt * k1[3], k2);
    DERIV(t1 + 0.5 * dt * k2[0], t2 + 0.5 * dt * k2[1],
          w1 + 0.5 * dt * k2[2], w2 + 0.5 * dt * k2[3], k3);
    DERIV(t1 + dt * k3[0], t2 + dt * k3[1],
          w1 + dt * k3[2], w2 + dt * k3[3], k4);

    for (i = 0; i < 4; ++i) {
        double d = (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]) * dt / 6.0;
        switch (i) {
        case 0: state[0] += d; break;
        case 1: state[1] += d; break;
        case 2: state[2] += d; break;
        default: state[3] += d; break;
        }
    }
    dp_normalize_angle(&state[0]);
    dp_normalize_angle(&state[1]);
#undef DERIV
}

/* 相空间着色字段：与 GPU 计算着色器使用同一个表达式。
 * u = fract((theta1 + theta2) / (4*pi) + 0.5)，再查 Lua 生成的 1024 项调色板。 */
float dp_field_value(float theta1, float theta2)
{
    float u = (theta1 + theta2) * 0.07957747154594767f + 0.5f;
    u = u - floorf(u);
    if (!(u == u)) u = 0.5f;              /* NaN -> 中性色 */
    if (u < 0.0f) u = 0.0f;
    if (u >= 1.0f) u = 0.99999994f;
    return u;
}

static void hsv_to_rgb(double h, double s, double v, int *r, int *g, int *b)
{
    double c, x, m, rr, gg, bb;
    h = fmod(h, 360.0);
    if (h < 0.0) h += 360.0;
    h /= 60.0;
    c = v * s;
    x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
    rr = gg = bb = 0.0;
    if (h < 1.0)      { rr = c; gg = x; }
    else if (h < 2.0) { rr = x; gg = c; }
    else if (h < 3.0) { gg = c; bb = x; }
    else if (h < 4.0) { gg = x; bb = c; }
    else if (h < 5.0) { rr = x; bb = c; }
    else              { rr = c; bb = x; }
    m = v - c;
    *r = (int)((rr + m) * 255.0 + 0.5);
    *g = (int)((gg + m) * 255.0 + 0.5);
    *b = (int)((bb + m) * 255.0 + 0.5);
}

/* 没有 Lua 时的兜底调色板：原始程序的 HSV(hue, 0.8, 1.0)。 */
void dp_phase_lut(unsigned char *lut)
{
    int i;
    for (i = 0; i < DP_LUT_N; ++i) {
        int r, g, b;
        hsv_to_rgb((double)i * 360.0 / (double)DP_LUT_N, 0.8, 1.0, &r, &g, &b);
        lut[i * 3 + 0] = (unsigned char)r;
        lut[i * 3 + 1] = (unsigned char)g;
        lut[i * 3 + 2] = (unsigned char)b;
    }
}

void dp_colorize(const float *field, int count, const unsigned char *lut, unsigned char *rgba)
{
    int i;
    for (i = 0; i < count; ++i) {
        float u = field[i];
        int idx;
        if (!(u == u)) u = 0.5f;
        idx = (int)(u * (float)DP_LUT_N);
        if (idx < 0) idx = 0;
        else if (idx >= DP_LUT_N) idx = DP_LUT_N - 1;
        rgba[i * 4 + 0] = lut[idx * 3 + 0];
        rgba[i * 4 + 1] = lut[idx * 3 + 1];
        rgba[i * 4 + 2] = lut[idx * 3 + 2];
        rgba[i * 4 + 3] = 255;
    }
}

unsigned int dp_hash_bytes(const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned int h = 2166136261u;
    size_t i;
    for (i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}