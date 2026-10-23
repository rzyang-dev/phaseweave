/* atlas.c - CPU model-agnostic sampler; this is the reference path for the
 * 2D map.  The GPU display currently consumes the RGBA image produced here;
 *  a compute-shader backend can be added behind the same channels contract. */
#include "atlas.h"

#include "physics.h"
#include "pool.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Atlas {
    int res;
    int cells;
    float *cells_data;  /* per cell: state[4] + model values[12] */
    float *channels;    /* per cell: model channels */
    float *analysis;    /* per cell: FTLE / analysis scalar */
    int analysis_valid;
    int analysis_mode; /* 1 FTLE, 2 escape */
    double post_min, post_max;
    unsigned char *rgba;
    ThreadPool *pool;
};

typedef struct {
    Atlas *a;
    const Experiment *e;
    const Model *m;
    int steps;
} AtlasJob;

static void channel_at(Atlas *a, int ch, int x, int y, double *v)
{
    int cx = x < 0 ? 0 : (x >= a->res ? a->res - 1 : x);
    int cy = y < 0 ? 0 : (y >= a->res ? a->res - 1 : y);
    *v = a->channels[((size_t)cy * a->res + cx) * EXP_NCH + ch];
}

static double wrap_diff(double a, double b)
{
    double d = a - b;
    dp_normalize_angle(&d);
    return d;
}

Atlas *atlas_create(const Experiment *e, int res, char *err, size_t errsz)
{
    Atlas *a = (Atlas *)calloc(1, sizeof(Atlas));
    if (err && errsz) err[0] = 0;
    if (!a) {
        if (err) snprintf(err, errsz, "atlas: out of memory");
        return NULL;
    }
    if (res < 16) res = 16;
    if (res > 1024) res = 1024;
    a->res = res;
    a->cells = res * res;
    a->analysis_valid = 0;
    a->cells_data = (float *)calloc((size_t)a->cells * (4u + MODEL_MAX_PARAMS), sizeof(float));
    a->channels = (float *)calloc((size_t)a->cells * EXP_NCH, sizeof(float));
    a->analysis = (float *)calloc((size_t)a->cells, sizeof(float));
    a->rgba = (unsigned char *)calloc((size_t)a->cells * 4u, 1);
    if (!a->cells_data || !a->channels || !a->analysis || !a->rgba) {
        atlas_destroy(a);
        if (err) snprintf(err, errsz, "atlas: out of memory (%dx%d)", res, res);
        return NULL;
    }
    a->pool = pool_create(0);
    a->analysis_valid = 0;
    atlas_reset(a, e);
    return a;
}

void atlas_destroy(Atlas *a)
{
    if (!a) return;
    if (a->pool) pool_destroy(a->pool);
    free(a->cells_data);
    free(a->channels);
    free(a->analysis);
    free(a->rgba);
    free(a);
}

void atlas_resize(Atlas *a, int res, char *err, size_t errsz)
{
    if (!a) return;
    if (res < 16) res = 16;
    if (res > 1024) res = 1024;
    if (res == a->res) return;
    free(a->cells_data); free(a->channels); free(a->analysis); free(a->rgba);
    a->res = res;
    a->cells = res * res;
    a->cells_data = (float *)calloc((size_t)a->cells * (4u + MODEL_MAX_PARAMS), sizeof(float));
    a->channels = (float *)calloc((size_t)a->cells * EXP_NCH, sizeof(float));
    a->analysis = (float *)calloc((size_t)a->cells, sizeof(float));
    a->rgba = (unsigned char *)calloc((size_t)a->cells * 4u, 1);
    if (!a->cells_data || !a->channels || !a->analysis || !a->rgba) {
        if (err) snprintf(err, errsz, "atlas: out of memory (%dx%d)", res, res);
    }
}

