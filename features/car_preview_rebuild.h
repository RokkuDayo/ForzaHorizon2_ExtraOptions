#ifndef FH2_CAR_PREVIEW_REBUILD_H
#define FH2_CAR_PREVIEW_REBUILD_H
#include "extra_options.h"
/* Re-render the garage thumbnails of all cars with a saved one ([Bindings]
   RebuildCarPreviews, F8 by default). */
void car_preview_rebuild_install(unsigned char *game_image);
void car_preview_rebuild_start(void);
int car_preview_rebuild_running(void);
#endif
