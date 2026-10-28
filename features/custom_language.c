#include "custom_language.h"
#include "voice_subtitles.h"
#include "logging.h"
#include "support.h"

/* Azimuth language code -> the game's string table archive
   (media\stripped\stringtables\<archive>.zip). */
static const struct {
    const wchar_t *slot, *archive;
} slots[] = {{L"en-US", L"EN"}, {L"en-GB", L"GB"}, {L"de-DE", L"DE"}, {L"fr-FR", L"FR"},
             {L"it-IT", L"IT"}, {L"es-ES", L"ES"}, {L"es-MX", L"MX"}, {L"pt-BR", L"br"},
             {L"pl-PL", L"PL"}, {L"ru-RU", L"RU"}, {L"ja-JP", L"JP"}, {L"ko-KR", L"KO"},
             {L"zh-TW", L"CHT"}, {L"zh-HK", L"CHT"}, {L"nl-NL", L"nl"}, {L"cs-CZ", L"cz"},
             {L"hu-HU", L"HU"}};

#define PACK_ID_MAX 32

static wchar_t pack_directory[PATH_CAPACITY], launcher_ini[PATH_CAPACITY];
static wchar_t pack_name[64], pack_slot[16];
static const wchar_t *pack_archive;
static char pack_id[PACK_ID_MAX + 1], mount_path[64];
static const char *problem;

static void append(char *output, size_t capacity, const char *text) {
    size_t used = text_length(output);
    while (*text && used + 1 < capacity) {
        output[used++] = *text++;
    }
    output[used] = 0;
}

static void log_wide(const char *label, const wchar_t *text, const char *tail) {
    char buffer[256];
    buffer[0] = 0;
    append(buffer, sizeof(buffer), label);
    size_t used = text_length(buffer);
    int written = WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer + used,
                                      (int)(sizeof(buffer) - used - 64), NULL, NULL);
    buffer[written > 0 ? used + (size_t)written - 1 : used] = 0;
    append(buffer, sizeof(buffer), tail);
    log_append(buffer);
}

static int id_character(unsigned c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_';
}

/* GetPrivateProfileString keeps "; comment" tails; drop them as settings.c does. */
static void strip_comment(wchar_t *text) {
    unsigned length = 0;
    while (text[length] && text[length] != L';' && text[length] != L'#') {
        ++length;
    }
    while (length && (text[length - 1] == L' ' || text[length - 1] == L'\t')) {
        --length;
    }
    text[length] = 0;
}

int custom_language_valid_id(const char *id) {
    size_t length = 0;
    for (; id[length]; ++length) {
        if (length >= PACK_ID_MAX || !id_character((unsigned char)id[length])) {
            return 0;
        }
    }
    return length > 0;
}

void custom_language_prepare(const wchar_t *executable_path, const wchar_t *settings_directory) {
    static wchar_t path[PATH_CAPACITY];
    wchar_t id[64];
    mount_path[0] = 0;
    pack_id[0] = 0;
    pack_archive = NULL;
    problem = NULL;
    if (!settings_directory || !executable_path || !path_join(path, settings_directory, MOD_INI_NAME) ||
        !GetPrivateProfileStringW(L"Language", L"Custom", L"", id, ARRAY_COUNT(id), path)) {
        return;
    }
    strip_comment(id);
    if (!id[0]) {
        return;
    }
    /* The id names a folder and becomes part of a game path: letters, digits, - and _. */
    unsigned length = 0;
    for (; id[length]; ++length) {
        if (length >= PACK_ID_MAX || !id_character(id[length])) {
            problem = "LANGUAGE: [Language] Custom must be a folder name of letters, digits, - or _; "
                      "custom language off.\r\n";
            return;
        }
        pack_id[length] = (char)id[length];
    }
    pack_id[length] = 0;
    /* <game>\Mount is the folder of the game EXE; game:\ is the same folder. */
    unsigned folder = 0;
    for (unsigned i = 0; executable_path[i] && i + 1 < PATH_CAPACITY; ++i) {
        path[i] = executable_path[i];
        if (path[i] == L'\\' || path[i] == L'/') {
            folder = i + 1;
        }
    }
    path[folder] = 0;
    wchar_t relative[16 + PACK_ID_MAX];
    unsigned used = 10;
    memory_copy(relative, L"languages\\", used * sizeof(wchar_t));
    for (unsigned i = 0; id[i]; ++i) {
        relative[used++] = id[i];
    }
    relative[used] = 0;
    if (!folder || !path_join(pack_directory, path, relative) ||
        !path_join(path, pack_directory, L"language.ini") ||
        GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        problem = "LANGUAGE: the custom language pack has no languages\\<id>\\language.ini next to the "
                  "game EXE; custom language off.\r\n";
        return;
    }
    pack_slot[0] = pack_name[0] = 0;
    GetPrivateProfileStringW(L"Language", L"Slot", L"", pack_slot, ARRAY_COUNT(pack_slot), path);
    GetPrivateProfileStringW(L"Language", L"Name", L"", pack_name, ARRAY_COUNT(pack_name), path);
    strip_comment(pack_slot);
    strip_comment(pack_name);
    for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
        if (!lstrcmpiW(pack_slot, slots[i].slot)) {
            pack_archive = slots[i].archive;
        }
    }
    if (!pack_archive) {
        problem = "LANGUAGE: language.ini [Language] Slot is not a game language code such as ru-RU; "
                  "custom language off.\r\n";
        return;
    }
    if (!pack_name[0]) {
        for (unsigned i = 0; i <= length; ++i) {
            pack_name[i] = id[i];
        }
    }
    if (!path_join(launcher_ini, settings_directory, L"data\\azimuth.ini")) {
        launcher_ini[0] = 0;
    }
    append(mount_path, sizeof(mount_path), "game:\\languages\\");
    append(mount_path, sizeof(mount_path), pack_id);
    append(mount_path, sizeof(mount_path), "\\");
}

