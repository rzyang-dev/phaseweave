/* selftest.c - 无显示器也可跑的验证：
 *   C 参考 RK4 vs Fortran 内核、线程确定性、Lua 契约、调色板、BMP、GPU 一步同步 */
#include "selftest.h"

#include "atlas.h"
#include "bmp.h"
#include "experiment.h"
#include "expr.h"
#include "gpu.h"
#include "lua_hot.h"
#include "model_custom.h"
#include "model_glsl.h"
#include "phase_view.h"
#include "session.h"
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

static void test_experiment_atlas(void)
{
    Experiment e;
    Atlas *a;
    char err[512];
    double state[MODEL_MAX_STATE], vals[MODEL_MAX_PARAMS];
    float st[MODEL_MAX_STATE], fp[MODEL_MAX_PARAMS], ch[MODEL_MAX_CHANNELS];
    unsigned char lut[DP_LUT_N * 3];
    unsigned int colors[64];
    int i, ncolors = 0;

    printf("-- experiment / atlas\n");
    exp_init(&e, &DP_MODEL);
    check(exp_index(&e, "theta1") == 0 && exp_index(&e, "m2") == 5,
          "double-pendulum schema has named parameters");
    e.res = 16;
    check(exp_set_axis(&e, 0, "m1") && exp_set_axis(&e, 1, "m2"),
          "axes can be switched to masses");
    check(!exp_set_axis(&e, 0, "m2"), "x and y axes must differ");
    exp_set_range(&e, 0, 0.5, 2.0);
    exp_set_range(&e, 1, 0.5, 2.0);
    exp_set_value(&e, "theta1", 0.5);
    exp_set_value(&e, "theta2", 0.2);

    exp_cell_state(&e, 0, 0, state, vals);
    check(fabs(vals[4] - 0.546875) < 1e-6 && fabs(vals[5] - 0.546875) < 1e-6,
          "mass-axis cell maps to parameter values");
    check(fabs(state[0] - 0.5) < 1e-9 && fabs(state[1] - 0.2) < 1e-9,
          "fixed angles are used when masses are axes");

    a = atlas_create(&e, e.res, err, sizeof(err));
    check(a != NULL, "atlas creation");
    if (a) {
        dp_phase_lut(lut);
        e.channel = 2;
        atlas_colorize(a, &e, lut);
        atlas_read_cell(a, 0, st, fp, ch);
        check(fabs(fp[4] - vals[4]) < 1e-5 && fabs(fp[5] - vals[5]) < 1e-5,
              "atlas cell preserves per-pixel masses");
        check(isfinite(ch[2]), "energy channel is finite");
        for (i = 0; i < 64; ++i) colors[i] = 0;
        for (i = 0; i < 64 && ncolors < 64; ++i) {
            const unsigned char *p = atlas_rgba(a) + (size_t)(i * 17 % (16 * 16)) * 4u;
            unsigned int c = ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
            int j, seen = 0;
            for (j = 0; j < ncolors; ++j) if (colors[j] == c) seen = 1;
            if (!seen) colors[ncolors++] = c;
        }
        check(ncolors > 1, "mass map produces more than one color");
        e.post = 2;
        atlas_colorize(a, &e, lut);
        check(atlas_rgba(a) != NULL, "energy/range view colorizes");
        atlas_destroy(a);
    }

    {
        DpParams p;
        double s0[4] = { 0.0, 0.0, 0.0, 0.0 };
        double e0, e1;
        p.m1 = p.m2 = p.l1 = p.l2 = 1.0;
        p.g = 9.81; p.damping = 0.0; p.dt = 0.001;
        e0 = dp_energy(s0, &p);
        check(fabs(e0 + 29.43) < 1e-6, "rest energy matches m g l");
        s0[0] = 0.5; s0[1] = 0.2;
        e0 = dp_energy(s0, &p);
        for (i = 0; i < 2000; ++i) dp_rk4_step(s0, &p);
        e1 = dp_energy(s0, &p);
        check(fabs(e1 - e0) < 1e-3 * fmax(1.0, fabs(e0)),
              "RK4 conserves energy for damping=0");
    }
}

