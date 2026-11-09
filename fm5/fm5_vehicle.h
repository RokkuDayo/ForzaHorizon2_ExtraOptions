#ifndef EO_FM5_VEHICLE_H
#define EO_FM5_VEHICLE_H
#include "fm5/fm5_patch.h"
unsigned fm5_vehicle_prepare(unsigned char *,Fm5Site *,int,const wchar_t *);
void fm5_vehicle_photo_begin(void *);
void fm5_vehicle_photo_end(void *);
int fm5_vehicle_photo_get(unsigned);
int fm5_vehicle_photo_set(void *,unsigned,int);
int fm5_vehicle_photo_tick(void *);
int fm5_vehicle_input(unsigned short *, int allow_keyboard);
int fm5_vehicle_local_car(void *);
#endif
