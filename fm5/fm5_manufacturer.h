#ifndef EO_FM5_MANUFACTURER_H
#define EO_FM5_MANUFACTURER_H
#include "fm5/fm5_patch.h"
unsigned fm5_manufacturer_prepare(unsigned char *image,Fm5Site *sites,void (*log)(const char *));
int fm5_manufacturer_resource(const char *text,size_t length);
size_t fm5_manufacturer_xaml(const char *text,size_t length,char *out,size_t capacity);
#endif
