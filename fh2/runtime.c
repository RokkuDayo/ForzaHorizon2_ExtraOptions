#include "shared/boot_diagnostics.h"
#include "fh2/runtime.h"
#include "fh2/features.h"
#include "shared/addons.h"
#include "shared/plugins.h"
#include "fh2/video_settings.h"
#include "shared/logging.h"
#include "fh2/patch_engine.h"
#include "fh2/settings.h"
#include "shared/support.h"

#ifdef FH2_AZIMUTH
#include "shared/adapters/azimuth_markers.h"
#endif

static void log_enabled_features(const wchar_t *settings_directory, const ModOptions *options) {
    log_set_directory(settings_directory);
    if (options->garage_from_pause) {
        log_append("GARAGE: XML parse hook installed; awaiting menu-flow resources.\r\n");
    }
    if (options->skip_ineligible_car_dialogue) {
        log_append("FRONTEND: race dialogue skip enabled; awaiting race activation "
                   "flow.\r\n");
    }
    if (options->skip_car_meet_on_startup) {
        log_append("STARTUP: mainland car-meet boot skipped; Storm Island retains "
                   "normal boot.\r\n");
    }
    if (options->spawn_at_festival && options->skip_car_meet_on_startup) {
        log_append("STARTUP: mainland festival spawn enabled; Storm Island retains "
                   "normal boot.\r\n");
    }
    if (options->media_overrides) {
        log_append("FILES: native override hook installed; folders are mounted on "
                   "the first "
                   "file lookup outside DLL initialization.\r\n");
    }
    if (options->autodrive) {
        log_append("AUTODRIVE: configurable GPS/AI shortcuts installed; cinematic "
                   "delay is 3 "
                   "seconds.\r\n");
    }
    if (options->all_rims_category) {
        log_append("UPGRADES: virtual All Rims category enabled; database file "
                   "unchanged.\r\n");
    }
    if (options->unlock_aero_tuning) {
        log_append("WARNING: experimental aero unlock uses Forza aero ranges and "
                   "can change PI "
                   "and handling, including at default tuning values.\r\n");
    }
    log_append("VEHICLE: configurable shortcuts enabled; roof hold is 1500 ms and signal hold is 500 ms.\r\n");
    log_append("SETTINGS: Video and HUD menu hooks installed; awaiting menu "
               "resources.\r\n");
}

int runtime_initialize(unsigned char *game_image, const wchar_t *executable_path,
                       const wchar_t *settings_directory) {
    ModOptions options;
    settings_defaults(&options);
    addons_configure(executable_path);
    log_reset();
    int initialized = settings_read(settings_directory, &options);
    if (initialized) {
        video_settings_load(settings_directory);
        video_settings_seed(&options);
        video_settings_apply(&options);
    }
    if (initialized && options.enabled) {
        uint64_t begin=boot_diag_now();initialized = verify_game_file(executable_path);
        boot_diag_end("boot.verify_game_executable",NULL,begin);
    }
    static wchar_t cache_ini[PATH_CAPACITY];
    int cache=path_join(cache_ini,settings_directory,MOD_INI_NAME) &&
              GetPrivateProfileIntW(L"Mods",L"DatabaseCache",1,cache_ini)!=0;
    addons_cache_configure(settings_directory,cache,0);
    if (initialized && options.enabled) addons_layout_prepare();
    plugins_configure(game_image, executable_path, initialized && options.enabled);
    if (initialized) {
        uint64_t begin=boot_diag_now();initialized = patch_engine_apply(game_image, &options);
        boot_diag_end("boot.FH2_prepare_apply_hooks",NULL,begin);
    }
    log_start(settings_directory, &options);
#ifdef FH2_AZIMUTH
    if (initialized && options.enabled) {
        log_set_directory(settings_directory);
        if (options.generate_missing_save_commits){
            uint64_t begin=boot_diag_now();azimuth_repair_missing_markers((unsigned char *)GetModuleHandleW(NULL));
            boot_diag_end("boot.generate_missing_save_commits",NULL,begin);
        }
    }
#endif
    if (initialized && options.enabled) {
        log_enabled_features(settings_directory, &options);
        addons_layout_log();
    }
    return initialized;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestInitialize(void *base, const wchar_t *path,
                                            const wchar_t *directory) {
    return runtime_initialize(base, path, directory);
}

__declspec(dllexport) int Fh2TestVerifyFile(const wchar_t *path) {
    return verify_game_file(path);
}

__declspec(dllexport) int Fh2TestReadOptions(const wchar_t *directory, ModOptions *options) {
    return settings_read(directory, options);
}
#endif
