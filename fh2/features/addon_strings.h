#pragma once
#include "addon_json.h"

int addon_strings_validate(const JsonValue *document);
void addon_strings_prepare_hooks(unsigned char *image);
void addon_strings_refresh(void);
extern PatchDefinition addon_strings_hook_patches[2];
