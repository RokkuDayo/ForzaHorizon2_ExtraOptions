#include "runtime.h"
#include "settings.h"
#include "logging.h"
#include "support.h"

#ifndef FH2_TWEAKS_TEST
static wchar_t mod_directory[PATH_CAPACITY], exe_path[PATH_CAPACITY], file_path[PATH_CAPACITY];
#endif
/* Azimuth 0.9.5's helper ABI, reimplemented without its binary or source:
   one 336-byte zero TLS template, alignment 16, no callbacks/zero fill;
   two data exports, the native TLS index and process-attach thread ID.
   The loader maps/relocates FH2 before loading this helper. No host addresses,
   remote injection, worker thread or LoadLibrary is needed. */
__declspec(dllexport) DWORD azimuth_tls_index = TLS_OUT_OF_INDEXES;
__declspec(dllexport) DWORD azimuth_tls_attached;
__attribute__((section(".tls"),
               aligned(16))) static const unsigned char azimuth_tls_template[0x150] = {0};
static const PIMAGE_TLS_CALLBACK azimuth_tls_callbacks[1] = {0};
const IMAGE_TLS_DIRECTORY64 _tls_used = {
    (ULONGLONG)(uintptr_t)azimuth_tls_template,
    (ULONGLONG)(uintptr_t)(azimuth_tls_template + sizeof(azimuth_tls_template)),
    (ULONGLONG)(uintptr_t)&azimuth_tls_index,
    (ULONGLONG)(uintptr_t)azimuth_tls_callbacks,
    0,
    0x00500000};

static int azimuth_region_is_readable(const MEMORY_BASIC_INFORMATION *m) {
    DWORD p = m->Protect & 0xff;
    return m->State == MEM_COMMIT && !(m->Protect & PAGE_GUARD) &&
           (p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
            p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY);
}

static int azimuth_has_game_headers(const unsigned char *base) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!memory_read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 64 || dos.e_lfanew > 4096 ||
        !memory_read(base + dos.e_lfanew, &nt, sizeof(nt))) {
        return 0;
    }
    return nt.Signature == IMAGE_NT_SIGNATURE &&
           nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
           !(nt.FileHeader.Characteristics & IMAGE_FILE_DLL) &&
           nt.FileHeader.TimeDateStamp == GAME_TIMESTAMP &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt.OptionalHeader.SizeOfImage == GAME_IMAGE_SIZE;
}

static int azimuth_image_is_complete(unsigned char *base) {
    uintptr_t p = (uintptr_t)base, end = p + GAME_IMAGE_SIZE;
    if (end < p) {
        return 0;
    }
    while (p < end) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery((void *)p, &m, sizeof(m)) != sizeof(m) ||
            !azimuth_region_is_readable(&m) || m.AllocationBase != base) {
            return 0;
        }
        uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (next <= p) {
            return 0;
        }
        p = next;
    }
    return 1;
}

static unsigned char *azimuth_find_game_image(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t p = (uintptr_t)info.lpMinimumApplicationAddress,
              max = (uintptr_t)info.lpMaximumApplicationAddress;
    unsigned char *found = 0;
    while (p <= max) {
        MEMORY_BASIC_INFORMATION m;
        if (VirtualQuery((void *)p, &m, sizeof(m)) != sizeof(m)) {
            break;
        }
        if (m.BaseAddress == m.AllocationBase && (m.Type == MEM_PRIVATE || m.Type == MEM_IMAGE) &&
            azimuth_region_is_readable(&m) && m.RegionSize >= sizeof(IMAGE_DOS_HEADER) &&
            azimuth_has_game_headers(m.AllocationBase)) {
            if (!azimuth_image_is_complete(m.AllocationBase)) {
                status_set(
                    "REFUSED: mapped FH2 image is incomplete or inaccessible. No patches applied.");
                return 0;
            }
            if (found) {
                status_set(
                    "REFUSED: more than one supported FH2 image is mapped. No patches applied.");
                return 0;
            }
            found = m.AllocationBase;
        }
        uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (next <= p) {
            break;
        }
        p = next;
    }
    if (!found) {
        status_set(
            "REFUSED: Azimuth has not mapped the supported FH2 executable. No patches applied.");
    }
    return found;
}
#if !defined(FH2_TWEAKS_TEST)
static wchar_t azimuth_folder[32768];

static void azimuth_copy_path(wchar_t *dst, const wchar_t *src) {
    size_t n = 0;
    do {
        dst[n] = src[n];
    } while (src[n++]);
}

