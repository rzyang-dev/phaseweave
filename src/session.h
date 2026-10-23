/* session.h - small text experiment session (save/load). */
#ifndef DP_SESSION_H
#define DP_SESSION_H

#include "experiment.h"
#include "phase_view.h"
#include <stddef.h>

typedef struct {
    char name[24];
    double value;
} SessionParam;

typedef struct {
    char model[32];
    char title[80];
    char axis_x[24], axis_y[24];
    double xmin, xmax, ymin, ymax;
    int res;
    double dt;
    int steps;
    char channel[32];
    char post[16];
    char palette[32];
    int grid, periodic, circle;
    int backend_gpu;
    int selected, sel_x, sel_y;
    int export_scale;
    int export_overlay;
    double escape_radius;
    int cluster_k;
    double converge_eps;
    double view_cx, view_cy, view_zoom;
    int nparams;
    SessionParam params[MODEL_MAX_PARAMS];
} SessionData;

int session_save(const char *path, const Experiment *e, const PhaseView *v,
                 int backend_gpu, int selected, int sel_x, int sel_y,
                 char *err, size_t errsz);
int session_load(const char *path, SessionData *s, char *err, size_t errsz);

#endif