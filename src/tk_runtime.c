/* tk_runtime.c - DPTCL86 归档解包 + Tcl/Tk DLL 动态加载 */
#include "tk_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int rd_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void seterr(char *err, size_t errsz, const char *fmt, ...)
{
    va_list ap;
    if (!err || errsz == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, errsz, fmt, ap);
    va_end(ap);
}

static int file_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int ensure_dir(const char *path)
{
    char tmp[MAX_PATH];
    char *p;
    size_t n;
    if (!path || !path[0]) return 0;
    if (dir_exists(path)) return 1;
    n = strlen(path);
    if (n >= sizeof(tmp)) return 0;
    memcpy(tmp, path, n + 1);
    for (p = tmp + 3; *p; ++p) {
        if (*p == '\\' || *p == '/') {
            char c = *p;
            *p = 0;
            if (!dir_exists(tmp)) CreateDirectoryA(tmp, NULL);
            *p = c;
        }
    }
    if (!dir_exists(tmp)) CreateDirectoryA(tmp, NULL);
    return dir_exists(path);
}

static int ensure_parent(const char *path)
{
    char tmp[MAX_PATH];
    char *slash;
    size_t n;
    if (!path) return 0;
    n = strlen(path);
    if (n >= sizeof(tmp)) return 0;
    memcpy(tmp, path, n + 1);
    slash = strrchr(tmp, '\\');
    if (!slash) slash = strrchr(tmp, '/');
    if (!slash) return 1;
    *slash = 0;
    return ensure_dir(tmp);
}

static int safe_rel(const char *name)
{
    const char *p = name;
    if (!name || !name[0]) return 0;
    if (name[0] == '/' || name[0] == '\\' || name[1] == ':') return 0;
    while (*p) {
        if (p[0] == '.' && p[1] == '.' &&
            (p[2] == 0 || p[2] == '/' || p[2] == '\\')) return 0;
        ++p;
    }
    return 1;
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    if (len && fwrite(data, 1, len, f) != len) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

static int extract_archive(const char *base, const unsigned char *data, size_t len,
                           char *err, size_t errsz)
{
    const unsigned char *p = data;
    size_t left = len;
    unsigned int count, i;
    if (left < 12 || memcmp(p, "DPTCL86", 7) != 0) {
        seterr(err, errsz, "bad Tcl/Tk archive magic");
        return 0;
    }
    p += 8;
    left -= 8;
    count = rd_u32(p);
    p += 4;
    left -= 4;
    if (!ensure_dir(base)) {
        seterr(err, errsz, "cannot create %s", base);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        unsigned int nlen, dlen;
        char rel[MAX_PATH];
        char full[MAX_PATH];
        char *q;
        if (left < 2) { seterr(err, errsz, "truncated archive"); return 0; }
        nlen = rd_u16(p); p += 2; left -= 2;
        if (nlen == 0 || nlen >= sizeof(rel) || left < nlen + 4) {
            seterr(err, errsz, "bad archive record");
            return 0;
        }
        memcpy(rel, p, nlen);
        rel[nlen] = 0;
        p += nlen; left -= nlen;
        dlen = rd_u32(p); p += 4; left -= 4;
        if (left < dlen) { seterr(err, errsz, "truncated archive data"); return 0; }
        if (!safe_rel(rel)) { seterr(err, errsz, "unsafe archive path: %s", rel); return 0; }
        snprintf(full, sizeof(full), "%s\\%s", base, rel);
        for (q = full + strlen(base); *q; ++q) if (*q == '/') *q = '\\';
        if (!ensure_parent(full) || !write_file(full, p, dlen)) {
            seterr(err, errsz, "cannot write %s", full);
            return 0;
        }
        p += dlen;
        left -= dlen;
    }
    return 1;
}

static void *dll_proc(HMODULE mod, const char *name)
{
    void *p;
    if (!mod) return NULL;
    p = (void *)GetProcAddress(mod, name);
    return p;
}

#define RESOLVE(dst, mod, name) do { \
    dst = (__typeof__(dst))dll_proc((mod), (name)); \
    if (!dst) { seterr(err, errsz, "missing %s", (name)); goto fail; } \
} while (0)

