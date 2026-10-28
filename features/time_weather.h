#ifndef FH2_TIME_WEATHER_H
#define FH2_TIME_WEATHER_H
#include "extra_options.h"
/* Time of day and weather chosen on the World page (world_menu.c), applied on
   the game thread from the open world's per-frame update. 0 is the game's own:
   nothing is overridden. */
enum { TIME_GAME, TIME_SUNRISE, TIME_MIDDAY, TIME_SUNSET, TIME_MIDNIGHT, TIME_CHOICE_COUNT };
enum { WEATHER_GAME, WEATHER_SUNNY, WEATHER_WET, WEATHER_RAIN, WEATHER_STORM, WEATHER_CHOICE_COUNT };
/* Returns 0 and changes nothing when the world update is not as expected. */
int time_weather_install(unsigned char *image, int time_choice, int weather_choice);
void time_weather_set_time(int choice);
void time_weather_set_weather(int choice);
#endif
