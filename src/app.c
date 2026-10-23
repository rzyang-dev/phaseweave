/* app.c - 窗口、Lua 热逻辑、两种内核、相空间视图与叠加层的编排 */
#include "app.h"

#include "bmp.h"
#include "gpu.h"
#include "lua_hot.h"
#include "phase_view.h"
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

    PhaseView view;
    int      selected;
    int      sel_x, sel_y;
    float    sel_state[4];
    int      dragging;
    int      drag_x, drag_y;

    int      steps_last;
    int      frame_no;
    unsigned long long frame_count;
    double   fps_t0;
    double   fps_frames;
    double   fps;
    double   title_t;
    double   acc;
    int      err_shown;

    int      hot_from_disk;
    char     last_action[256];

    unsigned char lut[DP_LUT_N * 3];
    char     hot_path[MAX_PATH];
    char     title[512];
    const void *embedded;
    size_t   embedded_len;
};

static void app_reload(App *a);
static void app_event(App *a, const DpEvent *ev);
static int  app_do_action(App *a, const char *name, const char *value);
static void app_read_selected(App *a);
static void app_render_phase(App *a);

static const char *kernel_tag(int kernel)
{
    return kernel == DP_KERNEL_GPU ? "gpu" : "cpu";
}

static const char *kernel_label(int kernel)
{
    return kernel == DP_KERNEL_GPU ? "GPU compute" : "Fortran CPU";
}

