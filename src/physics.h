/* physics.h - C 参考内核、颜色映射与哈希 */
#ifndef DP_PHYSICS_H
#define DP_PHYSICS_H

#include "core.h"

void dp_normalize_angle(double *angle);
void dp_rk4_step(double state[4], const DpParams *p);
float dp_field_value(float theta1, float theta2);
void dp_phase_lut(unsigned char *lut);
void dp_colorize(const float *field, int count, const unsigned char *lut, unsigned char *rgba);
unsigned int dp_hash_bytes(const void *data, size_t size);

#endif