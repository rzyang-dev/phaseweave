/* app.c - 把窗口、Lua 热逻辑、Fortran/GLSL 内核和显示串成一条帧流水线 */
#include "app.h"

#include "bmp.h"
#include "gpu.h"
#include "lua_hot.h"
#include "physics.h"
#include "platform.h"
#include "sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DP_HOT_RES_ID 1001

struct App {
    HINSTANCE inst;
    AppOptions opt;

    Platform *plat;
    Gpu      *gpu;
    Sim      *sim;
    LuaHot   *lua;

    DpConfig cfg;
    DpParams params;
    int      kernel;
    int      use_lua;
    int      legacy;
    int      paused;

    int      steps_last;
    int      frame_no;
    unsigned long long frame_count;
    double   fps_t0;
    double   fps_frames;
    double   fps;
    double   title_t;
    double   acc;             /* Lua 不可用时的兜底累加器 */
    int      err_shown;

    unsigned char lut[DP_LUT_N * 3];
    char     hot_path[MAX_PATH];
    char     title[512];
    const void *embedded;
    size_t   embedded_len;
};

static void app_reload(App *a);
static void app_key_cb(void *user, int vk, int down);

static const char *kernel_tag(int kernel)
{
    return kernel == DP_KERNEL_GPU ? "gpu" : "cpu";
}

static const char *kernel_label(int kernel)
{
    return kernel == DP_KERNEL_GPU ? "GPU compute" : "Fortran CPU";
}

