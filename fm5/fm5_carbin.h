#pragma once
#include "fm5/fm5_patch.h"
unsigned fm5_carbin_prepare(unsigned char *base,Fm5Site *sites,const wchar_t *directory);
int fm5_carbin_physical(const wchar_t *path);
void fm5_carbin_owner(void *volume,unsigned addon);
