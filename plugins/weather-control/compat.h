#ifndef EO_WEATHER_COMPAT_H
#define EO_WEATHER_COMPAT_H
#include <stdio.h>
#include <wchar.h>
#include <stddef.h>
int eo_snprintf(char *,size_t,const char *,...);
int eo_swprintf(wchar_t *,size_t,const wchar_t *,...);
#define snprintf eo_snprintf
#define swprintf eo_swprintf
#endif
