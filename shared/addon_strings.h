#pragma once
#include "shared/addon_json.h"

int addon_strings_validate(const JsonValue *document);
void addon_strings_prepare_hooks(unsigned char *image);
void addon_strings_refresh(void);
extern PatchDefinition addon_strings_hook_patches[2];

void addon_strings_bind_fm5(unsigned char *base);
void *addon_strings_fm5_load(void **stream,const char *path,unsigned flags);