static void app_error_box(App *a, const char *msg)
{
    {
        char path[MAX_PATH];
        DWORD n = GetTempPathA(MAX_PATH, path);
        if (n != 0) {
            FILE *f;
            strncat(path, "dp-phase-error.txt", MAX_PATH - strlen(path) - 1);
            f = fopen(path, "a");
            if (f) { fprintf(f, "%s\n", msg ? msg : "(null)"); fclose(f); }
        }
    }
    if (a->err_shown) return;
    a->err_shown = 1;
    if (!a->opt.headless) platform_message_box(msg, 1);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

static void app_params_from_cfg(App *a)
{
    a->params.m1 = a->cfg.physics.m1;
    a->params.m2 = a->cfg.physics.m2;
    a->params.l1 = a->cfg.physics.l1;
    a->params.l2 = a->cfg.physics.l2;
    a->params.g = a->cfg.physics.g;
    a->params.damping = a->cfg.physics.damping;
    a->params.dt = a->cfg.sim.dt;
}

/* 把 CPU 侧刚算出的状态/字段同步到显示路径 */
static void app_sync_display(App *a)
{
    if (!a->gpu || !a->sim) return;
    if (a->gpu && gpu_legacy(a->gpu)) {
        sim_refresh_rgba(a->sim);
        gpu_upload_rgba_legacy(a->gpu, sim_rgba(a->sim), sim_res(a->sim));
    } else if (a->gpu) {
        gpu_upload_field(a->gpu, sim_field(a->sim), sim_res(a->sim));
    }
}

static int app_apply_palette(App *a, const unsigned char *lut)
{
    memcpy(a->lut, lut, DP_LUT_N * 3);
    if (a->sim) sim_set_lut(a->sim, a->lut);
    if (a->gpu && gpu_compute(a->gpu)) gpu_set_lut(a->gpu, a->lut);
    return 1;
}

static int app_load_palette(App *a)
{
    unsigned char lut[DP_LUT_N * 3];
    char err[512];
    err[0] = 0;
    if (a->use_lua && lua_hot_palette(a->lua, lut, err, sizeof(err))) {
        return app_apply_palette(a, lut);
    }
    if (a->err_shown == 0 && err[0]) app_error_box(a, err);
    dp_phase_lut(lut);
    return app_apply_palette(a, lut);
}

static void app_set_title(App *a, int force)
{
    double now = platform_now();
    char buf[512];
    if (!force && now - a->title_t < 0.25) return;
    if (!a->plat) return;
    a->title_t = now;

    if (a->use_lua &&
        lua_hot_title(a->lua, (int)(a->fps + 0.5), a->steps_last, buf, sizeof(buf), NULL, 0)) {
        if (buf[0]) {
            platform_set_title(a->plat, buf);
            return;
        }
    }
    snprintf(buf, sizeof(buf), "Double Pendulum Phase Space | %s | %d fps | %dx%d | %s",
             kernel_label(a->kernel), (int)(a->fps + 0.5),
             a->cfg.grid.res, a->cfg.grid.res,
             a->paused ? "paused" : "phase");
    platform_set_title(a->plat, buf);
}

static void app_reset(App *a)
{
    if (!a->sim) return;
    sim_reset(a->sim, &a->cfg);
    if (a->kernel == DP_KERNEL_GPU && a->gpu && gpu_compute(a->gpu)) {
        gpu_upload_state(a->gpu, sim_state(a->sim), sim_res(a->sim));
        gpu_step(a->gpu, 0, &a->params);
    } else {
        app_sync_display(a);
    }
    if (a->use_lua) lua_hot_reset(a->lua);
    a->acc = 0.0;
    a->steps_last = 0;
    app_set_title(a, 1);
}

static int app_toggle_kernel(App *a)
{
    if (!a->gpu || !gpu_compute(a->gpu) || !a->sim) {
        app_error_box(a, "GPU compute shader is not available on this system.");
        return 0;
    }
    if (a->kernel == DP_KERNEL_GPU) {
        gpu_download_state(a->gpu, sim_state(a->sim), sim_res(a->sim));
        sim_refresh_field(a->sim);
        a->kernel = DP_KERNEL_CPU;
        app_sync_display(a);
    } else {
        gpu_upload_state(a->gpu, sim_state(a->sim), sim_res(a->sim));
        gpu_step(a->gpu, 0, &a->params);
        a->kernel = DP_KERNEL_GPU;
    }
    if (a->use_lua) lua_hot_set_kernel(a->lua, kernel_tag(a->kernel));
    app_set_title(a, 1);
    return 1;
}

static void app_advance(App *a, int steps)
{
    int done = 0;
    if (!a || !a->sim || steps <= 0) return;
    if (a->kernel == DP_KERNEL_GPU && a->gpu && gpu_compute(a->gpu)) {
        while (done < steps) {
            int chunk = steps - done;
            if (chunk > 64) chunk = 64;
            gpu_step(a->gpu, chunk, &a->params);
            done += chunk;
        }
    } else {
        sim_step_cpu(a->sim, steps, &a->params);
        app_sync_display(a);
        done = steps;
    }
    a->steps_last = done;
}

static int app_read_disk_hot(App *a, char **data, size_t *len)
{
    char path[MAX_PATH];
    char dir[MAX_PATH];
    platform_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s\\hot.lua", dir);
    strncpy(a->hot_path, path, sizeof(a->hot_path) - 1);
    return platform_read_file(path, data, len);
}

void app_key(App *a, int vk, int down)
{
    char action[64];
    char err[512];
    const char *name = NULL;
    char letter[2];

    if (!a) return;
    if (vk == VK_ESCAPE && down) {
        PostQuitMessage(0);
        return;
    }
    if (vk == VK_F5 && down) {
        app_reload(a);
        return;
    }

    if (vk >= 'A' && vk <= 'Z') {
        letter[0] = (char)vk;
        letter[1] = 0;
        name = letter;
    } else if (vk == VK_SPACE) {
        name = "SPACE";
    } else if (vk == VK_RETURN) {
        name = "RETURN";
    } else {
        return;
    }

    if (a->use_lua) {
        action[0] = 0;
        if (lua_hot_key(a->lua, name, down, action, sizeof(action), err, sizeof(err))) {
            if (!down) return;
            if (strcmp(action, "reset") == 0) app_reset(a);
            else if (strcmp(action, "palette") == 0) { app_load_palette(a); app_set_title(a, 1); }
            else if (strcmp(action, "kernel") == 0) app_toggle_kernel(a);
            else if (strcmp(action, "pause") == 0) app_set_title(a, 1);
            return;
        }
        app_error_box(a, err[0] ? err : "hot.key failed");
        return;
    }

    if (!down) return;
    if (name[0] == 'R' && name[1] == 0) app_reset(a);
    else if (strcmp(name, "SPACE") == 0) { a->paused = !a->paused; app_set_title(a, 1); }
}

static void app_key_cb(void *user, int vk, int down)
{
    app_key((App *)user, vk, down);
}

static void app_reload(App *a)
{
    char *disk = NULL;
    size_t disk_len = 0;
    const char *src;
    size_t len;
    char err[1024];
    DpConfig next;

    if (!a->use_lua && !a->lua) {
        app_error_box(a, "Lua hot logic is not available.");
        return;
    }

    if (app_read_disk_hot(a, &disk, &disk_len)) {
        src = disk;
        len = disk_len;
    } else {
        src = (const char *)a->embedded;
        len = a->embedded_len;
    }
    err[0] = 0;
    if (!src || len == 0 || !lua_hot_reload(a->lua, src, len, a->hot_path[0] ? a->hot_path : "hot.lua",
                                          err, sizeof(err))) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "hot.lua reload failed:\n%s", err[0] ? err : "unknown error");
        app_error_box(a, msg);
        if (disk) platform_free(disk);
        return;
    }

    next = a->cfg;
    err[0] = 0;
    if (lua_hot_defaults(a->lua, &next, err, sizeof(err))) {
        int res_changed = (next.grid.res != a->cfg.grid.res);
        int init_changed = (next.grid.range != a->cfg.grid.range ||
                            next.grid.omega1 != a->cfg.grid.omega1 ||
                            next.grid.omega2 != a->cfg.grid.omega2);
        a->cfg = next;
        app_params_from_cfg(a);
        lua_hot_set_res(a->lua, a->cfg.grid.res);
        if (res_changed) {
            if (a->sim) sim_destroy(a->sim);
            a->sim = sim_create(&a->cfg, a->cfg.sim.threads, err, sizeof(err));
            if (!a->sim) {
                app_error_box(a, err);
                if (disk) platform_free(disk);
                return;
            }
            if (a->gpu) gpu_set_res(a->gpu, a->cfg.grid.res);
            app_load_palette(a);
            app_reset(a);
        } else {
            app_load_palette(a);
            if (init_changed) app_reset(a);
        }
    }
    a->use_lua = 1;
    if (a->use_lua) lua_hot_set_kernel(a->lua, kernel_tag(a->kernel));
    app_set_title(a, 1);
    if (disk) platform_free(disk);
}

