/* phase_view.c - 纯数学，不依赖 Windows / OpenGL */
#include "phase_view.h"

#include <math.h>
#include <string.h>

void pv_init(PhaseView *v)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->zoom = 1.0;
    v->grid = 1;
    v->periodic = 0;
    v->circle = 1;
}

void pv_reset(PhaseView *v)
{
    if (!v) return;
    v->cx = 0.0;
    v->cy = 0.0;
    v->zoom = 1.0;
}

double pv_span(const PhaseView *v, double range)
{
    double z = (v && v->zoom > 0.0) ? v->zoom : 1.0;
    return range / z;
}

void pv_screen_to_world(const PhaseView *v, double range, int w, int h,
                        int sx, int sy, double *wx, double *wy)
{
    double half = pv_span(v, range) * 0.5;
    double fx = ((double)sx + 0.5) / (double)w - 0.5;
    double fy = ((double)sy + 0.5) / (double)h - 0.5;
    if (wx) *wx = v->cx + fx * 2.0 * half;
    if (wy) *wy = v->cy + fy * 2.0 * half;
}

static double wrap_range(double x, double lo, double range)
{
    x = lo + fmod(x - lo, range);
    if (x < lo) x += range;
    return x;
}

int pv_pick(const PhaseView *v, double range, int res, int w, int h,
            int sx, int sy, int *cell_x, int *cell_y)
{
    double wx, wy, rmin = -range * 0.5, cell;
    int cx, cy;
    if (!v || !res || w <= 0 || h <= 0) return 0;
    pv_screen_to_world(v, range, w, h, sx, sy, &wx, &wy);
    if (v->periodic) {
        wx = wrap_range(wx, rmin, range);
        wy = wrap_range(wy, rmin, range);
    } else if (wx < rmin || wx >= rmin + range || wy < rmin || wy >= rmin + range) {
        return 0;
    }
    cell = range / (double)res;
    cx = (int)floor((wx - rmin) / cell);
    cy = (int)floor((wy - rmin) / cell);
    if (cx < 0) cx = 0;
    if (cx >= res) cx = res - 1;
    if (cy < 0) cy = 0;
    if (cy >= res) cy = res - 1;
    if (cell_x) *cell_x = cx;
    if (cell_y) *cell_y = cy;
    return 1;
}

void pv_pan(PhaseView *v, double range, int w, int h, int dx, int dy)
{
    double half;
    if (!v || w <= 0 || h <= 0) return;
    half = pv_span(v, range) * 0.5;
    v->cx -= (double)dx * 2.0 * half / (double)w;
    v->cy -= (double)dy * 2.0 * half / (double)h;
    if (!v->periodic) {
        /* 不让视图彻底跑出相空间 */
        if (v->cx < -range) v->cx = -range;
        if (v->cx >  range) v->cx =  range;
        if (v->cy < -range) v->cy = -range;
        if (v->cy >  range) v->cy =  range;
    }
}

void pv_zoom_at(PhaseView *v, double range, int w, int h, int sx, int sy, double factor)
{
    double old_half, new_zoom, new_half, fx, fy, wx, wy;
    if (!v || w <= 0 || h <= 0 || factor <= 0.0) return;
    old_half = pv_span(v, range) * 0.5;
    fx = ((double)sx + 0.5) / (double)w - 0.5;
    fy = ((double)sy + 0.5) / (double)h - 0.5;
    wx = v->cx + fx * 2.0 * old_half;
    wy = v->cy + fy * 2.0 * old_half;
    new_zoom = v->zoom * factor;
    if (new_zoom < 0.25) new_zoom = 0.25;
    if (new_zoom > 64.0) new_zoom = 64.0;
    new_half = range / (2.0 * new_zoom);
    v->cx = wx - fx * 2.0 * new_half;
    v->cy = wy - fy * 2.0 * new_half;
    v->zoom = new_zoom;
}

/* ---------------------------------------------------------------- 叠加 */

typedef struct {
    float *v;
    int n;
    int max;
} Verts;

