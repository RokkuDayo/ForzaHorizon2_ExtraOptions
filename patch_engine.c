#include "patch_engine.h"
#include "features.h"
#include "features/local_liveries.h"
#include "features/plugins.h"
#include "features/offline_events.h"
#include "features/custom_showcases.h"
#include "features/addon_strings.h"
#include "features/addons.h"
#include "features/engine_swaps.h"
#include "features/photo_environment.h"
#include "features/transmission_swaps.h"
#include "features/autodrive_notice.h"
#include "features/ui_controls.h"
#include "features/roof_fade.h"
#include "features/roof_aero.h"
#include "features/speed_zone.h"
#include "features/manufacturer_jump.h"
#include "features/presentation_controls.h"
#include "features/driver_options.h"
#include "features/audio_volume.h"
#include "features/custom_language.h"
#include "features/display_units.h"
#include "features/pause_menu.h"
#include "patches_generated.h"
#include "support.h"

typedef struct {
    uintptr_t address;
    DWORD original_protection;
} PatchPage;

typedef struct {
    unsigned char *game_image;
    const ModOptions *options;
    SIZE_T page_size;
    PatchPage *pages;
    unsigned page_count;
    unsigned writable_page_count;
    unsigned changed_count;
} PatchTransaction;

static const PatchDefinition *const callback_patches[] = {&local_liveries_hook_patches[0],
                                                          &local_liveries_hook_patches[1],
                                                          &local_liveries_hook_patches[2],
                                                          &local_liveries_hook_patches[3],
                                                          &local_liveries_hook_patches[4],
                                                          &local_liveries_hook_patches[5],
                                                          &audio_volume_hook_patches[0],
    &audio_volume_hook_patches[1],
                                                          &display_units_hook_patches[0],
                                                          &display_units_hook_patches[1],
                                                          &presentation_controls_hook_patches[0],
                                                          &presentation_controls_hook_patches[1],
                                                          &presentation_controls_hook_patches[2],
                                                          &presentation_controls_hook_patches[3],
                                                          &presentation_controls_hook_patches[4],
                                                          &presentation_controls_hook_patches[5],
                                                          &presentation_controls_hook_patches[6],
                                                          &presentation_controls_hook_patches[7],
                                                          &presentation_controls_hook_patches[8],
                                                          &presentation_controls_hook_patches[9],
                                                          &presentation_controls_hook_patches[10],
                                                          &presentation_controls_hook_patches[11],
                                                          &presentation_controls_hook_patches[12],
                                                          &presentation_controls_hook_patches[13],
                                                          &presentation_controls_hook_patches[14],
                                                          &presentation_controls_hook_patches[15],
                                                          &presentation_controls_hook_patches[16],
                                                          &presentation_controls_hook_patches[17],
                                                          &presentation_controls_hook_patches[18],
                                                          &presentation_controls_hook_patches[19],
                                                          &presentation_controls_hook_patches[20],
                                                          &presentation_controls_hook_patches[21],
                                                          &presentation_controls_hook_patches[22],
                                                          &presentation_controls_hook_patches[23],
                                                          &presentation_controls_hook_patches[24],
                                                          &presentation_controls_hook_patches[25],
                                                          &presentation_controls_hook_patches[26],
                                                          &presentation_controls_hook_patches[27],
    &presentation_controls_hook_patches[28],
    &presentation_controls_hook_patches[29],
    &presentation_controls_hook_patches[30],
    &presentation_controls_hook_patches[31],
    &presentation_controls_hook_patches[32],
    &presentation_controls_hook_patches[33],
    &driver_options_patches[0],
    &driver_options_patches[1],
    &driver_options_patches[2],
    &driver_options_patches[3],
    &driver_options_patches[4],
                                                          &custom_showcases_hook_patches[0],
                                                          &custom_showcases_hook_patches[1],
                                                          &custom_showcases_hook_patches[2],
                                                          &offline_events_hook_patches[0],
                                                          &offline_events_hook_patches[1],
                                                          &offline_events_hook_patches[2],
                                                          &offline_events_hook_patches[3],
                                                          &offline_events_hook_patches[4],
                                                          &offline_events_hook_patches[5],
                                                          &offline_events_hook_patches[6],
                                                          &offline_events_hook_patches[7],
                                                          &offline_events_hook_patches[8],
                                                          &offline_events_hook_patches[9],
                                                          &offline_events_hook_patches[10],
                                                          &offline_events_hook_patches[11],
                                                          &offline_events_hook_patches[12],
                                                          &offline_events_hook_patches[13],
                                                          &offline_events_hook_patches[14],
                                                          &offline_events_hook_patches[15],
                                                          &offline_events_hook_patches[16],
                                                          &offline_events_hook_patches[17],
                                                          &offline_events_hook_patches[18],
                                                          &offline_events_hook_patches[19],
                                                          &offline_events_hook_patches[20],
                                                          &offline_events_hook_patches[21],
                                                          &offline_events_hook_patches[22],
                                                          &offline_events_hook_patches[23],
                                                          &offline_events_hook_patches[24],
                                                          &offline_events_hook_patches[25],
                                                          &offline_events_hook_patches[26],
                                                          &offline_events_hook_patches[27],
                                                          &offline_events_hook_patches[28],
                                                          &offline_events_hook_patches[29],
                                                          &offline_events_hook_patches[30],
                                                          &offline_events_hook_patches[31],
                                                          &offline_events_hook_patches[32],
                                                          &offline_events_hook_patches[33],
                                                          &offline_events_hook_patches[34],
                                                          &offline_events_hook_patches[35],
    &offline_events_hook_patches[36],
    &offline_events_hook_patches[37],
                                                          &addon_strings_hook_patches[0],
                                                          &video_redirect_hook_patch,
                                                          &addon_strings_hook_patches[1],
                                                          &engine_name_hook_patches[0],
                                                          &engine_name_hook_patches[1],
                                                          &addons_hook_patches[0],
                                                          &addons_hook_patches[1],
                                                          &addons_hook_patches[2],
                                                          &addons_hook_patches[3],
                                                          &garage_hook_patch,
                                                          &road_hook_patch,
                                                          &gamerpic_hook_patch,
                                                          &treasure_map_hook_patch,
                                                          &vehicle_controls_hook_patch,
                                                          &blinkers_radio_hook_patch,
                                                          &hazards_photo_hook_patch,
                                                          &hazards_photo_ready_hook_patch,
                                                          &wheel_variants_hook_patch,
                                                          &aero_tuning_hook_patches[0],
                                                          &aero_tuning_hook_patches[1],
                                                          &roof_fade_hook_patch,
                                                          &speed_zone_car_change_patch,
                                                          &roof_aero_hook_patches[0],
                                                          &roof_aero_hook_patches[1],
                                                          &roof_aero_hook_patches[2],
                                                          &autodrive_hook_patches[0],
                                                          &autodrive_hook_patches[1],
                                                          &autodrive_hook_patches[2],
                                                          &autodrive_hook_patches[3],
                                                          &autodrive_hook_patches[4],
                                                          &autodrive_hook_patches[5],
                                                          &autodrive_hook_patches[6],
                                                          &autodrive_notice_hook_patch,
                                                          &autodrive_hook_patches[7],
                                                          &autodrive_hook_patches[8],
                                                          &autodrive_hook_patches[9],
                                                          &autodrive_hook_patches[10],
                                                          &autodrive_hook_patches[11],
                                                          &minimap_hook_patch,
                                                          &media_overrides_hook_patch,
                                                          &media_overrides_disk_path_hook_patch,

                                                          &media_overrides_bank_diagnostics[0],
                                                          &media_overrides_bank_diagnostics[1],
                                                          &media_overrides_bank_diagnostics[2],
                                                          &rims_hook_patch,
                                                          &rim_name_hook_patch,
                                                          &rim_part_name_hook_patches[0],
                                                          &rim_part_name_hook_patches[1],
                                                          &showroom_photo_hook_patches[0],
                                                          &showroom_photo_hook_patches[1],
                                                          &showroom_photo_hook_patches[2],
                                                          &showroom_photo_hook_patches[3],
                                                          &showroom_photo_hook_patches[4],
                                                          &showroom_photo_hook_patches[5],
                                                          &showroom_photo_hook_patches[6],
                                                          &showroom_photo_hook_patches[7],
                                                          &showroom_photo_hook_patches[8],
                                                          &showroom_photo_hook_patches[9],
                                                          &showroom_photo_hook_patches[10],
                                                          &showroom_photo_hook_patches[11],
                                                          &showroom_photo_hook_patches[12],
                                                          &showroom_photo_hook_patches[13],
                                                          &showroom_photo_hook_patches[14],
                                                          &showroom_photo_hook_patches[15],
                                                          &showroom_photo_hook_patches[16],
                                                          &showroom_photo_hook_patches[17],
                                                          &showroom_photo_hook_patches[18],
                                                          &showroom_photo_hook_patches[19],
    &showroom_photo_hook_patches[20],
    &showroom_photo_hook_patches[21],
    &showroom_photo_hook_patches[22],
                                                          &return_garage_hook_patches[0],
                                                          &return_garage_hook_patches[1],
                                                          &return_garage_hook_patches[2],
                                                          &return_garage_hook_patches[3],
                                                          &stock_wheels_hook_patches[0],
                                                          &stock_wheels_hook_patches[1],
                                                          &stock_wheels_hook_patches[2],
                                                          &drift_camera_hook_patch,
                                                          &drift_camera_view_hook_patches[0],
                                                          &drift_camera_view_hook_patches[1],
                                                          &drift_camera_view_hook_patches[2],
                                                          &drift_camera_view_hook_patches[3],
                                                          &video_options_hook_patches[0],
                                                          &video_options_hook_patches[1],
                                                          &video_options_hook_patches[2],
                                                          &video_options_hook_patches[3],
                                                          &video_options_hook_patches[4],
                                                          &world_drivatars_hook_patches[0],
                                                          &world_drivatars_hook_patches[1],
                                                          &camera_fov_hook_patches[0],
                                                          &camera_fov_hook_patches[1],
                                                          &camera_fov_hook_patches[2],
                                                          &camera_fov_hook_patches[3],
                                                          &camera_fov_hook_patches[4],
                                                          &camera_fov_hook_patches[5],
                                                          &photo_sampling_patch,
                                                          &photo_capture_hook_patches[0],
                                                          &photo_capture_hook_patches[1],
                                                          &manufacturer_jump_hook_patches[0],
                                                          &manufacturer_jump_hook_patches[1],
                                                          &manufacturer_jump_hook_patches[2],
                                                          &manufacturer_jump_hook_patches[3],
                                                          &manufacturer_jump_hook_patches[4],
                                                          &manufacturer_jump_hook_patches[5],
                                                          &manufacturer_jump_hook_patches[6],
                                                          &manufacturer_jump_hook_patches[7],
                                                          &manufacturer_jump_hook_patches[8],
    &manufacturer_jump_hook_patches[9],
    &manufacturer_jump_hook_patches[10],
    &manufacturer_jump_hook_patches[11],
    &manufacturer_jump_hook_patches[12],
                                                          &ui_controls_hook_patches[0],
                                                          &ui_controls_hook_patches[1],
                                                          &ui_controls_hook_patches[2]};

