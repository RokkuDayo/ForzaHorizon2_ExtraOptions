#pragma once
#include "shared/support.h"

typedef struct LooseMediaIndex LooseMediaIndex;

LooseMediaIndex *loose_media_index_create(const wchar_t *folder);
void loose_media_index_destroy(LooseMediaIndex *index);
// A false result proves the path was absent at mount time.
int loose_media_may_exist(const LooseMediaIndex *index, const char *path, size_t length);
