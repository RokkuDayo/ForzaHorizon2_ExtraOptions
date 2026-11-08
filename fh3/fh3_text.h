#ifndef FH3_TEXT_H
#define FH3_TEXT_H
#include "extra_options.h"

/* The mod's own text follows the language the game loaded: `code` returns the
   game's current table code (EN, DE, CHT...) or NULL while it has none. Until
   then the guess is the game's own source, Azimuth's language for the game if
   one is set, else the Windows display language. English when FH3 has no
   table for the language. */
void fh3_text_init(const wchar_t *directory, const char *(*code)(void));
/* The language's code (EN, DE, ...), for the log. */
const char *fh3_text_language(void);
/* 1 once the language comes from the game rather than the startup guess. */
int fh3_text_from_game(void);
/* UTF-8 translation of an English string, or the string itself. */
const char *fh3_text(const char *english);
/* UTF-16 translation (kept for the process lifetime); length in characters. */
const wchar_t *fh3_wtext(const char *english, unsigned *length);
/* A copy of a XAML fragment whose quoted English texts (whole attribute
   values, or the part after "Extra Options: ") are translated; the fragment
   itself when nothing changes. */
const char *fh3_text_xaml(const char *xaml);
#endif
