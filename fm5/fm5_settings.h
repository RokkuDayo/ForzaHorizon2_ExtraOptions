#ifndef EO_FM5_SETTINGS_H
#define EO_FM5_SETTINGS_H
#include "shared/extra_options.h"
enum {FM5_SAVED_CAMERA_COUNT=13,FM5_SAVED_LIVERY_4K=13,FM5_SAVED_LIVERY_MSAA=14,FM5_SAVED_PLAYER=15,FM5_SAVED_AUDIO=16,FM5_AUDIO_COUNT=14,FM5_SAVED_COUNT=30};
void fm5_settings_load(const wchar_t *);
int *fm5_settings_values(void);
void fm5_settings_save(void);
#endif
