#ifndef FH2_DRIVER_OPTIONS_H
#define FH2_DRIVER_OPTIONS_H
#include "extra_options.h"
extern PatchDefinition driver_options_patches[5];
void driver_options_prepare(unsigned char *base);
void driver_models_database(void *database);
unsigned driver_model_count(void);
unsigned driver_model_revision(void);
void driver_model_label(unsigned index, wchar_t output[128]);
int driver_model_index(int id);
int driver_model_id(unsigned index);
int driver_photo_get(void);
int driver_photo_set(int off);
void driver_photo_reset(void);
void video_options_photo_refresh(void *page);
int driver_photo_is_xaml(const char *text, size_t length);
size_t driver_photo_xaml(const char *text, size_t length, char *out, size_t capacity);
#endif
