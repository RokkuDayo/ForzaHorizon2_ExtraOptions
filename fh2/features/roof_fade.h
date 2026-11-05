#ifndef FH2_ROOF_FADE_H
#define FH2_ROOF_FADE_H

#include "extra_options.h"

extern PatchDefinition roof_fade_hook_patch;
void roof_fade_prepare_hook(unsigned char *image);
void roof_fade_set_opacity(float opacity);
int roof_fade_black_frame_presented(void);

#endif
