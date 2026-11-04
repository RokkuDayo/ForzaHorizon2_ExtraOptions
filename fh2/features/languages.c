#include "game_addresses.h"
#include "features.h"
#include "support.h"
#include "logging.h"

const char *const language_codes[LANGUAGE_COUNT] = {"EN", "EN", "GB", "JP", "DE",  "FR",
                                                    "MX", "ES", "IT", "KO", "CHT", "BR",
                                                    "CZ", "HU", "PL", "RU", "NL"};
static unsigned char *language_game_image;
/* xg.dll can arrive after the language managers were constructed. Update only
   validated existing state; native resource work runs in the normal load hook. */
#define LANGUAGE_RELOAD_SLOT_RVA 0x2eb0dd0u
#define LANGUAGE_RELOAD_RVA 0x1245740u
typedef void (*LanguageReloadFn)(void *self);
typedef void (*LanguageLocaleFn)(void *self, int index);
typedef void (*LanguageSetCodeFn)(void *self, const char *code);
typedef void (*LanguageRefreshFn)(void);

typedef struct {
    unsigned char *address;
    unsigned size;
    unsigned char original[32];
} OriginalLanguageField;

typedef struct {
    unsigned char *address;
    unsigned size;
    unsigned char before[32], target[32];
} LanguageFieldEdit;

static OriginalLanguageField language_original_fields[4];
static LanguageFieldEdit language_edits[4];
static unsigned language_edit_count;
static unsigned char *language_objects_image;
static int selected_text_language = -1;
static char language_diagnostics[512];
static LanguageReloadFn original_language_reload;
static LanguageLocaleFn language_set_locale;
static LanguageSetCodeFn language_set_code;
static LanguageRefreshFn language_refresh;
static unsigned char language_original_pointer[8], language_target_pointer[8];

static int language_code_index(const char *code) {
    for (int i = 0; i < LANGUAGE_COUNT; ++i) {
        if (i != 1 && text_equal_ignore_case(code, language_codes[i])) {
            return i;
        }
    }
    return -1;
}

static int language_index_from_small_string(const unsigned char bytes[32]) {
    size_t length, capacity;
    memory_copy(&length, bytes + 16, 8);
    memory_copy(&capacity, bytes + 24, 8);
    if (capacity != 15 || length < 2 || length > 3 || bytes[length] != 0) {
        return -1;
    }
    char code[4] = {0};
    memory_copy(code, bytes, length);
    return language_code_index(code);
}

static void language_make_small_string(unsigned char bytes[32], int language_index) {
    for (unsigned i = 0; i < 32; ++i) {
        bytes[i] = 0;
    }
    size_t length = text_length(language_codes[language_index]), capacity = 15;
    memory_copy(bytes, language_codes[language_index], length);
    memory_copy(bytes + 16, &length, 8);
    memory_copy(bytes + 24, &capacity, 8);
}

static void language_append_diagnostic(const char *s) {
    size_t n = text_length(language_diagnostics), count = text_length(s);
    if (count >= sizeof(language_diagnostics) - n) {
        count = sizeof(language_diagnostics) - n - 1;
    }
    memory_copy(language_diagnostics + n, s, count);
    language_diagnostics[n + count] = 0;
}

static int language_stage_edit(unsigned slot, unsigned char *address, const void *target,
                               unsigned size) {
    if (size > 32 || language_edit_count >= 4 || !memory_is_writable(address, size)) {
        return 0;
    }
    LanguageFieldEdit *edit = &language_edits[language_edit_count];
    if (!memory_read(address, edit->before, size)) {
        return 0;
    }
    OriginalLanguageField *original_field = &language_original_fields[slot];
    if (original_field->address != address) {
        original_field->address = address;
        original_field->size = size;
        memory_copy(original_field->original, edit->before, size);
    }
    edit->address = address;
    edit->size = size;
    memory_copy(edit->target, target ? target : original_field->original, size);
    ++language_edit_count;
    return 1;
}

static int language_stage_restore(unsigned slot) {
    OriginalLanguageField *saved = &language_original_fields[slot];
    return !saved->address || language_stage_edit(slot, saved->address, 0, saved->size);
}