enum { ALL_PATCH_COUNT = FIXED_PATCH_COUNT + ARRAY_COUNT(callback_patches) };

static unsigned char original_bytes[ALL_PATCH_COUNT][MAX_PATCH_SIZE];

#ifdef FH2_TWEAKS_TEST
static int protect_failure_at = -1;
static int protect_call_count;
static int page_allocation_failure;
static unsigned last_page_count, page_allocations;
#endif

static const PatchDefinition *patch_at(unsigned index) {
    if (index < FIXED_PATCH_COUNT) {
        return &fixed_patches[index];
    }
    return callback_patches[index - FIXED_PATCH_COUNT];
}

static int patch_group_enabled(PatchGroup group, const ModOptions *options) {
    switch (group) {
        case GROUP_ENGINE_SWAPS:
            return options->all_engine_swaps;
        case GROUP_CAMERA_FOV:
        case GROUP_ADDONS:
        case GROUP_VIDEO_OPTIONS:
            return 1;
        case GROUP_PHOTO_SAMPLING:
            return options->photo_mode_samples > 0;
        case GROUP_DRIFT_CAMERA:
            return 1;
        case GROUP_RIMS:
            return options->all_rims_category;
        case GROUP_BOOT_CAR_MEET:
            return options->skip_car_meet_on_startup;
        case GROUP_MEDIA_OVERRIDES:
            return options->media_overrides || addons_configured() || plugins_configured() ||
                   custom_language_mount() != NULL;
        case GROUP_MINIMAP:
            return 1;
        case GROUP_FRONTEND_XML:
            return 1;
        case GROUP_SUN_SHADOWS:
            return options->smooth_sun_shadows > 0;
        case GROUP_AERO_TUNING:
            return options->unlock_aero_tuning;
        case GROUP_VEHICLE_CONTROLS:
            return 1;
        case GROUP_AUTODRIVE:
            return options->autodrive;
        case GROUP_TREASURE_MAP:
            return options->unlock_treasure_map;
        case GROUP_PHOTO_CAMERA:
            return options->free_photo_camera;
        case GROUP_GAMERPIC:
            return options->custom_gamerpic;
        case GROUP_PARENTAL:
            return options->disable_parental_locks;
        case GROUP_ISLAND_CARS:
            return options->unlock_storm_island_cars;
        case GROUP_ROAD:
            return options->garage_from_pause;
        case GROUP_DLC:
            return options->unlock_dlc;
        case GROUP_VIP:
            return options->offline_vip;
        case GROUP_INTRO:
            return options->skip_intro;
        case GROUP_SOCIAL:
            return options->enable_social;
        case GROUP_CLUBS:
            return options->enable_clubs;
        case GROUP_RIVALS:
            return options->enable_rivals;
        case GROUP_MARKETPLACE:
            return options->enable_marketplace;
        case GROUP_LIVERY:
            return options->enable_4k_liveries;
        case GROUP_MSAA:
            return options->livery_msaa == 4;
        case GROUP_PAUSE_GARAGE:
            return options->garage_from_pause;
    }
    return 0;
}

