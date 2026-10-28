#ifndef FH2_WORLD_MENU_H
#define FH2_WORLD_MENU_H
#include "extra_options.h"

/* [Frontend] WorldMenu (default 0): a World tile in the pause menu's Settings
   hub (A_PauseOptions), in the lower half of the Team Credits column. It opens
   the native Video page rebuilt as a World page that holds only the Time of
   Day and Weather rows (time_weather.c). The choices are kept in
   FH2_ExtraOptions.ini [World]. Nothing is hooked while the switch is 0. */
enum { WORLD_TIME, WORLD_WEATHER, WORLD_SETTING_COUNT };
void world_menu_install(unsigned char *game_image, int time_choice, int weather_choice);
/* 1 once the tile and the time of day and weather hooks are in. */
int world_menu_installed(void);
int world_menu_is_hub_xaml(const char *text, size_t length);
/* The hub's XAML with the World tile; returns the new length, or 0 when the
   layout did not match. */
size_t world_menu_extend_hub_xaml(unsigned char *game_image, const char *text, size_t length,
                                  char *output, size_t capacity);
/* 1 once after the World tile was chosen: the next Video page is the World
   page. */
int world_menu_take_request(void);
unsigned world_menu_count(unsigned setting);
int world_menu_get(unsigned setting);
/* Applies the choice and writes it to [World]; 1 when it changed. */
int world_menu_set(unsigned setting, int value);
#endif