static int language_stage_text_override(unsigned char *game_image, unsigned char *manager,
                                        unsigned char *text_tables, const ModOptions *options) {
    if (options->text_language >= 0) {
        if (manager) {
            int current;
            char code[5] = {0};
            if (!memory_read(manager + 0x28, &current, 4) || current < 0 ||
                current >= LANGUAGE_COUNT ||
                !memory_read(manager + 0x38 + (size_t)current * 0xe0, code, 4) ||
                !text_equal_ignore_case(code, language_codes[current])) {
                return 0;
            }
            if (!language_stage_edit(0, manager + 0x28, &options->text_language, 4)) {
                return 0;
            }
            language_append_diagnostic("LANGUAGE: existing text ");
            language_append_diagnostic(language_codes[current]);
            language_append_diagnostic(" -> ");
            language_append_diagnostic(language_codes[options->text_language]);
            language_append_diagnostic(".\r\n");
        } else {
            language_append_diagnostic(
                "LANGUAGE: text manager not yet constructed; constructor override ready.\r\n");
        }
        if (text_tables) {
            uintptr_t vtable;
            unsigned char small_string[32];
            if (!memory_read(text_tables, &vtable, 8) ||
                vtable != (uintptr_t)(game_image + GAME_RVA_TEXT_TABLES_VTABLE) ||
                !memory_read(text_tables + 0x30, small_string, 32) ||
                language_index_from_small_string(small_string) < 0) {
                return 0;
            }
            int current = language_index_from_small_string(small_string);
            language_make_small_string(small_string, options->text_language);
            if (!language_stage_edit(1, text_tables + 0x30, small_string, 32)) {
                return 0;
            }
            language_append_diagnostic("LANGUAGE: existing table code ");
            language_append_diagnostic(language_codes[current]);
            language_append_diagnostic(" -> ");
            language_append_diagnostic(language_codes[options->text_language]);
            language_append_diagnostic("; native reload hook ready.\r\n");
        }
    } else if (!language_stage_restore(0) || !language_stage_restore(1)) {
        return 0;
    }
    return 1;
}

static int language_stage_audio_override(unsigned char *game_image, unsigned char *audio_engine,
                                         const ModOptions *options) {
    int audio_selection = options->audio_language;
    if (audio_selection < 0 && options->text_language >= 0) {
        audio_selection =
            language_supports_audio(options->text_language) ? options->text_language : 0;
    }
    if (audio_selection >= 0) {
        if (audio_engine) {
            uintptr_t vtable;
            unsigned char small_string[32];
            int speech_id;
            if (!memory_read(audio_engine, &vtable, 8) ||
                (vtable != (uintptr_t)(game_image + GAME_RVA_AUDIO_ENGINE_VTABLE) &&
                 vtable != (uintptr_t)(game_image + GAME_RVA_AUDIO_ENGINE_ALT_VTABLE)) ||
                !memory_read(audio_engine + 0x428, small_string, 32) ||
                !memory_read(audio_engine + 0x448, &speech_id, 4)) {
                return 0;
            }
            int current = language_index_from_small_string(small_string);
            static const int speech_bank_ids[LANGUAGE_COUNT] = {0, 0, 0, 6, 1, 4, 3, 2, 5,
                                                                0, 0, 8, 0, 0, 7, 9, 0};
            if (current < 0 || !language_supports_audio(current) ||
                speech_id != speech_bank_ids[current]) {
                return 0;
            }
            language_make_small_string(small_string, audio_selection);
            speech_id = speech_bank_ids[audio_selection];
            if (!language_stage_edit(2, audio_engine + 0x428, small_string, 32) ||
                !language_stage_edit(3, audio_engine + 0x448, &speech_id, 4)) {
                return 0;
            }
            language_append_diagnostic("LANGUAGE: existing audio ");
            language_append_diagnostic(language_codes[current]);
            language_append_diagnostic(" -> ");
            language_append_diagnostic(language_codes[audio_selection]);
            language_append_diagnostic(" (bank suffix and speech ID).\r\n");
        } else {
            language_append_diagnostic(
                "LANGUAGE: audio engine not yet constructed; constructor override ready.\r\n");
        }
    } else if (!language_stage_restore(2) || !language_stage_restore(3)) {
        return 0;
    }
    return 1;
}

