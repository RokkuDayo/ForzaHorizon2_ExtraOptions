#ifndef FH2_SPEED_ZONE_H
#define FH2_SPEED_ZONE_H
#include "extra_options.h"
extern PatchDefinition speed_zone_car_change_patch;
void speed_zone_prepare(unsigned char *image);
void speed_zone_car_selection(void *page);
#endif
