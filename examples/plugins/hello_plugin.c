#include "fh2_plugin.h"

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    (void)module; (void)reason; (void)reserved;
    return TRUE;
}

__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *context) {
    if (!context || context->api_version != FH2_PLUGIN_API_VERSION ||
        context->size < sizeof(*context)) return FALSE;
    context->log_utf8("HELLO PLUGIN: initialized with the mapped FH2 image.\r\n");
    // context->game_image is the relocated FH2 image, including under Azimuth.
    // Install your own verified hooks here. No hook library is required by the API.
    return TRUE;
}
