/* selftest.c - 无显示器也可跑的验证：
 *   C 参考 RK4 vs Fortran 内核、线程确定性、Lua 契约、调色板、BMP、GPU 一步同步 */
#include "selftest.h"

#include "bmp.h"
#include "gpu.h"
#include "lua_hot.h"
#include "phase_view.h"
#include "physics.h"
#include "platform.h"
#include "sim.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <GL/gl.h>

static int g_pass;
static int g_fail;

static void check(int cond, const char *what)
{
    if (cond) {
        g_pass++;
        printf("  [ok]   %s\n", what);
    } else {
        g_fail++;
        printf("  [FAIL] %s\n", what);
    }
}

static void checkf(int cond, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    check(cond, buf);
}

static DpConfig test_cfg(int res)
{
    DpConfig c;
    dp_config_defaults(&c);
    c.grid.res = res;
    c.sim.dt = 0.01;
    c.sim.threads = 4;
    return c;
}

/* 可复现的初始扰动，避免每个格子都从同一个匀角速度开始 */
static void scatter_state(Sim *s, unsigned int seed)
{
    int i;
    float *st = sim_state(s);
    unsigned int x = seed ? seed : 1u;
    for (i = 0; i < sim_cells(s); ++i) {
        x = x * 1664525u + 1013904223u;
        st[i * 4 + 0] = (float)(-3.0 + 6.0 * ((x >> 8) & 0xFFFF) / 65535.0);
        x = x * 1664525u + 1013904223u;
        st[i * 4 + 1] = (float)(-3.0 + 6.0 * ((x >> 8) & 0xFFFF) / 65535.0);
        x = x * 1664525u + 1013904223u;
        st[i * 4 + 2] = (float)(-5.0 + 10.0 * ((x >> 8) & 0xFFFF) / 65535.0);
        x = x * 1664525u + 1013904223u;
        st[i * 4 + 3] = (float)(-5.0 + 10.0 * ((x >> 8) & 0xFFFF) / 65535.0);
    }
}

static void test_angles(void)
{
    double a;
    printf("-- angles / field\n");
    a = 0.0; dp_normalize_angle(&a);
    check(fabs(a) < 1e-12, "normalize(0) = 0");
    a = 3.5; dp_normalize_angle(&a);
    check(fabs(a - (3.5 - DP_TWO_PI)) < 1e-12, "normalize(3.5) wraps to [-pi,pi)");
    a = -3.5; dp_normalize_angle(&a);
    check(fabs(a - (-3.5 + DP_TWO_PI)) < 1e-12, "normalize(-3.5) wraps to [-pi,pi)");
    a = DP_PI; dp_normalize_angle(&a);
    check(fabs(a + DP_PI) < 1e-12, "normalize(pi) = -pi");

    checkf(fabsf(dp_field_value(0.0f, 0.0f) - 0.5f) < 1e-6f, "field(0,0) = 0.5");
    checkf(fabsf(dp_field_value((float)DP_PI, (float)DP_PI)) < 1e-5f, "field(pi,pi) = 0 (wrapped)");
    checkf(fabsf(dp_field_value((float)DP_TWO_PI, (float)DP_TWO_PI) - 0.5f) < 1e-5f, "field(2pi,2pi) = 0.5");
    checkf(fabsf(dp_field_value(NAN, 0.0f) - 0.5f) < 1e-6f, "field(NaN) falls back to 0.5");
}

