#ifndef FH2_PLUGIN_API_H
#define FH2_PLUGIN_API_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#define FH2_PLUGIN_API_VERSION 1u
enum { FH2_PLUGIN_AZIMUTH = 1, FH2_PLUGIN_MEZCAL = 2 };

typedef struct FH2PluginContext {
    uint32_t size;
    uint32_t api_version;
    uint32_t adapter;
    uint32_t game_image_size;
    void *game_image;
    HMODULE plugin_module;
    const wchar_t *plugins_directory;
    const wchar_t *plugin_path;
    const char *extra_options_version;
    void (*log_utf8)(const char *message);
} FH2PluginContext;

// Optional export: extern "C" __declspec(dllexport) BOOL WINAPI
// FH2PluginInitialize(const FH2PluginContext *context);
// Runs on the loader's worker thread AFTER this DLL's DllMain has returned.
// Context and its strings remain valid until process exit. No hot unloading.
typedef BOOL(WINAPI *FH2PluginInitializeFn)(const FH2PluginContext *context);

// Optional export: FH2PluginVehicleUpdate(const FH2PluginVehicleFrame *frame).
// Called on the native local-car update thread, after successful initialization.
// car is borrowed for this call only. input_enabled is false in menus/loading,
// without gameplay input or while the window is unfocused. Keep callbacks short.
typedef struct FH2PluginVehicleFrame {
    uint32_t size, input_enabled;
    void *car;
    uint32_t buttons;
    float delta_seconds;
    int (WINAPI *flash)(void);
} FH2PluginVehicleFrame;
typedef void(WINAPI *FH2PluginVehicleUpdateFn)(const FH2PluginVehicleFrame *frame);
// Optional database callback, available in Extra Options0.2.211+.
// Runs once per merged in-memory game DB on the owning game thread, before
// the database factory returns when provider initialization completes in time.
// A plugin loaded after the merge receives it on the next local-car update.
// Pointers are borrowed only for this call; never retain or use from a worker.
typedef struct FH2PluginDatabaseFrame {
    uint32_t size, api_version;
    void *database;
    int (*prepare)(void *, const char *, int, void **, const char **);
    int (*step)(void *);
    int (*finalize)(void *);
    int64_t (*column_int64)(void *, int);
} FH2PluginDatabaseFrame;
typedef BOOL(WINAPI *FH2PluginDatabaseUpdateFn)(const FH2PluginDatabaseFrame *frame);
// Optional export (EO0.2.212+), queried after successful initialization.
// An immutable private media root below plugins_directory. Files supplement
// the owning active mod's media folder through native sync/async asset I/O.
// Generate complete files before returning; do not change them while in use.
typedef const wchar_t *(WINAPI *FH2PluginMediaDirectoryFn)(void);
#endif
