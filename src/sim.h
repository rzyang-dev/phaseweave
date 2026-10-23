/* sim.h - 相空间网格状态、线程池、Fortran/CPU 侧内核调度 */
#ifndef DP_SIM_H
#define DP_SIM_H

#include "core.h"

typedef struct Sim Sim;

Sim  *sim_create(const DpConfig *cfg, int threads, char *err, size_t errsz);
void  sim_destroy(Sim *s);

void  sim_reset(Sim *s, const DpConfig *cfg);
int   sim_step_cpu(Sim *s, int steps, const DpParams *p);
void  sim_refresh_field(Sim *s);
void  sim_refresh_rgba(Sim *s);
void  sim_set_lut(Sim *s, const unsigned char *lut);

float         *sim_state(Sim *s);
float         *sim_field(Sim *s);
unsigned char *sim_rgba(Sim *s);
int            sim_res(const Sim *s);
int            sim_cells(const Sim *s);
int            sim_threads(const Sim *s);
void           sim_read_state(const Sim *s, float *dst);
void           sim_write_state(Sim *s, const float *src);
double         sim_bench(Sim *s, int steps, const DpParams *p);

#endif