int language_prepare_existing_objects(unsigned char *game_image, const ModOptions *options) {
    language_edit_count = 0;
    language_diagnostics[0] = 0;
    if (language_objects_image != game_image) {
        for (unsigned i = 0; i < 4; ++i) {
            language_original_fields[i].address = 0;
        }
        language_objects_image = game_image;
    }
    unsigned char *manager, *text_tables, *audio_engine;
    memory_copy(&manager, game_image + GAME_RVA_LOCALE_MANAGER, 8);
    memory_copy(&text_tables, game_image + GAME_RVA_TEXT_TABLES, 8);
    memory_copy(&audio_engine, game_image + GAME_RVA_AUDIO_ENGINE, 8);
    if (language_stage_text_override(game_image, manager, text_tables, options) &&
        language_stage_audio_override(game_image, audio_engine, options)) {
        return 1;
    }
    language_edit_count = 0;
    status_set("REFUSED: unsupported initialized language object. No patches applied.");
    return 0;
}

unsigned language_count_changes(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < language_edit_count; ++i) {
        if (!memory_equal(language_edits[i].before, language_edits[i].target,
                          language_edits[i].size)) {
            ++count;
        }
    }
    return count;
}

void language_write_changes(int rollback) {
    for (unsigned i = 0; i < language_edit_count; ++i) {
        LanguageFieldEdit *edit = &language_edits[i];
        memory_copy(edit->address, rollback ? edit->before : edit->target, edit->size);
    }
}

static void language_reload_hook(void *self) {
    int index = selected_text_language;
    unsigned char *manager, *tables;
    memory_copy(&manager, language_game_image + GAME_RVA_LOCALE_MANAGER, 8);
    memory_copy(&tables, language_game_image + GAME_RVA_TEXT_TABLES, 8);
    if (index >= 0 && manager && self == tables) {
        language_set_locale(manager, index);
        language_set_code(self, language_codes[index]);
        original_language_reload(self);
        language_refresh();
        log_append("LANGUAGE: native text tables reloaded for ");
        log_append(language_codes[index]);
        log_append(".\r\n");
    } else {
        original_language_reload(self);
    }
}

static const unsigned char *const language_hook_accepted[] = {language_original_pointer,
                                                              language_target_pointer};
PatchDefinition language_hook_patch = {LANGUAGE_RELOAD_SLOT_RVA,
                                       0,
                                       8,
                                       GROUP_TEXT_RELOAD,
                                       "language_text_reload_hook",
                                       language_original_pointer,
                                       language_target_pointer,
                                       language_target_pointer,
                                       language_hook_accepted,
                                       2};

void language_prepare_hook(unsigned char *game_image, const ModOptions *options) {
    (void)options;
    language_game_image = game_image;
    uintptr_t original_callback = (uintptr_t)(game_image + LANGUAGE_RELOAD_RVA),
              replacement_callback = (uintptr_t)language_reload_hook;
    memory_copy(language_original_pointer, &original_callback, 8);
    memory_copy(language_target_pointer, &replacement_callback, 8);
    original_language_reload = (LanguageReloadFn)original_callback;
    language_set_locale = (LanguageLocaleFn)(game_image + GAME_RVA_SET_LOCALE);
    language_set_code = (LanguageSetCodeFn)(game_image + GAME_RVA_SET_TEXT_CODE);
    language_refresh = (LanguageRefreshFn)(game_image + GAME_RVA_REFRESH_TEXT);
}

void language_commit_options(const ModOptions *options) {
    selected_text_language = options->text_language;
}

int language_supports_audio(int index) {
    return index == 0 || (index >= 3 && index <= 8) || index == 11 || index == 14 || index == 15;
}

int language_parse_setting(const char *value, int audio, int *setting) {
    if (text_equal_ignore_case(value, "Auto")) {
        *setting = -1;
        return 1;
    }
    for (int i = 0; i < LANGUAGE_COUNT; ++i) {
        if (i != 1 && text_equal_ignore_case(value, language_codes[i]) &&
            (!audio || language_supports_audio(i))) {
            *setting = i;
            return 1;
        }
    }
    return 0;
}

const char *language_diagnostics_get(void) {
    return language_diagnostics;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestResetLanguageState(void) {
    language_objects_image = 0;
    selected_text_language = -1;
}

__declspec(dllexport) void *Fh2TestLanguageHookAddress(void) {
    return (void *)language_reload_hook;
}
#endif