static void set_paths(TkRuntime *rt, const char *base)
{
    snprintf(rt->base, sizeof(rt->base), "%s", base);
    snprintf(rt->tcl_library, sizeof(rt->tcl_library), "%s\\tcl8.6", base);
    snprintf(rt->tk_library, sizeof(rt->tk_library), "%s\\tk8.6", base);
    snprintf(rt->dll_dir, sizeof(rt->dll_dir), "%s", base);
}

static int base_ready(TkRuntime *rt)
{
    char tcl_dll[MAX_PATH], tk_dll[MAX_PATH], init_tcl[MAX_PATH], tk_tcl[MAX_PATH];
    snprintf(tcl_dll, sizeof(tcl_dll), "%s\\tcl86.dll", rt->dll_dir);
    snprintf(tk_dll, sizeof(tk_dll), "%s\\tk86.dll", rt->dll_dir);
    snprintf(init_tcl, sizeof(init_tcl), "%s\\init.tcl", rt->tcl_library);
    snprintf(tk_tcl, sizeof(tk_tcl), "%s\\tk.tcl", rt->tk_library);
    return file_exists(tcl_dll) && file_exists(tk_dll) &&
           file_exists(init_tcl) && file_exists(tk_tcl);
}

static int try_base(TkRuntime *rt, const char *base, const void *archive, size_t archive_len,
                    char *err, size_t errsz)
{
    set_paths(rt, base);
    if (base_ready(rt)) return 1;
    if (archive && archive_len) {
        return extract_archive(base, (const unsigned char *)archive, archive_len, err, errsz);
    }
    return 0;
}