const char *custom_language_mount(void) {
    return mount_path[0] ? mount_path : NULL;
}

void custom_language_install(unsigned char *game_image) {
    static wchar_t stringtables[PATH_CAPACITY];
    if (problem) {
        log_append(problem);
    }
    if (!mount_path[0]) {
        return;
    }
    log_wide("LANGUAGE: custom language ", pack_name, " (languages\\");
    log_append(pack_id);
    log_wide(") replaces the ", pack_slot, " slot; its files are mounted below mediaoverride.\r\n");
    /* The launcher's language decides the slot the game runs in ("system" or
       empty, and without an Azimuth launcher: the Windows locale). */
    wchar_t language[LOCALE_NAME_MAX_LENGTH];
    language[0] = 0;
    if (launcher_ini[0]) {
        GetPrivateProfileStringW(L"launcher", L"language", L"", language, ARRAY_COUNT(language),
                                 launcher_ini);
        strip_comment(language);
    }
    if (!language[0] || !lstrcmpiW(language, L"system")) {
        if (!GetUserDefaultLocaleName(language, ARRAY_COUNT(language))) {
            language[0] = 0;
        }
    }
    if (lstrcmpiW(language, pack_slot)) {
        log_wide("LANGUAGE: the launcher runs ", language[0] ? language : L"an unknown language",
                 "; choose the pack's slot there to use it. Voice subtitles are off.\r\n");
        return;
    }
    if (path_join(stringtables, pack_directory, L"stripped\\stringtables")) {
        voice_subtitles_install(game_image, stringtables, pack_archive);
    }
}

#ifdef FH2_TWEAKS_TEST
/* Runs custom_language_prepare and describes the result in UTF-8:
   "mount=<game path>;slot=<slot>;archive=<archive>;name=<name>" or
   "problem=<log line>", or "" when off. Returns 1 when a pack is mounted. */
__declspec(dllexport) int Fh2TestCustomLanguage(const wchar_t *executable_path,
                                                const wchar_t *settings_directory, char *output,
                                                unsigned capacity) {
    custom_language_prepare(executable_path, settings_directory);
    char wide[192];
    output[0] = 0;
    if (!mount_path[0]) {
        append(output, capacity, problem ? "problem=" : "");
        append(output, capacity, problem ? problem : "");
        return 0;
    }
    append(output, capacity, "mount=");
    append(output, capacity, mount_path);
    const wchar_t *fields[] = {pack_slot, pack_archive, pack_name};
    const char *labels[] = {";slot=", ";archive=", ";name="};
    for (unsigned i = 0; i < ARRAY_COUNT(fields); ++i) {
        if (WideCharToMultiByte(CP_UTF8, 0, fields[i], -1, wide, sizeof(wide), NULL, NULL) <= 0) {
            wide[0] = 0;
        }
        append(output, capacity, labels[i]);
        append(output, capacity, wide);
    }
    return 1;
}
#endif
