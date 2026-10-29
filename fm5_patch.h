#ifndef EO_FM5_PATCH_H
#define EO_FM5_PATCH_H
#include "extra_options.h"

/* All FM5 features participate in the same verified startup transaction. */
typedef struct {
    unsigned rva, size;
    unsigned char original[13], replacement[13];
    DWORD protection;
} Fm5Site;

unsigned fm5_rims_prepare(unsigned char *image, Fm5Site *sites);
#endif
