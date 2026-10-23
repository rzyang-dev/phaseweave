/* app.c - experiment atlas: windows, CPU sampler, Tcl/Tk panel, GL/GDI display */
#include "app.h"

#include "atlas.h"
#include "bmp.h"
#include "experiment.h"
#include "gpu.h"
#include "lua_hot.h"
#include "model_custom.h"
#include "phase_view.h"
#include "pendulum_view.h"
#include "panel.h"
#include "physics.h"
#include "png.h"
#include "platform.h"
#include "session.h"

#include <commdlg.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct App {
    HINSTANCE inst;
    AppOptions opt;

    Platform *plat;
    Gpu      *gpu;
    Atlas    *atlas;
    LuaHot   *lua;
    PendulumView *pview;
    Panel    *panel;
    int      quit;
    int      right_x, right_w, pend_h;

    Experiment exp;
    CustomModel custom;
    int      has_custom;
    int      backend_gpu;
    int      gpu_custom_ok;
    float   *gpu_cells;
    float   *gpu_chans;
    size_t   gpu_cells_cap;
    size_t   gpu_chans_cap;
    int      use_lua;
    int      legacy;

    PhaseView view;
    int      selected;
    int      sel_x, sel_y;
    float    sel_state[MODEL_MAX_STATE];
    float    sel_vals[MODEL_MAX_PARAMS];
    float    sel_params[6];
    float    sel_ch[MODEL_MAX_CHANNELS];
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
};

static int app_apply_schema(App *a, int defaults)
{
    Schema s;
    char err[512];
    if (!a || !a->lua) return 0;
    if (!lua_hot_schema(a->lua, a->exp.model, &s, err, sizeof(err))) return 0;
    return exp_apply_schema(&a->exp, &s, defaults);
}

static void app_reload(App *a);
static int  app_load_custom(App *a);
static const Model *app_resolve_model(App *a, const char *id);
static void app_event(App *a, const DpEvent *ev);
static void app_pend_close(void *user);
static void app_pend_key(void *user, int vk, int down);
static int  panel_action_cb(void *user, const char *name, const char *value,
                            char *msg, size_t msgsz);
static void panel_status_cb(void *user, PanelStatus *st);
static void panel_query_cb(void *user, const char *what, char *out, size_t outsz);
static void panel_quit_cb(void *user);
static int  app_do_action(App *a, const char *name, const char *value);
static void app_set_last(App *a, const char *msg);
static void app_error_box(App *a, const char *msg);
static void app_set_title(App *a, int force);
static void app_read_selected(App *a);
static void app_render_phase(App *a);
static void app_apply_experiment(App *a);
static void app_colorize(App *a);
static int  app_apply_schema(App *a, int defaults);
static int  app_prepare_custom_gpu(App *a);
static int  app_gpu_advance(App *a, int steps);
static int  app_save_session(App *a, const char *path);
static int  app_load_session(App *a, const char *path);
static int  app_dialog_save(App *a);
static int  app_dialog_load(App *a);
static void app_set_model(App *a, const char *id)
{
    const Model *m;
    int res;
    char err[512];
    if (!a || !id) return;
    m = app_resolve_model(a, id);
    if (!m || a->exp.mdl == m) return;
    res = a->exp.res;
    if (a->pview) { pendulum_view_destroy(a->pview); a->pview = NULL; }
    exp_init(&a->exp, m);
    if (a->lua) app_apply_schema(a, 1);
    a->exp.res = res;
    a->selected = -1;
    if (a->atlas) {
        atlas_destroy(a->atlas);
        a->atlas = atlas_create(&a->exp, a->exp.res, err, sizeof(err));
        if (!a->atlas) app_error_box(a, err[0] ? err : "atlas creation failed");
    }
    if (strcmp(a->exp.model, "custom") != 0) a->backend_gpu = 0;
    else {
        app_prepare_custom_gpu(a);
        if (!a->gpu_custom_ok) a->backend_gpu = 0;
    }
    if (a->plat && !a->opt.capture_path && strcmp(a->exp.model, "double_pendulum") == 0) {
        a->pview = pendulum_view_create(a->inst, L"Double Pendulum",
                                        a->right_x, 16, a->right_w, a->pend_h,
                                        a, app_pend_close, app_pend_key);
    }
    pv_reset(&a->view);
    app_colorize(a);
    app_set_last(a, a->exp.title);
    app_set_title(a, 1);
}

static void app_apply_experiment(App *a);
static void app_colorize(App *a);
static void app_set_model(App *a, const char *id);

static void app_set_last(App *a, const char *msg)
{
    if (!a) return;
    strncpy(a->last_action, msg ? msg : "", sizeof(a->last_action) - 1);
    a->last_action[sizeof(a->last_action) - 1] = 0;
}

static void app_error_box(App *a, const char *msg)
{
    app_set_last(a, msg ? msg : "error");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
    if (a->opt.headless || a->opt.capture_path) return;
    if (a->panel) return;
    if (a->err_shown) return;
    a->err_shown = 1;
    platform_message_box(msg, 1);
}

static int app_load_palette(App *a)
{
    unsigned char lut[DP_LUT_N * 3];
    char err[512];
    err[0] = 0;
    if (a->use_lua && a->lua && lua_hot_palette(a->lua, lut, err, sizeof(err))) {
        memcpy(a->lut, lut, sizeof(lut));
        return 1;
    }
    dp_phase_lut(a->lut);
    return 0;
}

static void app_colorize(App *a)
{
    if (!a || !a->atlas) return;
    atlas_colorize(a->atlas, &a->exp, a->lut);
    if (a->gpu) {
        if (a->legacy) gpu_upload_rgba_legacy(a->gpu, atlas_rgba(a->atlas), atlas_res(a->atlas));
        else gpu_upload_atlas_rgba(a->gpu, atlas_rgba(a->atlas), atlas_res(a->atlas));
    }
}

static void app_set_title(App *a, int force)
{
    double now = platform_now();
    char buf[512];
    ExpChannelInfo ci;
    if (!force && now - a->title_t < 0.25) return;
    if (!a->plat) return;
    a->title_t = now;
    exp_channel_info(&a->exp, a->exp.channel, &ci);
    snprintf(buf, sizeof(buf), "%s Atlas | %s | %d fps | %dx%d | %s%s",
             a->exp.title, (a->backend_gpu && a->gpu_custom_ok) ? "GPU" : "CPU",
             (int)(a->fps + 0.5), a->exp.res, a->exp.res,
             ci.name, a->exp.paused ? " | paused" : "");
    platform_set_title(a->plat, buf);
}

