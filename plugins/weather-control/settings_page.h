#ifndef TIMELOCK_SETTINGS_PAGE_H
#define TIMELOCK_SETTINGS_PAGE_H
#include <stddef.h>

#include "config.h"

// Adds "Time of Day Lock" (On/Off) and "Locked Hour" (slider) after the
// Brightness and Subtitles rows of Settings > Video (the A_VideoOptions page),
// bound to native properties on the
// page's type. Chains onto Extra Options' UI resource hook. Returns 1 when
// both the properties and the XAML edit are in place.
// `dump_path`: where the page's text (as received, before our edit) is saved
// once for checking; NULL to skip.
// `weather`: also add the Weather row (only when the weather hold is ready).
int settings_page_install(unsigned char *image, size_t image_size, LogFn log, const wchar_t *dump_path, int weather);

// Offline pieces: the guard checks, and the text edit (returns the new length,
// or 0 when the text isn't the Video page or an anchor doesn't match exactly).
int settings_page_verify(const unsigned char *image, size_t image_size, LogFn log);
size_t settings_page_edit(const char *text, size_t length, char *out, size_t capacity);
int settings_page_is_xaml(const char *text, size_t length);

void settings_page_test_bind(void *base);
void settings_page_refresh(void *page);

#endif
