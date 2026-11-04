#pragma once
#include "extra_options.h"

enum { ADDON_WHEEL_PATH_CAPACITY = 512 };
void addon_wheels_bind(unsigned char *image);
void addon_wheels_register_database(void *database);
const char *addon_wheels_donor(const char *name, size_t length);
int addon_wheels_paths(const char *name, size_t length, const char **models, const char **textures);