static void vtx(Verts *o, float x, float y, const float c[4])
{
    if (o->n >= o->max) return;
    o->v[o->n * 6 + 0] = x;
    o->v[o->n * 6 + 1] = y;
    o->v[o->n * 6 + 2] = c[0];
    o->v[o->n * 6 + 3] = c[1];
    o->v[o->n * 6 + 4] = c[2];
    o->v[o->n * 6 + 5] = c[3];
    o->n++;
}

static void line(Verts *o, float x0, float y0, float x1, float y1, const float c[4])
{
    vtx(o, x0, y0, c);
    vtx(o, x1, y1, c);
}

static void ndc_of(const PhaseView *v, double range, int w, int h,
                   double wx, double wy, float *x, float *y)
{
    double half = pv_span(v, range) * 0.5;
    *x = (float)((wx - v->cx) / half);
    *y = (float)(-(wy - v->cy) / half);
    (void)w; (void)h;
}

int pv_build_overlay(const PhaseView *v, double range, int res, int w, int h,
                     int sel_x, int sel_y, float *verts, int max_verts,
                     int *grid_verts)
{
    Verts o;
    const float minor[4]  = { 0.62f, 0.65f, 0.70f, 0.22f };
    const float major[4]  = { 0.78f, 0.81f, 0.86f, 0.55f };
    const float border[4] = { 0.90f, 0.93f, 0.97f, 0.85f };
    const float ring0[4]  = { 0.00f, 0.00f, 0.00f, 0.65f };
    const float ring1[4]  = { 1.00f, 0.78f, 0.15f, 0.98f };
    double half, rmin = -range * 0.5, rmax = range * 0.5;
    double xmin, xmax, ymin, ymax, cell, cell_px;
    int step, k, k0, k1;
    int grid_n;

    if (!v || !verts || max_verts <= 0 || w <= 0 || h <= 0 || res <= 0) {
        if (grid_verts) *grid_verts = 0;
        return 0;
    }
    o.v = verts;
    o.n = 0;
    o.max = max_verts;

    half = pv_span(v, range) * 0.5;
    cell = range / (double)res;
    cell_px = (double)w / ((double)res * v->zoom);

    xmin = v->cx - half;
    xmax = v->cx + half;
    ymin = v->cy - half;
    ymax = v->cy + half;
    if (!v->periodic) {
        if (xmin < rmin) xmin = rmin;
        if (xmax > rmax) xmax = rmax;
        if (ymin < rmin) ymin = rmin;
        if (ymax > rmax) ymax = rmax;
    }

    step = 1;
    while (cell_px * (double)step < 8.0 && step < res) step *= 2;

    if (v->grid && xmax > xmin && ymax > ymin) {
        float x0f, y0f, x1f, y1f;

        k0 = (int)floor((xmin - rmin) / cell);
        k1 = (int)ceil((xmax - rmin) / cell);
        for (k = k0; k <= k1; ++k) {
            double x;
            if (step > 1 && (k % step) != 0) continue;
            x = rmin + (double)k * cell;
            if (x < xmin - 1e-9 || x > xmax + 1e-9) continue;
            ndc_of(v, range, w, h, x, ymin, &x0f, &y0f);
            ndc_of(v, range, w, h, x, ymax, &x1f, &y1f);
            line(&o, x0f, y0f, x1f, y1f, minor);
        }
        k0 = (int)floor((ymin - rmin) / cell);
        k1 = (int)ceil((ymax - rmin) / cell);
        for (k = k0; k <= k1; ++k) {
            double y;
            if (step > 1 && (k % step) != 0) continue;
            y = rmin + (double)k * cell;
            if (y < ymin - 1e-9 || y > ymax + 1e-9) continue;
            ndc_of(v, range, w, h, xmin, y, &x0f, &y0f);
            ndc_of(v, range, w, h, xmax, y, &x1f, &y1f);
            line(&o, x0f, y0f, x1f, y1f, minor);
        }

        /* 2*pi 周期边界用更亮的线 */
        k0 = (int)floor(xmin / DP_TWO_PI);
        k1 = (int)ceil(xmax / DP_TWO_PI);
        for (k = k0; k <= k1; ++k) {
            double x = (double)k * DP_TWO_PI;
            if (x < xmin - 1e-9 || x > xmax + 1e-9) continue;
            ndc_of(v, range, w, h, x, ymin, &x0f, &y0f);
            ndc_of(v, range, w, h, x, ymax, &x1f, &y1f);
            line(&o, x0f, y0f, x1f, y1f, major);
        }
        k0 = (int)floor(ymin / DP_TWO_PI);
        k1 = (int)ceil(ymax / DP_TWO_PI);
        for (k = k0; k <= k1; ++k) {
            double y = (double)k * DP_TWO_PI;
            if (y < ymin - 1e-9 || y > ymax + 1e-9) continue;
            ndc_of(v, range, w, h, xmin, y, &x0f, &y0f);
            ndc_of(v, range, w, h, xmax, y, &x1f, &y1f);
            line(&o, x0f, y0f, x1f, y1f, major);
        }

        if (!v->periodic) {
            float ax, ay, bx, by;
            ndc_of(v, range, w, h, rmin, rmin, &ax, &ay);
            ndc_of(v, range, w, h, rmax, rmin, &bx, &by);
            line(&o, ax, ay, bx, by, border);
            ndc_of(v, range, w, h, rmin, rmax, &ax, &ay);
            ndc_of(v, range, w, h, rmax, rmax, &bx, &by);
            line(&o, ax, ay, bx, by, border);
            ndc_of(v, range, w, h, rmin, rmin, &ax, &ay);
            ndc_of(v, range, w, h, rmin, rmax, &bx, &by);
            line(&o, ax, ay, bx, by, border);
            ndc_of(v, range, w, h, rmax, rmin, &ax, &ay);
            ndc_of(v, range, w, h, rmax, rmax, &bx, &by);
            line(&o, ax, ay, bx, by, border);
        }
    }
    grid_n = o.n;

    if (v->circle && sel_x >= 0 && sel_y >= 0 && sel_x < res && sel_y < res) {
        double bx = rmin + ((double)sel_x + 0.5) * cell;
        double by = rmin + ((double)sel_y + 0.5) * cell;
        double radius = cell_px * 0.55;
        double rx, ry;
        int kx0 = 0, kx1 = 0, ky0 = 0, ky1 = 0, kx, ky, seg;
        if (radius < 6.0) radius = 6.0;
        rx = radius * 2.0 / (double)w;
        ry = radius * 2.0 / (double)h;
        if (v->periodic) {
            kx0 = (int)floor((v->cx - half - bx) / range) - 1;
            kx1 = (int)ceil((v->cx + half - bx) / range) + 1;
            ky0 = (int)floor((v->cy - half - by) / range) - 1;
            ky1 = (int)ceil((v->cy + half - by) / range) + 1;
        }
        for (kx = kx0; kx <= kx1; ++kx) {
            for (ky = ky0; ky <= ky1; ++ky) {
                double wx = bx + (double)kx * range;
                double wy = by + (double)ky * range;
                float cx, cy;
                if (wx < v->cx - half - radius || wx > v->cx + half + radius) continue;
                if (wy < v->cy - half - radius || wy > v->cy + half + radius) continue;
                ndc_of(v, range, w, h, wx, wy, &cx, &cy);
                for (seg = 0; seg < 48; ++seg) {
                    double a0 = (double)seg * DP_TWO_PI / 48.0;
                    double a1 = (double)(seg + 1) * DP_TWO_PI / 48.0;
                    line(&o,
                         cx + (float)(cos(a0) * (rx + 1.5 * 2.0 / w)),
                         cy + (float)(sin(a0) * (ry + 1.5 * 2.0 / h)),
                         cx + (float)(cos(a1) * (rx + 1.5 * 2.0 / w)),
                         cy + (float)(sin(a1) * (ry + 1.5 * 2.0 / h)), ring0);
                }
                for (seg = 0; seg < 48; ++seg) {
                    double a0 = (double)seg * DP_TWO_PI / 48.0;
                    double a1 = (double)(seg + 1) * DP_TWO_PI / 48.0;
                    line(&o,
                         cx + (float)(cos(a0) * rx), cy + (float)(sin(a0) * ry),
                         cx + (float)(cos(a1) * rx), cy + (float)(sin(a1) * ry), ring1);
                }
            }
        }
    }

    if (grid_verts) *grid_verts = grid_n;
    return o.n;
}