static void test_maps(void)
{
    double vals[MODEL_MAX_PARAMS];
    double state[MODEL_MAX_STATE];
    float ch[MODEL_MAX_CHANNELS];
    Experiment e;
    Atlas *a;
    char err[512];
    int i;

    printf("-- map models\n");
    check(model_count() == 3, "three models are registered");
    check(model_by_id("logistic") == &LOGISTIC_MODEL, "logistic lookup");
    check(model_by_id("henon") == &HENON_MODEL, "henon lookup");

    memset(vals, 0, sizeof(vals));
    vals[0] = 2.5; vals[1] = 0.5;
    LOGISTIC_MODEL.init(&LOGISTIC_MODEL, vals, state);
    LOGISTIC_MODEL.step(&LOGISTIC_MODEL, state, vals, 0.0, 2000);
    check(fabs(state[0] - 0.6) < 1e-3, "logistic r=2.5 converges to 1-1/r");

    vals[0] = 4.0; vals[1] = 0.3;
    LOGISTIC_MODEL.init(&LOGISTIC_MODEL, vals, state);
    LOGISTIC_MODEL.step(&LOGISTIC_MODEL, state, vals, 0.0, 1000);
    LOGISTIC_MODEL.observe(&LOGISTIC_MODEL, state, vals, ch);
    check(ch[0] >= 0.0f && ch[0] <= 1.0f, "logistic state stays in [0,1]");
    check(fabs(ch[1] - 0.6931) < 0.15, "logistic r=4 Lyapunov ~= ln 2");

    memset(vals, 0, sizeof(vals));
    vals[0] = 1.4; vals[1] = 0.3; vals[2] = 0.0; vals[3] = 0.0;
    HENON_MODEL.init(&HENON_MODEL, vals, state);
    HENON_MODEL.step(&HENON_MODEL, state, vals, 0.0, 1000);
    check(isfinite(state[0]) && isfinite(state[1]), "henon state is finite");
    check(fabs(state[0]) < 2.0 && fabs(state[1]) < 1.0, "henon state stays bounded");
    HENON_MODEL.observe(&HENON_MODEL, state, vals, ch);
    check(ch[2] >= 0.0f && ch[2] < 3.0f, "henon radius channel");

    exp_init(&e, &LOGISTIC_MODEL);
    e.res = 16;
    a = atlas_create(&e, e.res, err, sizeof(err));
    check(a != NULL, "logistic atlas creation");
    if (a) {
        unsigned char lut[DP_LUT_N * 3];
        unsigned int colors[128];
        int ncolors = 0;
        dp_phase_lut(lut);
        e.channel = 1;
        atlas_step(a, &e, 100);
        atlas_colorize(a, &e, lut);
        for (i = 0; i < 128 && ncolors < 128; ++i) {
            const unsigned char *p = atlas_rgba(a) + (size_t)(i * 31 % (16 * 16)) * 4u;
            unsigned int c = ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
            int j, seen = 0;
            for (j = 0; j < ncolors; ++j) if (colors[j] == c) seen = 1;
            if (!seen) colors[ncolors++] = c;
        }
        check(ncolors > 1, "logistic parameter map is non-uniform");
        atlas_destroy(a);
    }
}