static void app_read_selected(App *a)
{
    int idx;
    if (!a || !a->atlas || a->selected < 0) return;
    idx = a->selected;
    atlas_read_cell(a->atlas, idx, a->sel_state, a->sel_vals, a->sel_ch);
    memset(a->sel_params, 0, sizeof(a->sel_params));
    if (a->exp.mdl == &DP_MODEL) {
        /* double-pendulum model values are m1,m2,l1,l2,g,damping at 4..9 */
        a->sel_params[0] = a->sel_vals[4];
        a->sel_params[1] = a->sel_vals[5];
        a->sel_params[2] = a->sel_vals[6];
        a->sel_params[3] = a->sel_vals[7];
        a->sel_params[4] = a->sel_vals[8];
        a->sel_params[5] = a->sel_vals[9];
    }
}

static void app_reset(App *a)
{
    if (!a || !a->atlas) return;
    atlas_reset(a->atlas, &a->exp);
    app_colorize(a);
    a->acc = 0.0;
    a->steps_last = 0;
    app_read_selected(a);
    app_set_last(a, "reset");
    app_set_title(a, 1);
}

static void app_apply_experiment(App *a)
{
    char err[512];
    if (!a || !a->atlas) return;
    if (a->exp.res != atlas_res(a->atlas)) {
        atlas_resize(a->atlas, a->exp.res, err, sizeof(err));
        if (atlas_res(a->atlas) != a->exp.res) {
            a->exp.res = atlas_res(a->atlas);
            app_error_box(a, "atlas resize failed");
        }
    }
    atlas_reset(a->atlas, &a->exp);
    app_colorize(a);
    app_read_selected(a);
    app_set_last(a, "applied");
    app_set_title(a, 1);
}

static int app_prepare_custom_gpu(App *a)
{
    char glsl[32768], err[1024];
    if (!a) return 0;
    a->gpu_custom_ok = 0;
    if (!a->gpu) return 0;
    if (!a->has_custom) return 0;
    if (strcmp(a->exp.model, "custom") != 0) return 0;
    glsl[0] = 0; err[0] = 0;
    if (!custom_model_glsl(&a->custom, glsl, sizeof(glsl), err, sizeof(err))) return 0;
    if (!gpu_compile_custom(a->gpu, glsl, err, sizeof(err))) return 0;
    a->gpu_custom_ok = 1;
    return 1;
}

static int app_gpu_advance(App *a, int steps)
{
    int res;
    size_t n, cn, hn;
    char err[512];
    float *cells;
    if (!a || !a->atlas || !a->gpu || !a->gpu_custom_ok || !a->backend_gpu) return 0;
    res = atlas_res(a->atlas);
    n = (size_t)res * (size_t)res;
    cn = n * 16u;
    hn = n * 4u;
    if (!a->gpu_cells || a->gpu_cells_cap < cn) {
        float *p = (float *)realloc(a->gpu_cells, cn * sizeof(float));
        if (!p) return 0;
        a->gpu_cells = p; a->gpu_cells_cap = cn;
    }
    if (!a->gpu_chans || a->gpu_chans_cap < hn) {
        float *p = (float *)realloc(a->gpu_chans, hn * sizeof(float));
        if (!p) return 0;
        a->gpu_chans = p; a->gpu_chans_cap = hn;
    }
    cells = atlas_cell_data(a->atlas);
    memcpy(a->gpu_cells, cells, cn * sizeof(float));
    err[0] = 0;
    if (!gpu_run_custom(a->gpu, a->gpu_cells, res, steps,
                        a->gpu_cells, a->gpu_chans, err, sizeof(err))) {
        a->backend_gpu = 0;
        app_set_last(a, err[0] ? err : "GPU failed; CPU fallback");
        return 0;
    }
    memcpy(cells, a->gpu_cells, cn * sizeof(float));
    memcpy(atlas_channel_data(a->atlas), a->gpu_chans, hn * sizeof(float));
    app_colorize(a);
    app_read_selected(a);
    a->steps_last = steps;
    return 1;
}

static void app_advance(App *a, int steps)
{
    if (!a || !a->atlas) return;
    if (a->backend_gpu && app_gpu_advance(a, steps > 0 ? steps : 0)) return;
    atlas_step(a->atlas, &a->exp, steps > 0 ? steps : 0);
    app_colorize(a);
    a->steps_last = steps;
    app_read_selected(a);
}

static int app_read_disk_lua(App *a, char **data, size_t *len)
{
    char dir[MAX_PATH];
    platform_exe_dir(dir, sizeof(dir));
    snprintf(a->hot_path, sizeof(a->hot_path), "%s\\experiments\\experiment.lua", dir);
    if (platform_read_file(a->hot_path, data, len)) { a->hot_from_disk = 1; return 1; }
    snprintf(a->hot_path, sizeof(a->hot_path), "%s\\hot.lua", dir);
    if (platform_read_file(a->hot_path, data, len)) { a->hot_from_disk = 1; return 1; }
    a->hot_from_disk = 0;
    return 0;
}

static int app_load_custom(App *a)
{
    CustomSpec spec;
    CustomModel tmp;
    char err[512];
    if (!a || !a->lua) { if (a) a->has_custom = 0; return 0; }
    if (!lua_hot_custom(a->lua, &spec, err, sizeof(err))) {
        a->has_custom = 0;
        return 0;
    }
    if (!custom_model_build(&tmp, &spec, err, sizeof(err))) {
        a->has_custom = 0;
        return 0;
    }
    a->custom = tmp;
    custom_model_rebind(&a->custom);
    a->has_custom = 1;
    return 1;
}

static const Model *app_resolve_model(App *a, const char *id)
{
    if (id && strcmp(id, "custom") == 0 && a && a->has_custom)
        return &a->custom.model;
    return model_by_id(id);
}

