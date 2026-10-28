#ifndef FH2_CUSTOM_LANGUAGE_H
#define FH2_CUSTOM_LANGUAGE_H
#include "extra_options.h"

/* Custom languages: FH2_ExtraOptions.ini [Language] Custom=<id> selects the
   pack in <game>\Mount\languages\<id>\. Its language.ini names it and the game
   language slot it takes over ([Language] Name=..., Slot=ru-RU: an Azimuth
   language code). The pack holds only the game files it changes, laid out like
   mediaoverride (media\X.zip\a\b -> languages\<id>\X\a\b, or a whole X.zip),
   fonts included (ui\Fonts\*.vfont), and is mounted over game:\media\ below
   mediaoverride and above mediapc. Choose the slot's language in the launcher
   to play in the pack's language; voice subtitles (voice_subtitles.c) follow
   the same choice. Without [Language] Custom nothing is mounted or hooked. */

/* Reads the setting and the pack (no logging: the log is not started yet). */
void custom_language_prepare(const wchar_t *executable_path, const wchar_t *settings_directory);
/* The game path the pack is mounted from ("game:\languages\<id>\"), or NULL. */
const char *custom_language_mount(void);
/* After log_start: logs the pack and installs its voice subtitles when the
   launcher runs the game in the pack's slot. */
void custom_language_install(unsigned char *game_image);
/* 1 for a folder name of 1-32 letters, digits, - or _ (the [Language] Custom value). */
int custom_language_valid_id(const char *id);

#endif
