#include "game_input.h"
#include "autodrive_game.h"
#include "logging.h"
#include "bindings.h"
#include "support.h"

typedef SHORT(WINAPI *KeyState)(int);
typedef HWND(WINAPI *ForegroundWindow)(void);
typedef DWORD(WINAPI *WindowProcess)(HWND, LPDWORD);

static KeyState key_state;
static ForegroundWindow foreground_window;
static WindowProcess window_process;
static int input_initialized;

#ifdef FH2_TWEAKS_TEST
static int use_test_input;
static KeyboardInput test_input;
static unsigned char test_keys[256];
#endif

static int initialize_input(void) {
    if (!input_initialized) {
        input_initialized = 1;
        HMODULE module = LoadLibraryExW(L"user32.dll", 0, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module) {
            key_state = (KeyState)(uintptr_t)GetProcAddress(module, "GetAsyncKeyState");
            foreground_window =
                (ForegroundWindow)(uintptr_t)GetProcAddress(module, "GetForegroundWindow");
            window_process =
                (WindowProcess)(uintptr_t)GetProcAddress(module, "GetWindowThreadProcessId");
        }
        if (!key_state || !foreground_window || !window_process) {
            log_append("INPUT: keyboard API unavailable; keyboard bindings are inactive.\r\n");
        }
    }
    return key_state && foreground_window && window_process;
}

int autodrive_game_window_focused(void) {
#ifdef FH2_TWEAKS_TEST
    if (use_test_input) {
        return test_input.focused;
    }
#endif
    if (!initialize_input()) {
        return 0;
    }
    HWND window = foreground_window();
    DWORD process_id = 0;
    if (window) {
        window_process(window, &process_id);
    }
    return process_id == GetCurrentProcessId();
}

int game_key_down(unsigned key) {
    if (!key || key > 255) return 0;
#ifdef FH2_TWEAKS_TEST
    if (use_test_input) return test_input.focused && test_keys[key];
#endif
    return initialize_input() && autodrive_game_window_focused() && (key_state((int)key)&0x8000)!=0;
}

int game_triggers_down(unsigned triggers) {
    if (!triggers) return 1;
    void *controller;
    unsigned short buttons;
    unsigned char values[2];
    return autodrive_game_buttons(&controller, &buttons) == 0 &&
           memory_read((unsigned char *)controller + 0x70, values, sizeof(values)) &&
           (!(triggers & 1) || values[0]) && (!(triggers & 2) || values[1]);
}

KeyboardInput game_input_read(void) {
    KeyboardInput input={0};
    input.focused=autodrive_game_window_focused();
    input.toggle=binding_key_down(BIND_AUTODRIVE);
    input.cancel=binding_key_down(BIND_CANCEL_AUTODRIVE);
    void *controller;unsigned short buttons;
    if (input.focused && autodrive_game_buttons(&controller,&buttons)==0)
        input.cancel|=binding_pad_down(BIND_CANCEL_AUTODRIVE,buttons);
    return input;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestAutodriveInput(int f2, int escape, int focus) {
    use_test_input = 1;
    test_input = (KeyboardInput){f2, escape, focus};
    test_keys[VK_F2]=f2!=0;
    test_keys[VK_ESCAPE]=escape!=0;
}
#endif

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestKeyboardKey(unsigned key,int down,int focused) {
    use_test_input=1;test_input.focused=focused;
    if(key<256)test_keys[key]=down!=0;
}
#endif
