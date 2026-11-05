#pragma once
#include "support.h"

void addon_archives_bind(unsigned char *image);
void addon_archives_reset(void);
int addon_archives_mount(void *filesystem);
void addon_archives_log_lookup(const char *path, size_t length, int found, void *volume);

int addon_archives_prepare_io(void);
int addon_archives_volume_addon(void *volume);
