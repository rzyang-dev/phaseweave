/* panel.h - Tcl/Tk 控制面板（zip 内动态链接） */
#ifndef DP_PANEL_H
#define DP_PANEL_H

#include <stddef.h>

typedef struct {
    int paused;
    int kernel_gpu;
    int res;
    int fps;
    int grid, periodic, circle;
    int selected;
    int sel_x, sel_y;
    double zoom;
    char scheme[32];
    char source[260];
    char last[256];
} PanelStatus;

typedef struct {
    void *user;
    int  (*action)(void *user, const char *name, const char *value,
                   char *msg, size_t msgsz);
    void (*status)(void *user, PanelStatus *st);
    void (*query)(void *user, const char *what, char *out, size_t outsz);
    void (*quit)(void *user);
} PanelCallbacks;

typedef struct Panel Panel;

Panel *panel_create(const PanelCallbacks *cb, const char *script_path,
                    int x, int y, int w, int h, char *err, size_t errsz);
void   panel_pump(Panel *p);
void   panel_destroy(Panel *p);
int    panel_eval(Panel *p, const char *script);

#endif