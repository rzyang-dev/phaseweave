/* main.c - WinMain、命令行与运行模式。
 *
 *   dp-phase.exe                              窗口模式（GPU 默认，自动回退）
 *   dp-phase.exe /cpu                         强制 Fortran 内核
 *   dp-phase.exe /legacy                      强制兼容 GL 上下文（验证回退）
 *   dp-phase.exe /selftest                    无窗口自测（CPU/Lua/BMP）
 *   dp-phase.exe /gpu-selftest                + GPU 计算管线自测
 *   dp-phase.exe /shot out.bmp [/cpu|/gpu] [/res N] [/steps N]
 *   dp-phase.exe /capture out.bmp [/size N] [/steps N] [/cpu|/gpu] [/legacy]
 */
#include "app.h"
#include "platform.h"
#include "selftest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum {
    MODE_GUI = 0,
    MODE_SELFTEST,
    MODE_GPUSELFTEST,
    MODE_SHOT,
    MODE_CAPTURE
};

static int ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

static int parse_args(char *buf, char **argv, int max)
{
    int n = 0;
    char *p = buf;
    while (*p && n < max) {
        char *start, *w;
        int quoted = 0;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        start = w = p;
        while (*p) {
            char c = *p++;
            if (c == '"') { quoted = !quoted; continue; }
            if (!quoted && (c == ' ' || c == '\t')) break;
            *w++ = c;
        }
        *w = 0;
        argv[n++] = start;
    }
    return n;
}

static void usage(void)
{
    printf("Dynamical Systems Atlas\n\n");
    printf("  dp-phase.exe                                 window (GPU first, Fortran fallback)\n");
    printf("  dp-phase.exe /cpu                            force the Fortran CPU kernel\n");
    printf("  dp-phase.exe /legacy                         force a compatibility GL context\n");
    printf("  dp-phase.exe /selftest                       headless self-test\n");
    printf("  dp-phase.exe /gpu-selftest                   self-test + GPU compute check\n");
    printf("  dp-phase.exe /shot out.bmp [/cpu|/gpu] [/res N] [/steps N]\n");
    printf("  dp-phase.exe /capture out.bmp [/size N] [/steps N] [/cpu|/gpu] [/legacy]\n");
    printf("  dp-phase.exe /shot out.bmp /axisx m1 /axisy m2 /channel energy /post range /set theta1=0.5\n\n");
    printf("  in the window:  R reset   Space pause   C palette   K GPU/CPU   F5 reload hot.lua   Esc quit\n");
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    char *buf;
    char *argv[64];
    int argc, i, mode = MODE_GUI;
    AppOptions opt;
    char err[1024];
    App *app;
    int rc = 0;

    (void)prev;
    (void)cmdline;
    (void)show;

    memset(&opt, 0, sizeof(opt));
    opt.force_kernel = -1;
    opt.window_side = 0;
    opt.capture_steps = 0;

    buf = (char *)malloc(strlen(GetCommandLineA()) + 2);
    if (!buf) return 1;
    strcpy(buf, GetCommandLineA());
    argc = parse_args(buf, argv, 64);

    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (ieq(a, "/?") || ieq(a, "/help") || ieq(a, "--help")) {
            platform_console_attach();
            usage();
            free(buf);
            return 0;
        } else if (ieq(a, "/selftest")) {
            mode = MODE_SELFTEST;
        } else if (ieq(a, "/gpu-selftest")) {
            mode = MODE_GPUSELFTEST;
        } else if (ieq(a, "/shot")) {
            mode = MODE_SHOT;
            if (i + 1 < argc && argv[i + 1][0] != '/') opt.capture_path = argv[++i];
        } else if (ieq(a, "/capture")) {
            mode = MODE_CAPTURE;
            if (i + 1 < argc && argv[i + 1][0] != '/') opt.capture_path = argv[++i];
        } else if (ieq(a, "/cpu")) {
            opt.force_kernel = DP_KERNEL_CPU;
        } else if (ieq(a, "/gpu")) {
            opt.force_kernel = DP_KERNEL_GPU;
        } else if (ieq(a, "/legacy")) {
            opt.force_legacy = 1;
        } else if (ieq(a, "/res") && i + 1 < argc) {
            opt.force_res = atoi(argv[++i]);
        } else if (ieq(a, "/steps") && i + 1 < argc) {
            opt.capture_steps = atoi(argv[++i]);
        } else if (ieq(a, "/size") && i + 1 < argc) {
            opt.window_side = atoi(argv[++i]);
        } else if (ieq(a, "/axisx") && i + 1 < argc) {
            opt.axis_x = argv[++i];
        } else if (ieq(a, "/axisy") && i + 1 < argc) {
            opt.axis_y = argv[++i];
        } else if (ieq(a, "/channel") && i + 1 < argc) {
            opt.channel = argv[++i];
        } else if (ieq(a, "/post") && i + 1 < argc) {
            opt.post = argv[++i];
        } else if (ieq(a, "/set") && i + 1 < argc) {
            if (opt.nset < 8) opt.set_values[opt.nset++] = argv[++i];
        } else if (a[0] != '/' && !opt.capture_path) {
            opt.capture_path = (char *)a;
        }
    }

    if (mode == MODE_SELFTEST || mode == MODE_GPUSELFTEST) {
        platform_console_attach();
        rc = dp_selftest(mode == MODE_GPUSELFTEST) ? 0 : 1;
        free(buf);
        return rc;
    }

    if (mode == MODE_SHOT || mode == MODE_CAPTURE) {
        platform_console_attach();
        if (!opt.capture_path) {
            printf("error: missing bitmap path\n");
            usage();
            free(buf);
            return 2;
        }
        if (mode == MODE_SHOT && opt.force_kernel == -1) opt.force_kernel = DP_KERNEL_CPU;
        opt.headless = (mode == MODE_SHOT) ? 1 : 0;
        err[0] = 0;
        app = app_create(inst, &opt, err, sizeof(err));
        if (!app) {
            printf("error: %s\n", err[0] ? err : "app_create failed");
            free(buf);
            return 3;
        }
        rc = (mode == MODE_SHOT)
                 ? (app_shot(app, opt.capture_path, opt.capture_steps) ? 0 : 4)
                 : (app_capture(app, opt.capture_path, opt.capture_steps) ? 0 : 4);
        app_destroy(app);
        free(buf);
        return rc;
    }

    err[0] = 0;
    app = app_create(inst, &opt, err, sizeof(err));
    if (!app) {
        platform_message_box(err[0] ? err : "startup failed", 1);
        free(buf);
        return 1;
    }
    app_loop(app);
    app_destroy(app);
    free(buf);
    return 0;
}