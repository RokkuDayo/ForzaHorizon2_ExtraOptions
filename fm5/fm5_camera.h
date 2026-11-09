#ifndef EO_FM5_CAMERA_H
#define EO_FM5_CAMERA_H
#include "fm5/fm5_patch.h"

enum {
    FM5_DRIFT_MODE, FM5_CHASE_SENSITIVITY, FM5_CHASE_SPEED, FM5_CHASE_RANGE,
    FM5_DRIVER_SENSITIVITY, FM5_DRIVER_SPEED, FM5_DRIVER_RANGE,
    FM5_FOV_CHASE, FM5_FOV_FAR, FM5_FOV_DRIVER, FM5_FOV_HOOD, FM5_FOV_BUMPER,
    FM5_COCKPIT_BLUR, FM5_CAMERA_SETTING_COUNT
};
unsigned fm5_camera_prepare(unsigned char *image, Fm5Site *sites,
                            const wchar_t *directory, int drift, int fov, void (*log)(const char *));
unsigned fm5_camera_menu_prepare(unsigned char *image, Fm5Site *sites, int drift, int fov, void (*log)(const char *));
void fm5_camera_menu_hide_level_faster(int hide);
void fm5_camera_menu_hide_drivatar_designs(int hide);
int fm5_camera_get(unsigned index);
int fm5_camera_set(unsigned index, int value);
void fm5_camera_reset(void);
#endif
