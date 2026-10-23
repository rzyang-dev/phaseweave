/* core.h - 全局类型与默认配置（不依赖任何 Windows 头文件） */
#ifndef DP_CORE_H
#define DP_CORE_H

#include <stddef.h>

#define DP_PI        3.141592653589793238462643383279
#define DP_TWO_PI    6.283185307179586476925286766559
#define DP_HALF_PI   1.570796326794896619231321691640
#define DP_LUT_N     1024
#define DP_MAX_THREADS 16
#define DP_MAX_STEPS 32

/* 双摆物理参数。单位：kg / m / s */
typedef struct {
    double m1, m2;
    double l1, l2;
    double g;
    double damping;
} DpPhysics;

/* 相空间网格：theta1 沿 x，theta2 沿 y，范围 [-range/2, range/2] */
typedef struct {
    int    res;          /* 每边格数（正方形） */
    double range;        /* 角度范围，默认 4*pi */
    double omega1;
    double omega2;
} DpGridConfig;

typedef struct {
    double dt;           /* 固定步长（秒） */
    int    max_steps;    /* 每帧最多推进的子步数 */
    int    threads;      /* CPU 内核线程数，0 = 自动 */
} DpSimConfig;

typedef struct {
    DpPhysics   physics;
    DpGridConfig grid;
    DpSimConfig sim;
} DpConfig;

/* 内核每次调用真正需要的参数（热逻辑每帧推送） */
typedef struct {
    double m1, m2, l1, l2, g, damping, dt;
} DpParams;

enum { DP_KERNEL_GPU = 0, DP_KERNEL_CPU = 1 };

void dp_config_defaults(DpConfig *cfg);
const char *dp_kernel_name(int kernel);
static __inline__ int dp_clampi(int v, int lo, int hi) __attribute__((unused));
static __inline__ int dp_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

#endif