static void test_view(void)
{
    PhaseView v;
    const double range = 4.0 * DP_PI;
    const int w = 800, h = 800, res = 64;
    double wx, wy, zx, zy;
    int cx, cy, grid_n = 0, total = 0;
    float *verts = (float *)malloc(20000u * 6u * sizeof(float));
    if (!verts) { check(0, "view test buffer"); return; }

    printf("-- phase view math\n");
    pv_init(&v);
    check(fabs(pv_span(&v, range) - range) < 1e-9, "zoom 1 shows the whole range");
    pv_screen_to_world(&v, range, w, h, w / 2, h / 2, &wx, &wy);
    check(fabs(wx) < range / (double)w && fabs(wy) < range / (double)h,
          "screen centre maps to world origin (within 1 px)");
    check(pv_pick(&v, range, res, w, h, w / 2, h / 2, &cx, &cy) && cx == res / 2 && cy == res / 2,
          "centre pick -> centre cell");
    check(pv_pick(&v, range, res, w, h, 0, 0, &cx, &cy) && cx == 0 && cy == 0,
          "top-left pick -> (0,0)");
    check(pv_pick(&v, range, res, w, h, w - 1, h - 1, &cx, &cy) &&
          cx == res - 1 && cy == res - 1, "bottom-right pick -> (res-1,res-1)");

    pv_reset(&v);
    pv_screen_to_world(&v, range, w, h, 620, 240, &wx, &wy);
    pv_zoom_at(&v, range, w, h, 620, 240, 2.0);
    pv_screen_to_world(&v, range, w, h, 620, 240, &zx, &zy);
    check(fabs(wx - zx) < 1e-6 && fabs(wy - zy) < 1e-6,
          "wheel zoom keeps the cursor world point fixed");

    pv_reset(&v);
    pv_pan(&v, range, w, h, 80, 0);
    check(fabs(v.cx + 80.0 * range / (double)w) < 1e-9, "drag right moves the view left");

    pv_reset(&v);
    v.zoom = 0.25;
    check(!pv_pick(&v, range, res, w, h, 0, 0, &cx, &cy),
          "non-periodic view rejects a pick outside the field");
    v.periodic = 1;
    check(pv_pick(&v, range, res, w, h, 0, 0, &cx, &cy),
          "periodic view wraps a pick outside the field");

    pv_init(&v);
    v.grid = 1;
    v.circle = 1;
    total = pv_build_overlay(&v, range, res, w, h, 10, 20, verts, 20000, &grid_n);
    check(total > 0 && grid_n > 0 && total > grid_n, "grid + selection circle produce overlay vertices");
    v.grid = 0;
    v.circle = 0;
    total = pv_build_overlay(&v, range, res, w, h, 10, 20, verts, 20000, &grid_n);
    check(total == 0, "grid and circle off -> no overlay");
    v.grid = 1;
    v.circle = 0;
    total = pv_build_overlay(&v, range, res, w, h, 10, 20, verts, 20000, &grid_n);
    check(total > 0 && total == grid_n, "grid only -> grid vertices");
    v.grid = 0;
    v.circle = 1;
    total = pv_build_overlay(&v, range, res, w, h, -1, -1, verts, 20000, &grid_n);
    check(total == 0, "circle without a selection -> no overlay");

    free(verts);
}

static void test_fortran(void)
{
    DpConfig cfg = test_cfg(24);
    Sim *s;
    Sim *s1;
    Sim *s4;
    DpParams p;
    float *initial;
    int cells, i, k;
    double maxd;
    char err[256];

    printf("-- Fortran kernel\n");
    err[0] = 0;
    s = sim_create(&cfg, 4, err, sizeof(err));
    if (!s) { check(0, err); return; }
    cells = sim_cells(s);
    initial = (float *)malloc((size_t)cells * 4u * sizeof(float));
    scatter_state(s, 12345u);
    memcpy(initial, sim_state(s), (size_t)cells * 4u * sizeof(float));

    p.m1 = cfg.physics.m1;
    p.m2 = cfg.physics.m2;
    p.l1 = cfg.physics.l1;
    p.l2 = cfg.physics.l2;
    p.g = cfg.physics.g;
    p.damping = cfg.physics.damping;
    p.dt = cfg.sim.dt;

    sim_step_cpu(s, 1, &p);
    maxd = 0.0;
    for (i = 0; i < cells; ++i) {
        double ref[4];
        for (k = 0; k < 4; ++k) ref[k] = initial[i * 4 + k];
        dp_rk4_step(ref, &p);
        for (k = 0; k < 4; ++k) {
            double d = fabs(ref[k] - (double)sim_state(s)[i * 4 + k]);
            if (d > maxd) maxd = d;
        }
    }
    checkf(maxd < 2e-5, "one step matches C reference (max %.3e)", maxd);

    /* 10 步：Fortran 从 initial 走 10 步，C 从 initial 走 10 步 */
    memcpy(sim_state(s), initial, (size_t)cells * 4u * sizeof(float));
    sim_step_cpu(s, 10, &p);
    maxd = 0.0;
    for (i = 0; i < cells; ++i) {
        double ref[4];
        int st;
        for (k = 0; k < 4; ++k) ref[k] = initial[i * 4 + k];
        for (st = 0; st < 10; ++st) dp_rk4_step(ref, &p);
        for (k = 0; k < 4; ++k) {
            double d = fabs(ref[k] - (double)sim_state(s)[i * 4 + k]);
            if (d > maxd) maxd = d;
        }
    }
    checkf(maxd < 5e-4, "ten steps match C reference (max %.3e)", maxd);

    /* 线程数不同，结果必须逐位相同（每个格子独立、行带切分不影响结果） */
    err[0] = 0;
    s1 = sim_create(&cfg, 1, err, sizeof(err));
    s4 = sim_create(&cfg, 4, err, sizeof(err));
    if (s1 && s4) {
        memcpy(sim_state(s1), initial, (size_t)cells * 4u * sizeof(float));
        memcpy(sim_state(s4), initial, (size_t)cells * 4u * sizeof(float));
        sim_step_cpu(s1, 3, &p);
        sim_step_cpu(s4, 3, &p);
        check(memcmp(sim_state(s1), sim_state(s4), (size_t)cells * 4u * sizeof(float)) == 0,
              "1-thread and 4-thread bands are bitwise identical");
    } else {
        check(0, "thread-pool determinism setup");
    }
    if (s1) sim_destroy(s1);
    if (s4) sim_destroy(s4);
    free(initial);
    sim_destroy(s);
}

