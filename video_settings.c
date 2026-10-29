#include "video_settings.h"
#include "camera_views.h"
#include "features.h"
#include "logging.h"
#include "support.h"
#include "audio_volume.h"
#include "display_units.h"

typedef struct {
    uint32_t magic, version;
    int values[VIDEO_SETTING_COUNT];
} SavedVideoSettings;

#define SETTINGS_MAGIC 0x32444846u
#define SETTINGS_VERSION 20u
#define DEFAULT_SETTING_VALUES                                                                     \
    {[DRIFT_MODE] = DRIFT_DRIVER_ONLY,                                                             \
     [DRIFT_CHASE_SENSITIVITY] = 35,                                                               \
     [DRIFT_CHASE_SPEED] = 70,                                                                     \
     [DRIFT_CHASE_RANGE] = 80,                                                                     \
     [DRIFT_DRIVER_SENSITIVITY] = 70,                                                              \
     [DRIFT_DRIVER_SPEED] = 55,                                                                    \
     [DRIFT_DRIVER_RANGE] = 65,                                                                    \
     [FOV_CHASE] = 97,                                                                             \
     [FOV_FAR_CHASE] = 116,                                                                        \
     [FOV_DRIVER] = 96,                                                                            \
     [FOV_HOOD] = 132,                                                                             \
     [FOV_BUMPER] = 96,                                                                            \
     [PHOTO_SAMPLES_PRESET] = 0,                                                                   \
     [MINIMAP_ORIENTATION] = 1,                                                                    \
     [AUTODRIVE_CINEMATIC] = 1,                                                                    \
     [SPAWN_LOCATION] = SPAWN_DEFAULT,                                                             \
     [CAR_CHANGE_POSITION] = CAR_POSITION_NEAREST_ROAD,                                            \
     [BEN_PHOTO_DIALOGUE] = 1,                                                                     \
     [ASHLEY_GARAGE_DIALOGUE] = 1,                                                                 \
     [WHEELSPIN_NOTIFICATION] = 1,                                                                 \
     [CHECKPOINT_SOUND] = 1,                                                                       \
     [MASTER_VOLUME] = 100,                                                                        \
     [OPEN_WORLD_DRIVATARS] = 1,                                                                   \
     [SKILL_POINT_NOTIFICATION] = 1, \
     [SKILL_COUNTER_SHAKE] = 1, \
     [SPEED_ZONE_VIGNETTE] = 1, \
     [VIGNETTE_EFFECT] = 1}

static const SavedVideoSettings defaults = {SETTINGS_MAGIC, SETTINGS_VERSION,
                                            DEFAULT_SETTING_VALUES};
static SavedVideoSettings settings = {SETTINGS_MAGIC, SETTINGS_VERSION, DEFAULT_SETTING_VALUES};
_Static_assert(sizeof(SavedVideoSettings) == 168, "Saved settings ABI");

static wchar_t settings_path[PATH_CAPACITY];
static int import_legacy, import_placement, import_livery;
static const int photo_samples[] = {0, 256, 512, 1024, 2048, 4096};

static int retired_setting(unsigned index) {
    return index == RESERVED_MOTION_SLOT || index == RESERVED_FILTERING_SLOT;
}

static int valid_value(unsigned index, int value) {
    if (index >= VIDEO_SETTING_COUNT || value < 0) {
        return 0;
    }
    if (index == MASTER_VOLUME) {
        return value <= 100;
    }
    if (index == POWER_UNITS || index == SPRING_UNITS) return value <= 3;
    if (index == DRIFT_MODE) {
        return value <= DRIFT_OFF;
    }
    if (index < DRIFT_SETTING_COUNT) {
        return value <= 100;
    }
    if (index < CAMERA_SETTING_COUNT) {
        return value >= FOV_MIN_DEGREES * 2 && value <= FOV_MAX_DEGREES * 2;
    }
    if (retired_setting(index)) {
        return value <= 3;
    }
    if (index == PLAYER_CHARACTER) return 1;
    if (index == REALTIME_SHADOWS) {
        return value <= SHADOWS_ON;
    }
    if (index == PHOTO_SAMPLES_PRESET) {
        return value <= 5;
    }
    if (index == SPAWN_LOCATION) {
        return value <= SPAWN_FESTIVAL_SITE;
    }
    return value <= 1;
}

