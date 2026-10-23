/* app.h - 应用编排：窗口 + 内核 + 热逻辑 + 帧循环 */
#ifndef DP_APP_H
#define DP_APP_H

#include "core.h"
#include <windows.h>

typedef struct {
    int force_kernel;    /* -1 = auto, DP_KERNEL_GPU / DP_KERNEL_CPU */
    int force_legacy;    /* 1 = 强制兼容上下文 */
    int headless;        /* 1 = 隐藏窗口（截图/测试） */
    int window_side;     /* 0 = 按桌面工作区自动取正方形 */
    int force_res;       /* 0 = 用 Lua 默认 */
    const char *capture_path;
    int capture_steps;
} AppOptions;

typedef struct App App;

App  *app_create(HINSTANCE inst, const AppOptions *opt, char *err, size_t errsz);
void  app_destroy(App *a);
int   app_frame(App *a);
int   app_loop(App *a);
void  app_key(App *a, int vk, int down);
int   app_shot(App *a, const char *path, int steps);
int   app_capture(App *a, const char *path, int steps);
const char *app_kernel_text(App *a);
const char *app_gpu_info(App *a);
int   app_res(App *a);

#endif