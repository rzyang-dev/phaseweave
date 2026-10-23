/* kernel_fortran.h - Fortran 内核的 C 侧适配层 */
#ifndef DP_KERNEL_FORTRAN_H
#define DP_KERNEL_FORTRAN_H

#include "core.h"

void fortran_kernel_init(void);
void fortran_step_band(float *state, int n, int y0, int y1, int steps, const DpParams *p);

#endif
