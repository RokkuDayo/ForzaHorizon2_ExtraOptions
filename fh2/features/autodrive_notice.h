#ifndef FH2_AUTODRIVE_NOTICE_H
#define FH2_AUTODRIVE_NOTICE_H
#include "patch_engine.h"

extern PatchDefinition autodrive_notice_hook_patch;
void autodrive_notice_prepare(unsigned char *image);
int autodrive_notice_show(void);
void autodrive_notice_update(float elapsed);
void autodrive_notice_clear(void);
#endif
