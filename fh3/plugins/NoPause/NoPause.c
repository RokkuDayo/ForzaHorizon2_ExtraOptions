/* NoPause for FH3: keeps Forza Horizon 3 running when its window loses focus.
   Port of FH2NoPauseWhenLosingFocus to the FH3 Extra Options mod loader.

   - The game window's WndProc swallows "you lost focus" messages
     (WM_ACTIVATEAPP false, WM_ACTIVATE inactive, WM_NCACTIVATE inactive,
     WM_KILLFOCUS).
   - GetForegroundWindow answers with the game window: user32's function is
     detoured in place, because Azimuth copies its address into each pad's
     record when the pad connects and only polls a pad while that call
     returns the game window. GetActiveWindow and GetFocus are redirected in
     every module's import table.
   - Windows.Gaming.Input then returns neutral readings to the unfocused
     window, so IGamepad::GetCurrentReading (vtable slot 8) is replaced and,
     while unfocused, filled from every connected XInput pad (not gated).

   Build: clang --target=x86_64-w64-mingw32 -shared -O2 -I ../.. NoPause.c -o NoPause.dll
   Install: mods\NoPause\plugins\NoPause.dll next to azimuth.exe
   Settings: mods\NoPause\NoPause.ini, [Settings] KeepRunning=1 */
#include "fh3_plugin.h"
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static const FH3PluginContext *host;
static volatile LONG keep = 1;
static HWND volatile game_window;
static WNDPROC original_proc;
static HMODULE self_module;

typedef HWND(WINAPI *WindowQuery)(void);
static WindowQuery real_foreground, real_active, real_focus;

static void say(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    host->log_utf8(line);
}

/* ---- focus queries ---------------------------------------------------------------- */

static HWND answer(WindowQuery real) {
    HWND window = game_window;
    return keep && window ? window : real();
}
static HWND WINAPI hooked_foreground(void) { return answer(real_foreground); }
static HWND WINAPI hooked_active(void) { return answer(real_active); }
static HWND WINAPI hooked_focus(void) { return answer(real_focus); }

static int game_unfocused(void) {
    HWND window = game_window;
    return window && real_foreground && real_foreground() != window;
}

/* Azimuth copies GetForegroundWindow's address into each pad's record when
   the pad connects (before plugins load), so an import patch alone misses
   it. user32!GetForegroundWindow is therefore also detoured in place: its
   prologue (mov [rsp+8],rbx; push rdi; sub rsp,20h; mov rcx,[rip+x]) moves
   to a trampoline, and a 14-byte absolute jump to the hook replaces it.
   Nothing is patched if the prologue differs. */
static const unsigned char foreground_prologue[13] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48,
                                                      0x83, 0xec, 0x20, 0x48, 0x8b, 0x0d};

static int detour_foreground(void) {
    unsigned char *target = (unsigned char *)real_foreground;
    for (unsigned i = 0; i < sizeof(foreground_prologue); ++i)
        if (target[i] != foreground_prologue[i]) return 0;
    int32_t displacement;
    memcpy(&displacement, target + 13, 4);
    unsigned char *slot = target + 17 + displacement;
    unsigned char *trampoline =
        VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return 0;
    unsigned char *at = trampoline;
    memcpy(at, target, 10); /* mov [rsp+8],rbx; push rdi; sub rsp,20h */
    at += 10;
    *at++ = 0x48, *at++ = 0xb9; /* mov rcx, slot */
    memcpy(at, &slot, 8);
    at += 8;
    *at++ = 0x48, *at++ = 0x8b, *at++ = 0x09; /* mov rcx, [rcx] */
    unsigned char *resume = target + 17;
    *at++ = 0xff, *at++ = 0x25, *at++ = 0, *at++ = 0, *at++ = 0, *at++ = 0; /* jmp [rip] */
    memcpy(at, &resume, 8);
    DWORD protection;
    if (!VirtualProtect(trampoline, 64, PAGE_EXECUTE_READ, &protection)) return 0;
    unsigned char jump[17] = {0xff, 0x25, 0, 0, 0, 0};
    void *hook = (void *)hooked_foreground;
    memcpy(jump + 6, &hook, 8);
    jump[14] = jump[15] = jump[16] = 0x90;
    if (!VirtualProtect(target, sizeof(jump), PAGE_EXECUTE_READWRITE, &protection)) return 0;
    real_foreground = (WindowQuery)(void *)trampoline; /* before the jump goes live */
    memcpy(target, jump, sizeof(jump));
    VirtualProtect(target, sizeof(jump), protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(jump));
    return 1;
}

