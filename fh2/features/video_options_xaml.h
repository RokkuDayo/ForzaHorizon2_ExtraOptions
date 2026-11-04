#ifndef FH2_VIDEO_OPTIONS_XAML_H
#define FH2_VIDEO_OPTIONS_XAML_H
#include "extra_options.h"

unsigned video_options_page(const char *text, size_t length);
int video_options_is_xaml(const char *text, size_t length);
size_t video_options_extend_xaml(unsigned char *game_image, const char *text, size_t length,
                                 char *output, size_t capacity);
#endif
