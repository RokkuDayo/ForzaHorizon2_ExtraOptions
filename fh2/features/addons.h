#ifndef FH2_ADDONS_H
#define FH2_ADDONS_H
#include "addon_json.h"
#include "extra_options.h"

void addons_configure(const wchar_t *game_executable);
int addons_configured(void);
void addons_prepare_hooks(unsigned char *image);
unsigned addons_count(void);
const wchar_t *addons_directory(unsigned index);
const JsonValue *addons_strings_document(unsigned index);
unsigned addons_media_revision(void);
const wchar_t *addons_media_folder(unsigned index);
int addons_media_active(unsigned index);
const wchar_t *addons_content_directory(void);
extern PatchDefinition addons_hook_patches[4];
#endif
