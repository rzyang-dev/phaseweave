/* session.c - line-oriented .dps experiment file */
#include "session.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s)
{
    size_t n;
    while (*s && isspace((unsigned char)*s)) memmove(s, s + 1, strlen(s));
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static int boolv(const char *v) { return v && (v[0] == '1' || v[0] == 't' || v[0] == 'T'); }

int session_save(const char *path, const Experiment *e, const PhaseView *v,
                 int backend_gpu, int selected, int sel_x, int sel_y,
                 char *err, size_t errsz)
{
    FILE *f;
    int i;
    const char *post = "raw";
    if (err && errsz) err[0] = 0;
    if (!path || !e || !v) return 0;
    if (e->post == 1) post = "gradient";
    else if (e->post == 2) post = "range";
    else if (e->post == 3) post = "variance";
    else if (e->post == 4) post = "ftle";
    f = fopen(path, "wb");
    if (!f) { if (err) snprintf(err, errsz, "cannot create %s", path); return 0; }
    fprintf(f, "DPEXPERIMENT 1\n");
    fprintf(f, "model=%s\n", e->model);
    fprintf(f, "title=%s\n", e->title);
    fprintf(f, "axis_x=%s\n", e->p[e->axis_x].name);
    fprintf(f, "axis_y=%s\n", e->p[e->axis_y].name);
    fprintf(f, "xmin=%.17g\nxmax=%.17g\nymin=%.17g\nymax=%.17g\n",
            e->xmin, e->xmax, e->ymin, e->ymax);
    fprintf(f, "res=%d\ndt=%.17g\nsteps=%d\n", e->res, e->dt, e->max_steps);
    fprintf(f, "channel=%s\npost=%s\npalette=%s\n",
            exp_channel_name(e, e->channel), post, e->palette);
    fprintf(f, "grid=%d\nperiodic=%d\ncircle=%d\n",
            e->grid, e->periodic, e->circle);
    fprintf(f, "backend=%s\n", backend_gpu ? "gpu" : "cpu");
    fprintf(f, "view_cx=%.17g\nview_cy=%.17g\nview_zoom=%.17g\n",
            v->cx, v->cy, v->zoom);
    fprintf(f, "selected=%d\nsel_x=%d\nsel_y=%d\nexport_scale=%d\nexport_overlay=%d\n",
            selected ? 1 : 0, sel_x, sel_y, e->export_scale, e->export_overlay);
    for (i = 0; i < e->np; ++i)
        fprintf(f, "param.%s=%.17g\n", e->p[i].name, e->p[i].value);
    fclose(f);
    return 1;
}

int session_load(const char *path, SessionData *s, char *err, size_t errsz)
{
    FILE *f;
    char line[512];
    int version = 0;
    if (err && errsz) err[0] = 0;
    if (!path || !s) return 0;
    memset(s, 0, sizeof(*s));
    s->res = 64; s->dt = 0.01; s->steps = 8; s->export_scale = 1; s->export_overlay = 1;
    s->view_zoom = 1.0;
    strcpy(s->post, "raw");
    strcpy(s->palette, "phase");
    f = fopen(path, "rb");
    if (!f) { if (err) snprintf(err, errsz, "cannot open %s", path); return 0; }
    while (fgets(line, sizeof(line), f)) {
        char *eq, *k, *v;
        trim(line);
        if (!line[0] || line[0] == '#') continue;
        if (sscanf(line, "DPEXPERIMENT %d", &version) == 1) continue;
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0; k = line; v = eq + 1;
        trim(k); trim(v);
        if (strcmp(k, "model") == 0) strncpy(s->model, v, sizeof(s->model) - 1);
        else if (strcmp(k, "title") == 0) strncpy(s->title, v, sizeof(s->title) - 1);
        else if (strcmp(k, "axis_x") == 0) strncpy(s->axis_x, v, sizeof(s->axis_x) - 1);
        else if (strcmp(k, "axis_y") == 0) strncpy(s->axis_y, v, sizeof(s->axis_y) - 1);
        else if (strcmp(k, "xmin") == 0) s->xmin = atof(v);
        else if (strcmp(k, "xmax") == 0) s->xmax = atof(v);
        else if (strcmp(k, "ymin") == 0) s->ymin = atof(v);
        else if (strcmp(k, "ymax") == 0) s->ymax = atof(v);
        else if (strcmp(k, "res") == 0) s->res = atoi(v);
        else if (strcmp(k, "dt") == 0) s->dt = atof(v);
        else if (strcmp(k, "steps") == 0) s->steps = atoi(v);
        else if (strcmp(k, "channel") == 0) strncpy(s->channel, v, sizeof(s->channel) - 1);
        else if (strcmp(k, "post") == 0) strncpy(s->post, v, sizeof(s->post) - 1);
        else if (strcmp(k, "palette") == 0) strncpy(s->palette, v, sizeof(s->palette) - 1);
        else if (strcmp(k, "grid") == 0) s->grid = boolv(v);
        else if (strcmp(k, "periodic") == 0) s->periodic = boolv(v);
        else if (strcmp(k, "circle") == 0) s->circle = boolv(v);
        else if (strcmp(k, "backend") == 0) s->backend_gpu = strcmp(v, "gpu") == 0;
        else if (strcmp(k, "view_cx") == 0) s->view_cx = atof(v);
        else if (strcmp(k, "view_cy") == 0) s->view_cy = atof(v);
        else if (strcmp(k, "view_zoom") == 0) s->view_zoom = atof(v);
        else if (strcmp(k, "selected") == 0) s->selected = boolv(v);
        else if (strcmp(k, "sel_x") == 0) s->sel_x = atoi(v);
        else if (strcmp(k, "sel_y") == 0) s->sel_y = atoi(v);
        else if (strcmp(k, "export_scale") == 0) s->export_scale = atoi(v);
        else if (strcmp(k, "export_overlay") == 0) s->export_overlay = boolv(v);
        else if (strncmp(k, "param.", 6) == 0 && s->nparams < MODEL_MAX_PARAMS) {
            strncpy(s->params[s->nparams].name, k + 6, sizeof(s->params[s->nparams].name) - 1);
            s->params[s->nparams].value = atof(v);
            s->nparams++;
        }
    }
    fclose(f);
    if (version != 1) { if (err) snprintf(err, errsz, "unsupported experiment file"); return 0; }
    if (!s->model[0]) { if (err) snprintf(err, errsz, "experiment file has no model"); return 0; }
    if (s->res < 16 || s->res > 512) s->res = 64;
    if (s->steps < 0 || s->steps > 256) s->steps = 8;
    if (s->xmax <= s->xmin) { s->xmin = 0; s->xmax = 1; }
    if (s->ymax <= s->ymin) { s->ymin = 0; s->ymax = 1; }
    if (s->view_zoom < 0.25) s->view_zoom = 1.0;
    if (s->export_scale != 2 && s->export_scale != 4) s->export_scale = 1;
    s->export_overlay = s->export_overlay ? 1 : 0;
    if (s->sel_x < 0 || s->sel_x >= s->res || s->sel_y < 0 || s->sel_y >= s->res)
        s->selected = 0;
    return 1;
}