App *app_create(HINSTANCE inst, const AppOptions *opt, char *err, size_t errsz)
{
    App *a;
    int need_display;
    int gpu_ok = 0;
    int force_kernel;
    char gpu_err[512];
    const void *embedded = NULL;
    size_t embedded_len = 0;
    char lua_err[512];

    if (err && errsz) err[0] = 0;
    a = (App *)calloc(1, sizeof(App));
    if (!a) { if (err) snprintf(err, errsz, "app: out of memory"); return NULL; }
    a->inst = inst;
    a->opt = *opt;
    dp_config_defaults(&a->cfg);
    app_params_from_cfg(a);

    force_kernel = opt->force_kernel;
    need_display = !(opt->headless && force_kernel == DP_KERNEL_CPU);

    /* ---- Lua 热逻辑：先加载，拿到网格/物理默认值 ---- */
    platform_read_resource(DP_HOT_RES_ID, &embedded, &embedded_len);
    a->embedded = embedded;
    a->embedded_len = embedded_len;

    {
        char *disk = NULL;
        size_t disk_len = 0;
        const char *src = NULL;
        size_t len = 0;
        if (app_read_disk_hot(a, &disk, &disk_len)) {
            src = disk;
            len = disk_len;
        } else if (embedded && embedded_len) {
            src = (const char *)embedded;
            len = embedded_len;
        }
        if (src && len) {
            lua_err[0] = 0;
            a->lua = lua_hot_create(src, len, a->hot_path[0] ? a->hot_path : "hot.lua",
                                    "gpu", a->cfg.grid.res, lua_err, sizeof(lua_err));
            if (a->lua) {
                a->use_lua = 1;
                lua_err[0] = 0;
                if (!lua_hot_defaults(a->lua, &a->cfg, lua_err, sizeof(lua_err))) {
                    a->use_lua = 0;
                    app_error_box(a, lua_err[0] ? lua_err : "hot.defaults failed");
                    lua_hot_destroy(a->lua);
                    a->lua = NULL;
                    dp_config_defaults(&a->cfg);
                }
            } else {
                app_error_box(a, lua_err[0] ? lua_err : "hot.lua failed to load");
            }
        }
        if (disk) platform_free(disk);
    }
    if (opt->force_res > 0) a->cfg.grid.res = dp_clampi(opt->force_res, 32, 1024);
    app_params_from_cfg(a);

    /* ---- 网格 ---- */
    a->sim = sim_create(&a->cfg, a->cfg.sim.threads, err, errsz);
    if (!a->sim) { app_destroy(a); return NULL; }

    /* ---- 窗口 ---- */
    if (need_display) {
        int side = opt->window_side;
        if (side <= 0) {
            RECT work;
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            side = (int)((work.bottom - work.top) * 0.78);
            if (work.right - work.left < work.bottom - work.top)
                side = (int)((work.right - work.left) * 0.78);
            side = dp_clampi(side, 480, 1000);
        }
        a->plat = platform_create(inst, L"Double Pendulum Phase Space", side,
                                  opt->headless, a, app_key_cb);
        if (!a->plat) {
            if (err) snprintf(err, errsz, "app: window creation failed");
            app_destroy(a);
            return NULL;
        }
    }

    /* ---- GPU ---- */
    if (need_display) {
        gpu_err[0] = 0;
        a->gpu = gpu_create(platform_hwnd(a->plat), platform_hdc(a->plat),
                            opt->force_legacy, gpu_err, sizeof(gpu_err));
        if (!a->gpu) {
            if (err) snprintf(err, errsz, "app: %s", gpu_err[0] ? gpu_err : "no OpenGL");
            app_destroy(a);
            return NULL;
        }
        gpu_ok = gpu_compute(a->gpu);
        a->legacy = gpu_legacy(a->gpu);
        platform_set_vsync(a->plat, gpu_vsync(a->gpu));
        gpu_set_res(a->gpu, a->cfg.grid.res);
    }

    if (force_kernel == DP_KERNEL_GPU && !gpu_ok) {
        if (err) snprintf(err, errsz, "app: GPU compute requested but unavailable%s%s",
                          gpu_err[0] ? ": " : "", gpu_err[0] ? gpu_err : "");
        app_destroy(a);
        return NULL;
    }
    a->kernel = (gpu_ok && force_kernel != DP_KERNEL_CPU) ? DP_KERNEL_GPU : DP_KERNEL_CPU;
    a->paused = 0;
    a->steps_last = 0;
    a->acc = 0.0;
    a->fps_t0 = platform_now();
    a->fps_frames = 0.0;
    a->title_t = 0.0;

    app_load_palette(a);
    if (a->use_lua) lua_hot_set_kernel(a->lua, kernel_tag(a->kernel));
    if (a->sim) sim_set_lut(a->sim, a->lut);

    /* 初始状态：CPU 填网格；若走 GPU 内核则上传并先跑 0 步刷新字段纹理 */
    sim_reset(a->sim, &a->cfg);
    if (a->kernel == DP_KERNEL_GPU && a->gpu) {
        gpu_upload_state(a->gpu, sim_state(a->sim), sim_res(a->sim));
        gpu_step(a->gpu, 0, &a->params);
    } else {
        app_sync_display(a);
    }
    app_set_title(a, 1);
    return a;
}