void atlas_reset(Atlas *a, const Experiment *e)
{
    int x, y, i;
    if (!a || !e) return;
    for (y = 0; y < a->res; ++y) {
        for (x = 0; x < a->res; ++x) {
            double state[MODEL_MAX_STATE], vals[MODEL_MAX_PARAMS];
            float *c;
            i = y * a->res + x;
            exp_cell_state(e, x, y, state, vals);
            c = a->cells_data + (size_t)i * (4u + MODEL_MAX_PARAMS);
            {
                int k;
                for (k = 0; k < MODEL_MAX_STATE; ++k) c[k] = (float)state[k];
                for (k = 0; k < MODEL_MAX_PARAMS; ++k) c[MODEL_MAX_STATE + k] = (float)vals[k];
            }
        }
    }
    a->analysis_valid = 0;
    a->analysis_mode = 0;
    atlas_step(a, e, 0);
    if (e->post == 4) atlas_compute_ftle(a, e);
    else if (e->post == 5) atlas_compute_escape(a, e);
}

static void atlas_band(void *user, int y0, int y1)
{
    AtlasJob *job = (AtlasJob *)user;
    Atlas *a = job->a;
    const Experiment *e = job->e;
    const Model *m = job->m;
    int x, y, k;
    for (y = y0; y < y1; ++y) {
        for (x = 0; x < a->res; ++x) {
            int i = y * a->res + x;
            float *c = a->cells_data + (size_t)i * (4u + MODEL_MAX_PARAMS);
            double state[MODEL_MAX_STATE];
            double vals[MODEL_MAX_PARAMS];
            float ch[MODEL_MAX_CHANNELS];
            for (k = 0; k < MODEL_MAX_STATE; ++k) state[k] = c[k];
            for (k = 0; k < MODEL_MAX_PARAMS; ++k) vals[k] = c[MODEL_MAX_STATE + k];
            m->step(m, state, vals, e->dt, job->steps);
            for (k = 0; k < MODEL_MAX_STATE; ++k) c[k] = (float)state[k];
            m->observe(m, state, vals, ch);
            for (k = 0; k < EXP_NCH; ++k)
                a->channels[(size_t)i * EXP_NCH + k] = (k < m->nchannels) ? ch[k] : 0.0f;
        }
    }
}

void atlas_step(Atlas *a, const Experiment *e, int steps)
{
    AtlasJob job;
    if (!a || !e || !e->mdl) return;
    if (steps < 0) steps = 0;
    job.a = a; job.e = e; job.m = e->mdl; job.steps = steps;
    if (a->pool) pool_run(a->pool, atlas_band, &job, a->res);
    else atlas_band(&job, 0, a->res);
}

static double post_value(Atlas *a, int ch, int cyclic, int x, int y, int post)
{
    if (post == 4 || post == 5) {
        int cx = x < 0 ? 0 : (x >= a->res ? a->res - 1 : x);
        int cy = y < 0 ? 0 : (y >= a->res ? a->res - 1 : y);
        return a->analysis[(size_t)cy * a->res + cx];
    }
    if (post == 1) {
        double tl, tr, bl, br, tc, bc, ml, mr, gx, gy;
        channel_at(a, ch, x - 1, y - 1, &tl);
        channel_at(a, ch, x + 1, y - 1, &tr);
        channel_at(a, ch, x - 1, y + 1, &bl);
        channel_at(a, ch, x + 1, y + 1, &br);
        channel_at(a, ch, x,     y - 1, &tc);
        channel_at(a, ch, x,     y + 1, &bc);
        channel_at(a, ch, x - 1, y,     &ml);
        channel_at(a, ch, x + 1, y,     &mr);
        if (cyclic) {
            gx = (wrap_diff(tr, tl) + 2.0 * wrap_diff(mr, ml) + wrap_diff(br, bl)) / 8.0;
            gy = (wrap_diff(bl, tl) + 2.0 * wrap_diff(bc, tc) + wrap_diff(br, tr)) / 8.0;
        } else {
            gx = (tr - tl + 2.0 * (mr - ml) + br - bl) / 8.0;
            gy = (bl - tl + 2.0 * (bc - tc) + br - tr) / 8.0;
        }
        return sqrt(gx * gx + gy * gy);
    }
    if (post == 2) {
        double mn = 1e300, mx = -1e300;
        int dx, dy;
        for (dy = -1; dy <= 1; ++dy) {
            for (dx = -1; dx <= 1; ++dx) {
                double q;
                channel_at(a, ch, x + dx, y + dy, &q);
                if (q < mn) mn = q;
                if (q > mx) mx = q;
            }
        }
        return mx - mn;
    }
    {
        double sum = 0.0, sum2 = 0.0;
        int dx, dy, n = 0;
        for (dy = -1; dy <= 1; ++dy) {
            for (dx = -1; dx <= 1; ++dx) {
                double q;
                channel_at(a, ch, x + dx, y + dy, &q);
                sum += q; sum2 += q * q; ++n;
            }
        }
        return sqrt(fabs(sum2 / n - (sum / n) * (sum / n)));
    }
}

