/* pendulum_view.h - 独立的双摆窗口（GDI，双缓冲） */
#ifndef DP_PENDULUM_VIEW_H
#define DP_PENDULUM_VIEW_H

#include "core.h"
#include <windows.h>

typedef struct PendulumView PendulumView;
typedef void (*PendulumCloseFn)(void *user);
typedef void (*PendulumKeyFn)(void *user, int vk, int down);

PendulumView *pendulum_view_create(HINSTANCE inst, const wchar_t *title,
                                   int x, int y, int w, int h,
                                   void *user, PendulumCloseFn closefn, PendulumKeyFn keyfn);
void pendulum_view_destroy(PendulumView *v);
void pendulum_view_set_state(PendulumView *v, const float state[4], int has,
                             int cell_x, int cell_y, const DpParams *params);

#endif
