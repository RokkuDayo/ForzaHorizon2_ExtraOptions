#include "azimuth_proxy.h"
#include "azimuth_host.h"
#include "logging.h"
#include "runtime.h"
#include "settings.h"
#include "sha256.h"
#include "support.h"
#include "features/monitor_bootstrap.h"

#define FH2_TITLE_ID 0x1a96545bu

typedef struct {
    AzimuthBuild build;
    unsigned file_size, map_game, map_call, thunk, startup_return;
    unsigned char hash[32];
    const char *success;
} HostProfile;
static const HostProfile host_profiles[] = {
    {AZIMUTH_101, 9692672u, 0x400e0u, 0xa027bu, 0x91170u, 0x9e950u,
     {0x6e,0x9a,0x22,0x90,0x97,0x15,0x00,0xa1,0x33,0x69,0x99,0x99,0xb3,0x02,0x4e,0xec,
      0x7b,0xe9,0x3c,0xf5,0xd4,0x11,0x6d,0x18,0xd5,0x4b,0x83,0x72,0xd1,0x11,0x72,0xf7},
     "AZIMUTH: 1.0.1 initialized for FH2 (1A96545B); native game loader and TLS retained.\r\n"},
    {AZIMUTH_102, 9715712u, 0x3f2e0u, 0x9f4ebu, 0x903e0u, 0x9dbc0u,
     {0x42,0x52,0x79,0x64,0xf4,0x8c,0x90,0xa5,0x37,0xf2,0x41,0x42,0x00,0xb8,0x59,0x8a,
      0xb9,0xa6,0xf9,0x29,0x19,0xbf,0xbe,0x04,0x6d,0xbe,0xcc,0xcb,0x36,0xe7,0xc5,0x97},
     "AZIMUTH: 1.0.2 initialized for FH2 (1A96545B); native game loader and TLS retained.\r\n"},
};
static const HostProfile *host_profile;
#define MAP_GAME (host_profile->map_game)
#define MAP_GAME_CALL (host_profile->map_call)
#define MAP_GAME_THUNK (host_profile->thunk)

typedef void *(*MapGame)(void *, const void *, const void *, const void *);
typedef int(WINAPI *MessageBoxFunction)(HWND, LPCWSTR, LPCWSTR, UINT);
static HINSTANCE mod_instance;
static unsigned char *host_image;
static volatile LONG initialized, handed_off;
static wchar_t mod_directory[PATH_CAPACITY], game_path[PATH_CAPACITY];
static wchar_t filename[PATH_CAPACITY];
static unsigned char hash_buffer[16384];
static int notifications;
static unsigned char original_call[5];
static unsigned char installed_call[5], original_thunk[12], installed_thunk[12];
static unsigned char original_hashes[2][32], installed_hashes[2][32];

static void notify_refusal(void) {
    ++notifications;
#ifndef FH2_TWEAKS_TEST
    HMODULE user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    MessageBoxFunction show =
        user32 ? (MessageBoxFunction)(uintptr_t)GetProcAddress(user32, "MessageBoxW") : NULL;
    if (show) {
        wchar_t message[1024];
        int length =
            MultiByteToWideChar(CP_UTF8, 0, status_get(), -1, message, ARRAY_COUNT(message));
        if (length) {
            show(NULL, message, L"Forza Horizon 2 Extra Options", MB_OK | MB_ICONWARNING);
        }
    }
    if (user32) {
        FreeLibrary(user32);
    }
#endif
}

static int refuse(const char *reason) {
    status_set(reason);
    ModOptions options;
    settings_defaults(&options);
    log_start(mod_directory, &options);
    notify_refusal();
    return 0;
}

static void hash_bytes(const void *bytes, unsigned size, unsigned char *digest) {
    Sha256Context hash;
    sha256_initialize(&hash);
    sha256_update(&hash, bytes, size);
    sha256_finalize(&hash, digest);
}

static int verify_host_file(const wchar_t *path, const HostProfile *profile) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    Sha256Context context;
    unsigned char digest[32];
    DWORD count;
    uint64_t total = 0;
    int valid = GetFileSizeEx(file, &size) && size.QuadPart == profile->file_size;
    sha256_initialize(&context);
    while (valid && total < profile->file_size) {
        valid = ReadFile(file, hash_buffer, sizeof(hash_buffer), &count, NULL) && count;
        if (valid) {
            sha256_update(&context, hash_buffer, count);
            total += count;
        }
    }
    CloseHandle(file);
    sha256_finalize(&context, digest);
    return valid && total == profile->file_size && memory_equal(digest, profile->hash, 32);
}

static int directory_from_module(HMODULE module, wchar_t *directory) {
    DWORD count = GetModuleFileNameW(module, directory, PATH_CAPACITY);
    if (!count || count >= PATH_CAPACITY) return 0;
    while (count && directory[count - 1] != L'\\' && directory[count - 1] != L'/') --count;
    directory[count] = 0;
    return count != 0;
}

