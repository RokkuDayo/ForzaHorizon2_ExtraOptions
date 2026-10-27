#ifndef FH2_PHOTO_SCAN_H
#define FH2_PHOTO_SCAN_H
#include "extra_options.h"
/* Photo mode: RB runs the game's photo-collection car detection and credit
   on the current photo camera, without taking a picture. INI: [PhotoMode]
   QuickCarScan (default 0). */
void photo_scan_install(unsigned char *game_image);
#endif
