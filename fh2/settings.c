#include "settings.h"
#include "features.h"
#include "support.h"
#include "features/bindings.h"

#define INI_MAX_SIZE 8192
#define SETTING(section, key, field, type) {section, key, key, offsetof(ModOptions, field), type}

const SettingDefinition setting_definitions[] = {
    SETTING("Global", "Enabled", enabled, SETTING_BOOLEAN),
    SETTING("Content", "UnlockDLC", unlock_dlc, SETTING_BOOLEAN),
    SETTING("Content", "OfflineVIP", offline_vip, SETTING_BOOLEAN),
    SETTING("Experiments", "LocalDrivatarLiveries", local_drivatar_liveries, SETTING_BOOLEAN),
    SETTING("Experiments", "MaxDrivatarLiveries", max_drivatar_liveries, SETTING_LIVERY_COUNT),
    SETTING("Frontend", "SkipIntro", skip_intro, SETTING_BOOLEAN),
    SETTING("Frontend", "CarsNextToHome", cars_next_to_home, SETTING_BOOLEAN),
    SETTING("Frontend", "MenuTurboIntervalMs", menu_turbo_interval_ms, SETTING_TURBO_INTERVAL),
    SETTING("Frontend", "AllRimsCategory", all_rims_category, SETTING_BOOLEAN),
    SETTING("Experiments", "AllEngineSwaps", all_engine_swaps, SETTING_BOOLEAN),
    SETTING("Experiments", "AllTransmissionSwaps", all_transmission_swaps, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableMarketplace", enable_marketplace, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableSocial", enable_social, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableClubs", enable_clubs, SETTING_BOOLEAN),
    SETTING("Frontend", "EnableRivals", enable_rivals, SETTING_BOOLEAN),
    SETTING("Frontend", "GetInCarFromPauseMenu", garage_from_pause, SETTING_BOOLEAN),
    SETTING("Content", "UnlockStormIslandCars", unlock_storm_island_cars, SETTING_BOOLEAN),
    SETTING("Content", "UnlockTreasureMap", unlock_treasure_map, SETTING_BOOLEAN),
    SETTING("Compatibility", "DisableParentalLocks", disable_parental_locks, SETTING_BOOLEAN),
    SETTING("Compatibility", "DisableAzimuthSaveChecksums", disable_azimuth_save_checksums,
            SETTING_BOOLEAN),
    SETTING("Profile", "CustomGamerpic", custom_gamerpic, SETTING_BOOLEAN),
    SETTING("PhotoMode", "FreeCamera", free_photo_camera, SETTING_BOOLEAN),
    SETTING("Files", "EnableMediaOverrides", media_overrides, SETTING_BOOLEAN),
    SETTING("Experiments", "UnlockAeroTuning", unlock_aero_tuning, SETTING_BOOLEAN),
    {"Autodrive", "Enabled", "AutodriveEnabled", offsetof(ModOptions, autodrive), SETTING_BOOLEAN},
};

static const SettingDefinition legacy_settings[] = {
    SETTING("Frontend", "AllEngineSwaps", all_engine_swaps, SETTING_BOOLEAN),
    SETTING("Gameplay", "UnlockAeroTuning", unlock_aero_tuning, SETTING_BOOLEAN),
    SETTING("Livery", "Enable4K", enable_4k_liveries, SETTING_BOOLEAN),
    SETTING("Livery", "IntermediateMSAA", livery_msaa, SETTING_MSAA),
    SETTING("Livery", "ExtraSupersampling", livery_supersampling, SETTING_BOOLEAN),
    SETTING("Frontend", "SnapToRoadOnCarChange", snap_car_to_road, SETTING_BOOLEAN),
    SETTING("Frontend", "SkipCarMeetOnStartup", skip_car_meet_on_startup, SETTING_BOOLEAN),
    SETTING("Frontend", "SpawnAtFestival", spawn_at_festival, SETTING_BOOLEAN),
    SETTING("Frontend", "SkipIneligibleCarDialogue", skip_ineligible_car_dialogue, SETTING_BOOLEAN),
    SETTING("Graphics", "SmoothSunShadows", smooth_sun_shadows, SETTING_BOOLEAN),
    SETTING("HUD", "MinimapFollowsCamera", minimap_follows_camera, SETTING_BOOLEAN),
    SETTING("PhotoMode", "Samples", photo_mode_samples, SETTING_PHOTO_SAMPLES),
    {"Autodrive", "Cinematic", "AutodriveCinematic", offsetof(ModOptions, autodrive_cinematic),
     SETTING_BOOLEAN},
};

const size_t setting_count = ARRAY_COUNT(setting_definitions);
static char ini_buffer[INI_MAX_SIZE + 1];
static wchar_t ini_path[PATH_CAPACITY];

void settings_defaults(ModOptions *options) {
    *options = (ModOptions){
        .enabled = 1,
        .unlock_dlc = 1,
        .offline_vip = 1,
        .skip_intro = 1,
        .enable_marketplace = 1,
        .enable_4k_liveries = 0,
        .livery_msaa = 2,
        .livery_supersampling = 0,
        .garage_from_pause = 1,
        .snap_car_to_road = 1,
        .unlock_storm_island_cars = 1,
        .disable_parental_locks = 1,
        .custom_gamerpic = 1,
        .free_photo_camera = 1,
        .unlock_treasure_map = 0,
        .autodrive = 0,
        .autodrive_cinematic = 1,
        .disable_azimuth_save_checksums = 1,
        .smooth_sun_shadows = -1,
        .skip_ineligible_car_dialogue = 0,
        .minimap_follows_camera = 1,
        .media_overrides = 0,
        .spawn_at_festival = 0,
        .skip_car_meet_on_startup = 0,
        .all_rims_category = 0,
        .all_engine_swaps = 0,
        .all_transmission_swaps = 0,
        .cars_next_to_home = 1,
        .local_drivatar_liveries = 1,
        .max_drivatar_liveries = 6,
        .menu_turbo_interval_ms = 100,
        .drift_camera = 1,
        .enable_social = 1,
        .enable_clubs = 1,
        .enable_rivals = 1,
        .photo_mode_samples = 0,
        .unlock_aero_tuning = 0,
    };
}

int settings_value(const ModOptions *options, const SettingDefinition *definition) {
    const unsigned char *fields = (const unsigned char *)options;
    return *(const int *)(fields + definition->field_offset);
}

static char *trim_whitespace(char *text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    size_t length = text_length(text);
    while (length &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r')) {
        text[--length] = 0;
    }
    return text;
}

static int known_section(const char *name) {
    if (text_equal_ignore_case(name, "Gameplay") || text_equal_ignore_case(name, "Camera") ||
        text_equal_ignore_case(name, "Graphics") || text_equal_ignore_case(name, "Livery") ||
        text_equal_ignore_case(name, "HUD") || text_equal_ignore_case(name, "Bindings") ||
        text_equal_ignore_case(name, "General") || text_equal_ignore_case(name, "Language")) {
        return 1;
    }
    for (size_t index = 0; index < setting_count; ++index) {
        if (text_equal_ignore_case(name, setting_definitions[index].section)) {
            return 1;
        }
    }
    return 0;
}

static const SettingDefinition *find_setting(const char *section, const char *key) {
    if (text_equal_ignore_case(section, "General")) {
        section = "Global";
    }
    for (size_t index = 0; index < setting_count; ++index) {
        const SettingDefinition *definition = &setting_definitions[index];
        if (text_equal_ignore_case(section, definition->section) &&
            text_equal_ignore_case(key, definition->key)) {
            return definition;
        }
    }
    for (unsigned index = 0; index < ARRAY_COUNT(legacy_settings); ++index) {
        const SettingDefinition *definition = &legacy_settings[index];
        if (text_equal_ignore_case(section, definition->section) &&
            text_equal_ignore_case(key, definition->key)) {
            return definition;
        }
    }
    return NULL;
}

static int parse_value(const SettingDefinition *definition, const char *text, int *value) {
    if (definition->type == SETTING_TURBO_INTERVAL) {
        unsigned interval = 0;
        if (!*text) return 0;
        for (; *text; ++text) {
            if (*text < '0' || *text > '9' || interval > 1000) return 0;
            interval = interval * 10 + (unsigned)(*text - '0');
        }
        if (interval > 1000 || (interval && interval < 20)) return 0;
        *value = (int)interval;
        return 1;
    }
    if (definition->type == SETTING_LIVERY_COUNT) {
        unsigned count = 0;
        if (!*text) return 0;
        for (; *text; ++text) {
            if (*text < '0' || *text > '9' || count > 32) return 0;
            count = count * 10 + (unsigned)(*text - '0');
        }
        if (count > 32) return 0;
        *value = (int)count;
        return 1;
    }
    if (definition->type == SETTING_PHOTO_SAMPLES) {
        unsigned samples = 0;
        if (!*text) {
            return 0;
        }
        for (const char *digit = text; *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || samples > 4096) {
                return 0;
            }
            samples = samples * 10 + (unsigned)(*digit - '0');
        }
        if (samples > 4096 || (samples && samples < 16)) {
            return 0;
        }
        *value = (int)samples;
        return 1;
    }
    if (definition->type == SETTING_MSAA) {
        if (text_equal_ignore_case(text, "2")) {
            *value = 2;
            return 1;
        }
        if (text_equal_ignore_case(text, "4")) {
            *value = 4;
            return 1;
        }
        return 0;
    }
    if (text_equal_ignore_case(text, "0") || text_equal_ignore_case(text, "1")) {
        *value = text[0] - '0';
        return 1;
    }
    return 0;
}