static const unsigned char *patch_target(const PatchDefinition *patch, const ModOptions *options) {
    if (patch->group == GROUP_LIVERY || patch->group == GROUP_MSAA) {
        return livery_patch_target(patch, options);
    }
    if (patch->group == GROUP_SUN_SHADOWS && options->smooth_sun_shadows < 0) {
        return sun_shadows_layer_default(patch);
    }
    if (!patch_group_enabled(patch->group, options)) {
        return patch->original;
    }
    return patch->standard;
}

static int supported_image(const unsigned char *game_image) {
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)game_image;
    if (!game_image || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 ||
        dos->e_lfanew > 4096) {
        return 0;
    }
    const IMAGE_NT_HEADERS64 *headers = (const IMAGE_NT_HEADERS64 *)(game_image + dos->e_lfanew);
    return headers->Signature == IMAGE_NT_SIGNATURE &&
           headers->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           headers->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           headers->FileHeader.TimeDateStamp == GAME_TIMESTAMP &&
           headers->OptionalHeader.SizeOfImage == GAME_IMAGE_SIZE;
}

static int validate_patch_sites(PatchTransaction *transaction) {
    for (unsigned index = 0; index < ALL_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = patch_at(index);
        const unsigned char *loaded_bytes = transaction->game_image + patch->rva;
        int recognized = 0;
        for (unsigned variant = 0; variant < patch->accepted_count; ++variant) {
            if (memory_equal(loaded_bytes, patch->accepted[variant], patch->size)) {
                recognized = 1;
                break;
            }
        }
        if (!recognized) {
            status_set("REFUSED: unexpected loaded bytes at ");
            status_append(patch->name);
            status_append(". No patches applied.");
            return 0;
        }
        memory_copy(original_bytes[index], loaded_bytes, patch->size);
        if (!memory_equal(loaded_bytes, patch_target(patch, transaction->options), patch->size)) {
            ++transaction->changed_count;
        }
    }
    return 1;
}