static int nearest_driver_setting(unsigned index, float value) {
    int selected = 0;
    float distance = 1e30f;
    for (int percent = 0; percent <= 100; ++percent) {
        float candidate;
        if (index == DRIFT_DRIVER_SENSITIVITY) {
            candidate = percent * percent / 10000.0f;
        } else if (index == DRIFT_DRIVER_SPEED) {
            float frequency = 2 + percent * 0.1f;
            candidate = frequency * frequency;
        } else {
            candidate = percent * 0.45f;
        }
        float error = candidate - value;
        if (error < 0) {
            error = -error;
        }
        if (error < distance) {
            distance = error;
            selected = percent;
        }
    }
    return selected;
}

static void migrate_driver_settings(SavedVideoSettings *saved) {
    int *values = saved->values;
    if (values[DRIFT_DRIVER_SENSITIVITY] == 50 && values[DRIFT_DRIVER_SPEED] == 100 &&
        values[DRIFT_DRIVER_RANGE] == 20) {
        for (unsigned index = DRIFT_DRIVER_SENSITIVITY; index <= DRIFT_DRIVER_RANGE; ++index) {
            values[index] = defaults.values[index];
        }
        return;
    }
    int speed = values[DRIFT_DRIVER_SPEED];
    float scale = speed <= 50 ? 0.25f + speed * 0.015f : 1 + (speed - 50) * 0.06f;
    values[DRIFT_DRIVER_SENSITIVITY] =
        nearest_driver_setting(DRIFT_DRIVER_SENSITIVITY, values[DRIFT_DRIVER_SENSITIVITY] / 50.0f);
    values[DRIFT_DRIVER_SPEED] = nearest_driver_setting(DRIFT_DRIVER_SPEED, 2.475f * scale);
    values[DRIFT_DRIVER_RANGE] =
        nearest_driver_setting(DRIFT_DRIVER_RANGE, values[DRIFT_DRIVER_RANGE] * 1.2f);
}

