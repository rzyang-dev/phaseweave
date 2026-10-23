/* panel.c - 直接链接 Tcl/Tk，从 exe 旁边的 ui/panel.tcl 启动面板。
 *
 * 发布目录：
 *   dp-phase.exe
 *   tcl86.dll / tk86.dll / libgcc_s_dw2-1.dll / zlib1.dll
 *   lib/tcl8.6, lib/tk8.6
 *   ui/panel.tcl
 *
 * Tcl/Tk 的脚本库路径由 exe 目录决定，不依赖当前工作目录或系统安装。
 */
#include "panel.h"

#include <tcl.h>
#include <tk.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Panel {
    Tcl_Interp *ip;
    PanelCallbacks cb;
};

static int dir_exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void exe_paths(char *exe, size_t exesz, char *dir, size_t dirsz)
{
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)exesz);
    char *slash;
    if (n == 0 || n >= exesz) { exe[0] = 0; dir[0] = 0; return; }
    exe[n] = 0;
    strncpy(dir, exe, dirsz - 1);
    dir[dirsz - 1] = 0;
    slash = strrchr(dir, '\\');
    if (slash) *slash = 0;
}

static int set_library_paths(char *err, size_t errsz)
{
    char exe[MAX_PATH], dir[MAX_PATH];
    char tcl_lib[MAX_PATH], tk_lib[MAX_PATH];
    char env[2 * MAX_PATH];
    exe_paths(exe, sizeof(exe), dir, sizeof(dir));
    if (!dir[0]) {
        snprintf(err, errsz, "cannot locate dp-phase.exe");
        return 0;
    }
    snprintf(tcl_lib, sizeof(tcl_lib), "%s\\lib\\tcl8.6", dir);
    snprintf(tk_lib, sizeof(tk_lib), "%s\\lib\\tk8.6", dir);
    if (!dir_exists(tcl_lib) || !dir_exists(tk_lib)) {
        /* 开发时的兜底：直接用工具链里的脚本库 */
        const char *mingw = getenv("MINGW_ROOT");
        if (!mingw || !mingw[0]) mingw = "C:\\MinGW";
        snprintf(tcl_lib, sizeof(tcl_lib), "%s\\lib\\tcl8.6", mingw);
        snprintf(tk_lib, sizeof(tk_lib), "%s\\lib\\tk8.6", mingw);
        if (!dir_exists(tcl_lib) || !dir_exists(tk_lib)) {
            snprintf(err, errsz, "Tcl/Tk scripts not found: %s / %s", tcl_lib, tk_lib);
            return 0;
        }
    }
    snprintf(env, sizeof(env), "TCL_LIBRARY=%s", tcl_lib);
    _putenv(env);
    SetEnvironmentVariableA("TCL_LIBRARY", tcl_lib);
    snprintf(env, sizeof(env), "TK_LIBRARY=%s", tk_lib);
    _putenv(env);
    SetEnvironmentVariableA("TK_LIBRARY", tk_lib);
    Tcl_FindExecutable(exe);
    return 1;
}

static int cmd_action(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])
{
    Panel *p = (Panel *)cd;
    const char *name;
    const char *value = "";
    char msg[512];
    msg[0] = 0;
    if (objc < 2) {
        Tcl_SetObjResult(ip, Tcl_NewStringObj("dp_action: missing action", -1));
        return TCL_OK;
    }
    name = Tcl_GetString(objv[1]);
    if (objc > 2) value = Tcl_GetString(objv[2]);
    if (p->cb.action) p->cb.action(p->cb.user, name, value, msg, sizeof(msg));
    Tcl_SetObjResult(ip, Tcl_NewStringObj(msg, -1));
    return TCL_OK;
}

static void add_pair(Tcl_Interp *ip, Tcl_Obj *list, const char *k, const char *v)
{
    Tcl_ListObjAppendElement(ip, list, Tcl_NewStringObj(k, -1));
    Tcl_ListObjAppendElement(ip, list, Tcl_NewStringObj(v, -1));
}