static int pin_callback_module(const ModOptions *options) {
    (void)options;
    HMODULE pinned_module;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCWSTR)(uintptr_t)garage_callback_address(), &pinned_module)) {
        return 1;
    }
    status_set("REFUSED: cannot keep the callback DLL resident. No patches applied.");
    return 0;
}

static size_t patch_page_capacity(const PatchTransaction *transaction) {
    size_t capacity = 0;
    for (unsigned index = 0; index < ALL_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = patch_at(index);
        if (memory_equal(original_bytes[index], patch_target(patch, transaction->options),
                         patch->size)) {
            continue;
        }
        uintptr_t address = (uintptr_t)transaction->game_image + patch->rva;
        size_t first = address / transaction->page_size;
        size_t last = (address + patch->size - 1) / transaction->page_size;
        capacity += last - first + 1;
    }
    return capacity;
}

static PatchPage *allocate_patch_pages(size_t capacity) {
#ifdef FH2_TWEAKS_TEST
    if (page_allocation_failure) {
        return NULL;
    }
#endif
    return HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(PatchPage));
}

static int collect_patch_pages(PatchTransaction *transaction) {
    transaction->pages = allocate_patch_pages(patch_page_capacity(transaction));
    if (!transaction->pages) {
        status_set("REFUSED: cannot allocate patch page tracking. No patches applied.");
        return 0;
    }
#ifdef FH2_TWEAKS_TEST
    ++page_allocations;
#endif
    for (unsigned index = 0; index < ALL_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = patch_at(index);
        unsigned char *address = transaction->game_image + patch->rva;
        if (memory_equal(original_bytes[index], patch_target(patch, transaction->options),
                         patch->size)) {
            continue;
        }
        uintptr_t first_page = (uintptr_t)address / transaction->page_size * transaction->page_size;
        uintptr_t last_page = (uintptr_t)(address + patch->size - 1) / transaction->page_size *
                              transaction->page_size;
        for (uintptr_t page = first_page; page <= last_page; page += transaction->page_size) {
            unsigned existing = 0;
            while (existing < transaction->page_count &&
                   transaction->pages[existing].address != page) {
                ++existing;
            }
            if (existing != transaction->page_count) {
                continue;
            }
            transaction->pages[transaction->page_count++].address = page;
        }
    }
#ifdef FH2_TWEAKS_TEST
    last_page_count = transaction->page_count;
#endif
    return 1;
}

