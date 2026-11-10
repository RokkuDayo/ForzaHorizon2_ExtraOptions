#ifndef EO_FH3_TEXT_H
#define EO_FH3_TEXT_H
#include "shared/extra_options.h"

void fh3_text_init(const wchar_t *directory, int (*code)(char [4]));
int fh3_text_language(void);
const char *fh3_text_code(int language);
int fh3_text_from_game(void);
unsigned fh3_text_wide(const char *english, wchar_t *out, unsigned capacity);
/* Returns an owned, ASCII-encoded fragment, or NULL to use the original. */
char *fh3_text_xaml(const char *xaml, int language);

#endif
