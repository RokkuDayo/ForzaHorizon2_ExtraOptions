#ifndef FH2_PAUSE_MENU_H
#define FH2_PAUSE_MENU_H
#include "extra_options.h"
void pause_menu_configure(int enabled);
int pause_menu_is_xaml(const char *text, size_t length);
size_t pause_menu_reorder(const char *text, size_t length, char *output, size_t capacity);
#endif
