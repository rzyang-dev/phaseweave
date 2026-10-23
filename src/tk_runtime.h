/* tk_runtime.h - 从 exe 资源里取出 Tcl/Tk 运行时，运行时加载 DLL 与脚本库
 *
 * 工具链只提供 tcl86.dll/tk86.dll 与 C:\MinGW\lib\tcl8.6、tk8.6 脚本；
 * 为了让交付物仍然是一个 exe，构建时把 DLL 和脚本树打成 DPTCL86 归档，
 * 首次启动解到 %LOCALAPPDATA%\dp-phase\tcl86-8.6.0，再用 GetProcAddress 调用。
 */
#ifndef DP_TK_RUNTIME_H
#define DP_TK_RUNTIME_H

#include <windows.h>
#include <tcl.h>
#include <tk.h>

typedef struct {
    Tcl_Interp *(*CreateInterp)(void);
    int         (*Init)(Tcl_Interp *);
    int         (*TkInit)(Tcl_Interp *);
    void        (*DeleteInterp)(Tcl_Interp *);
    int         (*EvalEx)(Tcl_Interp *, const char *, int, int);
    const char *(*GetStringResult)(Tcl_Interp *);
    const char *(*SetVar)(Tcl_Interp *, const char *, const char *, int);
    Tcl_Command (*CreateObjCommand)(Tcl_Interp *, const char *, Tcl_ObjCmdProc *,
                                    ClientData, Tcl_CmdDeleteProc *);
    Tcl_Obj    *(*NewStringObj)(const char *, int);
    Tcl_Obj    *(*NewListObj)(int, Tcl_Obj *const[]);
    int         (*ListObjAppendElement)(Tcl_Interp *, Tcl_Obj *, Tcl_Obj *);
    void        (*SetObjResult)(Tcl_Interp *, Tcl_Obj *);
    const char *(*GetString)(Tcl_Obj *);
    int         (*DoOneEvent)(int);
} TkApi;

typedef struct {
    HMODULE tcl;
    HMODULE tk;
    TkApi api;
    char base[MAX_PATH];
    char tcl_library[MAX_PATH];
    char tk_library[MAX_PATH];
    char dll_dir[MAX_PATH];
} TkRuntime;

int  tk_runtime_init(TkRuntime *rt, const void *archive, size_t archive_len,
                     char *err, size_t errsz);
void tk_runtime_destroy(TkRuntime *rt);

#endif
