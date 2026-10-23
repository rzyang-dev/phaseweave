/* lua_hot.c - Lua 5.2 宿主：只开 base/table/string/math，不碰 io/os/package/debug。
 * 脚本本体编进 exe 的 RCDATA；若 exe 同目录存在 hot.lua，则优先读磁盘（开发时热改）。 */
#include "lua_hot.h"

#include "platform.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct LuaHot {
    lua_State *L;
    char kernel[16];
    int  res;
    int  failed;
};

static void set_err(char *err, size_t errsz, const char *fmt, ...)
{
    va_list ap;
    if (!err || errsz == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
}

static double cfg_num(lua_State *L, int idx, const char *key, double def)
{
    double v = def;
    lua_getfield(L, idx, key);
    if (lua_isnumber(L, -1)) v = (double)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

static int cfg_int(lua_State *L, int idx, const char *key, int def)
{
    int v = def;
    lua_getfield(L, idx, key);
    if (lua_isnumber(L, -1)) v = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return v;
}

/* ---- host.* 绑定 ---------------------------------------------------- */

static int host_kernel(lua_State *L)
{
    LuaHot *h = (LuaHot *)lua_touserdata(L, lua_upvalueindex(1));
    lua_pushstring(L, h ? h->kernel : "cpu");
    return 1;
}

static int host_res(lua_State *L)
{
    LuaHot *h = (LuaHot *)lua_touserdata(L, lua_upvalueindex(1));
    lua_pushinteger(L, h ? h->res : 0);
    return 1;
}

static int host_log(lua_State *L)
{
    const char *s = lua_tostring(L, 1);
    if (s) {
        OutputDebugStringA("[hot.lua] ");
        OutputDebugStringA(s);
        OutputDebugStringA("\n");
    }
    return 0;
}

static int host_time(lua_State *L)
{
    lua_pushnumber(L, platform_now());
    return 1;
}

static const luaL_Reg host_funcs[] = {
    { "kernel", host_kernel },
    { "res",    host_res },
    { "log",    host_log },
    { "time",   host_time },
    { NULL, NULL }
};

static void open_sandbox(lua_State *L)
{
    luaL_requiref(L, "_G",     luaopen_base,   1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME,  luaopen_table,  1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME,  luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math,   1); lua_pop(L, 1);
}

static int push_hot(LuaHot *h, const char *fn)
{
    lua_State *L = h->L;
    lua_getglobal(L, "hot");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    lua_getfield(L, -1, fn);
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    return 1;
}

static int load_chunk(lua_State *L, const char *src, size_t len, const char *chunkname,
                      char *err, size_t errsz)
{
    int rc;
    if (!src || len == 0) {
        set_err(err, errsz, "hot.lua: empty script");
        return 0;
    }
    rc = luaL_loadbuffer(L, src, len, chunkname ? chunkname : "hot.lua");
    if (rc != 0) {
        set_err(err, errsz, "hot.lua load: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    rc = lua_pcall(L, 0, 1, 0);
    if (rc != 0) {
        set_err(err, errsz, "hot.lua run: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot.lua must return a table");
        return 0;
    }
    lua_setglobal(L, "hot");
    return 1;
}

/* 在同一个 LuaHot 上重建解释器。host.* 的 upvalue 始终指向 h 本身，
 * 因此 reload 换掉 lua_State 后不会留下悬空指针。 */
static int build_state(LuaHot *h, const char *src, size_t len, const char *chunkname,
                       char *err, size_t errsz)
{
    lua_State *L = luaL_newstate();
    if (!L) { set_err(err, errsz, "lua: cannot create state"); return 0; }
    open_sandbox(L);
    lua_newtable(L);
    lua_pushlightuserdata(L, h);
    luaL_setfuncs(L, host_funcs, 1);
    lua_setglobal(L, "host");
    if (!load_chunk(L, src, len, chunkname, err, errsz)) {
        lua_close(L);
        return 0;
    }
    if (h->L) lua_close(h->L);
    h->L = L;
    return 1;
}

LuaHot *lua_hot_create(const char *src, size_t len, const char *chunkname,
                       const char *kernel, int res, char *err, size_t errsz)
{
    LuaHot *h;

    if (err && errsz) err[0] = 0;
    h = (LuaHot *)calloc(1, sizeof(LuaHot));
    if (!h) { set_err(err, errsz, "lua: out of memory"); return NULL; }
    h->res = res;
    strncpy(h->kernel, kernel ? kernel : "cpu", sizeof(h->kernel) - 1);
    if (!build_state(h, src, len, chunkname, err, errsz)) {
        free(h);
        return NULL;
    }
    return h;
}

void lua_hot_destroy(LuaHot *h)
{
    if (!h) return;
    if (h->L) lua_close(h->L);
    free(h);
}

int lua_hot_reload(LuaHot *h, const char *src, size_t len, const char *chunkname,
                   char *err, size_t errsz)
{
    if (!h) return 0;
    if (!build_state(h, src, len, chunkname, err, errsz)) return 0;
    h->failed = 0;
    return 1;
}

void lua_hot_set_kernel(LuaHot *h, const char *kernel)
{
    if (!h || !kernel) return;
    strncpy(h->kernel, kernel, sizeof(h->kernel) - 1);
    h->kernel[sizeof(h->kernel) - 1] = 0;
}

void lua_hot_set_res(LuaHot *h, int res) { if (h) h->res = res; }

int lua_hot_defaults(LuaHot *h, DpConfig *cfg, char *err, size_t errsz)
{
    lua_State *L;
    DpConfig c;
    if (!h || !cfg) return 0;
    L = h->L;
    if (!push_hot(h, "defaults")) {
        set_err(err, errsz, "hot.defaults is missing");
        return 0;
    }
    if (lua_pcall(L, 0, 1, 0) != 0) {
        set_err(err, errsz, "hot.defaults: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot.defaults must return a table");
        return 0;
    }
    c = *cfg;

    lua_getfield(L, -1, "physics");
    if (lua_istable(L, -1)) {
        c.physics.m1      = cfg_num(L, -1, "m1", c.physics.m1);
        c.physics.m2      = cfg_num(L, -1, "m2", c.physics.m2);
        c.physics.l1      = cfg_num(L, -1, "l1", c.physics.l1);
        c.physics.l2      = cfg_num(L, -1, "l2", c.physics.l2);
        c.physics.g       = cfg_num(L, -1, "g", c.physics.g);
        c.physics.damping = cfg_num(L, -1, "damping", c.physics.damping);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "grid");
    if (lua_istable(L, -1)) {
        c.grid.res    = cfg_int(L, -1, "res", c.grid.res);
        c.grid.range  = cfg_num(L, -1, "range", c.grid.range);
        c.grid.omega1 = cfg_num(L, -1, "omega1", c.grid.omega1);
        c.grid.omega2 = cfg_num(L, -1, "omega2", c.grid.omega2);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "sim");
    if (lua_istable(L, -1)) {
        c.sim.dt        = cfg_num(L, -1, "dt", c.sim.dt);
        c.sim.max_steps = cfg_int(L, -1, "max_steps", c.sim.max_steps);
        c.sim.threads   = cfg_int(L, -1, "threads", c.sim.threads);
    }
    lua_pop(L, 1);

    lua_pop(L, 1);

    /* 参数护栏：脚本写错也不至于把进程带崩 */
    c.grid.res = dp_clampi(c.grid.res, 32, 1024);
    if (c.grid.range < 0.1) c.grid.range = 0.1;
    if (c.grid.range > 64.0) c.grid.range = 64.0;
    if (c.grid.omega1 < -1000.0) c.grid.omega1 = -1000.0;
    if (c.grid.omega1 >  1000.0) c.grid.omega1 =  1000.0;
    if (c.grid.omega2 < -1000.0) c.grid.omega2 = -1000.0;
    if (c.grid.omega2 >  1000.0) c.grid.omega2 =  1000.0;
    if (c.physics.m1 <= 0.0) c.physics.m1 = 1.0;
    if (c.physics.m2 <= 0.0) c.physics.m2 = 1.0;
    if (c.physics.l1 <= 0.0) c.physics.l1 = 1.0;
    if (c.physics.l2 <= 0.0) c.physics.l2 = 1.0;
    if (c.physics.g < 0.0) c.physics.g = 0.0;
    if (c.physics.damping < 0.0) c.physics.damping = 0.0;
    if (c.physics.damping > 10.0) c.physics.damping = 10.0;
    if (c.sim.dt < 1e-4) c.sim.dt = 1e-4;
    if (c.sim.dt > 0.5) c.sim.dt = 0.5;
    c.sim.max_steps = dp_clampi(c.sim.max_steps, 0, DP_MAX_STEPS);
    c.sim.threads = dp_clampi(c.sim.threads, 0, DP_MAX_THREADS);

    *cfg = c;
    return 1;
}

int lua_hot_palette(LuaHot *h, unsigned char *lut, char *err, size_t errsz)
{
    lua_State *L;
    int i;
    size_t n;
    if (!h || !lut) return 0;
    L = h->L;
    if (!push_hot(h, "palette")) {
        set_err(err, errsz, "hot.palette is missing");
        return 0;
    }
    if (lua_pcall(L, 0, 1, 0) != 0) {
        set_err(err, errsz, "hot.palette: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot.palette must return a table");
        return 0;
    }
    n = lua_rawlen(L, -1);
    if (n < (size_t)DP_LUT_N * 3u) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot.palette: expected %d values, got %u",
                DP_LUT_N * 3, (unsigned)n);
        return 0;
    }
    for (i = 0; i < DP_LUT_N * 3; ++i) {
        int v;
        lua_rawgeti(L, -1, i + 1);
        v = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        lut[i] = (unsigned char)v;
    }
    lua_pop(L, 1);
    return 1;
}

int lua_hot_params(LuaHot *h, DpParams *p, int *max_steps, char *err, size_t errsz)
{
    lua_State *L;
    DpParams q;
    int steps;
    int i;
    if (!h || !p) return 0;
    L = h->L;
    if (!push_hot(h, "params")) {
        set_err(err, errsz, "hot.params is missing");
        return 0;
    }
    if (lua_pcall(L, 0, 8, 0) != 0) {
        set_err(err, errsz, "hot.params: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (lua_gettop(L) < 8) {
        lua_settop(L, 0);
        set_err(err, errsz, "hot.params must return 8 numbers");
        return 0;
    }
    q.m1      = (double)lua_tonumber(L, -8);
    q.m2      = (double)lua_tonumber(L, -7);
    q.l1      = (double)lua_tonumber(L, -6);
    q.l2      = (double)lua_tonumber(L, -5);
    q.g       = (double)lua_tonumber(L, -4);
    q.damping = (double)lua_tonumber(L, -3);
    q.dt      = (double)lua_tonumber(L, -2);
    steps     = (int)lua_tointeger(L, -1);
    for (i = 0; i < 8; ++i) lua_pop(L, 1);

    if (q.m1 <= 0.0) q.m1 = 1.0;
    if (q.m2 <= 0.0) q.m2 = 1.0;
    if (q.l1 <= 0.0) q.l1 = 1.0;
    if (q.l2 <= 0.0) q.l2 = 1.0;
    if (q.g < 0.0) q.g = 0.0;
    if (q.damping < 0.0) q.damping = 0.0;
    if (q.dt < 1e-4) q.dt = 1e-4;
    if (q.dt > 0.5) q.dt = 0.5;
    steps = dp_clampi(steps, 0, DP_MAX_STEPS);
    *p = q;
    if (max_steps) *max_steps = steps;
    return 1;
}

int lua_hot_tick(LuaHot *h, double dt, int *steps, char *err, size_t errsz)
{
    lua_State *L;
    int n;
    if (!h || !steps) return 0;
    L = h->L;
    if (!push_hot(h, "tick")) {
        set_err(err, errsz, "hot.tick is missing");
        return 0;
    }
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 1, 1, 0) != 0) {
        set_err(err, errsz, "hot.tick: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    n = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    *steps = dp_clampi(n, 0, DP_MAX_STEPS);
    return 1;
}

int lua_hot_status(LuaHot *h, int *paused, char *scheme, size_t scheme_sz,
                   char *err, size_t errsz)
{
    lua_State *L;
    if (paused) *paused = 0;
    if (scheme && scheme_sz) scheme[0] = 0;
    if (!h) return 0;
    L = h->L;
    if (!push_hot(h, "status")) {
        set_err(err, errsz, "hot.status is missing");
        return 0;
    }
    if (lua_pcall(L, 0, 2, 0) != 0) {
        set_err(err, errsz, "hot.status: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (paused) *paused = lua_toboolean(L, -2) ? 1 : 0;
    if (scheme && scheme_sz) {
        const char *s = lua_tostring(L, -1);
        if (s) {
            strncpy(scheme, s, scheme_sz - 1);
            scheme[scheme_sz - 1] = 0;
        }
    }
    lua_pop(L, 2);
    return 1;
}

int lua_hot_set_scheme(LuaHot *h, const char *name, char *err, size_t errsz)
{
    lua_State *L;
    int ok = 0;
    if (!h || !name) return 0;
    L = h->L;
    if (!push_hot(h, "set_scheme")) {
        set_err(err, errsz, "hot.set_scheme is missing");
        return 0;
    }
    lua_pushstring(L, name);
    if (lua_pcall(L, 1, 1, 0) != 0) {
        set_err(err, errsz, "hot.set_scheme: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    ok = lua_toboolean(L, -1) ? 1 : 0;
    lua_pop(L, 1);
    if (!ok) set_err(err, errsz, "unknown palette: %s", name);
    return ok;
}

int lua_hot_key(LuaHot *h, const char *name, int down, char *action, size_t action_sz,
                char *err, size_t errsz)
{
    lua_State *L;
    if (!h || !name) return 0;
    if (action && action_sz) action[0] = 0;
    L = h->L;
    if (!push_hot(h, "key")) {
        set_err(err, errsz, "hot.key is missing");
        return 0;
    }
    lua_pushstring(L, name);
    lua_pushboolean(L, down ? 1 : 0);
    if (lua_pcall(L, 2, 1, 0) != 0) {
        set_err(err, errsz, "hot.key: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (lua_isstring(L, -1) && action && action_sz) {
        strncpy(action, lua_tostring(L, -1), action_sz - 1);
        action[action_sz - 1] = 0;
    }
    lua_pop(L, 1);
    return 1;
}

int lua_hot_title(LuaHot *h, int fps, int steps, char *out, size_t out_sz,
                  char *err, size_t errsz)
{
    lua_State *L;
    const char *s;
    if (!h || !out || out_sz == 0) return 0;
    out[0] = 0;
    L = h->L;
    if (!push_hot(h, "title")) {
        set_err(err, errsz, "hot.title is missing");
        return 0;
    }
    lua_pushstring(L, h->kernel);
    lua_pushinteger(L, fps);
    lua_pushinteger(L, steps);
    if (lua_pcall(L, 3, 1, 0) != 0) {
        set_err(err, errsz, "hot.title: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    s = lua_tostring(L, -1);
    if (s) {
        strncpy(out, s, out_sz - 1);
        out[out_sz - 1] = 0;
    }
    lua_pop(L, 1);
    return 1;
}

void lua_hot_reset(LuaHot *h)
{
    lua_State *L;
    if (!h) return;
    L = h->L;
    if (!push_hot(h, "reset")) return;
    if (lua_pcall(L, 0, 0, 0) != 0) {
        lua_pop(L, 1);
        return;
    }
}
/* ---- experiment schema -------------------------------------------------
 * Contract: hot.schema(model_id) -> table {
 *   title, axes={x,y},
 *   params = { {name,label,unit,default,min,max,step,axis}, ... },
 *   channels = { {name,label,unit,min,max,cyclic}, ... }
 * }
 * Only metadata is exchanged; the Model callbacks still own the dynamics. */
static void schema_str(lua_State *L, int idx, const char *key, char *out, size_t outsz)
{
    if (!out || outsz == 0) return;
    lua_getfield(L, idx, key);
    if (lua_isstring(L, -1)) {
        strncpy(out, lua_tostring(L, -1), outsz - 1);
        out[outsz - 1] = 0;
    }
    lua_pop(L, 1);
}

static double schema_num(lua_State *L, int idx, const char *key, double def)
{
    double v = def;
    lua_getfield(L, idx, key);
    if (lua_isnumber(L, -1)) v = (double)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

static int schema_bool(lua_State *L, int idx, const char *key, int def)
{
    int v = def;
    lua_getfield(L, idx, key);
    if (lua_isboolean(L, -1)) v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

static void parse_schema_param(lua_State *L, int idx, SchemaParam *p)
{
    memset(p, 0, sizeof(*p));
    schema_str(L, idx, "name", p->name, sizeof(p->name));
    schema_str(L, idx, "label", p->label, sizeof(p->label));
    schema_str(L, idx, "unit", p->unit, sizeof(p->unit));
    p->def = schema_num(L, idx, "default", 0.0);
    p->min = schema_num(L, idx, "min", 0.0);
    p->max = schema_num(L, idx, "max", 0.0);
    p->step = schema_num(L, idx, "step", 0.0);
    p->axis = schema_bool(L, idx, "axis", 0);
}

static void parse_schema_channel(lua_State *L, int idx, SchemaChannel *c)
{
    memset(c, 0, sizeof(*c));
    schema_str(L, idx, "name", c->name, sizeof(c->name));
    schema_str(L, idx, "label", c->label, sizeof(c->label));
    schema_str(L, idx, "unit", c->unit, sizeof(c->unit));
    c->min = schema_num(L, idx, "min", 0.0);
    c->max = schema_num(L, idx, "max", 0.0);
    c->cyclic = schema_bool(L, idx, "cyclic", 0);
}

int lua_hot_schema(LuaHot *h, const char *model_id, Schema *out,
                   char *err, size_t errsz)
{
    lua_State *L;
    size_t n, i;
    if (err && errsz) err[0] = 0;
    if (!h || !out || !model_id) return 0;
    L = h->L;
    memset(out, 0, sizeof(*out));
    lua_getglobal(L, "hot");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot table missing");
        return 0;
    }
    lua_getfield(L, -1, "schema");
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot.schema is missing");
        return 0;
    }
    lua_pushstring(L, model_id);
    if (lua_pcall(L, 1, 1, 0) != 0) {
        set_err(err, errsz, "hot.schema: %s",
                lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot.schema(%s) must return a table", model_id);
        return 0;
    }
    strncpy(out->model, model_id, sizeof(out->model) - 1);
    schema_str(L, -1, "title", out->title, sizeof(out->title));
    lua_getfield(L, -1, "axes");
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1);
        if (lua_isstring(L, -1)) strncpy(out->axis_x, lua_tostring(L, -1), sizeof(out->axis_x) - 1);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, 2);
        if (lua_isstring(L, -1)) strncpy(out->axis_y, lua_tostring(L, -1), sizeof(out->axis_y) - 1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_getfield(L, -1, "params");
    if (lua_istable(L, -1)) {
        n = lua_rawlen(L, -1);
        if (n > MODEL_MAX_PARAMS) n = MODEL_MAX_PARAMS;
        for (i = 0; i < n; ++i) {
            lua_rawgeti(L, -1, (int)i + 1);
            if (lua_istable(L, -1)) {
                parse_schema_param(L, -1, &out->params[out->nparams]);
                if (out->params[out->nparams].name[0]) out->nparams++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_getfield(L, -1, "channels");
    if (lua_istable(L, -1)) {
        n = lua_rawlen(L, -1);
        if (n > MODEL_MAX_CHANNELS) n = MODEL_MAX_CHANNELS;
        for (i = 0; i < n; ++i) {
            lua_rawgeti(L, -1, (int)i + 1);
            if (lua_istable(L, -1)) {
                parse_schema_channel(L, -1, &out->channels[out->nchannels]);
                if (out->channels[out->nchannels].name[0]) out->nchannels++;
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_pop(L, 1);
    return 1;
}
int lua_hot_custom(LuaHot *h, CustomSpec *out, char *err, size_t errsz)
{
    lua_State *L;
    int i;
    if (err && errsz) err[0] = 0;
    if (!h || !out) return 0;
    memset(out, 0, sizeof(*out));
    if (!lua_hot_schema(h, "custom", &out->schema, err, errsz)) return 0;
    L = h->L;
    lua_getglobal(L, "hot");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); set_err(err, errsz, "hot table missing"); return 0; }
    lua_getfield(L, -1, "custom");
    lua_remove(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        set_err(err, errsz, "hot.custom is missing");
        return 0;
    }
    lua_getfield(L, -1, "state");
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, 1);
        if (lua_isstring(L, -1)) strncpy(out->state_names[0], lua_tostring(L, -1), 23);
        lua_pop(L, 1);
        lua_rawgeti(L, -1, 2);
        if (lua_isstring(L, -1)) strncpy(out->state_names[1], lua_tostring(L, -1), 23);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    if (!out->state_names[0][0]) strcpy(out->state_names[0], "x");
    if (!out->state_names[1][0]) strcpy(out->state_names[1], "y");

    lua_getfield(L, -1, "init");
    if (lua_istable(L, -1)) {
        schema_str(L, -1, out->state_names[0], out->init_x, sizeof(out->init_x));
        schema_str(L, -1, out->state_names[1], out->init_y, sizeof(out->init_y));
    }
    lua_pop(L, 1);
    {
        char kind[16];
        kind[0] = 0;
        schema_str(L, -1, "type", kind, sizeof(kind));
        out->type = strcmp(kind, "ode") == 0 ? 1 : 0;
    }
    if (out->type == 1) {
        lua_getfield(L, -1, "deriv");
        if (lua_istable(L, -1)) {
            schema_str(L, -1, out->state_names[0], out->deriv_x, sizeof(out->deriv_x));
            schema_str(L, -1, out->state_names[1], out->deriv_y, sizeof(out->deriv_y));
        }
        lua_pop(L, 1);
        if (!out->deriv_x[0] || !out->deriv_y[0]) {
            lua_pop(L, 1);
            set_err(err, errsz, "custom.deriv must define both derivative expressions");
            return 0;
        }
    } else {
        lua_getfield(L, -1, "step");
        if (lua_istable(L, -1)) {
            schema_str(L, -1, out->state_names[0], out->step_x, sizeof(out->step_x));
            schema_str(L, -1, out->state_names[1], out->step_y, sizeof(out->step_y));
        }
        lua_pop(L, 1);
        if (!out->step_x[0] || !out->step_y[0]) {
            lua_pop(L, 1);
            set_err(err, errsz, "custom.step must define both state expressions");
            return 0;
        }
    }
    lua_getfield(L, -1, "channels");
    if (lua_istable(L, -1)) {
        for (i = 0; i < out->schema.nchannels; ++i) {
            lua_rawgeti(L, -1, i + 1);
            if (lua_istable(L, -1)) {
                schema_str(L, -1, "expr", out->chan_expr[i], sizeof(out->chan_expr[i]));
                if (!out->chan_expr[i][0])
                    strncpy(out->chan_expr[i], out->schema.channels[i].name, sizeof(out->chan_expr[i]) - 1);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    lua_pop(L, 1);
    return 1;
}