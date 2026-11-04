/* Minimal FH3 Extra Options plugin.
   Build: clang --target=x86_64-w64-mingw32 -shared -O2 -I ../.. ExamplePlugin.c -o ExamplePlugin.dll
   Install: mods\Example\plugins\ExamplePlugin.dll next to azimuth.exe */
#include "fh3_plugin.h"

static const FH3PluginContext *host;
static unsigned frames;

__declspec(dllexport) BOOL WINAPI FH3PluginInitialize(const FH3PluginContext *context) {
    if (!context || context->api_version < FH3_PLUGIN_API_VERSION) return FALSE;
    host = context;
    host->log_utf8("Example plugin initialized.");
    /* patch_bytes refuses to write when the expected bytes do not match. */
    static const unsigned char wrong[2] = {0x12, 0x34}, nothing[2] = {0x90, 0x90};
    host->log_utf8(host->patch_bytes(0x1000, wrong, nothing, 2) ? "Unexpected: patch applied."
                                                                : "patch_bytes verified the bytes.");
    return TRUE;
}

__declspec(dllexport) void WINAPI FH3PluginUpdate(const FH3PluginFrame *frame) {
    if (++frames == 120)
        host->log_utf8(frame->car ? "Example plugin: per-frame update with the player car."
                                  : "Example plugin: per-frame update, no car yet.");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