void app_destroy(App *app)
{
    App *a = app;
    if (!a) return;
    if (a->gpu) gpu_destroy(a->gpu);
    if (a->sim) sim_destroy(a->sim);
    if (a->lua) lua_hot_destroy(a->lua);
    if (a->plat) platform_destroy(a->plat);
    free(a);
}

int app_frame(App *a)
{
    double dt;
    int steps = 0;
    int max_steps;
    char err[512];

    if (!a || !a->plat || !a->sim) return 0;
    dt = platform_dt(a->plat);

    if (a->use_lua) {
        DpParams p;
        err[0] = 0;
        if (lua_hot_params(a->lua, &p, &max_steps, err, sizeof(err))) {
            a->params = p;
        } else {
            a->use_lua = 0;
            app_error_box(a, err[0] ? err : "hot.params failed");
        }
        if (a->use_lua) {
            err[0] = 0;
            if (lua_hot_tick(a->lua, dt, &steps, err, sizeof(err))) {
                /* steps 已由 Lua 限幅 */
            } else {
                a->use_lua = 0;
                app_error_box(a, err[0] ? err : "hot.tick failed");
                steps = 0;
            }
        }
    }

    if (!a->use_lua) {
        if (a->paused) {
            steps = 0;
        } else {
            double step = a->params.dt > 0.0 ? a->params.dt : 0.1;
            int cap = dp_clampi(a->cfg.sim.max_steps, 0, DP_MAX_STEPS);
            a->acc += dt;
            while (a->acc >= step && steps < cap) {
                a->acc -= step;
                steps++;
            }
            if (a->acc > step) a->acc = 0.0;
        }
    }

    if (steps > 0) app_advance(a, steps);
    else a->steps_last = 0;

    if (a->gpu) {
        gpu_draw(a->gpu, platform_width(a->plat), platform_height(a->plat));
        platform_frame_end(a->plat);
    }

    a->frame_count++;
    a->fps_frames += 1.0;
    {
        double now = platform_now();
        double span = now - a->fps_t0;
        if (span >= 0.5) {
            a->fps = a->fps_frames / span;
            a->fps_frames = 0.0;
            a->fps_t0 = now;
        }
    }
    app_set_title(a, 0);
    return 1;
}

