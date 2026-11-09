#include "fh2/runtime.h"
#include "shared/support.h"

__declspec(dllexport) const char *Fh2TweaksStatus(void) {
    return status_get();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        adapter_process_attach(instance);
    }
    return TRUE;
}
