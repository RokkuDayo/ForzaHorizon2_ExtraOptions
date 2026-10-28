#ifndef FH2_CAR_PREVIEWS_H
#define FH2_CAR_PREVIEWS_H
#include "extra_options.h"
/* Garage thumbnails at 2, 3 or 4 times the console's size ([Graphics]
   CarPreviewScale; 1, the default, leaves the game alone); applies to
   thumbnails the game makes after startup (a car bought, painted or upgraded). */
int car_previews_install(unsigned char *image, unsigned scale);
#endif