void atlas_colorize(Atlas *a, const Experiment *e, const unsigned char *lut)
{
    ExpChannelInfo ci;
    int x, y, ch;
    double low, high, span;
    double pmin = 1e300, pmax = -1e300;
    if (!a || !e || !lut) return;
    ch = e->channel;
    if (ch < 0 || ch >= EXP_NCH) ch = 0;
    exp_channel_info(e, ch, &ci);
    low = ci.min; high = ci.max;
    span = high - low;
    if (fabs(span) < 1e-9) span = 1.0;
    if (e->post == 4 && (!a->analysis_valid || a->analysis_mode != 1)) atlas_compute_ftle(a, e);
    if (e->post == 5 && (!a->analysis_valid || a->analysis_mode != 2)) atlas_compute_escape(a, e);

    if (e->post != 0) {
        for (y = 0; y < a->res; ++y) {
            for (x = 0; x < a->res; ++x) {
                double v = post_value(a, ch, ci.cyclic, x, y, e->post);
                if (v == v) {
                    if (v < pmin) pmin = v;
                    if (v > pmax) pmax = v;
                }
            }
        }
        if (!(pmin < pmax)) { pmin = 0.0; pmax = 1.0; }
        a->post_min = pmin; a->post_max = pmax;
    }

    for (y = 0; y < a->res; ++y) {
        for (x = 0; x < a->res; ++x) {
            int i = y * a->res + x;
            double u;
            if (e->post == 0) {
                double v = a->channels[(size_t)i * EXP_NCH + ch];
                u = (v - low) / span;
                if (ci.cyclic) u = u - floor(u);
            } else {
                double v = post_value(a, ch, ci.cyclic, x, y, e->post);
                u = (v - pmin) / (pmax - pmin);
            }
            if (!(u == u)) u = 0.0;
            if (ci.cyclic) u = u - floor(u);
            else if (u < 0.0) u = 0.0;
            else if (u > 1.0) u = 1.0;
            {
                int idx = (int)(u * (double)(DP_LUT_N - 1) + 0.5);
                if (idx < 0) idx = 0;
                if (idx >= DP_LUT_N) idx = DP_LUT_N - 1;
                a->rgba[(size_t)i * 4u + 0] = lut[idx * 3 + 0];
                a->rgba[(size_t)i * 4u + 1] = lut[idx * 3 + 1];
                a->rgba[(size_t)i * 4u + 2] = lut[idx * 3 + 2];
                a->rgba[(size_t)i * 4u + 3] = 255;
            }
        }
    }
}

int atlas_res(const Atlas *a) { return a ? a->res : 0; }
int atlas_cells(const Atlas *a) { return a ? a->cells : 0; }
int atlas_threads(const Atlas *a) { return a && a->pool ? pool_threads(a->pool) : 0; }
const unsigned char *atlas_rgba(const Atlas *a) { return a ? a->rgba : NULL; }
float *atlas_cell_data(Atlas *a) { return a ? a->cells_data : NULL; }
float *atlas_channel_data(Atlas *a) { return a ? a->channels : NULL; }
int atlas_cell_floats(void) { return 4 + MODEL_MAX_PARAMS; }

void atlas_read_cell(const Atlas *a, int index, float state[MODEL_MAX_STATE], float vals[MODEL_MAX_PARAMS], float ch[MODEL_MAX_CHANNELS])
{
    const float *c;
    if (!a || !state || !vals || !ch || index < 0 || index >= a->cells) return;
    c = a->cells_data + (size_t)index * (4u + MODEL_MAX_PARAMS);
    memcpy(state, c, MODEL_MAX_STATE * sizeof(float));
    memcpy(vals, c + MODEL_MAX_STATE, MODEL_MAX_PARAMS * sizeof(float));
    memcpy(ch, a->channels + (size_t)index * EXP_NCH, 4u * sizeof(float));
}
const float *atlas_analysis_data(Atlas *a) { return a ? a->analysis : NULL; }