static void app_reload(App *a)
{
    char *disk = NULL;
    size_t len = 0;
    char err[1024];
    if (!a) return;
    if (!app_read_disk_lua(a, &disk, &len)) {
        app_error_box(a, "experiments/experiment.lua not found next to dp-phase.exe");
        return;
    }
    if (!a->lua) {
        a->lua = lua_hot_create(disk, len, a->hot_path, "cpu", a->exp.res, err, sizeof(err));
        if (!a->lua) {
            app_error_box(a, err[0] ? err : "Lua experiment failed to load");
            platform_free(disk);
            return;
        }
        a->use_lua = 1;
    } else if (!lua_hot_reload(a->lua, disk, len, a->hot_path, err, sizeof(err))) {
        char msg[1200];
        snprintf(msg, sizeof(msg), "experiment reload failed: %s", err[0] ? err : "unknown error");
        app_error_box(a, msg);
        platform_free(disk);
        return;
    }
    {
        int was_custom = strcmp(a->exp.model, "custom") == 0;
        app_load_custom(a);
        if (was_custom) {
            if (a->has_custom) {
                int res = a->exp.res;
                exp_init(&a->exp, &a->custom.model);
                a->exp.res = res;
                a->selected = -1;
            } else {
                app_set_model(a, "double_pendulum");
            }
        }
    }
    app_apply_schema(a, 0);
    if (strcmp(a->exp.model, "custom") == 0) {
        app_prepare_custom_gpu(a);
        if (!a->gpu_custom_ok) a->backend_gpu = 0;
    } else {
        a->backend_gpu = 0;
    }
    app_load_palette(a);
    app_apply_experiment(a);
    app_set_last(a, "schema/palette reloaded");
    app_set_title(a, 1);
    platform_free(disk);
}

static int parse_assignment(const char *value, char *name, size_t namesz, double *v)
{
    const char *eq;
    char *end;
    size_t n;
    if (!value || !(eq = strchr(value, '='))) return 0;
    n = (size_t)(eq - value);
    if (n == 0 || n >= namesz) return 0;
    memcpy(name, value, n); name[n] = 0;
    *v = strtod(eq + 1, &end);
    return end != eq + 1;
}

static int parse_axis_range(const char *value, int *which, double *lo, double *hi)
{
    const char *eq;
    char *end;
    if (!value || !(eq = strchr(value, '='))) return 0;
    if (eq - value == 1 && (value[0] == 'x' || value[0] == 'y')) *which = value[0] == 'x' ? 0 : 1;
    else return 0;
    *lo = strtod(eq + 1, &end);
    if (*end != ',') return 0;
    *hi = strtod(end + 1, NULL);
    return *hi > *lo;
}

static int app_export_path(App *a, const char *ext, char *out, size_t outsz)
{
    char dir[MAX_PATH];
    char base[MAX_PATH];
    SYSTEMTIME t;
    if (!a || !ext || !out || outsz == 0) return 0;
    platform_exe_dir(dir, sizeof(dir));
    snprintf(base, sizeof(base), "%s\\exports", dir);
    CreateDirectoryA(base, NULL);
    GetLocalTime(&t);
    snprintf(out, outsz, "%s\\atlas-%04d%02d%02d-%02d%02d%02d.%s",
             base, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, ext);
    return 1;
}

static int app_export_png(App *a)
{
    char path[MAX_PATH], err[256];
    if (!a || !a->atlas) { app_set_last(a, "export: no atlas"); return 0; }
    if (!app_export_path(a, "png", path, sizeof(path))) return 0;
    if (!png_write_rgba(path, atlas_rgba(a->atlas), atlas_res(a->atlas),
                        atlas_res(a->atlas), err, sizeof(err))) {
        app_set_last(a, err);
        return 0;
    }
    app_set_last(a, path);
    return 1;
}

static int app_export_csv(App *a)
{
    char path[MAX_PATH];
    FILE *f;
    int x, y, i;
    if (!a || !a->atlas) { app_set_last(a, "export: no atlas"); return 0; }
    if (!app_export_path(a, "csv", path, sizeof(path))) return 0;
    f = fopen(path, "wb");
    if (!f) { app_set_last(a, "export: cannot create csv"); return 0; }
    fprintf(f, "model,ix,iy,x_%s,y_%s",
            a->exp.p[a->exp.axis_x].name, a->exp.p[a->exp.axis_y].name);
    for (i = 0; i < a->exp.mdl->state_dim && i < MODEL_MAX_STATE; ++i)
        if (a->exp.mdl->state_names[i]) fprintf(f, ",state_%s", a->exp.mdl->state_names[i]);
    for (i = 0; i < a->exp.nch; ++i) fprintf(f, ",ch_%s", exp_channel_name(&a->exp, i));
    fprintf(f, "\n");
    for (y = 0; y < atlas_res(a->atlas); ++y) {
        for (x = 0; x < atlas_res(a->atlas); ++x) {
            float st[MODEL_MAX_STATE], vals[MODEL_MAX_PARAMS], ch[MODEL_MAX_CHANNELS];
            double axis_vals[MODEL_MAX_PARAMS];
            int idx = y * atlas_res(a->atlas) + x;
            atlas_read_cell(a->atlas, idx, st, vals, ch);
            exp_axis_values(&a->exp, x, y, axis_vals);
            fprintf(f, "%s,%d,%d,%.9g,%.9g", a->exp.mdl->id, x, y,
                    axis_vals[a->exp.axis_x], axis_vals[a->exp.axis_y]);
            for (i = 0; i < a->exp.mdl->state_dim && i < MODEL_MAX_STATE; ++i)
                if (a->exp.mdl->state_names[i]) fprintf(f, ",%.9g", (double)st[i]);
            for (i = 0; i < a->exp.nch; ++i) fprintf(f, ",%.9g", (double)ch[i]);
            fprintf(f, "\n");
        }
    }
    fclose(f);
    app_set_last(a, path);
    return 1;
}


static int app_save_session(App *a, const char *path)
{
    char err[512];
    if (!a || !path) return 0;
    err[0] = 0;
    if (!session_save(path, &a->exp, &a->view, a->backend_gpu, err, sizeof(err))) {
        app_set_last(a, err[0] ? err : "save failed");
        return 0;
    }
    app_set_last(a, path);
    return 1;
}

