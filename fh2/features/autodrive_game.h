#ifndef FH2_AUTODRIVE_GAME_H
#define FH2_AUTODRIVE_GAME_H
#include "extra_options.h"

typedef struct {
    void *object, *counter;
} GameShared;

typedef struct {
    GameShared wrapper;
    GameShared object;
} AutodriveHud;

typedef struct {
    void *camera, *relative;
    int previous_group;
    double previous_selection, selection;
    unsigned char label;
    int cycling;
} AutodriveCamera;

void autodrive_game_bind(unsigned char *image);
int autodrive_game_get_hud(AutodriveHud *hud);
void autodrive_game_release_shared(GameShared *value);
int autodrive_game_is_freeroam(void);
int autodrive_game_event_context(void);
int autodrive_game_can_start(void *controller);
int autodrive_game_can_drive(void *controller);
void *autodrive_game_event_route(void *car);
int autodrive_game_has_gameplay_input(void *controller);
int autodrive_game_presentation_mode(void);
int autodrive_game_is_carmeet(void);
int autodrive_game_window_focused(void);
int autodrive_game_buttons(void **controller, unsigned short *buttons);
int autodrive_game_dpad_down(void **controller);
int autodrive_game_driving_input(void *controller);
void *autodrive_game_get_car(void);
void autodrive_game_release_car(void *car);
int autodrive_game_driver_mode(void *car);
int autodrive_game_set_ai(void *car, int enabled);
void autodrive_game_clear_rewind(void);
void *autodrive_game_route(void *satnav, int *arrived);
int autodrive_game_tracker_matches(void *tracker, void *satnav);
int autodrive_game_begin_route(void *tracker);
void autodrive_game_end_route(void *tracker);
void *autodrive_game_camera(void);
int autodrive_game_camera_group(void *camera);
int autodrive_game_set_camera_group(void *camera, int group);
int autodrive_game_slow_spin(AutodriveCamera *saved);
void autodrive_game_cycle_camera(AutodriveCamera *saved);
int autodrive_game_restore_camera(AutodriveCamera *saved);
int autodrive_game_flash(void *camera);
#endif
