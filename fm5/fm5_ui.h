#ifndef EO_FM5_UI_H
#define EO_FM5_UI_H
#include "fm5/fm5_patch.h"
unsigned fm5_ui_prepare(unsigned char *,Fm5Site *,const wchar_t *);
unsigned fm5_read_binding(const wchar_t *ini, const wchar_t *key, int controller,
                          const wchar_t *fallback);
int fm5_keyboard_down(unsigned key);
#endif