/* Point every import of user32's real function at the hook, by value, so
   name, ordinal and API-set imports are all covered. Returns slots patched. */
static unsigned patch_module(HMODULE module, void *real, void *hook) {
    unsigned char *base = (unsigned char *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return 0;
    IMAGE_DATA_DIRECTORY directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || !directory.Size) return 0;
    unsigned patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *entry =
             (IMAGE_IMPORT_DESCRIPTOR *)(base + directory.VirtualAddress);
         entry->Name; ++entry) {
        void **slot = (void **)(base + entry->FirstThunk);
        for (; *slot; ++slot) {
            if (*slot != real) continue;
            DWORD protection;
            if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) continue;
            InterlockedExchangePointer(slot, hook);
            VirtualProtect(slot, sizeof(*slot), protection, &protection);
            ++patched;
        }
    }
    return patched;
}

static void *user32_foreground; /* the exported address imports point at */

static void patch_imports(int report) {
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) return;
    unsigned count = needed / sizeof(HMODULE), total = 0;
    if (count > 1024) count = 1024;
    for (unsigned i = 0; i < count; ++i) {
        if (modules[i] == self_module) continue;
        unsigned patched = (user32_foreground == (void *)real_foreground
                                ? patch_module(modules[i], user32_foreground, (void *)hooked_foreground)
                                : 0) +
                           patch_module(modules[i], (void *)real_active, (void *)hooked_active) +
                           patch_module(modules[i], (void *)real_focus, (void *)hooked_focus);
        if (patched && report) {
            wchar_t name[MAX_PATH] = L"";
            K32GetModuleBaseNameW(GetCurrentProcess(), modules[i], name, MAX_PATH);
            say("NoPause: focus queries redirected in %ls (%u imports).", name, patched);
        }
        total += patched;
    }
    (void)total;
}

/* ---- window ----------------------------------------------------------------------- */

static volatile LONG focus_messages;
static volatile LONG xinput_reads;

static LRESULT CALLBACK game_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (keep) {
        int swallow = (message == WM_ACTIVATEAPP && !wparam) ||
                      (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) ||
                      message == WM_KILLFOCUS;
        if (swallow) {
            InterlockedIncrement(&focus_messages);
            return 0;
        }
        if (message == WM_NCACTIVATE && !wparam) { /* TRUE: deactivation "accepted" */
            InterlockedIncrement(&focus_messages);
            return TRUE;
        }
    }
    return CallWindowProcW(original_proc, window, message, wparam, lparam);
}

typedef struct {
    HWND best;
    long area;
} Find;

/* The game's window: the biggest visible top-level window of this process.
   Tool windows and click-through overlays don't count. */
static BOOL CALLBACK find_window(HWND window, LPARAM data) {
    Find *find = (Find *)data;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
        return TRUE;
    LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (style & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) return TRUE;
    RECT client;
    if (!GetClientRect(window, &client)) return TRUE;
    long area = (client.right - client.left) * (client.bottom - client.top);
    if (area > find->area) {
        find->area = area;
        find->best = window;
    }
    return TRUE;
}

static void subclass(HWND window) {
    if ((WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC) == game_proc) return;
    WNDPROC previous =
        (WNDPROC)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)game_proc);
    if (!previous) {
        say("NoPause: couldn't subclass the game window (error %lu).", GetLastError());
        return;
    }
    original_proc = previous;
    game_window = window;
    wchar_t title[96] = L"", name[96] = L"";
    GetWindowTextW(window, title, 96);
    GetClassNameW(window, name, 96);
    say("NoPause: watching game window %p (class %ls, title \"%ls\").", (void *)window, name, title);
}

/* The window comes and goes (loading, mode switches) and modules load late,
   so keep checking. One window at a time: original_proc is shared. */
static DWORD WINAPI watcher(LPVOID unused) {
    (void)unused;
    LONG reported = 0;
    for (unsigned pass = 0;; ++pass) {
        Find find = {NULL, 0};
        EnumWindows(find_window, (LPARAM)&find);
        HWND current = game_window;
        if (find.best && (!current || !IsWindow(current))) subclass(find.best);
        patch_imports(pass == 0);
        LONG swallowed = focus_messages;
        if (swallowed != reported && (reported == 0 || swallowed - reported >= 50)) {
            say("NoPause: %ld focus-loss messages swallowed so far.", swallowed);
            reported = swallowed;
        }
        Sleep(1500);
    }
    return 0;
}

