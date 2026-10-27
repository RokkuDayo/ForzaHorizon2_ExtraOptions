#ifndef FH2_RADIO_STATION_H
#define FH2_RADIO_STATION_H
#include "extra_options.h"

// Adds a native radio station that streams the MP3 files in the configured
// folder through the game's own FMOD radio. Settings: FH2_ExtraOptions.ini
// [CustomRadio] (off by default); keys: [Bindings] RadioStation/RadioNext/RadioRestart.
void radio_station_install(unsigned char *game_image, const wchar_t *directory);
// Game thread, once per local-car update: handles the RadioStation binding.
void radio_station_game_tick(void);
#endif
