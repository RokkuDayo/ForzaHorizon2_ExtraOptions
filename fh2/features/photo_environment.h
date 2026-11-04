#ifndef FH2_PHOTO_ENVIRONMENT_H
#define FH2_PHOTO_ENVIRONMENT_H
#include <stddef.h>
void photo_environment_prepare(unsigned char *image);
void photo_environment_begin(void *page);
void photo_environment_end(void *page);
void photo_environment_invalidate(void *page);
int photo_environment_get(unsigned setting);
int photo_environment_set(unsigned setting, int value);
unsigned photo_environment_count(unsigned setting);
const wchar_t *photo_environment_label(unsigned setting, unsigned index);
const wchar_t *photo_environment_title(unsigned setting);
#endif