static int azimuth_resolve_game_paths(HINSTANCE instance) {
    DWORD n = GetModuleFileNameW(0, exe_path, 32768);
    if (!n || n >= 32768) {
        return 0;
    }
    wchar_t *name = exe_path;
    for (wchar_t *p = exe_path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            name = p + 1;
        }
    }
    const wchar_t expected[] = L"azimuth.exe";
    for (unsigned i = 0;; ++i) {
        wchar_t c = name[i];
        if (c >= L'A' && c <= L'Z') {
            c += L'a' - L'A';
        }
        if (c != expected[i]) {
            return 0;
        }
        if (!c) {
            break;
        }
    }
    n = GetModuleFileNameW(instance, mod_directory, 32768);
    if (!n || n >= 32768) {
        return 0;
    }
    while (n && mod_directory[n - 1] != L'\\' && mod_directory[n - 1] != L'/') {
        --n;
    }
    mod_directory[n] = 0;
    n = (DWORD)(name - exe_path);
    exe_path[n] = 0; /* Launcher directory. */
    if (!path_join(file_path, exe_path, L"data\\azimuth.ini")) {
        goto path_error;
    }
    n = GetPrivateProfileStringW(L"launcher", L"game_folder", L"", azimuth_folder, 32768,
                                 file_path);
    if (!n) {
        /* Azimuth 0.9.9+: launcher.game names the selected title and its
           [title-<game>] section holds the game folder as folder=. */
        wchar_t section[64] = L"title-";
        DWORD id = GetPrivateProfileStringW(L"launcher", L"game", L"", section + 6, 64 - 6,
                                            file_path);
        if (id && id < 64 - 7) {
            n = GetPrivateProfileStringW(section, L"folder", L"", azimuth_folder, 32768,
                                         file_path);
        }
    }
    if (!n || n >= 32767) {
        goto path_error;
    }
    /* Resolve relative game_folder against azimuth.exe, as its launcher does. */
    if (!((azimuth_folder[0] && azimuth_folder[1] == L':') || azimuth_folder[0] == L'\\' ||
          azimuth_folder[0] == L'/')) {
        if (!path_join(file_path, exe_path, azimuth_folder)) {
            goto path_error;
        }
    } else {
        azimuth_copy_path(file_path, azimuth_folder);
    }
    n = GetFullPathNameW(file_path, 32768, azimuth_folder, 0);
    if (!n || n >= 32768) {
        goto path_error;
    }
    if (!path_join(exe_path, azimuth_folder, L"Mount\\forza_durango_release_final.exe")) {
        goto path_error;
    }
    return 1;
path_error:
    status_set(
        "REFUSED: cannot resolve the game folder from data/azimuth.ini (launcher.game_folder or "
        "[title-<game>] folder). No patches applied.");
    ModOptions options;
    settings_defaults(&options);
    log_start(mod_directory, &options);
    return 0;
}

static void azimuth_initialize(HINSTANCE instance) {
    if (!azimuth_resolve_game_paths(instance)) {
        return;
    }
    unsigned char *base = azimuth_find_game_image();
    if (!base) {
        ModOptions options;
        settings_defaults(&options);
        log_start(mod_directory, &options);
        return;
    }
    if (runtime_initialize(base, exe_path, mod_directory)) {
        char message[] =
            "AZIMUTH: mapped FH2 image at 0x0000000000000000; native TLS helper preserved.\r\n";
        const char digits[] = "0123456789abcdef";
        uintptr_t value = (uintptr_t)base;
        for (unsigned i = 0; i < 16; ++i) {
            message[sizeof("AZIMUTH: mapped FH2 image at 0x") - 1 + i] =
                digits[(value >> (60 - 4 * i)) & 15];
        }
        /* Log even when no dynamic callback options were selected. */
        log_set_directory(mod_directory);
        log_append(message);
    }
}
#endif
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestFindAzimuthImage(void) {
    return azimuth_find_game_image();
}
#endif

void adapter_process_attach(HINSTANCE instance) {
    azimuth_tls_attached = GetCurrentThreadId();
#ifndef FH2_TWEAKS_TEST
    azimuth_initialize(instance);
#ifdef FH2_AZIMUTH_DIAGNOSTIC
    ExitProcess(status_get()[0] == 'O' || status_get()[0] == 'D' ? 0 : 1);
#endif
#else
    (void)instance;
#endif
}
