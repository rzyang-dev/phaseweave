/* sim.c - 网格状态 + 行带线程池 + Fortran 内核调用 + 着色字段 */
#include "sim.h"

#include "kernel_fortran.h"
#include "physics.h"
#include "pool.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct {
    Sim *sim;
    int steps;
    DpParams params;
} CpuJob;

struct Sim {
    int res;
    int cells;
    float *state;   /* cells*4 */
    float *field;   /* cells  u in [0,1) */
    unsigned char *rgba;
    unsigned char lut[DP_LUT_N * 3];
    ThreadPool *pool;
    CpuJob job;
};

static void sim_band(void *user, int y0, int y1)
{
    CpuJob *j = (CpuJob *)user;
    fortran_step_band(j->sim->state, j->sim->res, y0, y1, j->steps, &j->params);
}

static double qpc_now(void)
{
    LARGE_INTEGER f, c;
    static double freq = 0.0;
    if (freq == 0.0) {
        QueryPerformanceFrequency(&f);
        freq = (double)f.QuadPart;
    }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / freq;
}

Sim *sim_create(const DpConfig *cfg, int threads, char *err, size_t errsz)
{
    Sim *s;
    int res = cfg->grid.res;

    if (err && errsz) err[0] = 0;
    if (res < 16) res = 16;
    if (res > 1024) res = 1024;

    s = (Sim *)calloc(1, sizeof(Sim));
    if (!s) {
        if (err) snprintf(err, errsz, "sim: out of memory");
        return NULL;
    }
    s->res = res;
    s->cells = res * res;
    s->state = (float *)malloc((size_t)s->cells * 4u * sizeof(float));
    s->field = (float *)malloc((size_t)s->cells * sizeof(float));
    s->rgba  = (unsigned char *)malloc((size_t)s->cells * 4u);
    if (!s->state || !s->field || !s->rgba) {
        sim_destroy(s);
        if (err) snprintf(err, errsz, "sim: out of memory (%d x %d)", res, res);
        return NULL;
    }

    fortran_kernel_init();

    s->pool = pool_create(threads);
    if (!s->pool) {
        sim_destroy(s);
        if (err) snprintf(err, errsz, "sim: thread pool creation failed");
        return NULL;
    }

    dp_phase_lut(s->lut);
    sim_reset(s, cfg);
    return s;
}

void sim_destroy(Sim *s)
{
    if (!s) return;
    if (s->pool) pool_destroy(s->pool);
    free(s->state);
    free(s->field);
    free(s->rgba);
    free(s);
}

void sim_reset(Sim *s, const DpConfig *cfg)
{
    int x, y;
    double half = cfg->grid.range * 0.5;
    double denom = (s->res > 1) ? (double)(s->res - 1) : 1.0;
    if (!s) return;
    for (y = 0; y < s->res; ++y) {
        for (x = 0; x < s->res; ++x) {
            int i = y * s->res + x;
            s->state[i * 4 + 0] = (float)(-half + ((double)x / denom) * cfg->grid.range);
            s->state[i * 4 + 1] = (float)(-half + ((double)y / denom) * cfg->grid.range);
            s->state[i * 4 + 2] = (float)cfg->grid.omega1;
            s->state[i * 4 + 3] = (float)cfg->grid.omega2;
        }
    }
    sim_refresh_field(s);
    sim_refresh_rgba(s);
}

int sim_step_cpu(Sim *s, int steps, const DpParams *p)
{
    if (!s) return 0;
    if (steps > 0) {
        s->job.sim = s;
        s->job.steps = steps;
        s->job.params = *p;
        pool_run(s->pool, sim_band, &s->job, s->res);
    }
    sim_refresh_field(s);
    return steps;
}

void sim_refresh_field(Sim *s)
{
    int i;
    if (!s) return;
    for (i = 0; i < s->cells; ++i)
        s->field[i] = dp_field_value(s->state[i * 4 + 0], s->state[i * 4 + 1]);
}

void sim_refresh_rgba(Sim *s)
{
    if (!s) return;
    dp_colorize(s->field, s->cells, s->lut, s->rgba);
}

void sim_set_lut(Sim *s, const unsigned char *lut)
{
    if (!s || !lut) return;
    memcpy(s->lut, lut, DP_LUT_N * 3);
    sim_refresh_rgba(s);
}

float         *sim_state(Sim *s) { return s ? s->state : NULL; }
float         *sim_field(Sim *s) { return s ? s->field : NULL; }
unsigned char *sim_rgba(Sim *s)  { return s ? s->rgba : NULL; }
int            sim_res(const Sim *s) { return s ? s->res : 0; }
int            sim_cells(const Sim *s) { return s ? s->cells : 0; }
int            sim_threads(const Sim *s) { return s ? pool_threads(s->pool) : 0; }

void sim_read_state(const Sim *s, float *dst)
{
    if (!s || !dst) return;
    memcpy(dst, s->state, (size_t)s->cells * 4u * sizeof(float));
}

void sim_write_state(Sim *s, const float *src)
{
    if (!s || !src) return;
    memcpy(s->state, src, (size_t)s->cells * 4u * sizeof(float));
    sim_refresh_field(s);
}

double sim_bench(Sim *s, int steps, const DpParams *p)
{
    double t0, t1;
    if (!s) return 0.0;
    t0 = qpc_now();
    sim_step_cpu(s, steps, p);
    t1 = qpc_now();
    return (t1 - t0) * 1000.0;
}