/* The inspected package stores std::wstring members, including the parsed
   XboxLive TitleId (+28) and resolved executable path (+c0). Never infer the
   active title from the launcher's saved selection: --game may override it. */
static int package_text(const void *package, unsigned offset, wchar_t *out, size_t capacity) {
    struct { uintptr_t data[2]; size_t length, capacity; } value;
    const unsigned char *member = (const unsigned char *)package + offset;
    if (!package || !memory_read(member, &value, sizeof(value)) || !capacity ||
        value.length >= capacity || value.length > value.capacity) return 0;
    const void *text = value.capacity > 7 ? (void *)value.data[0] : member;
    if (!memory_read(text, out, (value.length + 1) * sizeof(wchar_t)) || out[value.length]) return 0;
    for (size_t i = 0; i < value.length; ++i) if (!out[i]) return 0;
    return 1;
}

static int package_is_fh2(const void *package) {
    wchar_t title[9];
    if (!package_text(package, 0x28, title, ARRAY_COUNT(title)) || lstrlenW(title) != 8) return 0;
    unsigned id = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned ch = title[i];
        if (ch >= '0' && ch <= '9') ch -= '0';
        else if (ch >= 'a' && ch <= 'f') ch = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') ch = ch - 'A' + 10;
        else return 0;
        id = (id << 4) | ch;
    }
    return id == FH2_TITLE_ID;
}

static int resolve_game_path(const void *package) {
    if (!package_text(package, 0xc0, filename, ARRAY_COUNT(filename))) return 0;
    const wchar_t *name = filename;
    for (const wchar_t *p = filename; *p; ++p) if (*p == L'\\' || *p == L'/') name = p + 1;
    if (lstrcmpiW(name, GAME_EXE_NAME)) return 0;
    DWORD size = GetFullPathNameW(filename, PATH_CAPACITY, game_path, NULL);
    return size && size < PATH_CAPACITY;
}

static int game_image_is_readable(unsigned char *base) {
    uintptr_t cursor = (uintptr_t)base, end = cursor + GAME_IMAGE_SIZE;
    if (end < cursor) {
        return 0;
    }
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((void *)cursor, &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT || region.AllocationBase != base ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            return 0;
        }
        DWORD protection = region.Protect & 0xff;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) {
            return 0;
        }
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) {
            return 0;
        }
        cursor = next;
    }
    return 1;
}

static int change_loader(int install) {
    const unsigned rvas[] = {MAP_GAME_THUNK, MAP_GAME_CALL};
    const unsigned sizes[] = {sizeof(original_thunk), sizeof(original_call)};
    const unsigned char *original[] = {original_thunk, original_call};
    const unsigned char *replacement[] = {installed_thunk, installed_call};
    HostCodePatch patches[2];
    HostPatchSet sets[2];
    for (unsigned i = 0; i < 2; ++i) {
        patches[i] = (HostCodePatch){0, sizes[i], install ? replacement[i] : original[i]};
        sets[i] = (HostPatchSet){rvas[i], sizes[i],
            install ? original_hashes[i] : installed_hashes[i],
            install ? installed_hashes[i] : original_hashes[i], &patches[i], 1};
    }
    return azimuth_patch_host_codes(host_image, sets, ARRAY_COUNT(sets)) > 0;
}