static int app_load_session(App *a, const char *path)
{
    SessionData s;
    const Model *m;
    char err[512];
    int i, ax, ay;
    if (!a || !path) return 0;
    err[0] = 0;
    if (!session_load(path, &s, err, sizeof(err))) {
        app_set_last(a, err[0] ? err : "load failed");
        return 0;
    }
    if (strcmp(s.model, "custom") == 0 && !a->has_custom) {
        app_set_last(a, "load: custom model is not defined in experiment.lua");
        return 0;
    }
    m = app_resolve_model(a, s.model);
    if (!m) { app_set_last(a, "load: unknown model"); return 0; }
    if (a->pview) { pendulum_view_destroy(a->pview); a->pview = NULL; }
    exp_init(&a->exp, m);
    if (a->lua) app_apply_schema(a, 1);
    for (i = 0; i < s.nparams; ++i) {
        int idx = exp_index(&a->exp, s.params[i].name);
        double v;
        ExpParam *p;
        if (idx < 0) continue;
        p = &a->exp.p[idx];
        v = s.params[i].value;
        if (v < p->min) v = p->min;
        if (v > p->max) v = p->max;
        p->value = v;
    }
    ax = exp_index(&a->exp, s.axis_x);
    ay = exp_index(&a->exp, s.axis_y);
    if (ax >= 0 && ay >= 0 && ax != ay &&
        a->exp.p[ax].axis_ok && a->exp.p[ay].axis_ok) {
        a->exp.axis_x = ax;
        a->exp.axis_y = ay;
    }
    if (s.xmax > s.xmin) { a->exp.xmin = s.xmin; a->exp.xmax = s.xmax; }
    if (s.ymax > s.ymin) { a->exp.ymin = s.ymin; a->exp.ymax = s.ymax; }
    a->exp.res = dp_clampi(s.res, 16, 512);
    a->exp.dt = s.dt > 0.0 ? s.dt : 0.01;
    a->exp.max_steps = dp_clampi(s.steps, 0, 256);
    a->exp.grid = s.grid;
    a->exp.periodic = s.periodic;
    a->exp.circle = s.circle;
    a->exp.channel = 0;
    for (i = 0; i < a->exp.nch; ++i)
        if (strcmp(exp_channel_name(&a->exp, i), s.channel) == 0) a->exp.channel = i;
    a->exp.post = 0;
    if (strcmp(s.post, "gradient") == 0) a->exp.post = 1;
    else if (strcmp(s.post, "range") == 0) a->exp.post = 2;
    else if (strcmp(s.post, "variance") == 0) a->exp.post = 3;
    if (s.palette[0]) strncpy(a->exp.palette, s.palette, sizeof(a->exp.palette) - 1);
    if (a->use_lua && a->lua) lua_hot_set_scheme(a->lua, a->exp.palette, NULL, 0);
    a->selected = -1;
    pv_reset(&a->view);
    a->view.cx = s.view_cx;
    a->view.cy = s.view_cy;
    a->view.zoom = s.view_zoom >= 0.25 ? s.view_zoom : 1.0;
    if (a->atlas) atlas_destroy(a->atlas);
    a->atlas = atlas_create(&a->exp, a->exp.res, err, sizeof(err));
    if (!a->atlas) { app_set_last(a, err[0] ? err : "atlas creation failed"); return 0; }
    a->backend_gpu = s.backend_gpu ? 1 : 0;
    if (strcmp(a->exp.model, "custom") == 0) {
        app_prepare_custom_gpu(a);
        if (!a->gpu_custom_ok) a->backend_gpu = 0;
    } else {
        a->backend_gpu = 0;
    }
    app_load_palette(a);
    app_colorize(a);
    app_read_selected(a);
    a->acc = 0.0;
    a->steps_last = 0;
    if (a->plat && !a->opt.capture_path && a->right_x > 0 &&
        strcmp(a->exp.model, "double_pendulum") == 0) {
        a->pview = pendulum_view_create(a->inst, L"Double Pendulum",
                                        a->right_x, 16, a->right_w, a->pend_h,
                                        a, app_pend_close, app_pend_key);
    }
    app_set_title(a, 1);
    app_set_last(a, path);
    return 1;
}

static int app_dialog_save(App *a)
{
    char file[MAX_PATH];
    OPENFILENAMEA ofn;
    if (!a || !a->plat) return 0;
    memset(&ofn, 0, sizeof(ofn));
    snprintf(file, sizeof(file), "experiment.dps");
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = platform_hwnd(a->plat);
    ofn.lpstrFilter = "dp-phase experiment (*.dps)\0*.dps\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrDefExt = "dps";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn)) { app_set_last(a, "save canceled"); return 0; }
    return app_save_session(a, file);
}

static int app_dialog_load(App *a)
{
    char file[MAX_PATH];
    OPENFILENAMEA ofn;
    if (!a || !a->plat) return 0;
    memset(&ofn, 0, sizeof(ofn));
    file[0] = 0;
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = platform_hwnd(a->plat);
    ofn.lpstrFilter = "dp-phase experiment (*.dps)\0*.dps\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrDefExt = "dps";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) { app_set_last(a, "load canceled"); return 0; }
    return app_load_session(a, file);
}

