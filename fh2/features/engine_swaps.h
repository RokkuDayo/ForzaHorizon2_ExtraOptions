#ifndef FH2_ENGINE_SWAPS_H
#define FH2_ENGINE_SWAPS_H
#include "extra_options.h"

void engine_swaps_configure(int enabled);
int engine_swaps_enabled(void);
int engine_swaps_expand(void *database);
extern PatchDefinition engine_name_hook_patches[2];
void engine_swaps_prepare_hooks(unsigned char *image);
#endif
