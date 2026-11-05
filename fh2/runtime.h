#ifndef FH2_RUNTIME_H
#define FH2_RUNTIME_H
#include "extra_options.h"
int runtime_initialize(unsigned char *game_image, const wchar_t *executable,
                       const wchar_t *directory);
void adapter_process_attach(HINSTANCE instance);
#endif
