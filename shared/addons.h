#ifndef FH2_ADDONS_H
#define FH2_ADDONS_H
#include "shared/addon_json.h"
#include "shared/extra_options.h"
#include "shared/addon_database.h"

void addons_configure(const wchar_t *game_executable);
int addons_layout_prepare(void);
void addons_layout_log(void);
int addons_cache_folder(wchar_t *out,const wchar_t *leaf);
void addons_cache_configure(const wchar_t *directory,int enabled,int fm5);
void addons_configure_fm5(const wchar_t *game_executable);
void addons_merge_database(void **connection);
void addons_database_profile(unsigned char *base,const AddonSqlite *api);
int addons_is_fm5(void);
int addons_configured(void);
void addons_prepare_hooks(unsigned char *image);
unsigned addons_count(void);
const wchar_t *addons_directory(unsigned index);
const JsonValue *addons_strings_document(unsigned index);
const JsonValue *addons_database_document(unsigned index);
const JsonValue *addons_manifest_document(unsigned index);
unsigned addons_media_revision(void);
const wchar_t *addons_media_folder(unsigned index);
int addons_media_active(unsigned index);
const wchar_t *addons_content_directory(void);
extern PatchDefinition addons_hook_patches[4];
#endif

int addons_physical_path(const wchar_t *path);
