#ifndef FH3_INTERNAL_H
#define FH3_INTERNAL_H
/* Services fh3.c shares with the other FH3 modules. */
#include <windows.h>

unsigned char *fh3_image(void);
const wchar_t *fh3_ini_path(void);
int fh3_mount(void *filesystem, const char *folder, unsigned priority);
void *fh3_player_car(void);

/* Mod loader (fh3_mods.c). */
void fh3_mods_configure(const wchar_t *directory, const wchar_t *executable);
void fh3_mods_mount(void *filesystem);
void fh3_mods_load_plugins(void);
void fh3_mods_frame(float dt);
#endif
