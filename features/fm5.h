#ifndef EO_FM5_H
#define EO_FM5_H
#include "extra_options.h"

#define FM5_TITLE_ID 0x7b35d917u
#define FM5_IMAGE_SIZE 0x279d000u
#define FM5_TIMESTAMP 0x53d8d1f2u

/* Independent title profile. Never initializes FH2 settings, hooks or plugins. */
int fm5_initialize(unsigned title_id, unsigned char *game, const wchar_t *executable,
                   const wchar_t *directory);
#endif
