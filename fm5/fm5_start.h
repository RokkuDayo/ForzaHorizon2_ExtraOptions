#ifndef EO_FM5_START_H
#define EO_FM5_START_H
#include "shared/extra_options.h"
void fm5_start_configure(int enabled);
int fm5_start_resource(const char *source,size_t length);
size_t fm5_start_xaml(const char *source,size_t length,char *out,size_t capacity);
#endif
