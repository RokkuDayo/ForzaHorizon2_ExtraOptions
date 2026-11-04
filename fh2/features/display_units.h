#ifndef FH2_DISPLAY_UNITS_H
#define FH2_DISPLAY_UNITS_H
#include "extra_options.h"
extern PatchDefinition display_units_hook_patches[2];
void display_units_prepare_hooks(unsigned char *base);
void display_units_refresh(void);
#endif