static void save_settings(void) {
    if (!settings_path[0]) {
        return;
    }
    HANDLE file = CreateFileW(settings_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    int saved = file != INVALID_HANDLE_VALUE &&
                WriteFile(file, &settings, sizeof(settings), &written, NULL) &&
                written == sizeof(settings);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (!saved) {
        log_append("VIDEO: could not save Video menu choices; current choices "
                   "remain active.\r\n");
    }
}

void video_settings_load(const wchar_t *directory) {
    memory_copy(&settings, &defaults, sizeof(settings));
    display_units_refresh();
    import_legacy = import_placement = import_livery = 1;
    settings_path[0] = 0;
    if (!directory || !path_join(settings_path, directory, L"FH2_ExtraOptions.dat")) {
        return;
    }
    HANDLE file = CreateFileW(settings_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    SavedVideoSettings saved;
    memory_copy(&saved, &defaults, sizeof(saved));
    LARGE_INTEGER size;
    DWORD read = 0;

    static const struct {
        DWORD bytes;
        unsigned settings;
    } layouts[] = {
        {8 + DRIFT_SETTING_COUNT * sizeof(int), DRIFT_SETTING_COUNT},
        {8 + CAMERA_SETTING_COUNT * sizeof(int), CAMERA_SETTING_COUNT},
        {12 + CAMERA_SETTING_COUNT * sizeof(int), CAMERA_SETTING_COUNT},
        {8 + SPAWN_LOCATION * sizeof(int), SPAWN_LOCATION},
        {92, LIVERY_RESOLUTION},
        {92, LIVERY_RESOLUTION},
        {92, LIVERY_RESOLUTION},
        {104, SHOW_ALL_EVENTS},
        {108, AUTODRIVE_EVENTS},
        {112, CUSTOM_SHOWCASES},
        {116, BEN_PHOTO_DIALOGUE},
        {132, MASTER_VOLUME},
        {136, OPEN_WORLD_DRIVATARS},
        {140, SKILL_POINT_NOTIFICATION},
        {144, POWER_UNITS},
        {152, SKILL_COUNTER_SHAKE},
        {156, PLAYER_CHARACTER},
        {160, SPEED_ZONE_VIGNETTE},
        {164, VIGNETTE_EFFECT},
        {sizeof(SavedVideoSettings), VIDEO_SETTING_COUNT},
    };

    int valid = GetFileSizeEx(file, &size) && size.QuadPart >= 8 &&
                size.QuadPart <= (LONGLONG)sizeof(saved) &&
                ReadFile(file, &saved, (DWORD)size.QuadPart, &read, NULL) && read == size.QuadPart;
    CloseHandle(file);
    unsigned count = 0;
    if (valid && saved.magic == SETTINGS_MAGIC && saved.version >= 1 &&
        saved.version <= ARRAY_COUNT(layouts)) {
        count = layouts[saved.version - 1].settings;
        valid = size.QuadPart == layouts[saved.version - 1].bytes;
    } else {
        valid = 0;
    }
    for (unsigned index = 0; valid && index < count; ++index) {
        valid = valid_value(index, saved.values[index]);
        if (index == REALTIME_SHADOWS && saved.version < 7) {
            valid = valid && saved.values[index] <= 1;
        }
    }
    if (valid) {
        import_livery = saved.version < 8;
        import_legacy = saved.version < 4;
        import_placement = saved.version < 5;
        int driver_migration = saved.version < 6;
        int shadow_migration = saved.version < 7;
        if (shadow_migration && !import_legacy) {
            saved.values[REALTIME_SHADOWS] =
                saved.values[REALTIME_SHADOWS] ? SHADOWS_ON : SHADOWS_OFF;
        }
        if (driver_migration) {
            migrate_driver_settings(&saved);
        }
        saved.version = defaults.version;
        if (import_legacy) {
            for (unsigned index = REALTIME_SHADOWS; index < SPAWN_LOCATION; ++index) {
                saved.values[index] = defaults.values[index];
            }
        }
        saved.values[RESERVED_FILTERING_SLOT] = saved.values[RESERVED_MOTION_SLOT] = 0;
        memory_copy(&settings, &saved, sizeof(settings));
        display_units_refresh();
        if ((driver_migration || shadow_migration) && !import_legacy && !import_placement) {
            save_settings();
        }
    }
}

int video_setting_get(unsigned index) {
    return index < VIDEO_SETTING_COUNT ? settings.values[index] : 0;
}

int video_setting_set(unsigned index, int value) {
    if (retired_setting(index) || !valid_value(index, value) || settings.values[index] == value) {
        return 0;
    }
    settings.values[index] = value;
    if (index >= MINIMAP_ORIENTATION && index <= CAR_CHANGE_POSITION) {
        video_settings_refresh_hud();
    }
    if (index == MASTER_VOLUME) {
        audio_volume_refresh();
    }
    if (index == POWER_UNITS || index == SPRING_UNITS) display_units_refresh();
    save_settings();
    return 1;
}

void video_settings_reset(void) {
    memory_copy(&settings, &defaults, sizeof(settings));
    video_settings_refresh_hud();
    audio_volume_refresh();
    display_units_refresh();
    save_settings();
}

static int nearest_preset(const int *presets, unsigned count, int value) {
    int selected = 0, distance = 0x7fffffff;
    for (unsigned index = 0; index < count; ++index) {
        int error = presets[index] - value;
        if (error < 0) {
            error = -error;
        }
        if (error < distance) {
            selected = (int)index;
            distance = error;
        }
    }
    return selected;
}

void video_settings_seed(const ModOptions *legacy) {
    if (!import_legacy && !import_placement && !import_livery) {
        return;
    }
    if (import_legacy) {
        settings.values[REALTIME_SHADOWS] = legacy->smooth_sun_shadows < 0 ? SHADOWS_DEFAULT
                                            : legacy->smooth_sun_shadows   ? SHADOWS_ON
                                                                           : SHADOWS_OFF;
        settings.values[PHOTO_SAMPLES_PRESET] =
            nearest_preset(photo_samples, ARRAY_COUNT(photo_samples), legacy->photo_mode_samples);
        settings.values[MINIMAP_ORIENTATION] = legacy->minimap_follows_camera != 0;
        settings.values[AUTODRIVE_CINEMATIC] = legacy->autodrive_cinematic != 0;
        settings.values[SKIP_CHAMPIONSHIP_DIALOGUE] = legacy->skip_ineligible_car_dialogue != 0;
    }
    if (import_placement) {
        settings.values[SPAWN_LOCATION] = !legacy->skip_car_meet_on_startup ? SPAWN_DEFAULT
                                          : legacy->spawn_at_festival       ? SPAWN_FESTIVAL_SITE
                                                                      : SPAWN_CAR_MEET_ENTRANCE;
        settings.values[CAR_CHANGE_POSITION] =
            legacy->snap_car_to_road ? CAR_POSITION_NEAREST_ROAD : CAR_POSITION_SAME_LOCATION;
    }
    if (import_livery) {
        settings.values[LIVERY_RESOLUTION] = legacy->enable_4k_liveries != 0;
        settings.values[LIVERY_MSAA] = legacy->livery_msaa == 4;
        settings.values[LIVERY_SAMPLING] = legacy->livery_supersampling != 0;
    }
    import_legacy = import_placement = import_livery = 0;
    save_settings();
}

void video_settings_apply(ModOptions *options) {
    options->enable_4k_liveries = settings.values[LIVERY_RESOLUTION];
    options->livery_msaa = settings.values[LIVERY_MSAA] ? 4 : 2;
    options->livery_supersampling = settings.values[LIVERY_SAMPLING];
    options->drift_camera = 1;
    options->smooth_sun_shadows = settings.values[REALTIME_SHADOWS] - 1;
    options->photo_mode_samples = photo_samples[settings.values[PHOTO_SAMPLES_PRESET]];
    options->minimap_follows_camera = settings.values[MINIMAP_ORIENTATION];
    options->autodrive_cinematic = settings.values[AUTODRIVE_CINEMATIC];
    options->skip_ineligible_car_dialogue = settings.values[SKIP_CHAMPIONSHIP_DIALOGUE];
    options->skip_car_meet_on_startup = settings.values[SPAWN_LOCATION] != SPAWN_DEFAULT;
    options->spawn_at_festival = settings.values[SPAWN_LOCATION] == SPAWN_FESTIVAL_SITE;
    options->snap_car_to_road = settings.values[CAR_CHANGE_POSITION] == CAR_POSITION_NEAREST_ROAD;
}

void video_settings_refresh_hud(void) {
    display_units_refresh();
    minimap_set_follow_camera(settings.values[MINIMAP_ORIENTATION]);
    autodrive_set_cinematic(settings.values[AUTODRIVE_CINEMATIC]);
    road_set_enabled(settings.values[CAR_CHANGE_POSITION] == CAR_POSITION_NEAREST_ROAD);
}

unsigned video_setting_page(unsigned index) {
    if (index == OPEN_WORLD_DRIVATARS) {
        return DIFFICULTY_PAGE;
    }
    if (index == BEN_PHOTO_DIALOGUE || index == ASHLEY_GARAGE_DIALOGUE ||
        index == CHECKPOINT_SOUND || index == MASTER_VOLUME) {
        return AUDIO_PAGE;
    }
    return index == SHOW_ALL_EVENTS || index == AUTODRIVE_EVENTS || index == CUSTOM_SHOWCASES ||
                   index == WHEELSPIN_NOTIFICATION || index == SKILL_POINT_NOTIFICATION ||
                   index == POWER_UNITS || index == SPRING_UNITS || index == SKILL_COUNTER_SHAKE || index == PLAYER_CHARACTER || index == SPEED_ZONE_VIGNETTE ||
                   index == VIGNETTE_EFFECT || index < CAMERA_SETTING_COUNT ||
                   (index >= MINIMAP_ORIENTATION && index <= CAR_CHANGE_POSITION)
               ? HUD_PAGE
               : VIDEO_PAGE;
}

void video_settings_reset_page(unsigned page) {
    for (unsigned index = 0; index < VIDEO_SETTING_COUNT; ++index) {
        if (video_setting_page(index) == page) {
            settings.values[index] = defaults.values[index];
        }
    }
    if (page == HUD_PAGE) {
        video_settings_refresh_hud();
    } else if (page == AUDIO_PAGE) {
        audio_volume_refresh();
    }
    save_settings();
}

static unsigned fov_setting(unsigned view) {
    return view == CAMERA_CHASE || view == CAMERA_CHASE_NEAR ? FOV_CHASE
           : view >= CAMERA_CHASE_FAR && view <= CAMERA_BUMPER
               ? FOV_FAR_CHASE + view - CAMERA_CHASE_FAR
               : CAMERA_SETTING_COUNT;
}

float camera_fov_degrees(unsigned view) {
    unsigned index = fov_setting(view);
    return index < CAMERA_SETTING_COUNT ? video_setting_get(index) * 0.5f : 0;
}

float camera_fov_default(unsigned view) {
    unsigned index = fov_setting(view);
    return index < CAMERA_SETTING_COUNT ? defaults.values[index] * 0.5f : 0;
}

int drift_view_enabled(unsigned view) {
    int mode = settings.values[DRIFT_MODE];
    return (mode == DRIFT_ON && view >= CAMERA_CHASE && view <= CAMERA_DRIVER) ||
           (mode == DRIFT_DRIVER_ONLY && view == CAMERA_DRIVER);
}

float drift_look_speed(unsigned view) {
    int speed = settings.values[view >= CAMERA_DRIVER ? DRIFT_DRIVER_SPEED : DRIFT_CHASE_SPEED];
    if (view >= CAMERA_DRIVER) {
        return 2 + speed * 0.1f;
    }
    return speed <= 50 ? 0.25f + speed * 0.015f : 1.0f + (speed - 50) * 0.06f;
}

float drift_sensitivity(unsigned view) {
    int sensitivity =
        settings.values[view >= CAMERA_DRIVER ? DRIFT_DRIVER_SENSITIVITY : DRIFT_CHASE_SENSITIVITY];
    return view >= CAMERA_DRIVER ? sensitivity * sensitivity / 10000.0f : sensitivity / 50.0f;
}

float clamp_drift_angle(unsigned view, float angle) {
    unsigned setting = view >= CAMERA_DRIVER ? DRIFT_DRIVER_RANGE : DRIFT_CHASE_RANGE;
    float range = settings.values[setting] *
                  (view >= CAMERA_DRIVER ? 0.7853981633974483f : 2.09439510239f) / 100;
    return angle < -range ? -range : angle > range ? range : angle;
}

float drift_angle(unsigned view, float angle) {
    angle *= drift_sensitivity(view);
    if (view >= CAMERA_DRIVER) {
        float range = clamp_drift_angle(view, 1);
        float magnitude = angle < 0 ? -angle : angle;
        float start = range * 0.75f, shoulder = range * 0.25f;
        if (shoulder > 0 && magnitude > start) {
            float excess = magnitude - start;
            magnitude = start + shoulder * excess / (shoulder + excess);
            return angle < 0 ? -magnitude : magnitude;
        }
    }
    return clamp_drift_angle(view, angle);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestDriftSettingGet(unsigned index) {
    return index < DRIFT_SETTING_COUNT ? video_setting_get(index) : 0;
}

__declspec(dllexport) int Fh2TestDriftSettingSet(unsigned index, int value) {
    return index < DRIFT_SETTING_COUNT && video_setting_set(index, value);
}

__declspec(dllexport) void Fh2TestDriftSettingsLoad(const wchar_t *directory) {
    video_settings_load(directory);
}

__declspec(dllexport) void Fh2TestDriftSettingsReset(void) {
    video_settings_reset();
}

__declspec(dllexport) int Fh2TestCameraSettingGet(unsigned index) {
    return video_setting_get(index);
}

__declspec(dllexport) int Fh2TestCameraSettingSet(unsigned index, int value) {
    return video_setting_set(index, value);
}

__declspec(dllexport) void Fh2TestSettingsSeed(ModOptions *options) {
    video_settings_seed(options);
    video_settings_apply(options);
}
#endif