static int cmd_status(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])
{
    Panel *p = (Panel *)cd;
    PanelStatus st;
    Tcl_Obj *list;
    char buf[64];
    char sel[64];
    (void)objc; (void)objv;

    memset(&st, 0, sizeof(st));
    st.zoom = 1.0;
    strcpy(st.scheme, "phase");
    strcpy(st.source, "experiments/double-pendulum.lua");
    if (p->cb.status) p->cb.status(p->cb.user, &st);

    list = Tcl_NewListObj(0, NULL);
    add_pair(ip, list, "paused", st.paused ? "1" : "0");
    add_pair(ip, list, "kernel", st.kernel_gpu ? "gpu" : "cpu");
    add_pair(ip, list, "kernel_label", st.kernel_gpu ? "GPU compute" : "CPU (C)");
    snprintf(buf, sizeof(buf), "%d", st.fps);
    add_pair(ip, list, "fps", buf);
    snprintf(buf, sizeof(buf), "%d", st.res);
    add_pair(ip, list, "res", buf);
    add_pair(ip, list, "scheme", st.scheme);
    add_pair(ip, list, "grid", st.grid ? "1" : "0");
    add_pair(ip, list, "periodic", st.periodic ? "1" : "0");
    add_pair(ip, list, "circle", st.circle ? "1" : "0");
    snprintf(buf, sizeof(buf), "%.2f", st.zoom);
    add_pair(ip, list, "zoom", buf);
    if (st.selected) snprintf(sel, sizeof(sel), "(%d, %d)", st.sel_x, st.sel_y);
    else strcpy(sel, "none");
    add_pair(ip, list, "sel", sel);
    add_pair(ip, list, "source", st.source);
    add_pair(ip, list, "last", st.last);
    Tcl_SetObjResult(ip, list);
    return TCL_OK;
}

static int cmd_quit(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])
{
    Panel *p = (Panel *)cd;
    (void)objc; (void)objv;
    if (p->cb.quit) p->cb.quit(p->cb.user);
    Tcl_SetObjResult(ip, Tcl_NewStringObj("", -1));
    return TCL_OK;
}

Panel *panel_create(const PanelCallbacks *cb, const char *script_path,
                    int x, int y, int w, int h, char *err, size_t errsz)
{
    Panel *p;
    int rc;
    char buf[32];

    if (err && errsz) err[0] = 0;
    if (!cb || !script_path || !script_path[0]) {
        if (err) snprintf(err, errsz, "panel.tcl path is empty");
        return NULL;
    }
    p = (Panel *)calloc(1, sizeof(Panel));
    if (!p) return NULL;
    p->cb = *cb;

    if (!set_library_paths(err, errsz)) { free(p); return NULL; }

    p->ip = Tcl_CreateInterp();
    if (!p->ip) {
        if (err) snprintf(err, errsz, "Tcl_CreateInterp failed");
        free(p);
        return NULL;
    }
    Tcl_SetVar(p->ip, "argv0", "dp-phase", TCL_GLOBAL_ONLY);
    Tcl_SetVar(p->ip, "argv", "", TCL_GLOBAL_ONLY);
    Tcl_SetVar(p->ip, "argc", "0", TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", w); Tcl_SetVar(p->ip, "panelW", buf, TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", h); Tcl_SetVar(p->ip, "panelH", buf, TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", x); Tcl_SetVar(p->ip, "panelX", buf, TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", y); Tcl_SetVar(p->ip, "panelY", buf, TCL_GLOBAL_ONLY);

    rc = Tcl_Init(p->ip);
    if (rc != TCL_OK) {
        if (err) snprintf(err, errsz, "Tcl_Init: %s", Tcl_GetStringResult(p->ip));
        Tcl_DeleteInterp(p->ip);
        free(p);
        return NULL;
    }
    rc = Tk_Init(p->ip);
    if (rc != TCL_OK) {
        if (err) snprintf(err, errsz, "Tk_Init: %s", Tcl_GetStringResult(p->ip));
        Tcl_DeleteInterp(p->ip);
        free(p);
        return NULL;
    }

    Tcl_CreateObjCommand(p->ip, "dp_action", cmd_action, p, NULL);
    Tcl_CreateObjCommand(p->ip, "dp_status", cmd_status, p, NULL);
    Tcl_CreateObjCommand(p->ip, "dp_quit", cmd_quit, p, NULL);

    rc = Tcl_EvalFile(p->ip, script_path);
    if (rc != TCL_OK) {
        if (err) snprintf(err, errsz, "panel.tcl: %s", Tcl_GetStringResult(p->ip));
        Tcl_DeleteInterp(p->ip);
        free(p);
        return NULL;
    }
    return p;
}

void panel_pump(Panel *p)
{
    int n = 0;
    if (!p || !p->ip) return;
    while (n++ < 64 && Tcl_DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT)) {
        /* keep the UI responsive without starving the GL loop */
    }
}

void panel_destroy(Panel *p)
{
    if (!p) return;
    if (p->ip) {
        Tcl_EvalEx(p->ip, "destroy .", -1, 0);
        Tcl_DeleteInterp(p->ip);
        p->ip = NULL;
    }
    free(p);
}