void atlas_post_range(Atlas *a, double *lo, double *hi)
{
    if (!a) return;
    if (lo) *lo = a->post_min;
    if (hi) *hi = a->post_max;
}

int atlas_compute_ftle(Atlas *a, const Experiment *e)
{
    const Model *m;
    int x, y, steps, dim, k;
    double dt;
    if (!a || !e || !e->mdl || !a->analysis) return 0;
    a->analysis_valid = 1;
    a->analysis_mode = 1;
    m = e->mdl;
    steps = e->max_steps > 0 ? e->max_steps : 1;
    dim = m->state_dim > 0 ? m->state_dim : 1;
    if (dim > MODEL_MAX_STATE) dim = MODEL_MAX_STATE;
    dt = e->dt;
    for (y = 0; y < a->res; ++y) {
        for (x = 0; x < a->res; ++x) {
            float *c = a->cells_data + (size_t)(y * a->res + x) * (4u + MODEL_MAX_PARAMS);
            double s0[MODEL_MAX_STATE], p0[MODEL_MAX_STATE];
            double vals[MODEL_MAX_PARAMS];
            double eps, dist = 0.0, v;
            for (k = 0; k < MODEL_MAX_STATE; ++k) s0[k] = c[k];
            for (k = 0; k < MODEL_MAX_PARAMS; ++k) vals[k] = c[MODEL_MAX_STATE + k];
            eps = 1e-6 * (1.0 + fabs(s0[0]));
            for (k = 0; k < MODEL_MAX_STATE; ++k) p0[k] = s0[k];
            p0[0] += eps;
            m->step(m, s0, vals, dt, steps);
            m->step(m, p0, vals, dt, steps);
            for (k = 0; k < dim; ++k) {
                double d = p0[k] - s0[k];
                if (strcmp(m->id, "double_pendulum") == 0 && k < 2)
                    d = wrap_diff(p0[k], s0[k]);
                dist += d * d;
            }
            dist = sqrt(dist);
            if (dist > 0.0 && dist == dist)
                v = log(dist / eps) / (double)steps;
            else
                v = -20.0;
            if (v != v) v = -20.0;
            if (v < -20.0) v = -20.0;
            if (v > 20.0) v = 20.0;
            a->analysis[(size_t)(y * a->res + x)] = (float)v;
        }
    }
    return 1;
}
int atlas_compute_escape(Atlas *a, const Experiment *e)
{
    const Model *m;
    int x, y, steps, dim, k;
    double radius;
    if (!a || !e || !e->mdl || !a->analysis) return 0;
    a->analysis_valid = 1;
    a->analysis_mode = 2;
    radius = e->escape_radius > 0.0 ? e->escape_radius : 10.0;
    m = e->mdl;
    steps = e->max_steps > 0 ? e->max_steps : 1;
    dim = m->state_dim > 0 ? m->state_dim : 1;
    if (dim > MODEL_MAX_STATE) dim = MODEL_MAX_STATE;
    for (y = 0; y < a->res; ++y) {
        for (x = 0; x < a->res; ++x) {
            float *c = a->cells_data + (size_t)(y * a->res + x) * (4u + MODEL_MAX_PARAMS);
            double state[MODEL_MAX_STATE], vals[MODEL_MAX_PARAMS];
            int used = steps, s;
            for (k = 0; k < MODEL_MAX_STATE; ++k) state[k] = c[k];
            for (k = 0; k < MODEL_MAX_PARAMS; ++k) vals[k] = c[MODEL_MAX_STATE + k];
            for (s = 1; s <= steps; ++s) {
                int escaped = 0;
                m->step(m, state, vals, e->dt, 1);
                for (k = 0; k < dim; ++k) {
                    if (!(state[k] == state[k]) || fabs(state[k]) > radius) {
                        escaped = 1;
                        break;
                    }
                }
                if (escaped) { used = s; break; }
            }
            a->analysis[(size_t)(y * a->res + x)] = (float)used;
        }
    }
    return 1;
}