static int app_do_action(App *a, const char *name, const char *value)
{
    if (!a || !name) return 0;

    if (strcmp(name, "reset") == 0) { app_reset(a); return 1; }
    if (strcmp(name, "pause") == 0) {
        a->exp.paused = !a->exp.paused;
        app_set_last(a, a->exp.paused ? "paused" : "running");
        app_set_title(a, 1);
        return 1;
    }
    if (strcmp(name, "palette") == 0) {
        static const char *schemes[] = { "phase", "ember", "ice", "mono" };
        int i, n = 4, cur = 0;
        for (i = 0; i < n; ++i) if (strcmp(a->exp.palette, schemes[i]) == 0) cur = i;
        strcpy(a->exp.palette, schemes[(cur + 1) % n]);
        if (a->use_lua && a->lua) lua_hot_set_scheme(a->lua, a->exp.palette, NULL, 0);
        app_load_palette(a);
        app_colorize(a);
        app_set_last(a, "palette changed");
        return 1;
    }
    if (strcmp(name, "palette_set") == 0 && value && value[0]) {
        strncpy(a->exp.palette, value, sizeof(a->exp.palette) - 1);
        if (a->use_lua && a->lua) lua_hot_set_scheme(a->lua, value, NULL, 0);
        app_load_palette(a);
        app_colorize(a);
        app_set_last(a, value);
        return 1;
    }
    if (strcmp(name, "reload") == 0) { app_reload(a); return 1; }
    if (strcmp(name, "grid") == 0 || strcmp(name, "periodic") == 0 || strcmp(name, "circle") == 0) {
        int cur = (strcmp(name, "grid") == 0) ? a->exp.grid :
                  (strcmp(name, "periodic") == 0) ? a->exp.periodic : a->exp.circle;
        int val = (value && value[0]) ? (value[0] != '0') : !cur;
        if (strcmp(name, "grid") == 0) a->exp.grid = val;
        else if (strcmp(name, "periodic") == 0) a->exp.periodic = val;
        else a->exp.circle = val;
        app_set_last(a, name);
        return 1;
    }
    if (strcmp(name, "view_reset") == 0) { pv_reset(&a->view); app_set_last(a, "view reset"); return 1; }
    if (strcmp(name, "quit") == 0) { a->quit = 1; PostQuitMessage(0); return 1; }

    if (strcmp(name, "param_set") == 0) {
        char pname[EXP_NAME];
        double v;
        if (!parse_assignment(value, pname, sizeof(pname), &v)) {
            app_set_last(a, "param_set: expected name=value"); return 0;
        }
        if (!exp_set_value(&a->exp, pname, v)) {
            app_set_last(a, "param_set: out of range"); return 0;
        }
        app_set_last(a, pname);
        return 1;
    }
    if (strcmp(name, "axis_set") == 0) {
        int which;
        const char *eq;
        if (!value || !(eq = strchr(value, '=')) || eq - value != 1) return 0;
        which = value[0] == 'x' ? 0 : (value[0] == 'y' ? 1 : -1);
        if (which < 0 || !exp_set_axis(&a->exp, which, eq + 1)) {
            app_set_last(a, "axis_set failed"); return 0;
        }
        app_apply_experiment(a);
        return 1;
    }
    if (strcmp(name, "range_set") == 0) {
        int which;
        double lo, hi;
        if (!parse_axis_range(value, &which, &lo, &hi)) {
            app_set_last(a, "range_set: expected x=lo,hi"); return 0;
        }
        exp_set_range(&a->exp, which, lo, hi);
        app_set_last(a, "range changed");
        return 1;
    }
    if (strcmp(name, "res_set") == 0 && value) {
        int r = atoi(value);
        if (r < 16 || r > 512) { app_set_last(a, "res out of range"); return 0; }
        a->exp.res = r;
        app_set_last(a, "resolution changed");
        return 1;
    }
    if (strcmp(name, "dt_set") == 0 && value) {
        double d = atof(value);
        if (d <= 0.0) { app_set_last(a, "dt must be positive"); return 0; }
        a->exp.dt = d;
        app_set_last(a, "dt changed");
        return 1;
    }
    if (strcmp(name, "steps_set") == 0 && value) {
        int s = atoi(value);
        if (s < 1) s = 1;
        if (s > DP_MAX_STEPS) s = DP_MAX_STEPS;
        a->exp.max_steps = s;
        app_set_last(a, "steps changed");
        return 1;
    }
    if (strcmp(name, "channel_set") == 0 && value) {
        int i;
        for (i = 0; i < a->exp.nch; ++i) if (strcmp(exp_channel_name(&a->exp, i), value) == 0) break;
        if (i >= a->exp.nch) { app_set_last(a, "unknown channel"); return 0; }
        a->exp.channel = i;
        app_colorize(a);
        app_set_last(a, value);
        app_set_title(a, 1);
        return 1;
    }
    if (strcmp(name, "post_set") == 0 && value) {
        if (strcmp(value, "raw") == 0) a->exp.post = 0;
        else if (strcmp(value, "gradient") == 0) a->exp.post = 1;
        else if (strcmp(value, "range") == 0) a->exp.post = 2;
        else if (strcmp(value, "variance") == 0) a->exp.post = 3;
        else { app_set_last(a, "unknown post"); return 0; }
        app_colorize(a);
        app_set_last(a, value);
        return 1;
    }
    if (strcmp(name, "preset") == 0 && value) {
        if (strcmp(a->exp.model, "double_pendulum") != 0) app_set_model(a, "double_pendulum");
        if (strcmp(value, "masses") == 0) {
            exp_set_axis(&a->exp, 0, "m1");
            exp_set_axis(&a->exp, 1, "m2");
            exp_set_range(&a->exp, 0, 0.2, 3.0);
            exp_set_range(&a->exp, 1, 0.2, 3.0);
            exp_set_value(&a->exp, "theta1", 0.5);
            exp_set_value(&a->exp, "theta2", 0.0);
            exp_set_value(&a->exp, "omega1", 0.0);
            exp_set_value(&a->exp, "omega2", 0.0);
            a->exp.channel = 3;
            a->exp.post = 2;
        } else {
            exp_set_axis(&a->exp, 0, "theta1");
            exp_set_axis(&a->exp, 1, "theta2");
            exp_set_range(&a->exp, 0, -DP_PI, DP_PI);
            exp_set_range(&a->exp, 1, -DP_PI, DP_PI);
            a->exp.channel = 0;
            a->exp.post = 0;
        }
        a->selected = -1;
        pv_reset(&a->view);
        app_apply_experiment(a);
        app_set_last(a, value);
        return 1;
    }
    if (strcmp(name, "model_set") == 0 && value) {
        app_set_model(a, value);
        return 1;
    }
    if (strcmp(name, "backend_set") == 0) {
        if (value && strcmp(value, "gpu") == 0) {
            if (strcmp(a->exp.model, "custom") != 0 || !a->has_custom) {
                a->backend_gpu = 0;
                app_set_last(a, "GPU backend requires a Lua expression model");
            } else if (!app_prepare_custom_gpu(a)) {
                a->backend_gpu = 0;
                app_set_last(a, "GPU shader unavailable; CPU fallback");
            } else {
                a->backend_gpu = 1;
                app_set_last(a, "backend GPU");
            }
        } else {
            a->backend_gpu = 0;
            app_set_last(a, "backend CPU");
        }
        app_set_title(a, 1);
        return 1;
    }
    if (strcmp(name, "save_experiment") == 0) { app_dialog_save(a); return 1; }
    if (strcmp(name, "load_experiment") == 0) { app_dialog_load(a); return 1; }
    if (strcmp(name, "export_png") == 0) { app_export_png(a); return 1; }
    if (strcmp(name, "export_csv") == 0) { app_export_csv(a); return 1; }
    if (strcmp(name, "apply") == 0) { app_apply_experiment(a); return 1; }
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
        a->dragging = 1; a->drag_x = ev->b; a->drag_y = ev->c; return;
    }
    if (ev->type == DP_EV_MOUSE_UP && ev->a == 0) { a->dragging = 0; return; }
    if (ev->type == DP_EV_MOUSE_MOVE && a->dragging) {
        pv_pan(&a->view, 1.0, w, h, ev->a - a->drag_x, ev->b - a->drag_y);
        a->drag_x = ev->a; a->drag_y = ev->b;
        return;
    }
    if (ev->type == DP_EV_MOUSE_DOWN && ev->a == 1) {
        int cx, cy;
        if (pv_pick(&a->view, 1.0, atlas_res(a->atlas), w, h, ev->b, ev->c, &cx, &cy)) {
            char msg[64];
            a->selected = cy * atlas_res(a->atlas) + cx;
            a->sel_x = cx; a->sel_y = cy;
            app_read_selected(a);
            snprintf(msg, sizeof(msg), "selected (%d, %d)", cx, cy);
            app_set_last(a, msg);
        } else {
            a->selected = -1;
            app_set_last(a, "selection cleared");
        }
        return;
    }
    if (ev->type == DP_EV_WHEEL) {
        double factor = (ev->a > 0) ? 1.15 : 1.0 / 1.15;
        pv_zoom_at(&a->view, 1.0, w, h, ev->b, ev->c, factor);
        return;
    }
}

