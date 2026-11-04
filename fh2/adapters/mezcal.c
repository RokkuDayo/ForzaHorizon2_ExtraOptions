#include "runtime.h"
#include "support.h"

#ifndef FH2_TWEAKS_TEST
static wchar_t mod_directory[PATH_CAPACITY], exe_path[PATH_CAPACITY];
#endif
void adapter_process_attach(HINSTANCE instance) {
    (void)instance;
#ifndef FH2_TWEAKS_TEST
    DWORD n = GetModuleFileNameW(0, exe_path, 32768);
    if (!n || n >= 32768) {
        return;
    }
    wchar_t *name = exe_path;
    for (wchar_t *p = exe_path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            name = p + 1;
        }
    }
    const wchar_t expected[] = L"forza_durango_release_final.exe";
    for (unsigned i = 0;; ++i) {
        wchar_t c = name[i];
        if (c >= L'A' && c <= L'Z') {
            c += L'a' - L'A';
        }
        if (c != expected[i]) {
            return;
        }
        if (!c) {
            break;
        }
    }
    n = (DWORD)(name - exe_path);
    memory_copy(mod_directory, exe_path, n * sizeof(wchar_t));
    mod_directory[n] = 0;
    /* No LoadLibrary, COM, CRT, worker threads, or waits under the loader lock.
       This optional XG lookup occurs during graphics startup in the tested log.
       File I/O and pure validation are synchronous, before applying any bytes. */
    runtime_initialize((unsigned char *)GetModuleHandleW(0), exe_path, mod_directory);

#endif
}