/* ---- controller ------------------------------------------------------------------- */

/* Windows.Gaming.Input.GamepadReading */
typedef struct {
    UINT64 timestamp;
    UINT32 buttons;
    double left_trigger, right_trigger;
    double left_x, left_y, right_x, right_y;
} GamepadReading;

typedef struct {
    DWORD packet;
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT left_x, left_y, right_x, right_y;
} PadState;

typedef HRESULT(STDMETHODCALLTYPE *ReadingFn)(void *, GamepadReading *);
static DWORD(WINAPI *pad_state)(DWORD, PadState *);
static ReadingFn real_reading;

static UINT32 map_buttons(WORD buttons) {
    static const struct {
        WORD xinput;
        UINT32 gamepad;
    } map[] = {
        {0x0010, 0x1},    {0x0020, 0x2},    {0x1000, 0x4},   {0x2000, 0x8},   {0x4000, 0x10},
        {0x8000, 0x20},   {0x0001, 0x40},   {0x0002, 0x80},  {0x0004, 0x100}, {0x0008, 0x200},
        {0x0100, 0x400},  {0x0200, 0x800},  {0x0040, 0x1000}, {0x0080, 0x2000},
    };
    UINT32 result = 0;
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
        if (buttons & map[i].xinput) result |= map[i].gamepad;
    return result;
}

static double axis(SHORT value) { return value < 0 ? value / 32768.0 : value / 32767.0; }

static HRESULT STDMETHODCALLTYPE hooked_reading(void *self, GamepadReading *out) {
    HRESULT result = real_reading(self, out);
    if (!keep || !pad_state || !out || !game_unfocused()) return result;
    /* Merge every connected pad (the game may be driven by any of them):
       buttons combined, the strongest trigger and stick deflection win. */
    PadState merged = {0};
    int connected = 0;
    for (DWORD i = 0; i < 4; ++i) {
        PadState state;
        if (pad_state(i, &state) != ERROR_SUCCESS) continue;
        ++connected;
        merged.buttons |= state.buttons;
        if (state.left_trigger > merged.left_trigger) merged.left_trigger = state.left_trigger;
        if (state.right_trigger > merged.right_trigger) merged.right_trigger = state.right_trigger;
        SHORT *from = &state.left_x, *to = &merged.left_x;
        for (int k = 0; k < 4; ++k)
            if ((from[k] < 0 ? -(int)from[k] : from[k]) > (to[k] < 0 ? -(int)to[k] : to[k])) to[k] = from[k];
    }
    if (!connected) return result;
    out->buttons = map_buttons(merged.buttons);
    out->left_trigger = merged.left_trigger / 255.0;
    out->right_trigger = merged.right_trigger / 255.0;
    out->left_x = axis(merged.left_x);
    out->left_y = axis(merged.left_y);
    out->right_x = axis(merged.right_x);
    out->right_y = axis(merged.right_y);
    if (InterlockedIncrement(&xinput_reads) == 1)
        say("NoPause: feeding XInput (%d pad%s) into unfocused readings.", connected,
            connected == 1 ? "" : "s");
    return S_OK;
}

typedef struct HSTRING__ *HSTRING;
typedef struct {
    void *reserved[3];
} StringHeader;
typedef HRESULT(WINAPI *ActivationFactory)(HSTRING, REFIID, void **);
typedef HRESULT(WINAPI *StringReference)(const wchar_t *, UINT32, StringHeader *, HSTRING *);

/* Find IGamepad::GetCurrentReading through the Gamepad statics and replace
   its vtable slot. Needs one connected pad; the code is shared by all pads. */