static int parse_ini(char *text, ModOptions *options) {
    char section[32] = {0};
    if ((unsigned char)text[0] == 0xef && (unsigned char)text[1] == 0xbb &&
        (unsigned char)text[2] == 0xbf) {
        text += 3;
    }

    while (*text) {
        char *line = text;
        while (*text && *text != '\n') {
            ++text;
        }
        if (*text) {
            *text++ = 0;
        }
        for (char *cursor = line; *cursor; ++cursor) {
            if (*cursor == ';' || *cursor == '#') {
                *cursor = 0;
                break;
            }
        }
        line = trim_whitespace(line);
        if (!*line) {
            continue;
        }

        if (*line == '[') {
            char *closing = line;
            while (*closing && *closing != ']') {
                ++closing;
            }
            if (*closing != ']' || *trim_whitespace(closing + 1)) {
                goto invalid;
            }
            *closing = 0;
            char *name = trim_whitespace(line + 1);
            size_t length = text_length(name);
            if (length >= sizeof(section) || !known_section(name)) {
                goto invalid;
            }
            memory_copy(section, name, length + 1);
            continue;
        }

        char *separator = line;
        while (*separator && *separator != '=') {
            ++separator;
        }
        if (!*separator) {
            goto invalid;
        }
        *separator = 0;
        char *key = trim_whitespace(line);
        char *value = trim_whitespace(separator + 1);

        if (text_equal_ignore_case(section, "Frontend") &&
            text_equal_ignore_case(key, "HideOnlineTabs")) {
            if (!text_equal_ignore_case(value, "0") && !text_equal_ignore_case(value, "1")) {
                goto invalid;
            }
            continue;
        }

        // Retired keys are ignored so an older INI still loads.
        if ((text_equal_ignore_case(section, "Language") &&
             (text_equal_ignore_case(key, "Text") || text_equal_ignore_case(key, "Audio"))) ||
            (text_equal_ignore_case(section, "Graphics") &&
             (text_equal_ignore_case(key, "ForceAnisoFiltering") ||
              text_equal_ignore_case(key, "MotionBlurSamples")))) {
            continue;
        }

        if (text_equal_ignore_case(section, "Camera") &&
            (text_equal_ignore_case(key, "DriftCamera") ||
             text_equal_ignore_case(key, "DriftCameraStiffness") ||
             text_equal_ignore_case(key, "FirstPersonDriftCameraStiffness"))) {
            continue;
        }

        if (text_equal_ignore_case(section, "Bindings")) {
            if (bindings_parse(key, value) != 1) {
                goto invalid;
            }
            continue;
        }
        const SettingDefinition *definition = find_setting(section, key);
        int parsed;
        if (!definition || !parse_value(definition, value, &parsed)) {
            goto invalid;
        }
        int *field = (int *)((unsigned char *)options + definition->field_offset);
        *field = parsed;
    }
    return 1;

invalid:
    status_set("REFUSED: invalid INI section, option, or value. See the supplied "
               "FH2_ExtraOptions.ini.");
    return 0;
}

int settings_read(const wchar_t *directory, ModOptions *options) {
    bindings_reset();
    settings_defaults(options);
    if (!path_join(ini_path, directory, MOD_INI_NAME)) {
        status_set("REFUSED: INI path is too long.");
        return 0;
    }
    HANDLE file = CreateFileW(ini_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return 1;
        }
        status_set("REFUSED: cannot read FH2_ExtraOptions.ini.");
        return 0;
    }
    LARGE_INTEGER size;
    DWORD bytes_read;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > INI_MAX_SIZE ||
        !ReadFile(file, ini_buffer, (DWORD)size.QuadPart, &bytes_read, NULL) ||
        bytes_read != size.QuadPart) {
        CloseHandle(file);
        status_set("REFUSED: INI read failed or file exceeds 8192 bytes.");
        return 0;
    }
    CloseHandle(file);
    ini_buffer[bytes_read] = 0;
    for (unsigned index = 0; index < bytes_read; ++index) {
        if (!ini_buffer[index]) {
            status_set("REFUSED: INI must use UTF-8 or ASCII, not UTF-16.");
            return 0;
        }
    }
    return parse_ini(ini_buffer, options);
}
