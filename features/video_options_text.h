#ifndef FH2_VIDEO_OPTIONS_TEXT_H
#define FH2_VIDEO_OPTIONS_TEXT_H
#include <stddef.h>

typedef struct {
    const char *camera, *driver;
    const wchar_t *photo_lights, *photo_custom_time, *photo_hour;
    const char *player_character, *player_character_description;
    const char *open_world_drivatars, *open_world_drivatars_description;
    const char *fov_label;
    const char *master_volume;
    const char *skill_point_notification, *skill_point_description;
    const char *skill_counter_shake, *skill_counter_shake_description;
    const char *speed_zone_vignette, *speed_zone_vignette_description;
    const char *vignette_effect, *vignette_effect_description;
    const char *unit_names[2], *unit_descriptions[2];
    const wchar_t *garage_label, *garage_question;
    const char *parameters[3];
    const char *descriptions[4];
    const wchar_t *choices[3];
    const char *fov_views[5];
    const char *fov_description;
    const char *settings_names[19];
    const char *settings_descriptions[19];
    const wchar_t *default_choice;
    const wchar_t *minimap_choices[2];
    const wchar_t *spawn_choices[2];
    const wchar_t *car_position_choices[2];
    const wchar_t *division_label;
    const wchar_t *event_labels[4];
    const wchar_t *grid_choices[3];
    const wchar_t *manufacturer_jump;
} VideoOptionsText;

const VideoOptionsText *video_options_text(unsigned char *game_image);
#endif
