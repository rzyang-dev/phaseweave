/* gpu.h - OpenGL 4.3 计算着色器 + 现代/兼容两条显示路径 */
#ifndef DP_GPU_H
#define DP_GPU_H

#include "core.h"
#include <windows.h>

typedef struct Gpu Gpu;

/* force_legacy: 跳过 4.3 core，直接建普通上下文（用于验证回退路径） */
Gpu *gpu_create(HWND hwnd, HDC hdc, int force_legacy, char *err, size_t errsz);
void gpu_destroy(Gpu *g);

int  gpu_compute(Gpu *g);       /* 1 = 计算着色器可用 */
int  gpu_legacy(Gpu *g);
int  gpu_vsync(Gpu *g);
int  gpu_res(Gpu *g);

void gpu_set_res(Gpu *g, int res);
void gpu_set_lut(Gpu *g, const unsigned char *lut);
int  gpu_upload_state(Gpu *g, const float *state, int res);
int  gpu_download_state(Gpu *g, float *state, int res);
void gpu_step(Gpu *g, int steps, const DpParams *p);
void gpu_upload_field(Gpu *g, const float *field, int res);
void gpu_upload_rgba_legacy(Gpu *g, const unsigned char *rgba, int res);
void gpu_draw(Gpu *g, int w, int h);
int  gpu_read_field(Gpu *g, float *field, int res);
int  gpu_capture_rgba(Gpu *g, int w, int h, unsigned char *out_topdown);
int  gpu_capture_offscreen(Gpu *g, int w, int h, unsigned char *out_topdown);

const char *gpu_version(Gpu *g);
const char *gpu_renderer(Gpu *g);
const char *gpu_fallback_reason(Gpu *g);

#endif