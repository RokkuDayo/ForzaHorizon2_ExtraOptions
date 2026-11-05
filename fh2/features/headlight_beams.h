#ifndef FH2_HEADLIGHT_BEAMS_H
#define FH2_HEADLIGHT_BEAMS_H
#include "extra_options.h"
extern PatchDefinition headlight_beam_patches[4];
void headlight_beams_bind(unsigned char *image);
void headlight_beams_set_car(void *car, int enabled);
#endif
