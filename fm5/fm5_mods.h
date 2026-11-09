#pragma once
#include "fm5/fm5_patch.h"
unsigned fm5_mods_prepare(unsigned char *base,Fm5Site *sites,const wchar_t *executable,const wchar_t *directory,int cache);
void fm5_mods_files(void *filesystem);

wchar_t *fm5_mods_alias_path(const wchar_t *path);
void fm5_mods_builtin_files(unsigned char *base);
