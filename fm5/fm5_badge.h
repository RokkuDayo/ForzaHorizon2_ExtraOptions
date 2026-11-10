#ifndef FM5_BADGE_H
#define FM5_BADGE_H
#include "fm5/fm5_patch.h"
unsigned fm5_badge_prepare(unsigned char *,Fm5Site *,const wchar_t *);
void fm5_badge_database(void *);
wchar_t *fm5_badge_alias(const wchar_t *);
void fm5_badge_files(void *);
#endif
