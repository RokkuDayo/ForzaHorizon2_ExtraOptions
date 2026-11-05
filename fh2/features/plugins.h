#ifndef FH2_PLUGINS_H
#define FH2_PLUGINS_H
#include "extra_options.h"
void plugins_configure(unsigned char *game_image, const wchar_t *game_executable, int enabled);
wchar_t *plugins_media_file(unsigned addon,const char *relative,size_t length);
int plugins_configured(void);
void plugins_start(void);
void plugins_database_ready(void *database);
void plugins_vehicle_update(void *car, float dt);
#endif
