#include "shared/adapters/azimuth_proxy.h"
#include "shared/support.h"
#include "shared/adapters/winmm_exports.h"

static INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
static HMODULE system_winmm;
static FARPROC exports[ARRAY_COUNT(winmm_names)];
static wchar_t directory[PATH_CAPACITY], path[PATH_CAPACITY];

static BOOL CALLBACK load_system_winmm(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    UINT length = GetSystemDirectoryW(directory, ARRAY_COUNT(directory));
    if (!length || length >= ARRAY_COUNT(directory) || !path_join(path, directory, L"winmm.dll")) {
        return FALSE;
    }
    system_winmm = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(uintptr_t)load_system_winmm, &self);
    if (!system_winmm || system_winmm == self) {
        return FALSE;
    }
    for (unsigned index = 0; index < ARRAY_COUNT(exports); ++index) {
        exports[index] = GetProcAddress(system_winmm, winmm_names[index]);
    }
    return TRUE;
}

FARPROC winmm_resolve_export(unsigned index, void *caller) {
    DWORD error = GetLastError();
    if (!InitOnceExecuteOnce(&initialized, load_system_winmm, NULL, NULL) ||
        index >= ARRAY_COUNT(exports) || !exports[index]) {
        RaiseException(0xc0000139u, EXCEPTION_NONCONTINUABLE, 0, NULL);
        return NULL;
    }
    if (index == WINMM_TIME_BEGIN_INDEX) {
        azimuth_proxy_start(caller);
    }
    SetLastError(error);
    return exports[index];
}