int tk_runtime_init(TkRuntime *rt, const void *archive, size_t archive_len,
                    char *err, size_t errsz)
{
    char cand[MAX_PATH];
    char tcl_dll[MAX_PATH];
    char tk_dll[MAX_PATH];
    char init_tcl[MAX_PATH];
    char tk_tcl[MAX_PATH];
    DWORD n;
    int extracted = 0;

    if (!rt) return 0;
    memset(rt, 0, sizeof(*rt));
    if (err && errsz) err[0] = 0;

    /* 优先 LOCALAPPDATA；受限环境下再试 TEMP（DSH/沙箱通常允许临时目录） */
    n = GetEnvironmentVariableA("LOCALAPPDATA", cand, (DWORD)sizeof(cand));
    if (n > 0 && n < sizeof(cand)) {
        snprintf(cand, sizeof(cand), "%s\\dp-phase\\tcl86-8.6.0", cand);
        if (try_base(rt, cand, archive, archive_len, err, errsz)) extracted = 1;
    }
    if (!extracted) {
        n = GetTempPathA((DWORD)sizeof(cand), cand);
        if (n > 0 && n < sizeof(cand)) {
            snprintf(cand + n, sizeof(cand) - n, "dp-phase-tcl86-8.6.0");
            if (try_base(rt, cand, archive, archive_len, err, errsz)) extracted = 1;
        }
    }

    if (!extracted) {
        /* 开发/兜底：直接使用工具链里的运行时 */
        const char *mingw = getenv("MINGW_ROOT");
        if (!mingw || !mingw[0]) mingw = "C:\\MinGW";
        snprintf(rt->base, sizeof(rt->base), "%s", mingw);
        snprintf(rt->tcl_library, sizeof(rt->tcl_library), "%s\\lib\\tcl8.6", mingw);
        snprintf(rt->tk_library, sizeof(rt->tk_library), "%s\\lib\\tk8.6", mingw);
        snprintf(rt->dll_dir, sizeof(rt->dll_dir), "%s\\bin", mingw);
        snprintf(init_tcl, sizeof(init_tcl), "%s\\lib\\tcl8.6\\init.tcl", mingw);
        snprintf(tk_tcl, sizeof(tk_tcl), "%s\\lib\\tk8.6\\tk.tcl", mingw);
        snprintf(tcl_dll, sizeof(tcl_dll), "%s\\bin\\tcl86.dll", mingw);
        snprintf(tk_dll, sizeof(tk_dll), "%s\\bin\\tk86.dll", mingw);
        if (!file_exists(tcl_dll) || !file_exists(tk_dll) ||
            !file_exists(init_tcl) || !file_exists(tk_tcl)) {
            seterr(err, errsz, "Tcl/Tk runtime not found (archive, LOCALAPPDATA, TEMP, or %s)", mingw);
            return 0;
        }
    } else {
        snprintf(tcl_dll, sizeof(tcl_dll), "%s\\tcl86.dll", rt->dll_dir);
        snprintf(tk_dll, sizeof(tk_dll), "%s\\tk86.dll", rt->dll_dir);
    }

    /* Tcl 内部用 CRT 的 getenv() 找 init.tcl；只调 SetEnvironmentVariable
     * 不会同步到 MinGW 的 environ，所以还要 _putenv。 */
    {
        char env[2 * MAX_PATH];
        SetEnvironmentVariableA("TCL_LIBRARY", rt->tcl_library);
        SetEnvironmentVariableA("TK_LIBRARY", rt->tk_library);
        snprintf(env, sizeof(env), "TCL_LIBRARY=%s", rt->tcl_library);
        _putenv(env);
        snprintf(env, sizeof(env), "TK_LIBRARY=%s", rt->tk_library);
        _putenv(env);
    }

    rt->tcl = LoadLibraryExA(tcl_dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!rt->tcl) {
        seterr(err, errsz, "LoadLibrary(tcl86.dll) failed: %lu", (unsigned long)GetLastError());
        return 0;
    }
    rt->tk = LoadLibraryExA(tk_dll, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!rt->tk) {
        seterr(err, errsz, "LoadLibrary(tk86.dll) failed: %lu", (unsigned long)GetLastError());
        FreeLibrary(rt->tcl);
        rt->tcl = NULL;
        return 0;
    }

    RESOLVE(rt->api.CreateInterp, rt->tcl, "Tcl_CreateInterp");
    RESOLVE(rt->api.Init, rt->tcl, "Tcl_Init");
    RESOLVE(rt->api.TkInit, rt->tk, "Tk_Init");
    RESOLVE(rt->api.DeleteInterp, rt->tcl, "Tcl_DeleteInterp");
    RESOLVE(rt->api.EvalEx, rt->tcl, "Tcl_EvalEx");
    RESOLVE(rt->api.GetStringResult, rt->tcl, "Tcl_GetStringResult");
    RESOLVE(rt->api.SetVar, rt->tcl, "Tcl_SetVar");
    RESOLVE(rt->api.CreateObjCommand, rt->tcl, "Tcl_CreateObjCommand");
    RESOLVE(rt->api.NewStringObj, rt->tcl, "Tcl_NewStringObj");
    RESOLVE(rt->api.NewListObj, rt->tcl, "Tcl_NewListObj");
    RESOLVE(rt->api.ListObjAppendElement, rt->tcl, "Tcl_ListObjAppendElement");
    RESOLVE(rt->api.SetObjResult, rt->tcl, "Tcl_SetObjResult");
    RESOLVE(rt->api.GetString, rt->tcl, "Tcl_GetString");
    RESOLVE(rt->api.DoOneEvent, rt->tcl, "Tcl_DoOneEvent");
    return 1;

fail:
    if (rt->tk) FreeLibrary(rt->tk);
    if (rt->tcl) FreeLibrary(rt->tcl);
    rt->tk = rt->tcl = NULL;
    return 0;
}

void tk_runtime_destroy(TkRuntime *rt)
{
    if (!rt) return;
    if (rt->tk) FreeLibrary(rt->tk);
    if (rt->tcl) FreeLibrary(rt->tcl);
    rt->tk = rt->tcl = NULL;
}