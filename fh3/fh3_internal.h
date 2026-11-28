#ifndef FH3_INTERNAL_H
#define FH3_INTERNAL_H
/* Services fh3.c shares with the other FH3 modules. */
#include <windows.h>

unsigned char *fh3_image(void);
const wchar_t *fh3_ini_path(void);
int fh3_mount(void *filesystem, const char *folder, unsigned priority);
int fh3_mount_at(void *filesystem, const char *folder, const char *target, unsigned priority);
void *fh3_player_car(void);
void fh3_set_hud_hidden(int mode); /* plugins: 0 HUD, 1 hidden, 2 hidden + letterbox */
void fh3_set_hud_prompt(const wchar_t *before, const char *button, const wchar_t *after);
void fh3_set_anna_status(const wchar_t *label, const wchar_t *value); /* ANNA's ring lit, a tag above her */
void fh3_set_anna_ready(int ready); /* ANNA's button: D-pad Down where she's unavailable */

/* Mod loader (fh3_mods.c). */
void fh3_mods_configure(const wchar_t *directory, const wchar_t *executable);
void fh3_mods_mount(void *filesystem);
void fh3_mods_mount_cars(void *filesystem); /* mod car zips as game:\media\cars\<name>\ */
void fh3_mods_load_plugins(void);
void fh3_mods_frame(float dt);
unsigned char *fh3_code_stub(void); /* 16 bytes of executable memory within rel32 reach of the image */
int fh3_write_code(unsigned char *target, const void *bytes, size_t size);

/* Add-on packages (fh3_addons.c): database.json and strings.json, FH2's format. */
void fh3_addons_configure(const wchar_t *root, const wchar_t (*names)[MAX_PATH], unsigned count);
int fh3_addons_pending(void);
void fh3_addons_engine_audio(void (*fn)(const char *car, const char *donor, int explicit_donor));
int fh3_addons_database(void *database); /* packages applied, or -1: discard the in-memory copy */
#endif