static void app_event_cb(void *user, const DpEvent *ev) { app_event((App *)user, ev); }

static int panel_action_cb(void *user, const char *name, const char *value,
                           char *msg, size_t msgsz)
{
    App *a = (App *)user;
    int ok = app_do_action(a, name, value);
    if (msg && msgsz) snprintf(msg, msgsz, "%s", a ? a->last_action : "");
    return ok;
}

static void panel_status_cb(void *user, PanelStatus *st)
{
    App *a = (App *)user;
    if (!a || !st) return;
    memset(st, 0, sizeof(*st));
    st->paused = a->exp.paused;
    st->kernel_gpu = a->backend_gpu && a->gpu_custom_ok;
    st->res = a->exp.res;
    st->fps = (int)(a->fps + 0.5);
    st->grid = a->exp.grid;
    st->periodic = a->exp.periodic;
    st->circle = a->exp.circle;
    st->selected = a->selected >= 0;
    st->sel_x = a->sel_x; st->sel_y = a->sel_y;
    st->zoom = a->view.zoom;
    strncpy(st->scheme, a->exp.palette, sizeof(st->scheme) - 1);
    strncpy(st->source, a->hot_from_disk ? "experiments/experiment.lua" : "no experiment script",
            sizeof(st->source) - 1);
    strncpy(st->last, a->last_action, sizeof(st->last) - 1);
}

static void append_list(char *out, size_t outsz, const char *fmt, ...)
{
    size_t n = strlen(out);
    va_list ap;
    if (n >= outsz - 1) return;
    va_start(ap, fmt);
    vsnprintf(out + n, outsz - n, fmt, ap);
    va_end(ap);
}

static void panel_query_cb(void *user, const char *what, char *out, size_t outsz)
{
    App *a = (App *)user;
    int i;
    if (!out || outsz == 0) return;
    out[0] = 0;
    if (!a || !what) return;
    if (strcmp(what, "params") == 0) {
        for (i = 0; i < a->exp.np; ++i) {
            ExpParam *p = &a->exp.p[i];
            append_list(out, outsz, "{%s %s %.9g %.9g %.9g %.9g %d %d} ",
                        p->name, p->label, p->value, p->min, p->max, p->step,
                        p->axis_ok, p->group);
        }
    } else if (strcmp(what, "axes") == 0) {
        append_list(out, outsz, "%s %s %.9g %.9g %.9g %.9g",
                    a->exp.p[a->exp.axis_x].name, a->exp.p[a->exp.axis_y].name,
                    a->exp.xmin, a->exp.xmax, a->exp.ymin, a->exp.ymax);
    } else if (strcmp(what, "channels") == 0) {
        for (i = 0; i < a->exp.nch; ++i) {
            ExpChannelInfo ci;
            exp_channel_info(&a->exp, i, &ci);
            append_list(out, outsz, "{%s %s {%s} %.9g %.9g %d} ",
                        ci.name, ci.label, ci.unit, ci.min, ci.max, ci.cyclic);
        }
    } else if (strcmp(what, "view") == 0) {
        const char *post = "raw";
        if (a->exp.post == 1) post = "gradient";
        else if (a->exp.post == 2) post = "range";
        else if (a->exp.post == 3) post = "variance";
        append_list(out, outsz, "%s %s %s", exp_channel_name(&a->exp, a->exp.channel), post, a->exp.palette);
    } else if (strcmp(what, "integration") == 0) {
        append_list(out, outsz, "%d %.9g %d", a->exp.res, a->exp.dt, a->exp.max_steps);
    } else if (strcmp(what, "models") == 0) {
        for (i = 0; i < model_count(); ++i) {
            const Model *m = model_at(i);
            append_list(out, outsz, "{%s {%s}} ", m->id, m->title);
        }
        if (a->has_custom)
            append_list(out, outsz, "{%s {%s}} ", a->custom.model.id, a->custom.model.title);
    } else if (strcmp(what, "model") == 0) {
        append_list(out, outsz, "%s %s", a->exp.mdl->id, a->exp.mdl->title);
    } else if (strcmp(what, "palette") == 0) {
        for (i = 0; i < 32; ++i) {
            int k = i * (DP_LUT_N - 1) / 31;
            append_list(out, outsz, "#%02x%02x%02x ",
                        a->lut[k * 3 + 0], a->lut[k * 3 + 1], a->lut[k * 3 + 2]);
        }
    } else if (strcmp(what, "selected") == 0) {
        int n = 0, k;
        if (a->selected < 0) {
            snprintf(out, outsz, "click a pixel to inspect");
            return;
        }
        n += snprintf(out + n, outsz - n, "cell (%d, %d)\n", a->sel_x, a->sel_y);
        n += snprintf(out + n, outsz - n, "x: %s = %.6g    y: %s = %.6g\n",
                      a->exp.p[a->exp.axis_x].name, a->sel_vals[a->exp.axis_x],
                      a->exp.p[a->exp.axis_y].name, a->sel_vals[a->exp.axis_y]);
        n += snprintf(out + n, outsz - n, "state:\n");
        for (k = 0; k < a->exp.mdl->state_dim && k < MODEL_MAX_STATE; ++k) {
            if (!a->exp.mdl->state_names[k]) continue;
            n += snprintf(out + n, outsz - n, "  %s = %.6g\n",
                          a->exp.mdl->state_names[k], a->sel_state[k]);
        }
        n += snprintf(out + n, outsz - n, "channels:\n");
        for (k = 0; k < a->exp.nch; ++k) {
            ExpChannelInfo ci;
            exp_channel_info(&a->exp, k, &ci);
            n += snprintf(out + n, outsz - n, "  %s = %.6g %s\n",
                          ci.label, a->sel_ch[k], ci.unit);
        }
    } else if (strcmp(what, "source") == 0) {
        append_list(out, outsz, "%s", a->hot_path[0] ? a->hot_path : "(none)");
    } else if (strcmp(what, "title") == 0) {
        append_list(out, outsz, "%s", a->exp.title);
    }
}

