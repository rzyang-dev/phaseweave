/* kernel_fortran.c - 与 fortran/kernel.f90 的 bind(C) 符号对接。
 * 这一层刻意保持极薄：C 负责线程/内存/调度，Fortran 只拿到一段行带和参数。 */
#include "kernel_fortran.h"

/* 符号来自 fortran/kernel.f90：
 *   subroutine dp_step_band(state, n, y0, y1, steps, m1, m2, l1, l2, g, damp, dt) bind(C)
 *   subroutine dp_kernel_init() bind(C)
 */
extern void dp_kernel_init(void);
extern void dp_step_band(float *state, int n, int y0, int y1, int steps,
                         double m1, double m2, double l1, double l2,
                         double g, double damp, double dt);

static int g_fortran_ready = 0;

void fortran_kernel_init(void)
{
    if (!g_fortran_ready) {
        dp_kernel_init();
        g_fortran_ready = 1;
    }
}

void fortran_step_band(float *state, int n, int y0, int y1, int steps, const DpParams *p)
{
    if (!state || n <= 0 || y1 <= y0 || steps <= 0) return;
    dp_step_band(state, n, y0, y1, steps,
                 p->m1, p->m2, p->l1, p->l2, p->g, p->damping, p->dt);
}
