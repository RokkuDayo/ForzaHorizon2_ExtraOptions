#ifndef EO_FM5_VIDEO_H
#define EO_FM5_VIDEO_H
#include "fm5/fm5_patch.h"
unsigned fm5_video_prepare(unsigned char *game,Fm5Site *sites,const wchar_t *directory,void (*log)(const char *));
size_t fm5_video_xaml(const char *source,size_t length,char *out,size_t capacity);
#endif