static void test_timing(void)
{
    DpConfig cfg;
    Sim *s;
    DpParams p;
    char err[256];
    double ms;
    int threads;

    printf("-- CPU timing\n");
    dp_config_defaults(&cfg);
    cfg.grid.res = 256;
    cfg.sim.threads = 0;
    err[0] = 0;
    s = sim_create(&cfg, cfg.sim.threads, err, sizeof(err));
    if (!s) { check(0, "timing sim_create"); return; }
    p.m1 = cfg.physics.m1; p.m2 = cfg.physics.m2;
    p.l1 = cfg.physics.l1; p.l2 = cfg.physics.l2;
    p.g = cfg.physics.g; p.damping = cfg.physics.damping;
    p.dt = 0.1;
    ms = sim_bench(s, 1, &p);
    threads = sim_threads(s);
    printf("  [info] 256x256 x 1 step: %.2f ms, %.2f Mcell-steps/s, %d threads\n",
           ms, (double)cfg.grid.res * cfg.grid.res / (ms * 1000.0), threads);
    check(ms > 0.0 && ms < 5000.0, "CPU step completes in sane time");
    sim_destroy(s);
}

static void test_lua(void)
{
    const void *data = NULL;
    size_t len = 0;
    LuaHot *h;
    DpConfig cfg;
    unsigned char lut[DP_LUT_N * 3];
    DpParams p;
    int max_steps = 0;
    int steps = 0;
    char err[1024];
    char action[64];
    char title[256];
    char resstr[32];
    double script_dt = 0.1;

    printf("-- Lua hot logic\n");
    dp_config_defaults(&cfg);
    if (!platform_read_resource(1001, &data, &len) || !data || len == 0) {
        check(0, "embedded hot.lua resource present");
        return;
    }
    check(1, "embedded hot.lua resource present");

    err[0] = 0;
    h = lua_hot_create((const char *)data, len, "hot.lua", "gpu", 256, err, sizeof(err));
    if (!h) { checkf(0, "lua_hot_create: %s", err); return; }
    check(1, "Lua state created with sandbox libs");

    if (lua_hot_defaults(h, &cfg, err, sizeof(err))) {
        script_dt = cfg.sim.dt;
        checkf(cfg.grid.res >= 32 && cfg.grid.res <= 1024, "defaults.res = %d (in range)", cfg.grid.res);
        checkf(cfg.grid.range > 0.1 && cfg.grid.range <= 64.0, "defaults.range = %.3f (in range)", cfg.grid.range);
        checkf(cfg.sim.dt >= 1e-4 && cfg.sim.dt <= 0.5, "defaults.dt = %.3f (in range)", cfg.sim.dt);
        checkf(cfg.sim.max_steps >= 0 && cfg.sim.max_steps <= DP_MAX_STEPS,
               "defaults.max_steps = %d (in range)", cfg.sim.max_steps);
    } else {
        checkf(0, "hot.defaults: %s", err);
    }

    {
        int paused = 1;
        char scheme[32] = "";
        err[0] = 0;
        if (lua_hot_status(h, &paused, scheme, sizeof(scheme), err, sizeof(err))) {
            checkf(paused == 0 && strcmp(scheme, "phase") == 0,
                   "hot.status -> running / phase (paused=%d scheme=%s)", paused, scheme);
        } else {
            checkf(0, "hot.status: %s", err);
        }
        if (lua_hot_set_scheme(h, "ember", err, sizeof(err))) {
            paused = 1;
            scheme[0] = 0;
            lua_hot_status(h, &paused, scheme, sizeof(scheme), err, sizeof(err));
            checkf(strcmp(scheme, "ember") == 0, "hot.set_scheme(ember) is reflected by status");
        } else {
            checkf(0, "hot.set_scheme(ember): %s", err);
        }
        check(!lua_hot_set_scheme(h, "nope", err, sizeof(err)), "unknown palette is rejected");
        check(lua_hot_set_scheme(h, "phase", err, sizeof(err)), "palette restored to phase");
    }

    if (lua_hot_palette(h, lut, err, sizeof(err))) {
        check(1, "hot.palette returns 3072 bytes");
        checkf(lut[512 * 3 + 0] < 90 && lut[512 * 3 + 1] > 200 && lut[512 * 3 + 2] > 200,
               "phase palette midpoint is cyan (%d,%d,%d)",
               lut[512 * 3 + 0], lut[512 * 3 + 1], lut[512 * 3 + 2]);
    } else {
        checkf(0, "hot.palette: %s", err);
    }

    if (lua_hot_params(h, &p, &max_steps, err, sizeof(err))) {
        checkf(fabs(p.m1 - 1.0) < 1e-9 && fabs(p.dt - script_dt) < 1e-9 && max_steps == cfg.sim.max_steps,
               "hot.params -> m1=%.2f dt=%.3f max_steps=%d", p.m1, p.dt, max_steps);
    } else {
        checkf(0, "hot.params: %s", err);
    }

    lua_hot_reset(h);
    if (lua_hot_tick(h, script_dt * 0.5, &steps, err, sizeof(err))) {
        checkf(steps == 0, "tick(0.5*dt) = %d (below dt)", steps);
    } else {
        checkf(0, "hot.tick: %s", err);
    }
    if (lua_hot_tick(h, script_dt * 0.6, &steps, err, sizeof(err))) {
        checkf(steps == 1, "tick(0.5dt+0.6dt) = %d (crosses dt)", steps);
    } else {
        checkf(0, "hot.tick: %s", err);
    }

    action[0] = 0;
    check(lua_hot_key(h, "R", 1, action, sizeof(action), err, sizeof(err)) && strcmp(action, "reset") == 0,
          "key R -> reset");
    check(lua_hot_key(h, "C", 1, action, sizeof(action), err, sizeof(err)) && strcmp(action, "palette") == 0,
          "key C -> palette");
    check(lua_hot_key(h, "K", 1, action, sizeof(action), err, sizeof(err)) && strcmp(action, "kernel") == 0,
          "key K -> kernel");
    check(lua_hot_key(h, "SPACE", 1, action, sizeof(action), err, sizeof(err)) && strcmp(action, "pause") == 0,
          "key SPACE -> pause");
    check(lua_hot_tick(h, 1.0, &steps, err, sizeof(err)) && steps == 0, "paused tick returns 0");
    lua_hot_key(h, "SPACE", 1, action, sizeof(action), err, sizeof(err));

    snprintf(resstr, sizeof(resstr), "%dx%d", cfg.grid.res, cfg.grid.res);
    if (lua_hot_title(h, 60, 1, title, sizeof(title), err, sizeof(err))) {
        checkf(strstr(title, "GPU compute") != NULL && strstr(title, resstr) != NULL,
               "title mentions kernel and grid: \"%s\"", title);
    } else {
        checkf(0, "hot.title: %s", err);
    }

    err[0] = 0;
    check(!lua_hot_reload(h, "this is not valid lua", 21, "bad.lua", err, sizeof(err)),
          "broken reload is rejected");
    check(lua_hot_tick(h, 0.02, &steps, err, sizeof(err)) == 1, "old Lua state survives a rejected reload");

    err[0] = 0;
    {
        const char *tiny =
            "local M = {}\n"
            "function M.defaults() return { grid = { res = 128 } } end\n"
            "function M.palette() local t = {} for i = 1, 3072 do t[i] = (i * 7) % 256 end return t end\n"
            "function M.params() return 1, 1, 1, 1, 9.81, 0.1, 0.05, 2 end\n"
            "function M.tick(dt) return 1 end\n"
            "function M.key(n, d) return nil end\n"
            "function M.title() return 'tiny' end\n"
            "function M.reset() end\n"
            "return M\n";
        if (lua_hot_reload(h, tiny, strlen(tiny), "tiny.lua", err, sizeof(err))) {
            DpConfig ncfg = cfg;
            check(lua_hot_defaults(h, &ncfg, err, sizeof(err)) && ncfg.grid.res == 128,
                  "reload adopts a new script (res=128)");
        } else {
            checkf(0, "tiny reload: %s", err);
        }
    }
    lua_hot_destroy(h);
}

