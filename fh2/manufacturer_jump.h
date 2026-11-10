#ifndef FH2_MANUFACTURER_JUMP_H
#define FH2_MANUFACTURER_JUMP_H
#include "shared/extra_options.h"
enum { MANUFACTURER_JUMP_PATCH_COUNT = 51 };
extern PatchDefinition manufacturer_jump_hook_patches[MANUFACTURER_JUMP_PATCH_COUNT];
int manufacturer_jump_menu_key(void *root,unsigned char up);
void manufacturer_jump_prepare_hooks(unsigned char *base);
int manufacturer_jump_is_xaml(const char *text, size_t length);
size_t manufacturer_jump_xaml(const char *text, size_t length, char *output, size_t capacity);
#endif