static void release_patch_pages(PatchTransaction *transaction) {
    HeapFree(GetProcessHeap(), 0, transaction->pages);
    transaction->pages = NULL;
#ifdef FH2_TWEAKS_TEST
    --page_allocations;
#endif
}

static BOOL make_writable(void *address, SIZE_T size, DWORD *previous_protection) {
#ifdef FH2_TWEAKS_TEST
    if (protect_call_count++ == protect_failure_at) {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
#endif
    return VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, previous_protection);
}

static int restore_page_protection(const PatchTransaction *transaction) {
    int restored = 1;
    for (unsigned index = 0; index < transaction->writable_page_count; ++index) {
        DWORD ignored;
        const PatchPage *page = &transaction->pages[index];
        if (!VirtualProtect((void *)page->address, transaction->page_size,
                            page->original_protection, &ignored)) {
            restored = 0;
        }
    }
    return restored;
}

static int make_patch_pages_writable(PatchTransaction *transaction) {
    while (transaction->writable_page_count < transaction->page_count) {
        unsigned index = transaction->writable_page_count;
        PatchPage *page = &transaction->pages[index];
        if (!make_writable((void *)page->address, transaction->page_size,
                           &page->original_protection)) {
            restore_page_protection(transaction);
            status_set("REFUSED: cannot make every patch page writable. No bytes changed.");
            return 0;
        }
        ++transaction->writable_page_count;
    }
    return 1;
}

static void write_patch_sites(const PatchTransaction *transaction, int rollback) {
    for (unsigned index = 0; index < ALL_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = patch_at(index);
        const unsigned char *target = patch_target(patch, transaction->options);
        if (memory_equal(original_bytes[index], target, patch->size)) {
            continue;
        }
        const unsigned char *bytes = rollback ? original_bytes[index] : target;
        unsigned char *address = transaction->game_image + patch->rva;
        if (index >= FIXED_PATCH_COUNT && patch->size == sizeof(uintptr_t)) {
            uintptr_t callback;
            memory_copy(&callback, bytes, sizeof(callback));
            *(volatile uintptr_t *)address = callback;
        } else {
            memory_copy(address, bytes, patch->size);
        }
    }
}

static void commit_feature_options(const ModOptions *options) {
    pause_menu_configure(options->enabled && options->cars_next_to_home);
    engine_swaps_configure(options->all_engine_swaps);
    transmission_swaps_configure(options->all_transmission_swaps);
    garage_commit_options(options);
    road_set_enabled(options->snap_car_to_road);
    drift_camera_commit_options(options);
    photo_sampling_commit_options();
    minimap_set_follow_camera(options->minimap_follows_camera);
}

static int commit_transaction(const PatchTransaction *transaction) {
    write_patch_sites(transaction, 0);
    int flushed =
        FlushInstructionCache(GetCurrentProcess(), transaction->game_image, GAME_IMAGE_SIZE) != 0;
    if (flushed) {
        commit_feature_options(transaction->options);
        status_set("OK: requested tweaks applied in memory.");
    } else {
        write_patch_sites(transaction, 1);
        FlushInstructionCache(GetCurrentProcess(), transaction->game_image, GAME_IMAGE_SIZE);
        status_set("REFUSED: instruction-cache flush failed. Patch bytes rolled back.");
    }
    if (!restore_page_protection(transaction)) {
        status_append(" WARNING: page protection restoration failed.");
        return 0;
    }
    return flushed;
}