static void test_bmp(void)
{
    unsigned char px[4 * 3 * 4];
    char path[MAX_PATH];
    FILE *f;
    unsigned char hdr[54];
    int i, ok = 1;

    printf("-- BMP writer\n");
    for (i = 0; i < 4 * 3; ++i) {
        px[i * 4 + 0] = (unsigned char)(10 + i);
        px[i * 4 + 1] = (unsigned char)(20 + i);
        px[i * 4 + 2] = (unsigned char)(30 + i);
        px[i * 4 + 3] = 255;
    }
    GetTempPathA(MAX_PATH, path);
    strncat(path, "dp-phase-test.bmp", MAX_PATH - strlen(path) - 1);
    check(dp_write_bmp(path, px, 4, 3) == 1, "write 4x3 BMP");
    f = fopen(path, "rb");
    if (!f) { check(0, "reopen BMP"); return; }
    if (fread(hdr, 1, 54, f) != 54) ok = 0;
    fclose(f);
    check(ok && hdr[0] == 'B' && hdr[1] == 'M', "BMP magic");
    check(ok && hdr[18] == 4 && hdr[22] == 3 && hdr[28] == 24, "BMP 4x3 @ 24bpp");
    check(ok && hdr[2] == 90 && hdr[3] == 0 && hdr[4] == 0 && hdr[5] == 0, "BMP file size = 54 + 3*12");
    DeleteFileA(path);
}

