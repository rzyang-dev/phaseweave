/* atlas.h - CPU sampling of an Experiment into channels + RGBA */
#ifndef DP_ATLAS_H
#define DP_ATLAS_H

#include "experiment.h"

typedef struct Atlas Atlas;

Atlas *atlas_create(const Experiment *e, int res, char *err, size_t errsz);
void   atlas_destroy(Atlas *a);

void   atlas_reset(Atlas *a, const Experiment *e);
void   atlas_step(Atlas *a, const Experiment *e, int steps);
void   atlas_colorize(Atlas *a, const Experiment *e, const unsigned char *lut);
void   atlas_resize(Atlas *a, int res, char *err, size_t errsz);
int    atlas_threads(const Atlas *a);

int    atlas_res(const Atlas *a);
int    atlas_cells(const Atlas *a);
const unsigned char *atlas_rgba(const Atlas *a);
void   atlas_read_cell(const Atlas *a, int index, float state[4], float params[6], float ch[4]);

#endif