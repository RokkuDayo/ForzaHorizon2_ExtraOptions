#ifndef FH2_CUSTOM_SHOWCASES_H
#define FH2_CUSTOM_SHOWCASES_H
#include "extra_options.h"

extern PatchDefinition custom_showcases_hook_patches[3];
void custom_showcases_prepare_hooks(unsigned char *image);
int custom_showcases_transform(void *resource, unsigned kind);
void custom_showcases_resource_parsed(unsigned kind, int success);
int custom_showcases_selecting(void);
#endif
