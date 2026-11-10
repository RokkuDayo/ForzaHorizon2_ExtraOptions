#ifndef EO_FM5_DRIVER_H
#define EO_FM5_DRIVER_H
#include "fm5/fm5_patch.h"
unsigned fm5_driver_prepare(unsigned char *,Fm5Site *,int);
void fm5_driver_database(void *);
unsigned fm5_driver_count(void);
void fm5_driver_label(unsigned,wchar_t [128]);
int fm5_driver_index(void);
int fm5_driver_select(unsigned);
#endif
