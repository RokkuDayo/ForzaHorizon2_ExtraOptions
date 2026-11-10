#ifndef FH3_PLUGIN_API_H
#define FH3_PLUGIN_API_H
/* Forza Horizon 3 Extra Options plugin API.

   A mod is a folder in mods\Forza Horizon 3\<ModName>\ next to azimuth.exe:
     media\      loose files mounted over game:\media\ (above mediapc, below
                 mediaoverride; later packages in loadorder.txt win)
     plugins\    DLLs loaded once, in package order, on the game thread right
                 after the native file system is initialized
   A mod is disabled with [Mods] <ModName>=0 in FH3_ExtraOptions.ini.

   Plugin exports (all optional):
     BOOL WINAPI FH3PluginInitialize(const FH3PluginContext *context);
     void WINAPI FH3PluginUpdate(const FH3PluginFrame *frame);
   FH3PluginUpdate runs once per frame on the game thread while driving. The
   context and its strings stay valid until process exit. There is no
   unloading. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#define FH3_PLUGIN_API_VERSION 1u

typedef struct FH3PluginContext {
    uint32_t size;
    uint32_t api_version;
    void *game_image;           /* forza_durango_release_final.exe base */
    uint32_t game_image_size;
    uint32_t reserved;
    HMODULE plugin_module;
    const wchar_t *mod_directory; /* <Azimuth folder>\mods\Forza Horizon 3\<ModName> */
    const wchar_t *plugin_path;
    const char *extra_options_version;
    void (*log_utf8)(const char *message); /* one line, newline added */
    /* Writes size bytes at game_image + rva if the current bytes equal
       expected (expected may be NULL to skip the check). Returns 1 on success. */
    int (*patch_bytes)(uint32_t rva, const void *expected, const void *replacement,
                       uint32_t size);
    /* Redirects the rel32 call instruction at game_image + rva to callback
       and returns the original call target, or NULL if rva is not a call. */
    void *(*hook_call)(uint32_t rva, void *callback);
    /* Replaces the function pointer at game_image + rva (a vtable slot) and
       returns the previous pointer, or NULL on failure. */
    void *(*hook_pointer)(uint32_t rva, void *callback);
    /* The local player's ForzaCar, or NULL outside gameplay. */
    void *(*player_car)(void);
} FH3PluginContext;

typedef struct FH3PluginFrame {
    uint32_t size;
    float delta_seconds;
    void *car; /* local player's ForzaCar, borrowed for this call */
} FH3PluginFrame;

typedef BOOL(WINAPI *FH3PluginInitializeFn)(const FH3PluginContext *context);
typedef void(WINAPI *FH3PluginUpdateFn)(const FH3PluginFrame *frame);
#endif
