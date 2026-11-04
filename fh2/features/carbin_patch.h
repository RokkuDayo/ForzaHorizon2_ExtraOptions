#pragma once
#include "addon_json.h"

// A failed patch never changes the caller's base. Output is process-heap owned.
int carbin_patch_apply(const unsigned char *base, size_t bytes, const JsonValue *patch,
                       unsigned char **output, size_t *output_bytes, const char **error);
typedef unsigned (*CarbinIdMap)(void *, unsigned, unsigned);
int carbin_remap_ids(unsigned char *data, size_t bytes, CarbinIdMap map, void *context);
