/* panel.c - 把内嵌的 Tcl/Tk 运行时和 panel.tcl 跑起来 */
#include "panel.h"

#include "tk_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Panel {
    TkRuntime rt;
    Tcl_Interp *ip;
    PanelCallbacks cb;
};

static int cmd_action(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])
{
    Panel *p = (Panel *)cd;
    const char *name;
    const char *value = "";
    char msg[512];
    msg[0] = 0;
    if (objc < 2) {
        p->rt.api.SetObjResult(ip, p->rt.api.NewStringObj("dp_action: missing action", -1));
        return TCL_OK;
    }
    name = p->rt.api.GetString(objv[1]);
    if (objc > 2) value = p->rt.api.GetString(objv[2]);
    if (p->cb.action) p->cb.action(p->cb.user, name, value, msg, sizeof(msg));
    p->rt.api.SetObjResult(ip, p->rt.api.NewStringObj(msg, -1));
    return TCL_OK;
}

static void add_pair(Panel *p, Tcl_Interp *ip, Tcl_Obj *list, const char *k, const char *v)
{
    p->rt.api.ListObjAppendElement(ip, list, p->rt.api.NewStringObj(k, -1));
    p->rt.api.ListObjAppendElement(ip, list, p->rt.api.NewStringObj(v, -1));
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
    strcpy(st.source, "embedded");
    if (p->cb.status) p->cb.status(p->cb.user, &st);

    list = p->rt.api.NewListObj(0, NULL);
    add_pair(p, ip, list, "paused", st.paused ? "1" : "0");
    add_pair(p, ip, list, "kernel", st.kernel_gpu ? "gpu" : "cpu");
    add_pair(p, ip, list, "kernel_label", st.kernel_gpu ? "GPU compute" : "Fortran CPU");
    snprintf(buf, sizeof(buf), "%d", st.fps);
    add_pair(p, ip, list, "fps", buf);
    snprintf(buf, sizeof(buf), "%d", st.res);
    add_pair(p, ip, list, "res", buf);
    add_pair(p, ip, list, "scheme", st.scheme);
    add_pair(p, ip, list, "grid", st.grid ? "1" : "0");
    add_pair(p, ip, list, "periodic", st.periodic ? "1" : "0");
    add_pair(p, ip, list, "circle", st.circle ? "1" : "0");
    snprintf(buf, sizeof(buf), "%.2f", st.zoom);
    add_pair(p, ip, list, "zoom", buf);
    if (st.selected) snprintf(sel, sizeof(sel), "(%d, %d)", st.sel_x, st.sel_y);
    else strcpy(sel, "none");
    add_pair(p, ip, list, "sel", sel);
    add_pair(p, ip, list, "source", st.source);
    add_pair(p, ip, list, "last", st.last);
    p->rt.api.SetObjResult(ip, list);
    return TCL_OK;
}

static int cmd_quit(ClientData cd, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[])
{
    Panel *p = (Panel *)cd;
    (void)objc; (void)objv;
    if (p->cb.quit) p->cb.quit(p->cb.user);
    p->rt.api.SetObjResult(ip, p->rt.api.NewStringObj("", -1));
    return TCL_OK;
}

Panel *panel_create(const PanelCallbacks *cb, const char *script, size_t script_len,
                    const void *runtime, size_t runtime_len,
                    int x, int y, int w, int h, char *err, size_t errsz)
{
    Panel *p;
    int rc;
    char buf[32];

    if (err && errsz) err[0] = 0;
    if (!cb || !script || script_len == 0) return NULL;

    p = (Panel *)calloc(1, sizeof(Panel));
    if (!p) return NULL;
    p->cb = *cb;

    if (!tk_runtime_init(&p->rt, runtime, runtime_len, err, errsz)) {
        free(p);
        return NULL;
    }
    p->ip = p->rt.api.CreateInterp();
    if (!p->ip) {
        snprintf(err, errsz, "Tcl_CreateInterp failed");
        tk_runtime_destroy(&p->rt);
        free(p);
        return NULL;
    }

    p->rt.api.SetVar(p->ip, "argv0", "dp-phase", TCL_GLOBAL_ONLY);
    p->rt.api.SetVar(p->ip, "argv", "", TCL_GLOBAL_ONLY);
    p->rt.api.SetVar(p->ip, "argc", "0", TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", w); p->rt.api.SetVar(p->ip, "panelW", buf, TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", h); p->rt.api.SetVar(p->ip, "panelH", buf, TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", x); p->rt.api.SetVar(p->ip, "panelX", buf, TCL_GLOBAL_ONLY);
    snprintf(buf, sizeof(buf), "%d", y); p->rt.api.SetVar(p->ip, "panelY", buf, TCL_GLOBAL_ONLY);

    rc = p->rt.api.Init(p->ip);
    if (rc != TCL_OK) {
        snprintf(err, errsz, "Tcl_Init: %s", p->rt.api.GetStringResult(p->ip));
        p->rt.api.DeleteInterp(p->ip);
        tk_runtime_destroy(&p->rt);
        free(p);
        return NULL;
    }
    rc = p->rt.api.TkInit(p->ip);
    if (rc != TCL_OK) {
        snprintf(err, errsz, "Tk_Init: %s", p->rt.api.GetStringResult(p->ip));
        p->rt.api.DeleteInterp(p->ip);
        tk_runtime_destroy(&p->rt);
        free(p);
        return NULL;
    }

    p->rt.api.CreateObjCommand(p->ip, "dp_action", cmd_action, p, NULL);
    p->rt.api.CreateObjCommand(p->ip, "dp_status", cmd_status, p, NULL);
    p->rt.api.CreateObjCommand(p->ip, "dp_quit", cmd_quit, p, NULL);

    rc = p->rt.api.EvalEx(p->ip, script, (int)script_len, 0);
    if (rc != TCL_OK) {
        snprintf(err, errsz, "panel.tcl: %s", p->rt.api.GetStringResult(p->ip));
        p->rt.api.DeleteInterp(p->ip);
        tk_runtime_destroy(&p->rt);
        free(p);
        return NULL;
    }
    return p;
}

void panel_pump(Panel *p)
{
    int n = 0;
    if (!p || !p->ip) return;
    while (n++ < 64 && p->rt.api.DoOneEvent(TCL_ALL_EVENTS | TCL_DONT_WAIT)) {
        /* keep the UI responsive without starving the GL loop */
    }
}

void panel_destroy(Panel *p)
{
    if (!p) return;
    if (p->ip) {
        p->rt.api.EvalEx(p->ip, "destroy .", -1, 0);
        p->rt.api.DeleteInterp(p->ip);
        p->ip = NULL;
    }
    tk_runtime_destroy(&p->rt);
    free(p);
}
