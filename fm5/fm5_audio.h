#ifndef EO_FM5_AUDIO_H
#define EO_FM5_AUDIO_H
#include "fm5/fm5_patch.h"
unsigned fm5_audio_prepare(unsigned char *,Fm5Site *);
size_t fm5_audio_xaml(const char *,size_t,char *,size_t);
#endif
