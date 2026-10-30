#ifndef FH2_VIDEO_SETTINGS_H
#define FH2_VIDEO_SETTINGS_H
#include "extra_options.h"

enum { DRIFT_ON, DRIFT_DRIVER_ONLY, DRIFT_OFF };

enum { SPAWN_DEFAULT, SPAWN_CAR_MEET_ENTRANCE, SPAWN_FESTIVAL_SITE };

enum { CAR_POSITION_NEAREST_ROAD, CAR_POSITION_SAME_LOCATION };

enum {
    DRIFT_MODE,
    DRIFT_CHASE_SENSITIVITY,
    DRIFT_CHASE_SPEED,
    DRIFT_CHASE_RANGE,
    DRIFT_DRIVER_SENSITIVITY,
    DRIFT_DRIVER_SPEED,
    DRIFT_DRIVER_RANGE,
    DRIFT_SETTING_COUNT
};

enum {
    FOV_CHASE = DRIFT_SETTING_COUNT,
    FOV_FAR_CHASE,
    FOV_DRIVER,
    FOV_HOOD,
    FOV_BUMPER,
    CAMERA_SETTING_COUNT
};

enum {
    REALTIME_SHADOWS = CAMERA_SETTING_COUNT,
    RESERVED_MOTION_SLOT,
    PHOTO_SAMPLES_PRESET,
    RESERVED_FILTERING_SLOT,
    MINIMAP_ORIENTATION,
    AUTODRIVE_CINEMATIC,
    SKIP_CHAMPIONSHIP_DIALOGUE,
    SPAWN_LOCATION,
    CAR_CHANGE_POSITION,
    LIVERY_RESOLUTION,
    LIVERY_MSAA,
    LIVERY_SAMPLING,
    SHOW_ALL_EVENTS,
    AUTODRIVE_EVENTS,
    CUSTOM_SHOWCASES,
    BEN_PHOTO_DIALOGUE,
    ASHLEY_GARAGE_DIALOGUE,
    WHEELSPIN_NOTIFICATION,
    CHECKPOINT_SOUND,
    MASTER_VOLUME,
    OPEN_WORLD_DRIVATARS,
    SKILL_POINT_NOTIFICATION,
    POWER_UNITS,
    SPRING_UNITS,
    SKILL_COUNTER_SHAKE,
    PLAYER_CHARACTER,
    SPEED_ZONE_VIGNETTE,
    VIGNETTE_EFFECT,
    VIDEO_SETTING_COUNT
};

enum { VIDEO_PAGE, HUD_PAGE, AUDIO_PAGE, XP_PAGE, DIFFICULTY_PAGE, PHOTO_PAGE, SETTINGS_PAGE_COUNT };

enum { SHADOWS_DEFAULT, SHADOWS_OFF, SHADOWS_ON };

void video_settings_seed(const ModOptions *legacy);
void video_settings_apply(ModOptions *options);
unsigned video_setting_page(unsigned index);
void video_settings_reset_page(unsigned page);
void video_settings_refresh_hud(void);

#define FOV_MIN_DEGREES 20
#define FOV_MAX_DEGREES 120

void video_settings_load(const wchar_t *directory);
int video_setting_get(unsigned index);
int video_setting_set(unsigned index, int value);
void video_settings_reset(void);
float camera_fov_degrees(unsigned view);
float camera_fov_default(unsigned view);
int drift_view_enabled(unsigned view);
float drift_look_speed(unsigned view);
float drift_sensitivity(unsigned view);
float drift_angle(unsigned view, float angle);
float clamp_drift_angle(unsigned view, float angle);
#endif
