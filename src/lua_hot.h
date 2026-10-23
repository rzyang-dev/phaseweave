/* lua_hot.h - 嵌入式 Lua 5.2 热逻辑宿主
 *
 * 契约（脚本返回一个表 hot）：
 *   hot.defaults()                   -> {physics=..., grid=..., sim=...}
 *   hot.palette()                    -> 3072 个 0..255 的整数（1024 项 RGB）
 *   hot.params()                     -> m1,m2,l1,l2,g,damping,dt,max_steps
 *   hot.tick(dt)                     -> 本帧要推进的子步数
 *   hot.key(name, down)              -> "reset"|"pause"|"palette"|"kernel"|nil
 *   hot.title(kernel, fps, steps)    -> UTF-8 标题串
 *   hot.reset()                      -> 网格重置后的回调
 * 宿主函数：host.kernel() / host.res() / host.log(s) / host.time()
 */
#ifndef DP_LUA_HOT_H
#define DP_LUA_HOT_H

#include "core.h"

typedef struct LuaHot LuaHot;

LuaHot *lua_hot_create(const char *src, size_t len, const char *chunkname,
                       const char *kernel, int res, char *err, size_t errsz);
void    lua_hot_destroy(LuaHot *h);
int     lua_hot_reload(LuaHot *h, const char *src, size_t len, const char *chunkname,
                       char *err, size_t errsz);

void    lua_hot_set_kernel(LuaHot *h, const char *kernel);
void    lua_hot_set_res(LuaHot *h, int res);

int     lua_hot_defaults(LuaHot *h, DpConfig *cfg, char *err, size_t errsz);
int     lua_hot_palette(LuaHot *h, unsigned char *lut, char *err, size_t errsz);
int     lua_hot_params(LuaHot *h, DpParams *p, int *max_steps, char *err, size_t errsz);
int     lua_hot_tick(LuaHot *h, double dt, int *steps, char *err, size_t errsz);
int     lua_hot_status(LuaHot *h, int *paused, char *scheme, size_t scheme_sz,
                       char *err, size_t errsz);
int     lua_hot_set_scheme(LuaHot *h, const char *name, char *err, size_t errsz);
int     lua_hot_key(LuaHot *h, const char *name, int down, char *action, size_t action_sz,
                    char *err, size_t errsz);
int     lua_hot_title(LuaHot *h, int fps, int steps, char *out, size_t out_sz,
                      char *err, size_t errsz);
void    lua_hot_reset(LuaHot *h);

#endif