static void panel_quit_cb(void *user)
{
    App *a = (App *)user;
    if (a) a->quit = 1;
    PostQuitMessage(0);
}

static void app_pend_close(void *user)
{
    App *a = (App *)user;
    if (a) a->quit = 1;
    PostQuitMessage(0);
}

static void app_pend_key(void *user, int vk, int down) { app_key((App *)user, vk, down); }

void app_key(App *a, int vk, int down)
{
    DpEvent ev;
    ev.type = DP_EV_KEY; ev.a = vk; ev.b = down; ev.c = 0;
    app_event(a, &ev);
}

static void app_render_phase(App *a)
{
    static float verts[16384 * 6];
    int w, h, grid_n = 0, total;
    if (!a || !a->gpu || !a->plat) return;
    w = platform_width(a->plat);
    h = platform_height(a->plat);
    if (a->legacy) gpu_draw_view(a->gpu, w, h, &a->view);
    else gpu_draw_atlas_view(a->gpu, w, h, &a->view);
    total = pv_build_overlay(&a->view, 1.0, atlas_res(a->atlas), w, h,
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
    char gpu_err[512];
    char lua_err[512];

    if (err && errsz) err[0] = 0;
    a = (App *)calloc(1, sizeof(App));
    if (!a) { if (err) snprintf(err, errsz, "app: out of memory"); return NULL; }
    a->inst = inst;
    a->opt = *opt;
    a->selected = -1;
    {
        char *disk = NULL;
        size_t len = 0;
        int initial_res = opt->force_res > 0 ? dp_clampi(opt->force_res, 16, 512) : 64;
        if (app_read_disk_lua(a, &disk, &len)) {
            lua_err[0] = 0;
            a->lua = lua_hot_create(disk, len, a->hot_path, "cpu", initial_res, lua_err, sizeof(lua_err));
            if (a->lua) a->use_lua = 1;
            platform_free(disk);
        }
    }
    app_load_custom(a);
    exp_init(&a->exp, app_resolve_model(a, opt->model ? opt->model : "double_pendulum"));
    app_apply_schema(a, 1);
    if (a->lua) lua_hot_set_res(a->lua, a->exp.res);
    if (opt->force_res > 0) a->exp.res = dp_clampi(opt->force_res, 16, 512);
    if (opt->axis_x) exp_set_axis(&a->exp, 0, opt->axis_x);
    if (opt->axis_y) exp_set_axis(&a->exp, 1, opt->axis_y);
    {
        int si;
        for (si = 0; si < opt->nset; ++si) {
            const char *sv = opt->set_values[si];
            const char *eq = strchr(sv, '=');
            if (eq) {
                char nm[EXP_NAME];
                size_t nn = (size_t)(eq - sv);
                if (nn > 0 && nn < sizeof(nm)) {
                    memcpy(nm, sv, nn); nm[nn] = 0;
                    exp_set_value(&a->exp, nm, atof(eq + 1));
                }
            }
        }
    }
    if (opt->channel) {
        int ci;
        for (ci = 0; ci < a->exp.nch; ++ci)
            if (strcmp(exp_channel_name(&a->exp, ci), opt->channel) == 0) a->exp.channel = ci;
    }
    if (opt->post) {
        if (strcmp(opt->post, "gradient") == 0) a->exp.post = 1;
        else if (strcmp(opt->post, "range") == 0) a->exp.post = 2;
        else if (strcmp(opt->post, "variance") == 0) a->exp.post = 3;
    }
    pv_init(&a->view);
    dp_phase_lut(a->lut);
    if (a->lua) lua_hot_set_res(a->lua, a->exp.res);
    app_load_palette(a);

    need_display = !(opt->headless && opt->force_kernel == DP_KERNEL_CPU);

    a->atlas = atlas_create(&a->exp, a->exp.res, err, errsz);
    if (!a->atlas) { app_destroy(a); return NULL; }

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
        a->plat = platform_create(inst, L"Phase Space", side, opt->headless, a, app_event_cb);
        if (!a->plat) {
            if (err) snprintf(err, errsz, "app: window creation failed");
            app_destroy(a);
            return NULL;
        }
        platform_move_client(a->plat, 16, 16);
        gpu_err[0] = 0;
        a->gpu = gpu_create(platform_hwnd(a->plat), platform_hdc(a->plat),
                            opt->force_legacy, gpu_err, sizeof(gpu_err));
        if (!a->gpu) {
            if (err) snprintf(err, errsz, "app: %s", gpu_err[0] ? gpu_err : "no OpenGL");
            app_destroy(a);
            return NULL;
        }
        a->legacy = gpu_legacy(a->gpu);
        platform_set_vsync(a->plat, gpu_vsync(a->gpu));
        gpu_set_range(a->gpu, 1.0);
        app_prepare_custom_gpu(a);
        if (opt->force_kernel == DP_KERNEL_GPU && a->gpu_custom_ok &&
            strcmp(a->exp.model, "custom") == 0)
            a->backend_gpu = 1;

        if (!opt->capture_path) {
            int side_px = platform_width(a->plat);
            a->right_x = 16 + side_px + 24;
            a->right_w = dp_clampi((int)(side_px * 0.52), 300, 380);
            a->pend_h = dp_clampi((int)(side_px * 0.42), 220, 340);
            if (strcmp(a->exp.model, "double_pendulum") == 0) {
                a->pview = pendulum_view_create(inst, L"Double Pendulum",
                                                a->right_x, 16, a->right_w, a->pend_h,
                                                a, app_pend_close, app_pend_key);
            }
            {
                RECT work;
                int panel_y, panel_h;
                PanelCallbacks cb;
                char panel_err[512];
                char dir[MAX_PATH], ui_path[MAX_PATH];
                platform_exe_dir(dir, sizeof(dir));
                snprintf(ui_path, sizeof(ui_path), "%s\\ui\\panel.tcl", dir);
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                panel_y = 16 + a->pend_h + 46;
                panel_h = dp_clampi((int)(work.bottom - work.top) - panel_y - 16, 420, 620);
                memset(&cb, 0, sizeof(cb));
                cb.user = a;
                cb.action = panel_action_cb;
                cb.status = panel_status_cb;
                cb.query = panel_query_cb;
                cb.quit = panel_quit_cb;
                panel_err[0] = 0;
                a->panel = panel_create(&cb, ui_path, a->right_x, panel_y,
                                        a->right_w, panel_h, panel_err, sizeof(panel_err));
                if (!a->panel) {
                    char logpath[MAX_PATH];
                    DWORD ln = GetTempPathA(MAX_PATH, logpath);
                    FILE *lf;
                    if (ln) {
                        strncat(logpath, "dp-panel-error.txt", MAX_PATH - strlen(logpath) - 1);
                        lf = fopen(logpath, "a");
                        if (lf) { fprintf(lf, "panel failed: %s\n", panel_err[0] ? panel_err : "(no error)"); fclose(lf); }
                    }
                }
            }
        }
    }

    a->fps_t0 = platform_now();
    app_colorize(a);
    if (opt->load_path) {
        if (app_load_session(a, opt->load_path) && a->panel)
            panel_eval(a->panel, "rebuild_schema; update_ui");
    }
    if (opt->save_path) app_save_session(a, opt->save_path);
    app_set_last(a, "ready");
    app_set_title(a, 1);
    return a;
}