static void *on_map_game(void *output, const void *folder, const void *package, const void *options) {
    /* Restore the call and padding before entering native code. The mapper
       retains its own TLS reserve, imports, protection and error handling. */
    int first = !InterlockedExchange(&handed_off, 1);
    int restored = first && change_loader(0);
    void *result = ((MapGame)(host_image + MAP_GAME))(output, folder, package, options);
    DWORD error = GetLastError();
    if (!first) { SetLastError(error); return result; }
    if (!restored) {
        refuse("REFUSED: could not restore Azimuth's game-loading call. No game patches applied.");
    } else if (!package_is_fh2(package)) {
        status_set("INACTIVE: the loaded title is not Forza Horizon 2 (1A96545B).");
    } else if (!resolve_game_path(package)) {
        refuse("REFUSED: cannot resolve the loaded FH2 executable path. No game patches applied.");
    } else {
        unsigned char *context = NULL, *game = NULL;
        IMAGE_DOS_HEADER dos;
        IMAGE_NT_HEADERS64 nt;
        if (!memory_read_pointer(result, (void **)&context) ||
            !memory_read_pointer(context, (void **)&game) ||
            !memory_read(game, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew < 64 || dos.e_lfanew > 4096 ||
            !memory_read(game + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.TimeDateStamp != GAME_TIMESTAMP ||
            nt.OptionalHeader.SizeOfImage != GAME_IMAGE_SIZE || !game_image_is_readable(game)) {
            refuse("REFUSED: Azimuth's mapped game does not match FH2. No game patches applied.");
        } else if (runtime_initialize(game, game_path, mod_directory)) {
            log_append(host_profile->success);
        } else {
            notify_refusal();
        }
    }
    SetLastError(error);
    return result;
}

static int arm_loader(unsigned char *host) {
    static const unsigned char loader[] = {0x40,0x53,0x56,0x57,0x41,0x56,0x48,0x81,0xec,0xa8,1,0,0};
    unsigned char actual[sizeof(loader)];
    if (!memory_read(host + MAP_GAME, actual, sizeof(actual)) ||
        !memory_equal(actual, loader, sizeof(loader)))
        return refuse("REFUSED: Azimuth's game loading code has changed. No patches applied.");
    host_image = host;
    handed_off = 0;
    for (unsigned i = 0; i < sizeof(original_thunk); ++i) original_thunk[i] = 0xcc;
    const unsigned char jump[] = {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    memory_copy(installed_thunk, jump, sizeof(jump));
    uintptr_t callback = (uintptr_t)on_map_game;
    memory_copy(installed_thunk + 2, &callback, 8);
    original_call[0] = installed_call[0] = 0xe8;
    int32_t native_delta = (int32_t)(MAP_GAME - MAP_GAME_CALL - 5);
    memory_copy(original_call + 1, &native_delta, 4);
    int32_t delta = (int32_t)(MAP_GAME_THUNK - MAP_GAME_CALL - 5);
    memory_copy(installed_call + 1, &delta, 4);
    hash_bytes(original_thunk, sizeof(original_thunk), original_hashes[0]);
    hash_bytes(original_call, sizeof(original_call), original_hashes[1]);
    hash_bytes(installed_thunk, sizeof(installed_thunk), installed_hashes[0]);
    hash_bytes(installed_call, sizeof(installed_call), installed_hashes[1]);
    if (!change_loader(1))
        return refuse("REFUSED: Azimuth's game loading handoff is unavailable or changed. No game patches applied.");
    status_set("Waiting for Azimuth to prepare the loaded game.");
    return 1;
}

static int verify_host(unsigned char *host, const wchar_t *path) {
    AzimuthBuild build = azimuth_host_build(host);
    for (unsigned i = 0; i < ARRAY_COUNT(host_profiles); ++i) {
        const HostProfile *profile = &host_profiles[i];
        if (profile->build == build && verify_host_file(path, profile)) {
            host_profile = profile;
            return 1;
        }
    }
    return refuse("REFUSED: unsupported Azimuth build. Extra Options " MOD_VERSION
                  " supports the inspected Azimuth 1.0.1 and 1.0.2 executables only. "
                  "No game or save compatibility patches applied.");
}

void azimuth_proxy_start(void *caller) {
    if (InterlockedCompareExchange(&initialized, 1, 0)) return;
    DWORD length = GetModuleFileNameW(NULL, filename, PATH_CAPACITY);
    if (!length || length >= PATH_CAPACITY) return;
    const wchar_t *name = filename;
    for (const wchar_t *p = filename; *p; ++p) if (*p == L'\\' || *p == L'/') name = p + 1;
    if (lstrcmpiW(name, L"azimuth.exe")) return;
    unsigned char *host = (unsigned char *)GetModuleHandleW(NULL);
    if (!directory_from_module(mod_instance, mod_directory)) {
        refuse("REFUSED: cannot resolve the mod path. No patches applied.");
        return;
    }
    /* File fingerprint once at startup, never on a gameplay or per-file path. */
    if (!verify_host(host, filename)) return;
    if (caller != host + host_profile->startup_return) {
        refuse("REFUSED: unexpected Azimuth initialization order. No patches applied.");
        return;
    }
    if (arm_loader(host)) {
        monitor_arm(host, mod_directory);
    }
}

void adapter_process_attach(HINSTANCE instance) {
    mod_instance = instance;
    DisableThreadLibraryCalls(instance);
    /* [Monitor] (off by default): Azimuth creates its D3D12 device early, so
       the monitor's device hook is armed here, before Azimuth's code runs; the
       monitor itself loads at that device creation, outside the loader lock. */
    if (directory_from_module(instance, mod_directory) && monitor_requested(mod_directory)) {
        unsigned char *host = (unsigned char *)GetModuleHandleW(NULL);
        if (azimuth_host_build(host) != AZIMUTH_UNKNOWN) {
            monitor_arm(host, mod_directory);
        }
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestArmAzimuth(void *host, const wchar_t *path,
                                         const wchar_t *directory, const wchar_t *game) {
    (void)game;
    lstrcpyW(mod_directory, directory);
    return verify_host(host, path) && arm_loader(host);
}
__declspec(dllexport) int Fh2TestAzimuthNotifications(void) { return notifications; }
__declspec(dllexport) int Fh2TestAzimuthPackage(const void *package, wchar_t *output) {
    if (!package_is_fh2(package)) return -1;
    if (!resolve_game_path(package)) return 0;
    lstrcpyW(output, game_path);
    return 1;
}
#endif
