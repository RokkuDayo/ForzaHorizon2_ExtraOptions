#include "monitor_bootstrap.h"
#include "logging.h"
#include "support.h"

typedef FARPROC(WINAPI *GetProcedure)(HMODULE, LPCSTR);
typedef HRESULT(WINAPI *CreateDevice)(void *, unsigned, const IID *, void **);
typedef int (*AttachMonitor)(void *, const wchar_t *);
static GetProcedure original_get;
static CreateDevice original_create;
static volatile LONG armed, attached;
static wchar_t mod_directory[PATH_CAPACITY], monitor_path[PATH_CAPACITY];

/* The monitor is armed in DllMain and attaches at the game's first
   D3D12CreateDevice, both before the mod's log is started (log_start rewrites
   it). Its lines wait here until monitor_log_pending, then go straight out. */
static SRWLOCK note_lock = SRWLOCK_INIT;
static const char *pending_notes[4];
static unsigned pending_count;
static int log_ready;

static void monitor_note(const char *message) {
    AcquireSRWLockExclusive(&note_lock);
    if (log_ready) {
        log_append(message);
    } else if (pending_count < ARRAY_COUNT(pending_notes)) {
        pending_notes[pending_count++] = message;
    }
    ReleaseSRWLockExclusive(&note_lock);
}

void monitor_log_pending(void) {
    AcquireSRWLockExclusive(&note_lock);
    for (unsigned i = 0; i < pending_count; ++i) {
        log_append(pending_notes[i]);
    }
    pending_count = 0;
    log_ready = 1;
    ReleaseSRWLockExclusive(&note_lock);
}

/* The monitor needs the game folder and a device, so it loads right after the
   game's first successful D3D12CreateDevice (never under the loader lock). */
static HRESULT WINAPI create_device(void *adapter, unsigned level, const IID *iid, void **output) {
    HRESULT result = original_create(adapter, level, iid, output);
    DWORD error = GetLastError();
    if (SUCCEEDED(result) && output && *output && InterlockedCompareExchange(&attached, 1, 0) == 0) {
        HMODULE monitor = LoadLibraryExW(monitor_path, NULL,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        AttachMonitor attach = monitor ? (AttachMonitor)(uintptr_t)GetProcAddress(monitor, "MonAttach") : NULL;
        monitor_note(attach && attach(*output, mod_directory)
                       ? "MONITOR: fh2-monitor.dll attached (FPS counter, perf log).\r\n"
                       : "MONITOR: monitor\\fh2-monitor.dll unavailable; FPS counter and perf log are off.\r\n");
    }
    SetLastError(error);
    return result;
}

static FARPROC WINAPI get_procedure(HMODULE module, LPCSTR name) {
    FARPROC result = original_get(module, name);
    if (result && (uintptr_t)name > 65535 && text_equal_ignore_case(name, "D3D12CreateDevice")) {
        void *previous = InterlockedCompareExchangePointer((void *volatile *)&original_create,
                                                           (void *)(uintptr_t)result, NULL);
        if (!previous || previous == (void *)(uintptr_t)result) return (FARPROC)(uintptr_t)create_device;
    }
    return result;
}

int monitor_requested(const wchar_t *directory) {
    static wchar_t path[PATH_CAPACITY];
    return path_join(path, directory, MOD_INI_NAME) &&
           (GetPrivateProfileIntW(L"Monitor", L"ShowFps", 0, path) > 0 ||
            GetPrivateProfileIntW(L"Monitor", L"PerfLog", 0, path) == 1);
}

void monitor_arm(unsigned char *host, const wchar_t *directory) {
    if (InterlockedCompareExchange(&armed, 1, 0) || !monitor_requested(directory)) return;
    unsigned count = 0;
    while (directory[count] && count + 1 < PATH_CAPACITY) { mod_directory[count] = directory[count]; ++count; }
    mod_directory[count] = 0;
    if (!path_join(monitor_path, directory, L"monitor\\fh2-monitor.dll")) return;
    DWORD attributes = GetFileAttributesW(monitor_path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        monitor_note("MONITOR: monitor\\fh2-monitor.dll not found; FPS counter and perf log are off.\r\n");
        return;
    }
    HMODULE pinned = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(uintptr_t)monitor_arm, &pinned)) return;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)host;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(host + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) return;
    const IMAGE_IMPORT_DESCRIPTOR *descriptor = (const IMAGE_IMPORT_DESCRIPTOR *)(host + imports.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->OriginalFirstThunk) continue;
        int d3d12 = text_equal_ignore_case((const char *)(host + descriptor->Name), "d3d12.dll");
        const IMAGE_THUNK_DATA64 *names = (const IMAGE_THUNK_DATA64 *)(host + descriptor->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (IMAGE_THUNK_DATA64 *)(host + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            int ordinal = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal) != 0;
            const char *name = ordinal ? "" : (const char *)((const IMAGE_IMPORT_BY_NAME *)(host + names->u1.AddressOfData))->Name;
            /* The verified FH2 launcher imports D3D12CreateDevice by ordinal 101. */
            int direct = d3d12 && (ordinal ? IMAGE_ORDINAL64(names->u1.Ordinal) == 101 :
                text_equal_ignore_case(name, "D3D12CreateDevice"));
            if (!direct && (ordinal || !text_equal_ignore_case(name, "GetProcAddress"))) continue;
            void *expected;
            void *replacement;
            if (direct) {
                original_create = (CreateDevice)(uintptr_t)slots->u1.Function;
                expected = (void *)(uintptr_t)original_create;
                replacement = (void *)(uintptr_t)create_device;
            } else {
                original_get = GetProcAddress;
                expected = (void *)(uintptr_t)original_get;
                replacement = (void *)(uintptr_t)get_procedure;
            }
            if (slots->u1.Function != (uint64_t)(uintptr_t)expected) {
                monitor_note("MONITOR: another module owns the host procedure resolver; monitor not loaded.\r\n");
                return;
            }
            DWORD old, ignored;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return;
            void *previous = InterlockedCompareExchangePointer((void *volatile *)&slots->u1.Function,
                replacement, expected);
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &ignored);
            if (previous == expected)
                monitor_note("MONITOR: fh2-monitor.dll loads at the next D3D12 device creation.\r\n");
            if (direct) return;
        }
    }
    monitor_note("MONITOR: host procedure resolver unavailable; monitor not loaded.\r\n");
}
