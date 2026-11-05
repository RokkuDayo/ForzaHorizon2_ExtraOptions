#ifndef FH2_PATCH_ENGINE_H
#define FH2_PATCH_ENGINE_H
#include "extra_options.h"
int patch_engine_apply(unsigned char *game_image, const ModOptions *options);
int verify_game_file(const wchar_t *path);
#endif
