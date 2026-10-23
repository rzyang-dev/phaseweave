/* platform.c - 原生 Win32 窗口（无 raylib），QPC 计时，rc 资源读取 */
#include "platform.h"

#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

struct Platform {
    HINSTANCE inst;
    HWND      hwnd;
    HDC       hdc;
    int       width;
    int       height;
    int       hidden;
    int       quit;
    int       vsync;
    double    freq;
    double    t0;
    double    tprev;
    double    tframe;
    void     *user;
    PlatformEventFn eventfn;
};

static const wchar_t *kClass = L"DPPhaseWindow";
static int g_class_registered = 0;

static void utf8_to_wide(const char *src, wchar_t *dst, int cap)
{
    if (cap <= 0) return;
    dst[0] = 0;
    if (!src) return;
    MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, cap);
}

static LRESULT CALLBACK platform_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Platform *p = (Platform *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        p = (Platform *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
        if (p) p->hwnd = hwnd;
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (p) p->quit = 1;
        PostQuitMessage(0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (p) {
            p->width = LOWORD(lp);
            p->height = HIWORD(lp);
        }
        return 0;
    case WM_DPICHANGED:
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_CROSS));
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (p && p->eventfn && (lp & (1L << 30)) == 0) {
            DpEvent ev;
            ev.type = DP_EV_KEY; ev.a = (int)wp; ev.b = 1; ev.c = 0;
            p->eventfn(p->user, &ev);
        }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (p && p->eventfn) {
            DpEvent ev;
            ev.type = DP_EV_KEY; ev.a = (int)wp; ev.b = 0; ev.c = 0;
            p->eventfn(p->user, &ev);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (p && p->eventfn) {
            DpEvent ev;
            ev.type = DP_EV_MOUSE_DOWN;
            ev.a = (msg == WM_LBUTTONDOWN) ? 0 : 1;
            ev.b = (short)LOWORD(lp);
            ev.c = (short)HIWORD(lp);
            SetFocus(hwnd);
            if (ev.a == 0) SetCapture(hwnd);
            p->eventfn(p->user, &ev);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        if (p && p->eventfn) {
            DpEvent ev;
            ev.type = DP_EV_MOUSE_UP;
            ev.a = (msg == WM_LBUTTONUP) ? 0 : 1;
            ev.b = (short)LOWORD(lp);
            ev.c = (short)HIWORD(lp);
            if (ev.a == 0) ReleaseCapture();
            p->eventfn(p->user, &ev);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (p && p->eventfn) {
            DpEvent ev;
            ev.type = DP_EV_MOUSE_MOVE;
            ev.a = (short)LOWORD(lp);
            ev.b = (short)HIWORD(lp);
            ev.c = 0;
            p->eventfn(p->user, &ev);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (p && p->eventfn) {
            POINT pt;
            DpEvent ev;
            pt.x = (short)LOWORD(lp);
            pt.y = (short)HIWORD(lp);
            ScreenToClient(hwnd, &pt);
            ev.type = DP_EV_WHEEL;
            ev.a = GET_WHEEL_DELTA_WPARAM(wp);
            ev.b = pt.x;
            ev.c = pt.y;
            p->eventfn(p->user, &ev);
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static void register_class(HINSTANCE inst)
{
    WNDCLASSEXW wc;
    if (g_class_registered) return;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = platform_wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    g_class_registered = 1;
}

Platform *platform_create(HINSTANCE inst, const wchar_t *title, int client_side,
                          int hidden, void *user, PlatformEventFn eventfn)
{
    Platform *p;
    LARGE_INTEGER f;
    int side = client_side;
    DWORD style;
    RECT rc;
    int outer_w, outer_h, x, y;
    RECT work;

    if (side < 128) side = 128;

    p = (Platform *)calloc(1, sizeof(Platform));
    if (!p) return NULL;
    p->inst = inst;
    p->user = user;
    p->eventfn = eventfn;
    p->hidden = hidden;
    QueryPerformanceFrequency(&f);
    p->freq = (double)f.QuadPart;
    p->t0 = platform_now();
    p->tprev = p->t0;
    p->tframe = p->t0;

    register_class(inst);

    if (hidden) {
        style = WS_POPUP;
        outer_w = outer_h = 64;
        x = y = 0;
    } else {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        rc.left = rc.top = 0;
        rc.right = side;
        rc.bottom = side;
        AdjustWindowRectEx(&rc, style, FALSE, 0);
        outer_w = rc.right - rc.left;
        outer_h = rc.bottom - rc.top;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + ((work.right - work.left) - outer_w) / 2;
        y = work.top + ((work.bottom - work.top) - outer_h) / 2;
    }

    p->hwnd = CreateWindowExW(0, kClass, title ? title : L"Double Pendulum Phase Space",
                              style, x, y, outer_w, outer_h, NULL, NULL, inst, p);
    if (!p->hwnd) {
        free(p);
        return NULL;
    }

    p->hdc = GetDC(p->hwnd);
    {
        RECT cr;
        GetClientRect(p->hwnd, &cr);
        p->width = cr.right - cr.left;
        p->height = cr.bottom - cr.top;
    }

    if (!hidden) {
        ShowWindow(p->hwnd, SW_SHOW);
        UpdateWindow(p->hwnd);
        SetForegroundWindow(p->hwnd);
    }

    timeBeginPeriod(1);
    return p;
}

void platform_destroy(Platform *p)
{
    if (!p) return;
    if (p->hdc) ReleaseDC(p->hwnd, p->hdc);
    if (p->hwnd) DestroyWindow(p->hwnd);
    timeEndPeriod(1);
    free(p);
}

void platform_pump(Platform *p)
{
    MSG msg;
    if (!p) return;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            p->quit = 1;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

int platform_quit(const Platform *p) { return p ? p->quit : 1; }

double platform_now(void)
{
    static double freq = 0.0;
    LARGE_INTEGER c;
    if (freq == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = (double)f.QuadPart;
    }
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / freq;
}

double platform_dt(Platform *p)
{
    double now = platform_now();
    double dt = now - p->tprev;
    p->tprev = now;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.25) dt = 0.25;
    return dt;
}

void platform_set_vsync(Platform *p, int on) { if (p) p->vsync = on; }

void platform_frame_end(Platform *p)
{
    if (!p) return;
    if (p->hdc) SwapBuffers(p->hdc);
    if (!p->hidden && !p->vsync) {
        double now = platform_now();
        double elapsed = now - p->tframe;
        double target = 1.0 / 60.0;
        if (elapsed < target) {
            double remain = target - elapsed;
            if (remain > 0.002) Sleep((DWORD)((remain - 0.001) * 1000.0));
            while (platform_now() - p->tframe < target) { }
        }
        p->tframe = platform_now();
    } else {
        p->tframe = platform_now();
    }
}

void platform_set_title(Platform *p, const char *utf8)
{
    wchar_t wide[512];
    if (!p || !p->hwnd) return;
    utf8_to_wide(utf8, wide, 512);
    SetWindowTextW(p->hwnd, wide);
}

void platform_move_client(Platform *p, int x, int y)
{
    RECT rc;
    if (!p || !p->hwnd) return;
    rc.left = 0; rc.top = 0;
    rc.right = p->width; rc.bottom = p->height;
    AdjustWindowRectEx(&rc, (DWORD)GetWindowLongW(p->hwnd, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongW(p->hwnd, GWL_EXSTYLE));
    SetWindowPos(p->hwnd, NULL, x - rc.left, y - rc.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND platform_hwnd(Platform *p) { return p ? p->hwnd : NULL; }
HDC  platform_hdc(Platform *p) { return p ? p->hdc : NULL; }
int  platform_width(const Platform *p) { return p ? p->width : 0; }
int  platform_height(const Platform *p) { return p ? p->height : 0; }

void platform_console_attach(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    }
    if (GetConsoleWindow() != NULL)
        SetConsoleOutputCP(CP_UTF8);
}

int platform_read_resource(int id, const void **data, size_t *size)
{
    HRSRC res;
    HGLOBAL glob;
    void *ptr;
    DWORD n;
    if (data) *data = NULL;
    if (size) *size = 0;
    res = FindResourceA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(id), RT_RCDATA);
    if (!res) return 0;
    n = SizeofResource(GetModuleHandleA(NULL), res);
    glob = LoadResource(GetModuleHandleA(NULL), res);
    if (!glob) return 0;
    ptr = LockResource(glob);
    if (!ptr) return 0;
    if (data) *data = ptr;
    if (size) *size = (size_t)n;
    return 1;
}

int platform_read_file(const char *path, char **data, size_t *size)
{
    FILE *f;
    long n;
    char *buf;
    if (data) *data = NULL;
    if (size) *size = 0;
    if (!path) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    n = ftell(f);
    if (n < 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return 0; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[n] = 0;
    fclose(f);
    if (data) *data = buf;
    if (size) *size = (size_t)n;
    return 1;
}

void platform_free(void *ptr) { free(ptr); }

void platform_message_box(const char *utf8, int error)
{
    wchar_t wide[4096];
    utf8_to_wide(utf8, wide, 4096);
    MessageBoxW(NULL, wide, L"Double Pendulum Phase Space",
                MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

void platform_exe_dir(char *buf, size_t size)
{
    char path[MAX_PATH];
    DWORD n;
    size_t i;
    if (!buf || size == 0) return;
    buf[0] = 0;
    n = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (n == 0 || n >= sizeof(path)) return;
    path[n] = 0;
    for (i = n; i > 0; --i) {
        if (path[i - 1] == '\\' || path[i - 1] == '/') {
            path[i - 1] = 0;
            break;
        }
    }
    strncpy(buf, path, size - 1);
    buf[size - 1] = 0;
}