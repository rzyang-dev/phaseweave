/* pendulum_view.c - 选中像素对应的双摆 + 数值信息，单独一个 Win32 窗口 */
#include "pendulum_view.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PendulumView {
    HINSTANCE inst;
    HWND hwnd;
    int width, height;
    void *user;
    PendulumCloseFn closefn;
    PendulumKeyFn keyfn;

    float state[4];
    int has;
    int cell_x, cell_y;
    DpParams params;

    HFONT font_title;
    HFONT font_body;
    HFONT font_small;
};

static const wchar_t *kClass = L"DPPendulumView";
static int g_class_registered = 0;

static void paint(PendulumView *v)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(v->hwnd, &ps);
    RECT rc;
    HDC mem;
    HBITMAP bmp, oldbmp;
    int w, h;
    HBRUSH bg;
    HPEN pen1, pen2, pen3;
    HBRUSH br1, br2, brp;
    HPEN oldpen;
    HBRUSH oldbrush;

    GetClientRect(v->hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) { EndPaint(v->hwnd, &ps); return; }
    mem = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, w, h);
    oldbmp = (HBITMAP)SelectObject(mem, bmp);

    bg = CreateSolidBrush(RGB(10, 14, 18));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    SelectObject(mem, v->font_title);
    SetTextColor(mem, RGB(232, 236, 242));
    TextOutA(mem, 14, 10, "Selected pendulum", 17);

    if (!v->has) {
        RECT tr = rc;
        SelectObject(mem, v->font_body);
        SetTextColor(mem, RGB(150, 160, 175));
        tr.top += 60;
        DrawTextA(mem, "Right-click a pixel\nin the phase space", -1, &tr,
                  DT_CENTER | DT_TOP | DT_WORDBREAK);
        BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldbmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(v->hwnd, &ps);
        return;
    }

    {
        int info_h = 104;
        int area_h = h - info_h - 40;
        double l1 = v->params.l1 > 0.0 ? v->params.l1 : 1.0;
        double l2 = v->params.l2 > 0.0 ? v->params.l2 : 1.0;
        double avail = (w - 40 < area_h - 20) ? (w - 40) : (area_h - 20);
        double scale = 0.78 * avail / (l1 + l2);
        double ox = w * 0.5;
        double oy = 58.0;
        double p1x = ox + l1 * scale * sin((double)v->state[0]);
        double p1y = oy + l1 * scale * cos((double)v->state[0]);
        double p2x = p1x + l2 * scale * sin((double)v->state[1]);
        double p2y = p1y + l2 * scale * cos((double)v->state[1]);
        char line[160];

        pen1 = CreatePen(PS_SOLID, 3, RGB(225, 80, 80));
        pen2 = CreatePen(PS_SOLID, 3, RGB(80, 150, 240));
        pen3 = CreatePen(PS_SOLID, 1, RGB(120, 130, 145));
        br1 = CreateSolidBrush(RGB(225, 80, 80));
        br2 = CreateSolidBrush(RGB(80, 150, 240));
        brp = CreateSolidBrush(RGB(70, 78, 90));

        SelectObject(mem, pen3);
        SelectObject(mem, brp);
        Ellipse(mem, (int)ox - 4, (int)oy - 4, (int)ox + 4, (int)oy + 4);

        oldpen = (HPEN)SelectObject(mem, pen1);
        MoveToEx(mem, (int)ox, (int)oy, NULL);
        LineTo(mem, (int)p1x, (int)p1y);
        SelectObject(mem, pen1);
        oldbrush = (HBRUSH)SelectObject(mem, br1);
        Ellipse(mem, (int)p1x - 8, (int)p1y - 8, (int)p1x + 8, (int)p1y + 8);

        SelectObject(mem, pen2);
        MoveToEx(mem, (int)p1x, (int)p1y, NULL);
        LineTo(mem, (int)p2x, (int)p2y);
        SelectObject(mem, br2);
        Ellipse(mem, (int)p2x - 8, (int)p2y - 8, (int)p2x + 8, (int)p2y + 8);

        SelectObject(mem, oldpen);
        SelectObject(mem, oldbrush);
        DeleteObject(pen1); DeleteObject(pen2); DeleteObject(pen3);
        DeleteObject(br1); DeleteObject(br2); DeleteObject(brp);

        SelectObject(mem, v->font_small);
        SetTextColor(mem, RGB(155, 165, 180));
        snprintf(line, sizeof(line), "cell (%d, %d)", v->cell_x, v->cell_y);
        TextOutA(mem, 14, h - info_h, line, (int)strlen(line));

        SelectObject(mem, v->font_body);
        SetTextColor(mem, RGB(220, 225, 235));
        snprintf(line, sizeof(line), "theta1 %+0.3f   theta2 %+0.3f",
                 v->state[0], v->state[1]);
        TextOutA(mem, 14, h - info_h + 22, line, (int)strlen(line));
        snprintf(line, sizeof(line), "omega1 %+0.3f   omega2 %+0.3f",
                 v->state[2], v->state[3]);
        TextOutA(mem, 14, h - info_h + 44, line, (int)strlen(line));
        snprintf(line, sizeof(line), "damping %0.3f", v->params.damping);
        TextOutA(mem, 14, h - info_h + 66, line, (int)strlen(line));

        }

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldbmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(v->hwnd, &ps);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PendulumView *v = (PendulumView *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
        v = (PendulumView *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)v);
        if (v) v->hwnd = hwnd;
        return TRUE;
    }
    case WM_PAINT:
        if (v) paint(v);
        else {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (v) {
            v->width = LOWORD(lp);
            v->height = HIWORD(lp);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (v && v->keyfn && (lp & (1L << 30)) == 0)
            v->keyfn(v->user, (int)wp, 1);
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (v && v->keyfn)
            v->keyfn(v->user, (int)wp, 0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (v) {
            if (v->closefn) v->closefn(v->user);
            v->hwnd = NULL;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

PendulumView *pendulum_view_create(HINSTANCE inst, const wchar_t *title,
                                   int x, int y, int w, int h,
                                   void *user, PendulumCloseFn closefn, PendulumKeyFn keyfn)
{
    PendulumView *v;
    WNDCLASSEXW wc;
    DWORD style;
    RECT rc;

    v = (PendulumView *)calloc(1, sizeof(PendulumView));
    if (!v) return NULL;
    v->inst = inst;
    v->user = user;
    v->closefn = closefn;
    v->keyfn = keyfn;
    v->width = w;
    v->height = h;

    if (!g_class_registered) {
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = kClass;
        wc.hbrBackground = NULL;
        RegisterClassExW(&wc);
        g_class_registered = 1;
    }

    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    v->hwnd = CreateWindowExW(0, kClass, title ? title : L"Double Pendulum", style,
                              x, y, rc.right - rc.left, rc.bottom - rc.top,
                              NULL, NULL, inst, v);
    if (!v->hwnd) { free(v); return NULL; }
    SetWindowTextW(v->hwnd, title ? title : L"Double Pendulum");

    v->font_title = CreateFontA(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    v->font_body = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
    v->font_small = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    ShowWindow(v->hwnd, SW_SHOW);
    InvalidateRect(v->hwnd, NULL, TRUE);
    UpdateWindow(v->hwnd);
    return v;
}

void pendulum_view_destroy(PendulumView *v)
{
    if (!v) return;
    if (v->hwnd) DestroyWindow(v->hwnd);
    if (v->font_title) DeleteObject(v->font_title);
    if (v->font_body) DeleteObject(v->font_body);
    if (v->font_small) DeleteObject(v->font_small);
    free(v);
}

void pendulum_view_set_state(PendulumView *v, const float state[4], int has,
                             int cell_x, int cell_y, const DpParams *params)
{
    if (!v) return;
    v->has = has && state != NULL;
    if (state) memcpy(v->state, state, sizeof(v->state));
    v->cell_x = cell_x;
    v->cell_y = cell_y;
    if (params) v->params = *params;
    if (v->hwnd) InvalidateRect(v->hwnd, NULL, FALSE);
}