static void test_schema(void)
{
    static const char *src =
        "local M = {}\n"
        "M.schema = function(id)\n"
        "  if id ~= 'double_pendulum' then return nil end\n"
        "  return { title='Lua pendulum', axes={'theta1','theta2'},\n"
        "    params={\n"
        "      {name='theta1',label='angle1',unit='rad',default=0.8,min=-1,max=1,step=0.1,axis=true},\n"
        "      {name='theta2',label='angle2',unit='rad',default=0.1,min=-1,max=1,step=0.1,axis=true},\n"
        "    },\n"
        "    channels={ {name='energy',label='E',unit='J',min=-10,max=10,cyclic=false} }\n"
        "  }\n"
        "end\n"
        "return M\n";
    char err[512];
    LuaHot *h;
    Schema s;
    Experiment e;

    printf("-- Lua schema\n");
    h = lua_hot_create(src, strlen(src), "schema-test", "cpu", 16, err, sizeof(err));
    check(h != NULL, "schema Lua state creation");
    if (!h) return;
    check(lua_hot_schema(h, "double_pendulum", &s, err, sizeof(err)) == 1,
          "hot.schema parses");
    check(s.nparams == 2 && s.nchannels == 1, "schema has params/channels");
    check(strcmp(s.title, "Lua pendulum") == 0, "schema title");
    check(strcmp(s.params[0].label, "angle1") == 0 && s.params[0].axis,
          "schema param metadata");
    check(strcmp(s.axis_x, "theta1") == 0 && strcmp(s.axis_y, "theta2") == 0,
          "schema default axes");
    {
        static const char *src2 =
            "local M = {}\n"
            "M.schema = function(id)\n"
            "  return { title='Reloaded', axes={'theta1','theta2'},\n"
            "    params={ {name='theta1',label='angle3',unit='rad',default=0.7,min=-1,max=1,step=0.1,axis=true} },\n"
            "    channels={ {name='energy',label='E2',unit='J',min=-5,max=5,cyclic=false} } }\n"
            "end\n"
            "return M\n";
        memset(&s, 0, sizeof(s));
        check(lua_hot_reload(h, src2, strlen(src2), "schema-reload", err, sizeof(err)) == 1 &&
              lua_hot_schema(h, "double_pendulum", &s, err, sizeof(err)) == 1,
              "schema hot reload parses");
        check(strcmp(s.params[0].label, "angle3") == 0 &&
              strcmp(s.channels[0].label, "E2") == 0,
              "schema hot reload metadata");
    }
    lua_hot_destroy(h);

    exp_init(&e, &DP_MODEL);
    memset(&s, 0, sizeof(s));
    strcpy(s.title, "Lua pendulum");
    strcpy(s.axis_x, "theta1");
    strcpy(s.axis_y, "theta2");
    s.nparams = 2;
    strcpy(s.params[0].name, "theta1");
    strcpy(s.params[0].label, "angle1");
    strcpy(s.params[0].unit, "rad");
    s.params[0].def = 0.8; s.params[0].min = -1; s.params[0].max = 1;
    s.params[0].step = 0.1; s.params[0].axis = 1;
    strcpy(s.params[1].name, "theta2");
    strcpy(s.params[1].label, "angle2");
    strcpy(s.params[1].unit, "rad");
    s.params[1].def = 0.1; s.params[1].min = -1; s.params[1].max = 1;
    s.params[1].step = 0.1; s.params[1].axis = 1;
    s.nchannels = 1;
    strcpy(s.channels[0].name, "energy");
    strcpy(s.channels[0].label, "E");
    strcpy(s.channels[0].unit, "J");
    s.channels[0].min = -10; s.channels[0].max = 10;
    check(exp_apply_schema(&e, &s, 1) > 0, "schema applies to experiment");
    check(strcmp(e.title, "Lua pendulum") == 0, "experiment title from schema");
    check(exp_index(&e, "theta1") >= 0 &&
          strcmp(e.p[exp_index(&e, "theta1")].label, "angle1") == 0,
          "parameter label from schema");
    check(fabs(exp_value(&e, "theta1") - 0.8) < 1e-9, "schema default value");
    {
        ExpChannelInfo ci;
        exp_channel_info(&e, 2, &ci);
        check(strcmp(ci.label, "E") == 0 && fabs(ci.min + 10.0) < 1e-9,
              "channel metadata from schema");
    }
}

static void make_ode_spec(CustomSpec *spec)
{
    memset(spec, 0, sizeof(*spec));
    spec->type = 1;
    strcpy(spec->schema.title, "Harmonic oscillator");
    spec->schema.nparams = 2;
    strcpy(spec->schema.params[0].name, "x0");
    strcpy(spec->schema.params[0].label, "x0");
    spec->schema.params[0].def = 1.0;
    spec->schema.params[0].min = -2; spec->schema.params[0].max = 2;
    strcpy(spec->schema.params[1].name, "y0");
    strcpy(spec->schema.params[1].label, "y0");
    spec->schema.params[1].def = 0.0;
    spec->schema.params[1].min = -2; spec->schema.params[1].max = 2;
    spec->schema.nchannels = 2;
    strcpy(spec->schema.channels[0].name, "x");
    strcpy(spec->schema.channels[0].label, "x");
    spec->schema.channels[0].min = -2; spec->schema.channels[0].max = 2;
    strcpy(spec->schema.channels[1].name, "y");
    strcpy(spec->schema.channels[1].label, "y");
    spec->schema.channels[1].min = -2; spec->schema.channels[1].max = 2;
    strcpy(spec->state_names[0], "x");
    strcpy(spec->state_names[1], "y");
    strcpy(spec->init_x, "x0");
    strcpy(spec->init_y, "y0");
    strcpy(spec->deriv_x, "y");
    strcpy(spec->deriv_y, "-x");
    strcpy(spec->chan_expr[0], "x");
    strcpy(spec->chan_expr[1], "y");
}

