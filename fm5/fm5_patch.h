#ifndef EO_FM5_PATCH_H
#define EO_FM5_PATCH_H
#include "shared/extra_options.h"

/* All FM5 features participate in the same verified startup transaction. */
typedef struct {
    unsigned rva, size;
    unsigned char original[16], replacement[16];
    DWORD protection;
} Fm5Site;

void fm5_rims_database(void *database);
unsigned fm5_rims_prepare(unsigned char *image, Fm5Site *sites);
unsigned fm5_local_liveries_prepare(unsigned char *image, Fm5Site *sites,
                                    const wchar_t *directory, unsigned maximum,
                                    void (*log)(const char *));
#endif
