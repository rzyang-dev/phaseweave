/* platform.h - Win32 窗口、计时、控制台与资源读取 */
#ifndef DP_PLATFORM_H
#define DP_PLATFORM_H

#include <windows.h>

typedef struct Platform Platform;
typedef void (*PlatformKeyFn)(void *user, int vk, int down);

Platform *platform_create(HINSTANCE inst, const wchar_t *title, int client_side,
                          int hidden, void *user, PlatformKeyFn keyfn);
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