static void test_expressions_and_custom(void)
{
    Expr ex;
    const char *vars[2] = { "x", "y" };
    double v[2] = { 2.0, 0.0 };
    char err[512];
    double state[4], state2[4];
    float ch[4], ch2[4];
    double vals[MODEL_MAX_PARAMS];
    LuaHot *h;
    CustomSpec spec;
    CustomModel cm;
    char dir[MAX_PATH], path[MAX_PATH];
    char *src = NULL;
    size_t len = 0;

    printf("-- expressions / custom model\n");
    check(expr_compile(&ex, "1 + 2*x + sin(y)^2", vars, 2, err, sizeof(err)), "expression compiles");
    check(fabs(expr_eval(&ex, v) - 5.0) < 1e-12, "expression precedence/functions");
    check(expr_compile(&ex, "sqrt(9) + abs(-2)", vars, 2, err, sizeof(err)), "expression functions compile");
    check(fabs(expr_eval(&ex, v) - 5.0) < 1e-12, "sqrt/abs evaluate");
    check(expr_compile(&ex, "x < 0.5 ? 2*x : 2 - 2*x", vars, 1, err, sizeof(err)),
          "ternary/compare compiles");
    v[0] = 0.25;
    check(fabs(expr_eval(&ex, v) - 0.5) < 1e-12, "ternary true branch");
    v[0] = 0.75;
    check(fabs(expr_eval(&ex, v) - 0.5) < 1e-12, "ternary false branch");
    check(expr_compile(&ex, "if(x < 0, -x, x)", vars, 1, err, sizeof(err)),
          "if() control flow compiles");
    v[0] = -3.0;
    check(fabs(expr_eval(&ex, v) - 3.0) < 1e-12, "if() control flow evaluates");
    check(expr_compile(&ex, "x >= 0 && x <= 1 ? x : 0", vars, 1, err, sizeof(err)),
          "logical operators compile");
    v[0] = 0.5;
    check(fabs(expr_eval(&ex, v) - 0.5) < 1e-12, "logical expression matches");

    platform_exe_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s\\experiments\\experiment.lua", dir);
    if (!platform_read_file(path, &src, &len)) {
        check(0, "custom experiment.lua is readable");
        return;
    }
    h = lua_hot_create(src, len, path, "cpu", 16, err, sizeof(err));
    check(h != NULL, "custom Lua state creation");
    if (!h) { platform_free(src); return; }
    check(lua_hot_custom(h, &spec, err, sizeof(err)) == 1, "lua_hot_custom parses");
    check(custom_model_build(&cm, &spec, err, sizeof(err)) == 1, "custom model builds");
    check(strcmp(cm.model.id, "custom") == 0 && cm.nchannels == 4,
          "custom model metadata");

    memset(vals, 0, sizeof(vals));
    vals[0] = 1.4; vals[1] = 0.3; vals[2] = 0.0; vals[3] = 0.0;
    cm.model.init(&cm.model, vals, state);
    HENON_MODEL.init(&HENON_MODEL, vals, state2);
    check(fabs(state[0] - state2[0]) < 1e-12 && fabs(state[1] - state2[1]) < 1e-12,
          "custom init matches built-in Hénon");
    cm.model.step(&cm.model, state, vals, 0.0, 10);
    HENON_MODEL.step(&HENON_MODEL, state2, vals, 0.0, 10);
    check(fabs(state[0] - state2[0]) < 1e-9 && fabs(state[1] - state2[1]) < 1e-9,
          "custom step matches built-in Hénon");
    cm.model.observe(&cm.model, state, vals, ch);
    HENON_MODEL.observe(&HENON_MODEL, state2, vals, ch2);
    check(fabs(ch[2] - ch2[2]) < 1e-5, "custom channel expression matches");
    {
        CustomSpec ospec;
        CustomModel ocm;
        double ov[MODEL_MAX_PARAMS];
        double ost[4];
        float och[4];
        make_ode_spec(&ospec);
        check(custom_model_build(&ocm, &ospec, err, sizeof(err)) == 1, "ODE custom model builds");
        ov[0] = 1.0; ov[1] = 0.0;
        ocm.model.init(&ocm.model, ov, ost);
        ocm.model.step(&ocm.model, ost, ov, 0.01, 100);
        check(fabs(ost[0] - cos(1.0)) < 1e-4 && fabs(ost[1] + sin(1.0)) < 1e-4,
              "custom ODE matches harmonic oscillator");
        ocm.model.observe(&ocm.model, ost, ov, och);
        check(fabs(och[0] - (float)cos(1.0)) < 1e-4, "ODE channel evaluates");
    }

    lua_hot_destroy(h);
    platform_free(src);
}