static void test_gpu(void)
{
    Platform *plat;
    Gpu *g;
    Sim *s;
    DpConfig cfg = test_cfg(64);
    DpParams p;
    float *initial;
    float *a_state;
    float *b_state;
    int cells, i, k;
    double maxd;
    char err[1024];
    unsigned char *shot;

    printf("-- GPU compute pipeline\n");
    plat = platform_create(GetModuleHandleA(NULL), L"dp-gpu-test", 64, 1, NULL, NULL);
    if (!plat) { check(0, "hidden window"); return; }
    err[0] = 0;
    g = gpu_create(platform_hwnd(plat), platform_hdc(plat), 0, err, sizeof(err));
    if (!g) { checkf(0, "gpu_create: %s", err); platform_destroy(plat); return; }
    printf("  [info] GL_VERSION  = %s\n", gpu_version(g));
    printf("  [info] GL_RENDERER = %s\n", gpu_renderer(g));
    if (!gpu_compute(g)) {
        checkf(0, "compute shader unavailable (%s)", gpu_fallback_reason(g));
        gpu_destroy(g);
        platform_destroy(plat);
        return;
    }
    check(1, "OpenGL 4.3 core + compute shader compiled");

    gpu_set_res(g, 64);
    check(gpu_field_filter(g) == GL_NEAREST, "field texture uses GL_NEAREST (sharp pixels)");
    {
        unsigned char lut[DP_LUT_N * 3];
        dp_phase_lut(lut);
        gpu_set_lut(g, lut);
    }

    s = sim_create(&cfg, 1, err, sizeof(err));
    if (!s) { checkf(0, "sim_create: %s", err); gpu_destroy(g); platform_destroy(plat); return; }
    cells = sim_cells(s);
    scatter_state(s, 999u);
    initial = (float *)malloc((size_t)cells * 4u * sizeof(float));
    a_state = (float *)malloc((size_t)cells * 4u * sizeof(float));
    b_state = (float *)malloc((size_t)cells * 4u * sizeof(float));
    memcpy(initial, sim_state(s), (size_t)cells * 4u * sizeof(float));

    p.m1 = 1; p.m2 = 1; p.l1 = 1; p.l2 = 1; p.g = 9.81; p.damping = 0.1; p.dt = 0.01;

    gpu_upload_state(g, initial, 64);
    gpu_step(g, 1, &p);
    gpu_download_state(g, a_state, 64);
    maxd = 0.0;
    for (i = 0; i < cells; ++i) {
        double ref[4];
        for (k = 0; k < 4; ++k) ref[k] = initial[i * 4 + k];
        dp_rk4_step(ref, &p);
        for (k = 0; k < 4; ++k) {
            double d = fabs(ref[k] - (double)a_state[i * 4 + k]);
            if (d > maxd) maxd = d;
        }
    }
    checkf(maxd < 5e-5, "GPU one step matches C reference (max %.3e)", maxd);

    gpu_upload_state(g, initial, 64);
    gpu_step(g, 3, &p);
    gpu_download_state(g, a_state, 64);
    gpu_upload_state(g, initial, 64);
    for (i = 0; i < 3; ++i) gpu_step(g, 1, &p);
    gpu_download_state(g, b_state, 64);
    check(memcmp(a_state, b_state, (size_t)cells * 4u * sizeof(float)) == 0,
          "3 substeps in one dispatch == 3 separate dispatches (bitwise)");

    gpu_upload_state(g, initial, 64);
    gpu_step(g, 0, &p);
    gpu_read_field(g, sim_field(s), 64);
    maxd = 0.0;
    for (i = 0; i < cells; ++i) {
        float f = dp_field_value(initial[i * 4 + 0], initial[i * 4 + 1]);
        double d = fabs((double)f - (double)sim_field(s)[i]);
        if (d > maxd) maxd = d;
    }
    checkf(maxd < 1e-6, "field image matches CPU mapping (max %.3e)", maxd);

    gpu_draw(g, 128, 128);
    shot = (unsigned char *)malloc(256u * 256u * 4u);
    if (shot && gpu_capture_offscreen(g, 256, 256, shot)) {
        unsigned long sum = 0;
        int nonzero = 0;
        for (i = 0; i < 256 * 256; ++i) {
            sum += shot[i * 4 + 0] + shot[i * 4 + 1] + shot[i * 4 + 2];
            if (shot[i * 4 + 0] + shot[i * 4 + 1] + shot[i * 4 + 2] > 0) nonzero++;
        }
        check(nonzero > 256 * 256 / 2 && sum > 0, "offscreen FBO draw + readback produced a non-black frame");
    } else {
        check(0, "gpu_capture_rgba");
    }
    free(shot);

    /* 256x256 单步耗时：用来和 Fortran 行带内核对比 */
    {
        int n = 256;
        int cells256 = n * n;
        float *big = (float *)malloc((size_t)cells256 * 4u * sizeof(float));
        if (big) {
            int j;
            for (j = 0; j < cells256; ++j) {
                big[j * 4 + 0] = -2.0f * (float)DP_PI + (float)(j % n) / (n - 1) * (float)(4.0 * DP_PI);
                big[j * 4 + 1] = -2.0f * (float)DP_PI + (float)(j / n) / (n - 1) * (float)(4.0 * DP_PI);
                big[j * 4 + 2] = 10.0f;
                big[j * 4 + 3] = 10.0f;
            }
            gpu_set_res(g, n);
            gpu_upload_state(g, big, n);
            gpu_step(g, 1, &p);
            glFinish();
            {
                double t0 = platform_now();
                double ms;
                int rep;
                for (rep = 0; rep < 50; ++rep) gpu_step(g, 1, &p);
                glFinish();
                ms = (platform_now() - t0) * 1000.0 / 50.0;
                printf("  [info] GPU 256x256 x 1 step: %.3f ms, %.1f Mcell-steps/s\n",
                       ms, (double)cells256 / (ms * 1000.0));
            }
            free(big);
        }
    }

    free(initial);
    free(a_state);
    free(b_state);
    sim_destroy(s);
    gpu_destroy(g);
    platform_destroy(plat);
}

int dp_selftest(int with_gpu)
{
    g_pass = 0;
    g_fail = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\nDouble Pendulum Phase Space - selftest\n\n");
    test_angles();
    test_view();
    test_fortran();
    test_timing();
    test_lua();
    test_bmp();
    if (with_gpu) test_gpu();
    printf("\nselftest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0;
}