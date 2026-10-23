/* phase_view.h - 相空间窗口的视图变换、拾取与叠加线条
 *
 * 世界坐标：theta1 沿 x，theta2 沿 y，范围 [-range/2, range/2]，y 向下。
 * zoom=1 时整幅相空间铺满正方形客户区；窗口放大时显示范围 = range/zoom。
 * 叠加线条全部在 NDC 里给出，由 gpu.c 用线着色器（或兼容管线）画。
 */
#ifndef DP_PHASE_VIEW_H
#define DP_PHASE_VIEW_H

#include "core.h"

typedef struct {
    double cx, cy;      /* 视图中心（世界坐标） */
    double zoom;        /* 1 = 全范围 */
    int grid;           /* 灰色格线 */
    int periodic;       /* 循环边界视图 */
    int circle;         /* 选中像素圆圈 */
} PhaseView;

void   pv_init(PhaseView *v);
void   pv_reset(PhaseView *v);
double pv_span(const PhaseView *v, double range);

void   pv_screen_to_world(const PhaseView *v, double range, int w, int h,
                          int sx, int sy, double *wx, double *wy);
int    pv_pick(const PhaseView *v, double range, int res, int w, int h,
               int sx, int sy, int *cell_x, int *cell_y);
void   pv_pan(PhaseView *v, double range, int w, int h, int dx, int dy);
void   pv_zoom_at(PhaseView *v, double range, int w, int h, int sx, int sy, double factor);

/* 返回写入的顶点数；每个顶点 6 个 float：x, y, r, g, b, a（NDC + 0..1 颜色）。
 * grid_verts 返回格线部分的顶点数，圆圈从该下标开始（调用方可分别设置线宽）。 */
int    pv_build_overlay(const PhaseView *v, double range, int res, int w, int h,
                        int sel_x, int sel_y, float *verts, int max_verts,
                        int *grid_verts);
int    pv_build_axis_ticks(const PhaseView *v, double range, int w, int h,
                           double xmin, double xmax, double ymin, double ymax,
                           const char *xname, const char *yname,
                           float *verts, int max_verts);

#endif