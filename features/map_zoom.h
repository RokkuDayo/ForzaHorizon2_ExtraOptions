#ifndef FH2_MAP_ZOOM_H
#define FH2_MAP_ZOOM_H
#include "extra_options.h"

/* [Frontend] MapStartZoom / MapMaxZoomOut: how far out the pause (world)
   map opens and how far it can be zoomed out, in percent of the game's own
   distances (100, the default, = the game's value). MapIslandStartZoom /
   MapIslandMaxZoomOut: the same for the Storm Island map. With all four at
   100 nothing is installed. */
void map_zoom_config_defaults(void);
/* Returns -1 when the key is not a map zoom key, 0 for an invalid value and
   1 when the value was taken. */
int map_zoom_parse(const char *key, const char *value);
void map_zoom_install(unsigned char *game_image);

#endif