void app_destroy(App *a)
{
    if (!a) return;
    if (a->panel) panel_destroy(a->panel);
    if (a->pview) pendulum_view_destroy(a->pview);
    if (a->gpu) gpu_destroy(a->gpu);
    if (a->atlas) atlas_destroy(a->atlas);
    if (a->lua) lua_hot_destroy(a->lua);
    if (a->plat) platform_destroy(a->plat);
    free(a->gpu_cells);
    free(a->gpu_chans);
    free(a);
}

int app_frame(App *a)
{
    double dt;
    int steps = 0;
    if (!a || !a->plat || !a->atlas) return 0;
    dt = platform_dt(a->plat);

    if (!a->exp.paused) {
        double step = a->exp.dt > 0.0 ? a->exp.dt : 0.01;
        int cap = dp_clampi(a->exp.max_steps, 0, DP_MAX_STEPS);
        a->acc += dt;
        while (a->acc >= step && steps < cap) { a->acc -= step; ++steps; }
        if (a->acc > step) a->acc = 0.0;
    }
    if (steps > 0) app_advance(a, steps);
    else a->steps_last = 0;

    if (a->pview) {
        DpParams pp;
        pp.m1 = a->sel_params[0]; pp.m2 = a->sel_params[1];
        pp.l1 = a->sel_params[2]; pp.l2 = a->sel_params[3];
        pp.g = a->sel_params[4]; pp.damping = a->sel_params[5]; pp.dt = a->exp.dt;
        pendulum_view_set_state(a->pview, a->sel_state, a->selected >= 0,
                                a->sel_x, a->sel_y, &pp);
    }
    if (a->gpu) {
        app_render_phase(a);
        platform_frame_end(a->plat);
    }
    a->frame_count++;
    a->fps_frames += 1.0;
    {
        double now = platform_now();
        double span = now - a->fps_t0;
        if (span >= 0.5) { a->fps = a->fps_frames / span; a->fps_frames = 0.0; a->fps_t0 = now; }
    }
    app_set_title(a, 0);
    return 1;
}

int app_loop(App *a)
{
    if (!a) return 0;
    while (!a->quit && !platform_quit(a->plat)) {
        platform_pump(a->plat);
        if (a->panel) panel_pump(a->panel);
        if (a->quit || platform_quit(a->plat)) break;
        app_frame(a);
    }
    return 0;
}

int app_shot(App *a, const char *path, int steps)
{
    int res, ok;
    if (!a || !a->atlas || !path) return 0;
    atlas_reset(a->atlas, &a->exp);
    atlas_step(a->atlas, &a->exp, steps);
    atlas_colorize(a->atlas, &a->exp, a->lut);
    res = atlas_res(a->atlas);
    ok = dp_write_bmp(path, atlas_rgba(a->atlas), res, res);
    if (ok) printf("shot: %s  %dx%d  steps=%d  kernel=cpu  hash=%08X\n",
                   path, res, res, steps, (unsigned)dp_hash_bytes(atlas_rgba(a->atlas), (size_t)res * res * 4u));
    return ok;
}

int app_capture(App *a, const char *path, int steps)
{
    int w, h, ok;
    unsigned char *rgba;
    if (!a || !a->plat || !a->gpu || !a->atlas || !path) return 0;
    app_advance(a, steps);
    w = platform_width(a->plat);
    h = platform_height(a->plat);
    app_render_phase(a);
    rgba = (unsigned char *)malloc((size_t)w * (size_t)h * 4u);
    if (!rgba) return 0;
    ok = gpu_capture_rgba(a->gpu, w, h, rgba);
    if (ok) ok = dp_write_bmp(path, rgba, w, h);
    free(rgba);
    if (ok) printf("capture: %s  %dx%d  steps=%d  kernel=cpu  legacy=%d  renderer=%s\n",
                   path, w, h, steps, a->legacy, gpu_renderer(a->gpu));
    return ok;
}

const char *app_kernel_text(App *a) { (void)a; return "cpu"; }
const char *app_gpu_info(App *a) { return (a && a->gpu) ? gpu_renderer(a->gpu) : "(none)"; }
int app_res(App *a) { return a ? a->exp.res : 0; }