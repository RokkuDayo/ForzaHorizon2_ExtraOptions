#ifndef EO_FH3_H
#define EO_FH3_H
#include "extra_options.h"

#define FH3_TITLE_ID 0x4ce1dfabu
#define FH3_FILE_SIZE 85425664u
#define FH3_IMAGE_SIZE 0x5495000u
#define FH3_TIMESTAMP 0x5b16eccau

/* Independent title profile for Forza Horizon 3 1.0.125.0. Never initializes
   FH2 settings, hooks, plugins or save compatibility. */
int fh3_initialize(unsigned title_id, unsigned char *game, const wchar_t *executable,
                   const wchar_t *directory);
/* Startup refusals before the title is identified still go to the FH3 log. */
void fh3_log_status(const wchar_t *directory);
#endif