static DWORD WINAPI pad_hook(LPVOID unused) {
    (void)unused;
    HMODULE xinput = LoadLibraryExW(L"xinput1_4.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!xinput) xinput = LoadLibraryExW(L"xinput9_1_0.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (xinput) pad_state = (void *)GetProcAddress(xinput, "XInputGetState");
    HMODULE combase = LoadLibraryExW(L"combase.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    ActivationFactory factory =
        combase ? (ActivationFactory)(void *)GetProcAddress(combase, "RoGetActivationFactory") : NULL;
    StringReference reference =
        combase ? (StringReference)(void *)GetProcAddress(combase, "WindowsCreateStringReference")
                : NULL;
    if (!pad_state || !factory || !reference) {
        say("NoPause: XInput or WinRT unavailable; the pad stays foreground-only.");
        return 0;
    }
    HRESULT(WINAPI *initialize)(void *, DWORD) =
        (HRESULT(WINAPI *)(void *, DWORD))(void *)GetProcAddress(combase, "CoInitializeEx");
    if (initialize) initialize(NULL, 0); /* COINIT_MULTITHREADED */
    static const GUID statics_id = {
        0x8BBCE529, 0xD49C, 0x39E9, {0x95, 0x60, 0xE4, 0x7D, 0xDE, 0x96, 0xB7, 0xC8}};
    static const wchar_t name[] = L"Windows.Gaming.Input.Gamepad";
    StringHeader header;
    HSTRING class_name;
    if (FAILED(reference(name, (UINT32)(sizeof(name) / sizeof(name[0]) - 1), &header, &class_name)))
        return 0;
    typedef HRESULT(STDMETHODCALLTYPE * GetFn)(void *, void **);
    typedef HRESULT(STDMETHODCALLTYPE * SizeFn)(void *, UINT32 *);
    typedef HRESULT(STDMETHODCALLTYPE * AtFn)(void *, UINT32, void **);
    for (int tries = 0; tries < 600; ++tries, Sleep(1000)) {
        void **statics = NULL, **pads = NULL, **pad = NULL;
        if (FAILED(factory(class_name, &statics_id, (void **)&statics)) || !statics) continue;
        /* IGamepadStatics: 10 get_Gamepads. IVectorView: 6 GetAt, 7 get_Size. */
        UINT32 size = 0;
        if (SUCCEEDED(((GetFn)(*(void ***)statics)[10])(statics, (void **)&pads)) && pads) {
            ((SizeFn)(*(void ***)pads)[7])(pads, &size);
            if (size) ((AtFn)(*(void ***)pads)[6])(pads, 0, (void **)&pad);
        }
        if (!pad) continue;
        void **slot = &(*(void ***)pad)[8]; /* IGamepad::GetCurrentReading */
        DWORD protection;
        if (*slot == (void *)hooked_reading) return 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
            say("NoPause: couldn't hook the pad reading (error %lu).", GetLastError());
            return 0;
        }
        real_reading = (ReadingFn)*slot;
        InterlockedExchangePointer(slot, (void *)hooked_reading);
        VirtualProtect(slot, sizeof(*slot), protection, &protection);
        say("NoPause: Gamepad::GetCurrentReading hooked at %p.", (void *)real_reading);
        return 0;
    }
    say("NoPause: no Windows.Gaming.Input gamepad found in 10 minutes; pad hook not installed.");
    return 0;
}

/* ---- plugin ----------------------------------------------------------------------- */

__declspec(dllexport) BOOL WINAPI FH3PluginInitialize(const FH3PluginContext *context) {
    if (!context || context->api_version < FH3_PLUGIN_API_VERSION) return FALSE;
    host = context;
    self_module = context->plugin_module;
    wchar_t ini[MAX_PATH];
    _snwprintf(ini, MAX_PATH, L"%ls\\NoPause.ini", context->mod_directory);
    ini[MAX_PATH - 1] = 0;
    keep = GetPrivateProfileIntW(L"Settings", L"KeepRunning", 1, ini) != 0;
    if (!keep) {
        say("NoPause: off (KeepRunning=0 in NoPause.ini).");
        return TRUE;
    }
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    real_foreground = user32 ? (WindowQuery)(void *)GetProcAddress(user32, "GetForegroundWindow") : NULL;
    real_active = user32 ? (WindowQuery)(void *)GetProcAddress(user32, "GetActiveWindow") : NULL;
    real_focus = user32 ? (WindowQuery)(void *)GetProcAddress(user32, "GetFocus") : NULL;
    if (!real_foreground || !real_active || !real_focus) {
        say("NoPause: user32 focus functions not found; off.");
        return TRUE;
    }
    user32_foreground = (void *)real_foreground;
    say(detour_foreground() ? "NoPause: GetForegroundWindow detoured."
                            : "NoPause: GetForegroundWindow prologue unknown; imports only.");
    HANDLE thread = CreateThread(NULL, 0, watcher, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    thread = CreateThread(NULL, 0, pad_hook, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    say("NoPause: ready (on).");
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
