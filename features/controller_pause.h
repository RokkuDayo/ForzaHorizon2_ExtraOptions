#ifndef FH2_CONTROLLER_PAUSE_H
#define FH2_CONTROLLER_PAUSE_H
#include "extra_options.h"
/* [Frontend] PauseOnlyOnControllerUnplug (default 0): the game's
   "controller lost: pause" rule fires only when a pad that was attached goes
   away, not over and over while no pad is paired. */
void controller_pause_install(unsigned char *game_image);
#endif
