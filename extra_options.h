#ifndef FH2_EXTRA_OPTIONS_H
#define FH2_EXTRA_OPTIONS_H
#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#define MOD_NAME "Forza Horizon 2 Extra Options"
#define MOD_VERSION "0.3.2"
#define MOD_INI_NAME L"FH2_ExtraOptions.ini"
#define MOD_LOG_NAME L"FH2_ExtraOptions.log"
#define GAME_EXE_NAME L"forza_durango_release_final.exe"
#define PATH_CAPACITY 32768
#define GAME_FILE_SIZE 58204160u
#define GAME_IMAGE_SIZE 0x3a18000u
#define GAME_TIMESTAMP 0x565501edu
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef enum {
    GROUP_DLC,
    GROUP_VIP,
    GROUP_INTRO,
    GROUP_MARKETPLACE,
    GROUP_LIVERY,
    GROUP_MSAA,
    GROUP_PAUSE_GARAGE,
    GROUP_ROAD,
    GROUP_ISLAND_CARS,
    GROUP_PARENTAL,
    GROUP_GAMERPIC,
    GROUP_PHOTO_CAMERA,
    GROUP_TREASURE_MAP,
    GROUP_AUTODRIVE,
    GROUP_SUN_SHADOWS,
    GROUP_FRONTEND_XML,
    GROUP_MINIMAP,
    GROUP_MEDIA_OVERRIDES,
    GROUP_BOOT_CAR_MEET,
    GROUP_RIMS,
    GROUP_DRIFT_CAMERA,
    GROUP_VIDEO_OPTIONS,
    GROUP_CAMERA_FOV,
    GROUP_PHOTO_SAMPLING,
    GROUP_SOCIAL,
    GROUP_CLUBS,
    GROUP_RIVALS,
    GROUP_VEHICLE_CONTROLS,
    GROUP_AERO_TUNING,
    GROUP_ADDONS,
    GROUP_ENGINE_SWAPS
} PatchGroup;

typedef struct {
    uint32_t rva, offset, size;
    PatchGroup group;
    const char *name;
    const unsigned char *original, *standard, *supersampled;
    const unsigned char *const *accepted;
    uint32_t accepted_count;
} PatchDefinition;

typedef struct {
    int enabled;
    int unlock_dlc;
    int offline_vip;
    int skip_intro;
    int enable_marketplace;
    int enable_4k_liveries;
    int livery_msaa;
    int livery_supersampling;
    int garage_from_pause;
    int reserved_text_override;
    int reserved_audio_override;
    int snap_car_to_road;
    int unlock_storm_island_cars;
    int disable_parental_locks;
    int custom_gamerpic;
    int free_photo_camera;
    int unlock_treasure_map;
    int autodrive;
    int autodrive_cinematic;
    int disable_azimuth_save_checksums;
    int smooth_sun_shadows;
    int skip_ineligible_car_dialogue;
    int minimap_follows_camera;
    int media_overrides;
    int spawn_at_festival;
    int skip_car_meet_on_startup;
    int all_rims_category;
    int drift_camera;
    int reserved_motion_blur_override;
    int reserved_filtering_override;
    int photo_mode_samples;
    int enable_social, enable_clubs, enable_rivals;
    int unlock_aero_tuning;
    int all_engine_swaps;
    int cars_next_to_home;
    int local_drivatar_liveries;
    int max_drivatar_liveries;
    int menu_turbo_interval_ms;
    int all_transmission_swaps;
    int controller_pause_on_unplug_only;
} ModOptions;
#endif