int app_loop(App *a)
{
    if (!a) return 0;
    while (!platform_quit(a->plat)) {
        platform_pump(a->plat);
        if (platform_quit(a->plat)) break;
        app_frame(a);
    }
    return 0;
}

int app_shot(App *a, const char *path, int steps)
{
    int res;
    unsigned char *rgba;
    int ok;
    if (!a || !a->sim || !path) return 0;
    res = sim_res(a->sim);
    app_advance(a, steps);

    if (a->kernel == DP_KERNEL_GPU && a->gpu && gpu_compute(a->gpu)) {
        /* 把 GPU 上的状态取回，用同一套 CPU 着色公式出图：
         * 这样 CPU/GPU 两张 shot 只在“状态”上比较，不掺进 R32F 纹理的舍入。 */
        gpu_download_state(a->gpu, sim_state(a->sim), res);
    }
    sim_refresh_field(a->sim);
    rgba = (unsigned char *)malloc((size_t)res * (size_t)res * 4u);
    if (!rgba) return 0;
    dp_colorize(sim_field(a->sim), sim_cells(a->sim), a->lut, rgba);
    ok = dp_write_bmp(path, rgba, res, res);
    free(rgba);
    if (ok) printf("shot: %s  %dx%d  steps=%d  kernel=%s  hash=%08X\n",
                   path, res, res, steps, kernel_tag(a->kernel),
                   (unsigned)dp_hash_bytes(sim_field(a->sim), (size_t)sim_cells(a->sim) * 4u));
    return ok;
}

int app_capture(App *a, const char *path, int steps)
{
    int w, h, ok;
    unsigned char *rgba;
    if (!a || !a->plat || !a->gpu || !a->sim || !path) return 0;
    app_advance(a, steps);
    w = platform_width(a->plat);
    h = platform_height(a->plat);
    gpu_draw(a->gpu, w, h);
    rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    if (!rgba) return 0;
    ok = gpu_capture_rgba(a->gpu, w, h, rgba);
    if (ok) ok = dp_write_bmp(path, rgba, w, h);
    free(rgba);
    if (ok) printf("capture: %s  %dx%d  steps=%d  kernel=%s  legacy=%d  renderer=%s\n",
                   path, w, h, steps, kernel_tag(a->kernel), a->legacy, gpu_renderer(a->gpu));
    return ok;
}

const char *app_kernel_text(App *a) { return a ? kernel_tag(a->kernel) : "?"; }
const char *app_gpu_info(App *a) { return (a && a->gpu) ? gpu_renderer(a->gpu) : "(none)"; }
int app_res(App *a) { return a ? a->cfg.grid.res : 0; }