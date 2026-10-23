/* platform.h - Win32 窗口、计时、控制台与资源读取 */
#ifndef DP_PLATFORM_H
#define DP_PLATFORM_H

#include <windows.h>

typedef struct Platform Platform;

/* 输入事件：KEY(a=vk,b=down)、MOUSE_DOWN/MOUSE_UP(a=0 左/1 右,b=x,c=y)、
 * MOUSE_MOVE(a=x,b=y)、WHEEL(a=delta,b=x,c=y)。 */
enum {
    DP_EV_KEY = 1,
    DP_EV_MOUSE_DOWN,
    DP_EV_MOUSE_UP,
    DP_EV_MOUSE_MOVE,
    DP_EV_WHEEL
};
typedef struct {
    int type;
    int a, b, c;
} DpEvent;
typedef void (*PlatformEventFn)(void *user, const DpEvent *ev);

Platform *platform_create(HINSTANCE inst, const wchar_t *title, int client_side,
                          int hidden, void *user, PlatformEventFn eventfn);
void      platform_move_client(Platform *p, int x, int y);
void      platform_destroy(Platform *p);

void      platform_pump(Platform *p);
int       platform_quit(const Platform *p);
double    platform_now(void);
double    platform_dt(Platform *p);
void      platform_frame_end(Platform *p);
void      platform_set_title(Platform *p, const char *utf8);
void      platform_set_vsync(Platform *p, int on);

HWND      platform_hwnd(Platform *p);
HDC       platform_hdc(Platform *p);
int       platform_width(const Platform *p);
int       platform_height(const Platform *p);

void      platform_console_attach(void);
int       platform_read_resource(int id, const void **data, size_t *size);
int       platform_read_file(const char *path, char **data, size_t *size);
void      platform_free(void *ptr);
void      platform_message_box(const char *utf8, int error);
void      platform_exe_dir(char *buf, size_t size);

#endif