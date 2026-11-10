#ifndef FH2_VEHICLE_CONTROLS_H
#define FH2_VEHICLE_CONTROLS_H
void vehicle_controls_bind(unsigned char *image);
void vehicle_photo_lights_begin(void);
void vehicle_photo_lights_end(void);
int vehicle_photo_lights_get(void);
int vehicle_photo_lights_set(int value);
#endif
