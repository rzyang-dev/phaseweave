/* model.c - model registry */
#include "model.h"

#include <string.h>

static const Model *const g_models[] = {
    &DP_MODEL,
    &LOGISTIC_MODEL,
    &HENON_MODEL
};

int model_count(void) { return (int)(sizeof(g_models) / sizeof(g_models[0])); }

const Model *model_at(int index)
{
    if (index < 0 || index >= model_count()) return &DP_MODEL;
    return g_models[index];
}

const Model *model_by_id(const char *id)
{
    int i;
    if (!id) return &DP_MODEL;
    for (i = 0; i < model_count(); ++i)
        if (strcmp(g_models[i]->id, id) == 0) return g_models[i];
    return &DP_MODEL;
}
