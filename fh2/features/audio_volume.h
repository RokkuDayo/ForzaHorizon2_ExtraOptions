#ifndef FH2_AUDIO_VOLUME_H
#define FH2_AUDIO_VOLUME_H
#include "extra_options.h"
extern PatchDefinition audio_volume_hook_patches[2];
void audio_volume_prepare_hooks(unsigned char *base);
void audio_volume_refresh(void);
#endif
