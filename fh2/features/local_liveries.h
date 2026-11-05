#ifndef FH2_LOCAL_LIVERIES_H
#define FH2_LOCAL_LIVERIES_H
#include "extra_options.h"
extern PatchDefinition local_liveries_hook_patches[6];
void local_liveries_bind(unsigned char *image, int enabled, int limit);
void local_liveries_tick(void *player_car);
#endif
