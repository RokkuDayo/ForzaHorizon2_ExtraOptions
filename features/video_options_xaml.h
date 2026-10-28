#ifndef FH2_VIDEO_OPTIONS_XAML_H
#define FH2_VIDEO_OPTIONS_XAML_H
#include "extra_options.h"

unsigned video_options_page(const char *text, size_t length);
int video_options_is_xaml(const char *text, size_t length);
size_t video_options_extend_xaml(unsigned char *game_image, const char *text, size_t length,
                                 char *output, size_t capacity);
/* Writes text as XML attribute text: ASCII as is, anything else as character
   references, so it reads the same with or without a UTF-8 byte-order mark.
   Returns the length, or 0 when it does not fit. */
size_t video_options_xml_text(char *output, size_t capacity, const wchar_t *text);
/* The Video page rebuilt as the World page (world_menu.c); 0 on mismatch. */
size_t video_options_world_xaml(unsigned char *game_image, const char *text, size_t length,
                                char *output, size_t capacity);
#endif
