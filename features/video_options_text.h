#ifndef FH2_VIDEO_OPTIONS_TEXT_H
#define FH2_VIDEO_OPTIONS_TEXT_H
#include <stddef.h>

/* Keyboard & mouse page rows (kbm_menu.c): input mode, mouse look, mouse look
   up/down, invert mouse Y, mouse sensitivity, keyboard steering speed, menu
   pointer, cursor size, map cursor speed. Key row labels (kbm_rebind.c). */
enum { KBM_TEXT_ROWS = 9, KBM_TEXT_ACTIONS = 33 };
enum { KBM_LABEL_PRESS_KEY, KBM_LABEL_NONE, KBM_LABEL_UNAVAILABLE, KBM_LABEL_OFF,
       KBM_LABEL_SAVE_FAILED, KBM_LABEL_IN_USE, KBM_LABEL_RESET, KBM_LABEL_DONE,
       KBM_LABEL_COUNT };

typedef struct {
    const char *camera, *driver;
    const wchar_t *photo_lights;
    const char *player_character, *player_character_description;
    const char *open_world_drivatars, *open_world_drivatars_description;
    const char *fov_label;
    const char *master_volume;
    const char *skill_point_notification, *skill_point_description;
    const char *skill_counter_shake, *skill_counter_shake_description;
    const char *speed_zone_vignette, *speed_zone_vignette_description;
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
    /* Keyboard & mouse page (kbm_menu.c, kbm_rebind.c). */
    const char *keyboard_mouse; /* tile caption and page title, in capitals */
    const char *kbm_names[KBM_TEXT_ROWS], *kbm_descriptions[KBM_TEXT_ROWS];
    const wchar_t *kbm_modes[3]; /* Auto, Keyboard & mouse, Gamepad */
    const char *kbm_actions[KBM_TEXT_ACTIONS]; /* per KbmBinding (kbm_config.h) */
    const char *kbm_key_descriptions[2]; /* a driving key, a menu key */
    const char *kbm_reset, *kbm_reset_description;
    const wchar_t *kbm_labels[KBM_LABEL_COUNT];
} VideoOptionsText;

const VideoOptionsText *video_options_text(unsigned char *game_image);
#endif