int patch_engine_apply(unsigned char *game_image, const ModOptions *options) {
#ifdef FH2_TWEAKS_TEST
    last_page_count = 0;
#endif
    if (!supported_image(game_image)) {
        status_set("REFUSED: unsupported loaded executable headers. No patches applied.");
        return 0;
    }
    if (!options->enabled) {
        status_set("DISABLED: no memory changes made. Existing EXE patches remain "
                   "active.");
        return 1;
    }
    if (options->smooth_sun_shadows < -1 || options->smooth_sun_shadows > 1 ||
        options->menu_turbo_interval_ms < 0 || options->menu_turbo_interval_ms > 1000 ||
        (options->menu_turbo_interval_ms && options->menu_turbo_interval_ms < 20) ||
        options->photo_mode_samples < 0 || options->photo_mode_samples > 4096 ||
        (options->photo_mode_samples > 0 && options->photo_mode_samples < 16)) {
        status_set("REFUSED: invalid graphics selection. No patches applied.");
        return 0;
    }

    offline_events_prepare_hooks(game_image);
    custom_showcases_prepare_hooks(game_image);
    garage_prepare_hook(game_image);
    road_prepare_hook(game_image);
    gamerpic_prepare_hook(game_image);
    treasure_map_prepare_hook(game_image);
    minimap_prepare_hook(game_image);
    photo_sampling_prepare_patch(game_image, options);
    sun_shadows_prepare_defaults(game_image);
    livery_prepare_defaults(game_image);
    camera_fov_prepare_hooks(game_image);
    rims_prepare_hook(game_image);
    engine_swaps_prepare_hooks(game_image);
    aero_tuning_prepare_hooks(game_image);
    return_garage_prepare_hooks(game_image);
    showroom_photo_prepare_hooks(game_image);
    manufacturer_jump_prepare_hooks(game_image);
    speed_zone_prepare(game_image);
    audio_volume_prepare_hooks(game_image);
    display_units_prepare_hooks(game_image);
    presentation_controls_prepare_hooks(game_image);
    driver_options_prepare(game_image);
    photo_environment_prepare(game_image);
    drift_camera_prepare_hook(game_image);
    video_options_prepare_hooks(game_image);
    world_drivatars_prepare_hooks(game_image);
    media_overrides_prepare_hook(game_image);
    video_redirect_prepare_hook(game_image);
    media_overrides_set_enabled(options->media_overrides);
    addons_prepare_hooks(game_image);
    addon_strings_prepare_hooks(game_image);
    autodrive_prepare_hooks(game_image, options);
    ui_controls_prepare_hooks(game_image, options);
    local_liveries_bind(game_image, options->local_drivatar_liveries,
                       options->max_drivatar_liveries);

    PatchTransaction transaction;
    transaction.game_image = game_image;
    transaction.options = options;
    transaction.pages = NULL;
    transaction.page_count = 0;
    transaction.writable_page_count = 0;
    transaction.changed_count = 0;
    if (!validate_patch_sites(&transaction)) {
        return 0;
    }
    if (!pin_callback_module(options)) {
        return 0;
    }
    if (!transaction.changed_count) {
        commit_feature_options(options);
        status_set("OK: all requested tweaks already match the loaded executable.");
        return 1;
    }
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    transaction.page_size = system_info.dwPageSize;
    if (!collect_patch_pages(&transaction)) {
        return 0;
    }
    int applied = make_patch_pages_writable(&transaction) && commit_transaction(&transaction);
    release_patch_pages(&transaction);
    return applied;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestApply(void *base, const ModOptions *options) {
    return patch_engine_apply(base, options);
}

__declspec(dllexport) void Fh2TestProtectFailure(int index) {
    protect_failure_at = index;
    protect_call_count = 0;
}

__declspec(dllexport) void Fh2TestPatchPageAllocationFailure(int fail) {
    page_allocation_failure = fail;
}

__declspec(dllexport) unsigned Fh2TestPatchSiteCount(void) { return ALL_PATCH_COUNT; }
__declspec(dllexport) void Fh2TestPatchSite(unsigned index, unsigned *rva, unsigned *size) {
    const PatchDefinition *patch = index < ALL_PATCH_COUNT ? patch_at(index) : NULL;
    *rva = patch ? patch->rva : 0;
    *size = patch ? patch->size : 0;
}

__declspec(dllexport) unsigned Fh2TestPatchPageCount(void) {
    return last_page_count;
}

__declspec(dllexport) unsigned Fh2TestPatchPageAllocations(void) {
    return page_allocations;
}
#endif