static void test_session(void)
{
    Experiment e;
    PhaseView v;
    SessionData s;
    char dir[MAX_PATH], path[MAX_PATH];
    char err[512];
    printf("-- experiment session\n");
    exp_init(&e, &DP_MODEL);
    e.res = 32;
    exp_set_axis(&e, 0, "m1");
    exp_set_axis(&e, 1, "m2");
    exp_set_range(&e, 0, 0.5, 2.0);
    exp_set_range(&e, 1, 0.5, 2.0);
    exp_set_value(&e, "theta1", 0.7);
    e.channel = 2;
    e.post = 2;
    strcpy(e.palette, "ember");
    e.grid = 1; e.periodic = 1; e.circle = 0;
    pv_init(&v); v.cx = 0.1; v.cy = -0.2; v.zoom = 2.0;
    GetTempPathA(sizeof(dir), dir);
    snprintf(path, sizeof(path), "%sdp-session-test.dps", dir);
    check(session_save(path, &e, &v, 1, 1, 12, 13, err, sizeof(err)) == 1, "session save");
    check(session_load(path, &s, err, sizeof(err)) == 1, "session load");
    check(strcmp(s.model, "double_pendulum") == 0 && s.backend_gpu == 1,
          "session model/backend roundtrip");
    check(strcmp(s.axis_x, "m1") == 0 && fabs(s.xmax - 2.0) < 1e-12,
          "session axes/ranges roundtrip");
    check(s.res == 32 && fabs(s.dt - 0.01) < 1e-12 && s.steps == 8,
          "session integration roundtrip");
    check(strcmp(s.channel, "energy") == 0 && strcmp(s.post, "range") == 0,
          "session channel/post roundtrip");
    check(s.periodic == 1 && s.circle == 0 &&
          fabs(s.view_cx - 0.1) < 1e-12 && fabs(s.view_zoom - 2.0) < 1e-12,
          "session view/toggles roundtrip");
    check(s.selected == 1 && s.sel_x == 12 && s.sel_y == 13,
          "session selected cell roundtrip");
    DeleteFileA(path);
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

static void test_gpu_builtin(Gpu *g, const Model *m, const char *name)
{
    Experiment e;
    Atlas *a;
    char err[1024], glsl[32768];
    int n, stride, i;
    float *init, *cpu, *cpu_ch, *gout, *gch;
    exp_init(&e, m);
    e.res = 8;
    a = atlas_create(&e, 8, err, sizeof(err));
    if (!a) { checkf(0, "%s atlas creation: %s", name, err); return; }
    n = 8 * 8; stride = atlas_cell_floats();
    init = (float *)malloc((size_t)n * stride * sizeof(float));
    cpu = (float *)malloc((size_t)n * stride * sizeof(float));
    cpu_ch = (float *)malloc((size_t)n * 4u * sizeof(float));
    gout = (float *)malloc((size_t)n * stride * sizeof(float));
    gch = (float *)malloc((size_t)n * 4u * sizeof(float));
    memcpy(init, atlas_cell_data(a), (size_t)n * stride * sizeof(float));
    atlas_step(a, &e, 2);
    memcpy(cpu, atlas_cell_data(a), (size_t)n * stride * sizeof(float));
    memcpy(cpu_ch, atlas_channel_data(a), (size_t)n * 4u * sizeof(float));
    check(model_builtin_glsl(&e, glsl, sizeof(glsl), err, sizeof(err)) == 1,
          name);
    check(gpu_compile_custom(g, glsl, err, sizeof(err)) == 1, name);
    check(gpu_run_custom(g, init, 8, 2, e.dt, gout, gch, err, sizeof(err)) == 1, name);
    {
        double md = 0.0, mc = 0.0;
        for (i = 0; i < n; ++i) {
            double dx = fabs(gout[i*stride+0] - cpu[i*stride+0]);
            double dy = fabs(gout[i*stride+1] - cpu[i*stride+1]);
            int k;
            if (dx > md) md = dx;
            if (dy > md) md = dy;
            for (k = 0; k < 4; ++k) {
                double dc = fabs(gch[i*4+k] - cpu_ch[i*4+k]);
                if (dc > mc) mc = dc;
            }
        }
        checkf(md < 2e-3 && mc < 2e-3, "%s", name);
    }
    free(init); free(cpu); free(cpu_ch); free(gout); free(gch);
    atlas_destroy(a);
}

static void test_gpu_custom(Gpu *g)
{
    CustomSpec spec;
    CustomModel cm;
    char err[1024];
    char glsl[32768];
    float cells[16], out[16], ch[4];
    double state[4], vals[MODEL_MAX_PARAMS];
    int i;

    printf("-- GPU expression codegen\n");
    memset(&spec, 0, sizeof(spec));
    strcpy(spec.schema.title, "GPU Hénon");
    strcpy(spec.schema.axis_x, "a");
    strcpy(spec.schema.axis_y, "b");
    spec.schema.nparams = 4;
    spec.schema.params[0].def = 1.4; spec.schema.params[0].min = 0; spec.schema.params[0].max = 1.5;
    strcpy(spec.schema.params[0].name, "a"); strcpy(spec.schema.params[0].label, "a");
    spec.schema.params[1].def = 0.3; spec.schema.params[1].min = -1; spec.schema.params[1].max = 1;
    strcpy(spec.schema.params[1].name, "b"); strcpy(spec.schema.params[1].label, "b");
    spec.schema.params[2].def = 0; spec.schema.params[2].min = -2; spec.schema.params[2].max = 2;
    strcpy(spec.schema.params[2].name, "x0"); strcpy(spec.schema.params[2].label, "x0");
    spec.schema.params[3].def = 0; spec.schema.params[3].min = -2; spec.schema.params[3].max = 2;
    strcpy(spec.schema.params[3].name, "y0"); strcpy(spec.schema.params[3].label, "y0");
    spec.schema.nchannels = 4;
    strcpy(spec.schema.channels[0].name, "x"); strcpy(spec.schema.channels[0].label, "x");
    spec.schema.channels[0].min = -2; spec.schema.channels[0].max = 2;
    strcpy(spec.schema.channels[1].name, "y"); strcpy(spec.schema.channels[1].label, "y");
    spec.schema.channels[1].min = -2; spec.schema.channels[1].max = 2;
    strcpy(spec.schema.channels[2].name, "radius"); strcpy(spec.schema.channels[2].label, "radius");
    spec.schema.channels[2].min = 0; spec.schema.channels[2].max = 3;
    strcpy(spec.schema.channels[3].name, "x_abs"); strcpy(spec.schema.channels[3].label, "|x|");
    spec.schema.channels[3].min = 0; spec.schema.channels[3].max = 2;
    strcpy(spec.state_names[0], "x"); strcpy(spec.state_names[1], "y");
    strcpy(spec.init_x, "x0"); strcpy(spec.init_y, "y0");
    strcpy(spec.step_x, "1 - a*x*x + y"); strcpy(spec.step_y, "b*x");
    strcpy(spec.chan_expr[0], "x"); strcpy(spec.chan_expr[1], "y");
    strcpy(spec.chan_expr[2], "sqrt(x*x + y*y)"); strcpy(spec.chan_expr[3], "if(x < 0, -x, x)");
    check(custom_model_build(&cm, &spec, err, sizeof(err)) == 1, "GPU custom model builds");
    check(custom_model_glsl(&cm, glsl, sizeof(glsl), err, sizeof(err)) == 1, "GLSL source generated");
    check(gpu_compile_custom(g, glsl, err, sizeof(err)) == 1, "generated GLSL compiles");
    memset(cells, 0, sizeof(cells));
    cells[4] = 1.4f; cells[5] = 0.3f; cells[6] = 0.0f; cells[7] = 0.0f;
    check(gpu_run_custom(g, cells, 1, 10, 0.01, out, ch, err, sizeof(err)) == 1, "GPU custom dispatch");
    memset(vals, 0, sizeof(vals));
    vals[0] = 1.4; vals[1] = 0.3;
    cm.model.init(&cm.model, vals, state);
    for (i = 0; i < 10; ++i) cm.model.step(&cm.model, state, vals, 0.0, 1);
    {
        float cc[4];
        cm.model.observe(&cm.model, state, vals, cc);
        check(fabs(out[0] - state[0]) < 1e-5 && fabs(out[1] - state[1]) < 1e-5,
              "GPU custom state matches CPU");
        check(fabs(ch[0] - cc[0]) < 1e-4 && fabs(ch[2] - cc[2]) < 1e-4,
              "GPU custom channels match CPU");
    }
    {
        Experiment e;
        Atlas *a;
        int n, stride = atlas_cell_floats();
        float *init, *cpu, *cpu_ch, *gout, *gch;
        exp_init(&e, &cm.model);
        e.res = 8;
        a = atlas_create(&e, 8, err, sizeof(err));
        check(a != NULL, "custom atlas creation");
        if (a) {
            n = 8 * 8;
            init = (float *)malloc((size_t)n * stride * sizeof(float));
            cpu = (float *)malloc((size_t)n * stride * sizeof(float));
            cpu_ch = (float *)malloc((size_t)n * 4u * sizeof(float));
            gout = (float *)malloc((size_t)n * stride * sizeof(float));
            gch = (float *)malloc((size_t)n * 4u * sizeof(float));
            memcpy(init, atlas_cell_data(a), (size_t)n * stride * sizeof(float));
            atlas_step(a, &e, 2);
            memcpy(cpu, atlas_cell_data(a), (size_t)n * stride * sizeof(float));
            memcpy(cpu_ch, atlas_channel_data(a), (size_t)n * 4u * sizeof(float));
            check(gpu_run_custom(g, init, 8, 2, 0.01, gout, gch, err, sizeof(err)) == 1,
                  "GPU custom atlas dispatch");
            {
                double md = 0.0;
                int j;
                for (j = 0; j < n; ++j) {
                    double dx = fabs(gout[j * stride + 0] - cpu[j * stride + 0]);
                    double dy = fabs(gout[j * stride + 1] - cpu[j * stride + 1]);
                    if (dx > md) md = dx;
                    if (dy > md) md = dy;
                }
                checkf(md < 1e-3, "GPU custom atlas matches CPU (max %.3e)", md);
            }
            free(init); free(cpu); free(cpu_ch); free(gout); free(gch);
            atlas_destroy(a);
        }
    }
    {
        CustomSpec ospec;
        CustomModel ocm;
        double ov[MODEL_MAX_PARAMS], ost[4];
        float ocells[16], oout[16], och[4];
        make_ode_spec(&ospec);
        check(custom_model_build(&ocm, &ospec, err, sizeof(err)) == 1, "GPU ODE model builds");
        check(custom_model_glsl(&ocm, glsl, sizeof(glsl), err, sizeof(err)) == 1, "ODE GLSL generated");
        check(gpu_compile_custom(g, glsl, err, sizeof(err)) == 1, "ODE GLSL compiles");
        memset(ocells, 0, sizeof(ocells));
        ocells[0] = 1.0f;
        ocells[4] = 1.0f; /* x0 */
        ocells[5] = 0.0f; /* y0 */
        check(gpu_run_custom(g, ocells, 1, 100, 0.01, oout, och, err, sizeof(err)) == 1,
              "GPU ODE dispatch");
        ov[0] = 1.0; ov[1] = 0.0;
        ocm.model.init(&ocm.model, ov, ost);
        ocm.model.step(&ocm.model, ost, ov, 0.01, 100);
        check(fabs(oout[0] - ost[0]) < 1e-4 && fabs(oout[1] - ost[1]) < 1e-4,
              "GPU ODE matches CPU");
    }
    test_gpu_builtin(g, &LOGISTIC_MODEL, "logistic GPU atlas matches CPU");
    test_gpu_builtin(g, &HENON_MODEL, "henon GPU atlas matches CPU");
    test_gpu_builtin(g, &DP_MODEL, "double-pendulum GPU atlas matches CPU");
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
    test_gpu_custom(g);

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
    printf("\nDynamical Systems Atlas - selftest\n\n");
    test_angles();
    test_view();
    test_experiment_atlas();
    test_maps();
    test_schema();
    test_expressions_and_custom();
    test_session();
    test_fortran();
    test_timing();
    test_lua();
    test_bmp();
    if (with_gpu) test_gpu();
    printf("\nselftest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0;
}