static void app_set_last(App *a, const char *msg)
{
    if (!a) return;
    strncpy(a->last_action, msg ? msg : "", sizeof(a->last_action) - 1);
    a->last_action[sizeof(a->last_action) - 1] = 0;
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
    app_set_last(a, msg ? msg : "error");
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

static void app_sync_display(App *a)
{
    if (!a->gpu || !a->sim) return;
    if (gpu_legacy(a->gpu)) {
        sim_refresh_rgba(a->sim);
        gpu_upload_rgba_legacy(a->gpu, sim_rgba(a->sim), sim_res(a->sim));
    } else {
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
    snprintf(buf, sizeof(buf), "Phase Space | %s | %d fps | %dx%d | %s",
             kernel_label(a->kernel), (int)(a->fps + 0.5),
             a->cfg.grid.res, a->cfg.grid.res,
             a->paused ? "paused" : "phase");
    platform_set_title(a->plat, buf);
}

static void app_read_selected(App *a)
{
    int idx;
    if (!a || !a->sim || a->selected < 0) return;
    idx = a->selected;
    if (a->kernel == DP_KERNEL_GPU && a->gpu && gpu_compute(a->gpu)) {
        if (!gpu_read_cell(a->gpu, idx, a->sel_state)) {
            memset(a->sel_state, 0, sizeof(a->sel_state));
        }
    } else {
        memcpy(a->sel_state, sim_state(a->sim) + (size_t)idx * 4u, sizeof(a->sel_state));
    }
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
    app_read_selected(a);
    app_set_last(a, "reset");
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
    app_read_selected(a);
    app_set_last(a, a->kernel == DP_KERNEL_GPU ? "kernel: GPU compute" : "kernel: Fortran CPU");
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
    app_read_selected(a);
}

static int app_read_disk_hot(App *a, char **data, size_t *len)
{
    char path[MAX_PATH];
    char dir[MAX_PATH];
    platform_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s\\hot.lua", dir);
    strncpy(a->hot_path, path, sizeof(a->hot_path) - 1);
    a->hot_path[sizeof(a->hot_path) - 1] = 0;
    if (platform_read_file(path, data, len)) {
        a->hot_from_disk = 1;
        return 1;
    }
    a->hot_from_disk = 0;
    return 0;
}

/* 让 Lua 按键处理一次（保留 R/Space/C/K 的热逻辑语义） */
static int app_lua_key(App *a, const char *name)
{
    char action[64];
    char err[512];
    if (!a->use_lua || !a->lua) return 0;
    action[0] = 0;
    err[0] = 0;
    if (!lua_hot_key(a->lua, name, 1, action, sizeof(action), err, sizeof(err))) {
        app_error_box(a, err[0] ? err : "hot.key failed");
        return 0;
    }
    if (strcmp(action, "reset") == 0) app_reset(a);
    else if (strcmp(action, "palette") == 0) { app_load_palette(a); app_set_last(a, "palette changed"); }
    else if (strcmp(action, "kernel") == 0) app_toggle_kernel(a);
    else if (strcmp(action, "pause") == 0) app_set_title(a, 1);
    return 1;
}

static void app_set_view_flag(App *a, const char *name, int value)
{
    if (strcmp(name, "grid") == 0) a->view.grid = value ? 1 : 0;
    else if (strcmp(name, "periodic") == 0) a->view.periodic = value ? 1 : 0;
    else if (strcmp(name, "circle") == 0) a->view.circle = value ? 1 : 0;
    app_set_last(a, name);
}

static int app_do_action(App *a, const char *name, const char *value)
{
    if (!a || !name) return 0;
    if (strcmp(name, "reset") == 0) {
        if (!app_lua_key(a, "R")) app_reset(a);
        else app_set_last(a, "reset");
        return 1;
    }
    if (strcmp(name, "pause") == 0) {
        if (!app_lua_key(a, "SPACE")) {
            a->paused = !a->paused;
            app_set_last(a, a->paused ? "paused" : "running");
        } else {
            app_set_last(a, "pause toggled");
        }
        app_set_title(a, 1);
        return 1;
    }
    if (strcmp(name, "palette") == 0) {
        if (app_lua_key(a, "C")) app_set_last(a, "palette changed");
        return 1;
    }
    if (strcmp(name, "palette_set") == 0) {
        char err[512];
        if (!value || !value[0]) return 0;
        err[0] = 0;
        if (a->use_lua && lua_hot_set_scheme(a->lua, value, err, sizeof(err))) {
            app_load_palette(a);
            app_set_last(a, value);
            return 1;
        }
        app_set_last(a, err[0] ? err : "palette_set failed");
        return 0;
    }
    if (strcmp(name, "kernel") == 0) {
        if (!app_lua_key(a, "K")) app_toggle_kernel(a);
        return 1;
    }
    if (strcmp(name, "kernel_set") == 0) {
        int want_gpu = value && strcmp(value, "gpu") == 0;
        int is_gpu = (a->kernel == DP_KERNEL_GPU);
        if (want_gpu == is_gpu) {
            app_set_last(a, want_gpu ? "kernel: GPU compute" : "kernel: Fortran CPU");
            return 1;
        }
        return app_toggle_kernel(a);
    }
    if (strcmp(name, "reload") == 0) { app_reload(a); return 1; }
    if (strcmp(name, "grid") == 0 || strcmp(name, "periodic") == 0 || strcmp(name, "circle") == 0) {
        int cur = (strcmp(name, "grid") == 0) ? a->view.grid :
                  (strcmp(name, "periodic") == 0) ? a->view.periodic : a->view.circle;
        int val = (value && value[0]) ? (value[0] != '0') : !cur;
        app_set_view_flag(a, name, val);
        return 1;
    }
    if (strcmp(name, "view_reset") == 0) {
        pv_reset(&a->view);
        app_set_last(a, "view reset");
        return 1;
    }
    if (strcmp(name, "quit") == 0) {
        PostQuitMessage(0);
        return 1;
    }
    app_set_last(a, "unknown action");
    return 0;
}

static void app_event(App *a, const DpEvent *ev)
{
    const char *action = NULL;
    int w, h;
    if (!a || !ev) return;

    if (ev->type == DP_EV_KEY) {
        if (!ev->b) return;
        switch (ev->a) {
        case 'R': action = "reset"; break;
        case VK_SPACE: action = "pause"; break;
        case 'C': action = "palette"; break;
        case 'K': action = "kernel"; break;
        case 'G': action = "grid"; break;
        case 'P': action = "periodic"; break;
        case 'O': action = "circle"; break;
        case '0': action = "view_reset"; break;
        case VK_F5: action = "reload"; break;
        case VK_ESCAPE: action = "quit"; break;
        default: break;
        }
        if (action) app_do_action(a, action, "");
        return;
    }

    w = a->plat ? platform_width(a->plat) : 0;
    h = a->plat ? platform_height(a->plat) : 0;
    if (w <= 0 || h <= 0) return;

    if (ev->type == DP_EV_MOUSE_DOWN && ev->a == 0) {
        a->dragging = 1;
        a->drag_x = ev->b;
        a->drag_y = ev->c;
        return;
    }
    if (ev->type == DP_EV_MOUSE_UP && ev->a == 0) {
        a->dragging = 0;
        return;
    }
    if (ev->type == DP_EV_MOUSE_MOVE) {
        if (a->dragging) {
            int dx = ev->a - a->drag_x;
            int dy = ev->b - a->drag_y;
            pv_pan(&a->view, a->cfg.grid.range, w, h, dx, dy);
            a->drag_x = ev->a;
            a->drag_y = ev->b;
        }
        return;
    }
    if (ev->type == DP_EV_MOUSE_DOWN && ev->a == 1) {
        int cx, cy;
        if (pv_pick(&a->view, a->cfg.grid.range, sim_res(a->sim), w, h,
                    ev->b, ev->c, &cx, &cy)) {
            a->selected = cy * sim_res(a->sim) + cx;
            a->sel_x = cx;
            a->sel_y = cy;
            app_read_selected(a);
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "selected (%d, %d)", cx, cy);
                app_set_last(a, msg);
            }
        } else {
            a->selected = -1;
            app_set_last(a, "selection cleared");
        }
        return;
    }
    if (ev->type == DP_EV_WHEEL) {
        double factor = (ev->a > 0) ? 1.15 : 1.0 / 1.15;
        pv_zoom_at(&a->view, a->cfg.grid.range, w, h, ev->b, ev->c, factor);
        return;
    }
}

