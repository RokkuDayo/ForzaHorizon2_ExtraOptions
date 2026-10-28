#ifndef FH2_SMOOTH_MENUS_H
#define FH2_SMOOTH_MENUS_H
#include "extra_options.h"
/* Menus, replays and photo mode at up to 60 FPS instead of the render
   scenarios' 30, applied in memory as the game reads media\renderscenarios.
   INI: [Graphics] SmoothMenus (default 0). The game reads the scenarios at
   startup, so a change applies after a restart. */
int smooth_menus_install(unsigned char *game_image);
#endif
