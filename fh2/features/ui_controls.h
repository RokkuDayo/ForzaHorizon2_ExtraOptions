#ifndef FH2_UI_CONTROLS_H
#define FH2_UI_CONTROLS_H
#include "extra_options.h"
void ui_controls_restore(void);
void ui_controls_prepare_hooks(unsigned char *image, const ModOptions *options);
extern PatchDefinition ui_controls_hook_patches[3];
#endif