static void app_event_cb(void *user, const DpEvent *ev)
{
    app_event((App *)user, ev);
}

void app_key(App *a, int vk, int down)
{
    DpEvent ev;
    ev.type = DP_EV_KEY;
    ev.a = vk;
    ev.b = down;
    ev.c = 0;
    app_event(a, &ev);
}

static void app_reload(App *a)
{
    char *disk = NULL;
    size_t disk_len = 0;
    const char *src;
    size_t len;
    char err[1024];
    DpConfig next;

    if (!a->lua) {
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
        snprintf(msg, sizeof(msg), "hot.lua reload failed: %s", err[0] ? err : "unknown error");
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
            a->selected = -1;
            if (a->gpu) {
                gpu_set_res(a->gpu, a->cfg.grid.res);
                gpu_set_range(a->gpu, a->cfg.grid.range);
            }
            app_load_palette(a);
            app_reset(a);
        } else {
            if (a->gpu) gpu_set_range(a->gpu, a->cfg.grid.range);
            app_load_palette(a);
            if (init_changed) app_reset(a);
        }
        app_set_last(a, disk ? "hot.lua reloaded (disk)" : "hot.lua reloaded (embedded)");
    } else {
        app_set_last(a, err[0] ? err : "hot.defaults failed after reload");
    }
    a->use_lua = 1;
    if (a->use_lua) lua_hot_set_kernel(a->lua, kernel_tag(a->kernel));
    app_set_title(a, 1);
    if (disk) platform_free(disk);
}

static void app_render_phase(App *a)
{
    static float verts[16384 * 6];
    int w, h, grid_n = 0, total;
    if (!a->gpu || !a->plat) return;
    w = platform_width(a->plat);
    h = platform_height(a->plat);
    gpu_draw_view(a->gpu, w, h, &a->view);
    total = pv_build_overlay(&a->view, a->cfg.grid.range, sim_res(a->sim), w, h,
                             a->selected >= 0 ? a->sel_x : -1,
                             a->selected >= 0 ? a->sel_y : -1,
                             verts, 16384, &grid_n);
    if (grid_n > 0) gpu_draw_lines(a->gpu, verts, grid_n, 1.0f);
    if (total > grid_n) gpu_draw_lines(a->gpu, verts + grid_n * 6, total - grid_n, 2.0f);
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
    a->selected = -1;
    a->paused = 0;
    pv_init(&a->view);
    dp_config_defaults(&a->cfg);
    app_params_from_cfg(a);

    force_kernel = opt->force_kernel;
    need_display = !(opt->headless && force_kernel == DP_KERNEL_CPU);

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

    a->sim = sim_create(&a->cfg, a->cfg.sim.threads, err, errsz);
    if (!a->sim) { app_destroy(a); return NULL; }

    if (need_display) {
        int side = opt->window_side;
        RECT work;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        if (side <= 0) {
            side = (int)((work.bottom - work.top) * 0.68);
            if (work.right - work.left < work.bottom - work.top)
                side = (int)((work.right - work.left) * 0.68);
            side = dp_clampi(side, 420, 740);
        }
        a->plat = platform_create(inst, L"Phase Space", side,
                                  opt->headless, a, app_event_cb);
        if (!a->plat) {
            if (err) snprintf(err, errsz, "app: window creation failed");
            app_destroy(a);
            return NULL;
        }
        platform_move_client(a->plat, 16, 16);
    }

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
        gpu_set_range(a->gpu, a->cfg.grid.range);
    }

    if (force_kernel == DP_KERNEL_GPU && !gpu_ok) {
        if (err) snprintf(err, errsz, "app: GPU compute requested but unavailable%s%s",
                          gpu_err[0] ? ": " : "", gpu_err[0] ? gpu_err : "");
        app_destroy(a);
        return NULL;
    }
    a->kernel = (gpu_ok && force_kernel != DP_KERNEL_CPU) ? DP_KERNEL_GPU : DP_KERNEL_CPU;
    a->steps_last = 0;
    a->acc = 0.0;
    a->fps_t0 = platform_now();
    a->fps_frames = 0.0;
    a->title_t = 0.0;

    app_load_palette(a);
    if (a->use_lua) lua_hot_set_kernel(a->lua, kernel_tag(a->kernel));
    if (a->sim) sim_set_lut(a->sim, a->lut);

    sim_reset(a->sim, &a->cfg);
    if (a->kernel == DP_KERNEL_GPU && a->gpu) {
        gpu_upload_state(a->gpu, sim_state(a->sim), sim_res(a->sim));
        gpu_step(a->gpu, 0, &a->params);
    } else {
        app_sync_display(a);
    }
    app_set_last(a, "ready");
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
            if (!lua_hot_tick(a->lua, dt, &steps, err, sizeof(err))) {
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
        app_render_phase(a);
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
    app_render_phase(a);
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
