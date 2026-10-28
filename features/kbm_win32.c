#include "kbm_win32.h"
#include "logging.h"
#include "support.h"

#ifdef FH2_AZIMUTH
#include "adapters/azimuth_host.h"
#endif

/* Azimuth host globals and routines, per inspected build (RVAs in
   azimuth.exe). The window global holds the game HWND; the prompt flag is set
   while Azimuth's own overlay/quit/text prompt is open (its gamepad reader
   then returns empty readings). The cursor flag makes Azimuth's WM_SETCURSOR
   show the pointer instead of hiding it in fullscreen. Each global is proven
   by the instruction that uses it (verify_host). */
#define HOST_WINDOW_RVA (profile->window_rva)
#define HOST_PROMPT_RVA (profile->prompt_rva)
#define HOST_WINDOW_USE_RVA (profile->window_use_rva) /* mov rcx, [rip+window] */
#define HOST_PROMPT_USE_RVA (profile->prompt_use_rva) /* cmp byte [rip+prompt], 0 */
#define HOST_CURSOR_RVA (profile->cursor_rva)
#define HOST_CURSOR_USE_RVA (profile->cursor_use_rva) /* cmp [rip+cursor], r14b */

#define RING_SIZE 512u

#ifdef FH2_AZIMUTH
typedef struct {
    AzimuthBuild build;
    unsigned window_rva, prompt_rva, window_use_rva, prompt_use_rva, cursor_rva, cursor_use_rva;
    /* The cursor flag's use: cmp [rip+cursor], r14b (44 38 35), r14 being zero
       there; WM_SETCURSOR shows the arrow when the flag is set and otherwise
       hides it while Azimuth's own hide flag is set. */
    unsigned cursor_opcode_length;
    unsigned char cursor_opcode[3];
    /* Azimuth's Windows.Xbox.Input.IGamepad implementation: slot 11 is
       SetVibration, which every game rumble (impulse triggers included) goes
       through; slots 12/13 are GetCurrentReading and GetRawCurrentReading. */
    unsigned set_vibration_slot, set_vibration, get_reading, get_raw_reading;
} KbmHost;

static const KbmHost hosts[] = {
    {AZIMUTH_102, 0x909d20u, 0x95b97au, 0x6e7e4u, 0x5caec4u, 0x909d50u, 0x6fbe1u, 3,
     {0x44, 0x38, 0x35}, 0x801930u, 0x2a0590u, 0x2a0650u, 0x2a0720u},
};
static const KbmHost *profile;
#endif

#define HOST_SET_VIBRATION_SLOT (profile->set_vibration_slot)
#define HOST_SET_VIBRATION (profile->set_vibration)
#define HOST_GET_READING (profile->get_reading)
#define HOST_GET_RAW_READING (profile->get_raw_reading)

typedef ATOM(WINAPI *RegisterClassFn)(const WNDCLASSEXW *);
typedef HWND(WINAPI *CreateWindowFn)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND,
                                     HMENU, HINSTANCE, LPVOID);
typedef LRESULT(WINAPI *DefWindowProcFn)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL(WINAPI *GetMessageFn)(LPMSG, HWND, UINT, UINT);
typedef LRESULT(WINAPI *DispatchMessageFn)(const MSG *);
typedef BOOL(WINAPI *RegisterRawFn)(PCRAWINPUTDEVICE, UINT, UINT);
typedef UINT(WINAPI *GetRegisteredRawFn)(PRAWINPUTDEVICE, PUINT, UINT);
typedef UINT(WINAPI *GetRawDataFn)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
typedef HWND(WINAPI *GetForegroundFn)(void);
typedef BOOL(WINAPI *IsWindowFn)(HWND);
typedef DWORD(WINAPI *WindowThreadFn)(HWND, LPDWORD);
typedef SHORT(WINAPI *AsyncKeyFn)(int);
typedef UINT(WINAPI *MapKeyFn)(UINT, UINT);
typedef BOOL(WINAPI *ClipCursorFn)(const RECT *);
typedef BOOL(WINAPI *ClientRectFn)(HWND, LPRECT);
typedef BOOL(WINAPI *ClientToScreenFn)(HWND, LPPOINT);
typedef BOOL(WINAPI *CursorPosFn)(LPPOINT);

static struct {
    RegisterClassFn register_class;
    CreateWindowFn create_window;
    DefWindowProcFn default_proc;
    GetMessageFn get_message;
    DispatchMessageFn dispatch;
    RegisterRawFn register_raw;
    GetRegisteredRawFn registered_raw;
    GetRawDataFn raw_data;
    GetForegroundFn foreground;
    IsWindowFn is_window;
    WindowThreadFn window_thread;
    AsyncKeyFn async_key;
    MapKeyFn map_key;
    ClipCursorFn clip_cursor;
    ClientRectFn client_rect;
    ClientToScreenFn client_to_screen;
    ClientToScreenFn screen_to_client;
    CursorPosFn cursor_pos;
} user;

static unsigned char *host;
static volatile LONG state = KBM_WIN32_STARTING, started, raw_mouse;
static KbmKeyEvent ring[RING_SIZE];
static volatile LONG ring_head, ring_tail, ring_overflow;
static volatile LONG mouse_dx, mouse_dy, mouse_wheel;
static unsigned char raw_down[256];
static unsigned char vk_of[256];
static int captured, absolute_mouse_logged;
static uint64_t last_capture_refresh;

static int load_user32(void) {
    HMODULE module = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        return 0;
    }
#define USER_FN(field, type, name) user.field = (type)(uintptr_t)GetProcAddress(module, name)
    USER_FN(register_class, RegisterClassFn, "RegisterClassExW");
    USER_FN(create_window, CreateWindowFn, "CreateWindowExW");
    USER_FN(default_proc, DefWindowProcFn, "DefWindowProcW");
    USER_FN(get_message, GetMessageFn, "GetMessageW");
    USER_FN(dispatch, DispatchMessageFn, "DispatchMessageW");
    USER_FN(register_raw, RegisterRawFn, "RegisterRawInputDevices");
    USER_FN(registered_raw, GetRegisteredRawFn, "GetRegisteredRawInputDevices");
    USER_FN(raw_data, GetRawDataFn, "GetRawInputData");
    USER_FN(foreground, GetForegroundFn, "GetForegroundWindow");
    USER_FN(is_window, IsWindowFn, "IsWindow");
    USER_FN(window_thread, WindowThreadFn, "GetWindowThreadProcessId");
    USER_FN(async_key, AsyncKeyFn, "GetAsyncKeyState");
    USER_FN(map_key, MapKeyFn, "MapVirtualKeyW");
    USER_FN(clip_cursor, ClipCursorFn, "ClipCursor");
    USER_FN(client_rect, ClientRectFn, "GetClientRect");
    USER_FN(client_to_screen, ClientToScreenFn, "ClientToScreen");
    USER_FN(screen_to_client, ClientToScreenFn, "ScreenToClient");
    USER_FN(cursor_pos, CursorPosFn, "GetCursorPos");
#undef USER_FN
    return user.register_class && user.create_window && user.default_proc && user.get_message &&
           user.dispatch && user.register_raw && user.registered_raw && user.raw_data &&
           user.foreground && user.is_window && user.window_thread && user.async_key &&
           user.map_key && user.clip_cursor && user.client_rect && user.client_to_screen &&
           user.screen_to_client && user.cursor_pos;
}

#ifdef FH2_AZIMUTH
static int rip_target(const unsigned char *instruction, unsigned opcode_length, unsigned length,
                      unsigned rva, unsigned expected) {
    int32_t displacement;
    memory_copy(&displacement, instruction + opcode_length, 4);
    return (unsigned)((int64_t)rva + length + displacement) == expected;
}
#endif

static int verify_host(void) {
#ifdef FH2_AZIMUTH
    static const unsigned char window_use[3] = {0x48, 0x8b, 0x0d};
    static const unsigned char prompt_use[2] = {0x80, 0x3d};
    unsigned char *base = (unsigned char *)GetModuleHandleW(NULL);
    unsigned char code[7];
    AzimuthBuild build = azimuth_host_build(base);
    profile = NULL;
    for (unsigned i = 0; i < ARRAY_COUNT(hosts); ++i) {
        if (hosts[i].build == build) {
            profile = &hosts[i];
        }
    }
    if (!profile || !memory_read(base + HOST_WINDOW_USE_RVA, code, 7) ||
        !memory_equal(code, window_use, 3) ||
        !rip_target(code, 3, 7, HOST_WINDOW_USE_RVA, HOST_WINDOW_RVA) ||
        !memory_read(base + HOST_PROMPT_USE_RVA, code, 7) ||
        !memory_equal(code, prompt_use, 2) || code[6] != 0 ||
        !rip_target(code, 2, 7, HOST_PROMPT_USE_RVA, HOST_PROMPT_RVA)) {
        return 0;
    }
    unsigned opcode = profile->cursor_opcode_length;
    if (!memory_read(base + HOST_CURSOR_USE_RVA, code, 7) ||
        !memory_equal(code, profile->cursor_opcode, opcode) || (opcode == 2 && code[6] != 0) ||
        !rip_target(code, opcode, 7, HOST_CURSOR_USE_RVA, HOST_CURSOR_RVA)) {
        return 0;
    }
    host = base;
    return 1;
#else
    return 0;
#endif
}

static HWND game_window(void) {
    HWND window = NULL;
    DWORD process = 0;
    if (!host || !memory_read(host + HOST_WINDOW_RVA, &window, sizeof(window)) || !window ||
        !user.is_window(window)) {
        return NULL;
    }
    user.window_thread(window, &process);
    return process == GetCurrentProcessId() ? window : NULL;
}

int kbm_win32_host_prompt(void) {
    unsigned char prompt = 0;
    return host && memory_read(host + HOST_PROMPT_RVA, &prompt, 1) && prompt;
}

int kbm_win32_focused(void) {
    if (state == KBM_WIN32_STARTING || state == KBM_WIN32_FAILED) {
        return 0;
    }
    HWND window = game_window();
    return window && user.foreground() == window && !kbm_win32_host_prompt();
}

static void push_event(unsigned code, int down) {
    LONG head = ring_head;
    if ((ULONG)(head - ring_tail) >= RING_SIZE) {
        InterlockedExchange(&ring_overflow, 1);
        return;
    }
    KbmKeyEvent *event = &ring[(ULONG)head % RING_SIZE];
    event->code = (unsigned short)code;
    event->down = (unsigned char)down;
    event->time = (uint32_t)GetTickCount64();
    InterlockedExchange(&ring_head, head + 1);
}

int kbm_win32_next_event(KbmKeyEvent *event) {
    LONG tail = ring_tail;
    if (tail == ring_head) {
        return 0;
    }
    *event = ring[(ULONG)tail % RING_SIZE];
    InterlockedExchange(&ring_tail, tail + 1);
    return 1;
}

int kbm_win32_take_overflow(void) {
    return InterlockedExchange(&ring_overflow, 0) != 0;
}

long kbm_win32_take_wheel(void) {
    return InterlockedExchange(&mouse_wheel, 0);
}

int kbm_win32_cursor(float *x, float *y, float *width, float *height) {
    HWND window = game_window();
    RECT client;
    POINT point;
    if (state != KBM_WIN32_RAW_INPUT || !window || !user.cursor_pos(&point) ||
        !user.screen_to_client(window, &point) || !user.client_rect(window, &client) ||
        client.right <= 0 || client.bottom <= 0) {
        return 0;
    }
    *x = (float)point.x;
    *y = (float)point.y;
    *width = (float)client.right;
    *height = (float)client.bottom;
    return point.x >= 0 && point.y >= 0 && point.x < client.right && point.y < client.bottom;
}

/* The menu pointer: a large Horizon-styled arrow drawn here (white body,
   dark outline, pink inner rim) set as the game window's class cursor, so
   Windows draws exactly one pointer. Azimuth hides the pointer in
   fullscreen unless its cursor flag is set; WM_SETCURSOR then falls back
   to the class cursor. Both are restored when the pointer is not wanted. */
typedef HDC(WINAPI *GetDCFn)(HWND);
typedef int(WINAPI *ReleaseDCFn)(HWND, HDC);
typedef HICON(WINAPI *CreateIconFn)(PICONINFO);
typedef ULONG_PTR(WINAPI *ClassLongFn)(HWND, int);
typedef ULONG_PTR(WINAPI *SetClassLongFn)(HWND, int, LONG_PTR);
typedef HBITMAP(WINAPI *CreateDibFn)(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
typedef HBITMAP(WINAPI *CreateBitmapFn)(int, int, UINT, UINT, const void *);
typedef BOOL(WINAPI *DeleteObjectFn)(HGDIOBJ);

static struct {
    CreateIconFn create_icon;
    ClassLongFn get_class;
    SetClassLongFn set_class;
    CreateDibFn create_dib;
    CreateBitmapFn create_bitmap;
    DeleteObjectFn delete_object;
} gdi;
static int cursor_shown, cursor_percent = 250, cursor_pixels;
static HCURSOR game_cursor, class_cursor;

void kbm_win32_set_cursor_size(int percent) {
    cursor_percent = percent;
}

static int load_cursor_api(void) {
    static int loaded = -1;
    if (loaded >= 0) {
        return loaded;
    }
    HMODULE user = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE graphics = LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user && graphics) {
        gdi.create_icon = (CreateIconFn)(uintptr_t)GetProcAddress(user, "CreateIconIndirect");
        gdi.get_class = (ClassLongFn)(uintptr_t)GetProcAddress(user, "GetClassLongPtrW");
        gdi.set_class = (SetClassLongFn)(uintptr_t)GetProcAddress(user, "SetClassLongPtrW");
        gdi.create_dib = (CreateDibFn)(uintptr_t)GetProcAddress(graphics, "CreateDIBSection");
        gdi.create_bitmap = (CreateBitmapFn)(uintptr_t)GetProcAddress(graphics, "CreateBitmap");
        gdi.delete_object = (DeleteObjectFn)(uintptr_t)GetProcAddress(graphics, "DeleteObject");
    }
    loaded = gdi.create_icon && gdi.get_class && gdi.set_class && gdi.create_dib &&
             gdi.create_bitmap && gdi.delete_object;
    return loaded;
}

/* Arrow outline in a 32-unit design grid, tip at the origin. */
static const float arrow[][2] = {{0, 0}, {0, 25}, {6, 19}, {10, 29}, {14, 27.5f}, {10, 18.5f},
                                 {18, 18.5f}};

static float edge_distance(float x, float y, int *inside) {
    unsigned count = ARRAY_COUNT(arrow);
    float best = 1e9f;
    int crossings = 0;
    for (unsigned i = 0, j = count - 1; i < count; j = i++) {
        float ax = arrow[j][0], ay = arrow[j][1], bx = arrow[i][0], by = arrow[i][1];
        if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) {
            crossings ^= 1;
        }
        float dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
        float t = length > 0 ? ((x - ax) * dx + (y - ay) * dy) / length : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float px = ax + t * dx - x, py = ay + t * dy - y, distance = px * px + py * py;
        best = distance < best ? distance : best;
    }
    *inside = crossings;
    float root = best, guess = best > 1 ? best : 1;
    for (int k = 0; k < 12; ++k) {
        guess = 0.5f * (guess + root / guess);
    }
    return best > 0 ? guess : 0;
}

static HCURSOR build_cursor(int size) {
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    uint32_t *pixels = NULL;
    HBITMAP color = gdi.create_dib(NULL, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    HBITMAP mask = gdi.create_bitmap(size, size, 1, 1, NULL);
    if (!color || !mask || !pixels) {
        if (color) {
            gdi.delete_object(color);
        }
        if (mask) {
            gdi.delete_object(mask);
        }
        return NULL;
    }
    float unit = (float)size / 34.0f, outline = 1.6f, rim = 1.4f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float gx = ((float)x + 0.5f) / unit - 1.5f, gy = ((float)y + 0.5f) / unit - 1.5f;
            int inside;
            float distance = edge_distance(gx, gy, &inside);
            uint32_t alpha = 0, rgb = 0;
            if (inside) {
                alpha = 255;
                rgb = distance <= outline          ? 0x141414u
                      : distance <= outline + rim ? 0xff2d7au /* Horizon pink */
                                                  : 0xffffffu;
            } else if (distance < outline * 0.6f) {
                float fade = 1.0f - distance / (outline * 0.6f);
                alpha = (uint32_t)(fade * 255.0f);
                rgb = 0x141414u;
            }
            pixels[y * size + x] = alpha << 24 | rgb;
        }
    }
    ICONINFO icon;
    icon.fIcon = FALSE;
    icon.xHotspot = (DWORD)(1.5f * unit);
    icon.yHotspot = (DWORD)(1.5f * unit);
    icon.hbmMask = mask;
    icon.hbmColor = color;
    HCURSOR cursor = (HCURSOR)gdi.create_icon(&icon);
    gdi.delete_object(color);
    gdi.delete_object(mask);
    return cursor;
}

void kbm_win32_show_cursor(int show) {
    unsigned char *flag = host ? host + HOST_CURSOR_RVA : NULL;
    HWND window = game_window();
    unsigned char current;
    if (!flag || !window || kbm_win32_host_prompt() || !memory_read(flag, &current, 1) ||
        !load_cursor_api()) {
        return;
    }
    if (show) {
        RECT client;
        int height = user.client_rect(window, &client) ? client.bottom - client.top : 2160;
        int size = (int)(32.0f * (float)cursor_percent / 100.0f * (float)height / 2160.0f);
        size = size < 24 ? 24 : size > 256 ? 256 : size;
        if (!game_cursor || size != cursor_pixels) {
            HCURSOR built = build_cursor(size);
            if (built) {
                game_cursor = built; /* an earlier size is kept alive; it may be in use */
                cursor_pixels = size;
                cursor_shown = 0;
            }
        }
        if (!cursor_shown && game_cursor) {
            ULONG_PTR previous = gdi.set_class(window, GCLP_HCURSOR, (LONG_PTR)game_cursor);
            if (previous != (ULONG_PTR)game_cursor) {
                class_cursor = (HCURSOR)previous;
            }
            cursor_shown = 1;
        }
        if (!current) {
            *(volatile unsigned char *)flag = 1;
        }
    } else if (cursor_shown) {
        gdi.set_class(window, GCLP_HCURSOR, (LONG_PTR)class_cursor);
        if (current) {
            *(volatile unsigned char *)flag = 0;
        }
        cursor_shown = 0;
    }
}

void kbm_win32_take_mouse(long *dx, long *dy) {
    *dx = InterlockedExchange(&mouse_dx, 0);
    *dy = InterlockedExchange(&mouse_dy, 0);
}

/* In-game rebinding (kbm_rebind.c). While armed, the next key or mouse
   button press goes to the capture slot instead of the game, and so does its
   release. Alt and the Windows keys, and presses in the first 150 ms after
   arming, are not taken (they reach the game as usual). While activation keys
   are set (the settings page has a key binding row focused), a press of one
   of them (Confirm, Enter by default) is held back from the game with its
   release and reported to the page instead. */
enum { CAPTURE_KEY = 0x10000, CAPTURE_MOUSE = 0x20000, CAPTURE_GRACE_MS = 150 };
static volatile LONG capture_armed, capture_result, capture_ignored, activation_keys, activation;
static volatile LONG64 capture_armed_at;
static unsigned char capture_swallow[256]; /* input thread only */

static int system_key(unsigned code) {
    return code == 0x38 || code == 0xb8 || code == 0xdb || code == 0xdc || code == 0xdd;
}

static int capture_press(unsigned code, LONG result) {
    if (!capture_armed) {
        return 0;
    }
    if (system_key(code) || GetTickCount64() - (uint64_t)capture_armed_at < CAPTURE_GRACE_MS) {
        InterlockedExchange(&capture_ignored, (LONG)code | 0x100);
        return 0;
    }
    if (InterlockedCompareExchange(&capture_armed, 0, 1) != 1) {
        return 0;
    }
    capture_swallow[code & 0xff] = 1;
    InterlockedExchange(&capture_result, result);
    return 1;
}

static int activation_press(unsigned code) {
    LONG keys = activation_keys;
    if (!keys || capture_armed || !code ||
        (code != (keys & 0xff) && code != ((keys >> 8) & 0xff) && code != ((keys >> 16) & 0xff))) {
        return 0;
    }
    capture_swallow[code & 0xff] = 1;
    InterlockedExchange(&activation, (LONG)code);
    return 1;
}

int kbm_win32_capture_arm(void) {
    if (state != KBM_WIN32_RAW_INPUT) {
        return 0;
    }
    InterlockedExchange(&capture_result, 0);
    InterlockedExchange(&capture_ignored, 0);
    InterlockedExchange64(&capture_armed_at, (LONG64)GetTickCount64());
    InterlockedExchange(&capture_armed, 1);
    return 1;
}

void kbm_win32_capture_disarm(void) {
    InterlockedExchange(&capture_armed, 0);
    InterlockedExchange(&capture_result, 0);
}

int kbm_win32_capture_take(unsigned *code) {
    LONG result = InterlockedExchange(&capture_result, 0);
    *code = (unsigned)result & 0xffu;
    return result & CAPTURE_KEY ? 1 : result & CAPTURE_MOUSE ? 2 : 0;
}

unsigned kbm_win32_capture_ignored(void) {
    LONG code = InterlockedExchange(&capture_ignored, 0);
    return code ? (unsigned)code & 0xffu : 0u;
}

void kbm_win32_set_activation_keys(const unsigned short *codes, unsigned count) {
    LONG keys = 0;
    for (unsigned i = 0; i < count && i < 3; ++i) {
        keys |= (LONG)(codes[i] & 0xff) << (8 * i);
    }
    if (state != KBM_WIN32_RAW_INPUT) {
        keys = 0;
    }
    InterlockedExchange(&activation_keys, keys);
}

unsigned kbm_win32_take_activation(void) {
    return (unsigned)InterlockedExchange(&activation, 0);
}

static int modifier_held(void) {
    return raw_down[0x38] || raw_down[0xb8] || raw_down[0xdb] || raw_down[0xdc];
}

static void keyboard_packet(const RAWKEYBOARD *keyboard) {
    unsigned scan = keyboard->MakeCode, flags = keyboard->Flags;
    if (flags & RI_KEY_E1 || keyboard->VKey == 0xff) {
        return; /* Pause and the fake shifts that accompany some extended keys */
    }
    unsigned code = (scan & 0x7f) | (flags & RI_KEY_E0 ? 0x80 : 0);
    if (!scan) {
        /* Injected virtual-key-only input carries no make code. */
        unsigned mapped = user.map_key(keyboard->VKey, 4 /* MAPVK_VK_TO_VSC_EX */);
        if (!mapped) {
            return;
        }
        code = (mapped & 0x7f) | ((mapped >> 8) == 0xe0 || flags & RI_KEY_E0 ? 0x80 : 0);
    }
    if ((code == 0x2a || code == 0x36) && flags & RI_KEY_E0) {
        return;
    }
    code &= 0xff;
    int down = !(flags & RI_KEY_BREAK);
    if (!down) {
        raw_down[code] = 0;
        if (capture_swallow[code]) {
            capture_swallow[code] = 0; /* the release of a captured press */
            return;
        }
        push_event(code, 0); /* releases are always delivered */
        return;
    }
    if (raw_down[code]) {
        return; /* keyboard autorepeat */
    }
    int blocked = modifier_held();
    raw_down[code] = 1;
    int focused = kbm_win32_focused();
    if (focused && (capture_press(code, CAPTURE_KEY | (LONG)code) || activation_press(code))) {
        return;
    }
    if (!blocked && focused) {
        push_event(code, 1);
    }
}



/* Mouse buttons travel with the keys as virtual codes 0xe1-0xe3. */
static void mouse_buttons(unsigned flags, int focused) {
    static const unsigned short masks[3][2] = {{RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP},
                                               {RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP},
                                               {RI_MOUSE_MIDDLE_BUTTON_DOWN,
                                                RI_MOUSE_MIDDLE_BUTTON_UP}};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned code = KBM_MOUSE_LEFT + i;
        if (flags & masks[i][0] && !raw_down[code]) {
            raw_down[code] = 1;
            if (focused && !capture_press(code, CAPTURE_MOUSE | (LONG)code)) {
                push_event(code, 1);
            }
        }
        if (flags & masks[i][1]) {
            raw_down[code] = 0;
            if (capture_swallow[code]) {
                capture_swallow[code] = 0;
            } else {
                push_event(code, 0);
            }
        }
    }
}

static void mouse_packet(const RAWMOUSE *mouse) {
    unsigned flags = mouse->usButtonFlags;
    if (flags) {
        int focused = kbm_win32_focused();
        mouse_buttons(flags, focused);
        if (flags & RI_MOUSE_WHEEL && focused) {
            InterlockedExchangeAdd(&mouse_wheel, (SHORT)mouse->usButtonData);
        }
    }
    if (mouse->usFlags & MOUSE_MOVE_ABSOLUTE) {
        if (!absolute_mouse_logged) {
            absolute_mouse_logged = 1;
            log_append("KBM: absolute-position mouse packets ignored for mouse look.\r\n");
        }
        return;
    }
    if ((mouse->lLastX || mouse->lLastY) && kbm_win32_focused()) {
        InterlockedExchangeAdd(&mouse_dx, mouse->lLastX);
        InterlockedExchangeAdd(&mouse_dy, mouse->lLastY);
    }
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INPUT) {
        _Alignas(8) unsigned char buffer[sizeof(RAWINPUT) + 64];
        UINT size = sizeof(buffer);
        if (user.raw_data((HRAWINPUT)lparam, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) !=
                (UINT)-1 &&
            size >= sizeof(RAWINPUTHEADER)) {
            const RAWINPUT *input = (const RAWINPUT *)buffer;
            if (input->header.dwType == RIM_TYPEKEYBOARD) {
                keyboard_packet(&input->data.keyboard);
            } else if (input->header.dwType == RIM_TYPEMOUSE) {
                mouse_packet(&input->data.mouse);
            }
        }
    }
    /* WM_INPUT must reach DefWindowProc so the system releases the packet. */
    return user.default_proc(window, message, wparam, lparam);
}

/* Registers only device classes nobody else in the process owns. */
static int register_raw_input(HWND window, int *mouse_registered) {
    RAWINPUTDEVICE existing[16];
    UINT count = ARRAY_COUNT(existing);
    int keyboard_taken = 0, mouse_taken = 0;
    UINT found = user.registered_raw(existing, &count, sizeof(RAWINPUTDEVICE));
    if (found == (UINT)-1) {
        found = 0;
    }
    for (UINT i = 0; i < found; ++i) {
        if (existing[i].usUsagePage == 1 && existing[i].usUsage == 6) {
            keyboard_taken = 1;
        }
        if (existing[i].usUsagePage == 1 && existing[i].usUsage == 2) {
            mouse_taken = 1;
        }
    }
    if (keyboard_taken) {
        log_append("KBM: the process already owns Raw Input keyboard; using polling.\r\n");
        return 0;
    }
    /* INPUTSINK only lets this hidden window receive packets; every press is
       dropped unless the real game window is the foreground window. */
    RAWINPUTDEVICE devices[2];
    for (unsigned i = 0; i < 2; ++i) {
        devices[i].usUsagePage = 1;
        devices[i].usUsage = i ? 2 : 6;
        devices[i].dwFlags = RIDEV_INPUTSINK;
        devices[i].hwndTarget = window;
    }
    UINT wanted = mouse_taken ? 1 : 2;
    if (mouse_taken) {
        log_append("KBM: the process already owns Raw Input mouse; mouse look is off.\r\n");
    }
    if (!user.register_raw(devices, wanted, sizeof(RAWINPUTDEVICE))) {
        log_append_value("KBM: RegisterRawInputDevices failed; using polling. Error ",
                         (int)GetLastError());
        return 0;
    }
    *mouse_registered = !mouse_taken;
    return 1;
}

static DWORD WINAPI input_thread(void *unused) {
    (void)unused;
    if (!load_user32()) {
        log_append("KBM: user32 input functions unavailable; keyboard & mouse disabled.\r\n");
        InterlockedExchange(&state, KBM_WIN32_FAILED);
        return 0;
    }
    if (!verify_host()) {
        log_append("KBM: Azimuth window/prompt globals not verified; keyboard & mouse "
                   "disabled.\r\n");
        InterlockedExchange(&state, KBM_WIN32_FAILED);
        return 0;
    }
    for (unsigned code = 1; code < 256; ++code) {
        unsigned scan = (code & 0x7f) | (code & 0x80 ? 0xe000 : 0);
        unsigned vk = user.map_key(scan, 3 /* MAPVK_VSC_TO_VK_EX */);
        vk_of[code] = (unsigned char)(vk < 256 ? vk : 0);
    }
    vk_of[KBM_MOUSE_LEFT] = VK_LBUTTON;
    vk_of[KBM_MOUSE_RIGHT] = VK_RBUTTON;
    vk_of[KBM_MOUSE_MIDDLE] = VK_MBUTTON;
    unsigned waited = 0;
    while (!game_window()) {
        Sleep(100);
        if (++waited == 600) {
            log_append("KBM: still waiting for the Azimuth game window.\r\n");
        }
    }
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSEXW window_class = {0};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"FH2ExtraOptions.KeyboardMouse";
    HWND window = NULL;
    if (user.register_class(&window_class)) {
        window = user.create_window(0, window_class.lpszClassName, L"", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, NULL, instance, NULL);
    }
    int mouse = 0;
    if (!window || !register_raw_input(window, &mouse)) {
        if (!window) {
            log_append("KBM: message-only input window unavailable; using polling.\r\n");
        }
        InterlockedExchange(&state, KBM_WIN32_POLLING);
        log_append("KBM: keyboard polled once per game tick; mouse look unavailable.\r\n");
        return 0;
    }
    InterlockedExchange(&raw_mouse, mouse);
    InterlockedExchange(&state, KBM_WIN32_RAW_INPUT);
    log_append(mouse ? "KBM: Raw Input keyboard and mouse registered on a private window.\r\n"
                     : "KBM: Raw Input keyboard registered on a private window.\r\n");
    MSG message;
    while (user.get_message(&message, NULL, 0, 0) > 0) {
        user.dispatch(&message);
    }
    log_append("KBM: input window message loop ended.\r\n");
    InterlockedExchange(&state, KBM_WIN32_POLLING);
    return 0;
}

typedef HRESULT(STDMETHODCALLTYPE *SetVibrationFn)(void *, const float *);
static SetVibrationFn original_vibration;
static int (*rumble_muted)(void);
static volatile LONG rumble_was_muted;

static HRESULT STDMETHODCALLTYPE set_vibration(void *gamepad, const float *levels) {
    if (rumble_muted && rumble_muted()) {
        static const float silent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (!InterlockedExchange(&rumble_was_muted, 1)) {
            log_append("KBM: gamepad vibration muted while keyboard & mouse is active.\r\n");
        }
        return original_vibration(gamepad, silent);
    }
    InterlockedExchange(&rumble_was_muted, 0);
    return original_vibration(gamepad, levels);
}

int kbm_win32_gate_rumble(int (*muted)(void)) {
    if (!host && !verify_host()) {
        log_append("KBM: host not verified; gamepad vibration is not gated.\r\n");
        return 0;
    }
    uintptr_t *slots = (uintptr_t *)(host + HOST_SET_VIBRATION_SLOT), values[3];
    if (!memory_read(slots, values, sizeof(values)) ||
        values[0] != (uintptr_t)(host + HOST_SET_VIBRATION) ||
        values[1] != (uintptr_t)(host + HOST_GET_READING) ||
        values[2] != (uintptr_t)(host + HOST_GET_RAW_READING)) {
        log_append("KBM: unexpected Azimuth gamepad vtable; gamepad vibration is not gated.\r\n");
        return 0;
    }
    DWORD protection, ignored;
    if (!VirtualProtect(slots, sizeof(uintptr_t), PAGE_READWRITE, &protection)) {
        log_append("KBM: Azimuth gamepad vtable not writable; vibration is not gated.\r\n");
        return 0;
    }
    original_vibration = (SetVibrationFn)values[0];
    rumble_muted = muted;
    InterlockedExchangePointer((void *volatile *)slots, (void *)(uintptr_t)set_vibration);
    VirtualProtect(slots, sizeof(uintptr_t), protection, &ignored);
    log_append("KBM: gamepad vibration follows the input source (Azimuth SetVibration 0xcc170).\r\n");
    return 1;
}

int kbm_win32_start(void) {
    if (InterlockedExchange(&started, 1)) {
        return 1;
    }
    HANDLE thread = CreateThread(NULL, 0, input_thread, NULL, 0, NULL);
    if (!thread) {
        InterlockedExchange(&state, KBM_WIN32_FAILED);
        return 0;
    }
    CloseHandle(thread);
    return 1;
}

KbmWin32State kbm_win32_state(void) {
    return (KbmWin32State)state;
}

int kbm_win32_raw_mouse(void) {
    return state == KBM_WIN32_RAW_INPUT && raw_mouse;
}

int kbm_win32_key_down(unsigned code) {
    if (code >= 256 || !vk_of[code] || !user.async_key ||
        (state != KBM_WIN32_RAW_INPUT && state != KBM_WIN32_POLLING)) {
        return -1;
    }
    return (user.async_key(vk_of[code]) & 0x8000) != 0;
}

void kbm_win32_set_capture(int capture) {
    if (state != KBM_WIN32_RAW_INPUT) {
        capture = 0;
    }
    uint64_t now = GetTickCount64();
    if (!capture) {
        if (captured) {
            captured = 0;
            user.clip_cursor(NULL);
        }
        return;
    }
    if (captured && now - last_capture_refresh < 1000) {
        return;
    }
    HWND window = game_window();
    RECT client;
    POINT center;
    if (!window || !user.client_rect(window, &client) || client.right <= client.left ||
        client.bottom <= client.top) {
        return;
    }
    center.x = (client.left + client.right) / 2;
    center.y = (client.top + client.bottom) / 2;
    if (!user.client_to_screen(window, &center)) {
        return;
    }
    /* A 1x1 confinement keeps the pointer from wandering onto other monitors
       or windows while the mouse steers the view; relative Raw Input is
       unaffected. The rectangle follows window moves and resizes. */
    RECT box = {center.x, center.y, center.x + 1, center.y + 1};
    if (user.clip_cursor(&box)) {
        captured = 1;
        last_capture_refresh = now;
    }
}

/* Keycap images for the on-screen button prompts, drawn with GDI: a light
   key with a dark rim and the bound key's name. RGBA8, `size` x `size`. */
typedef HDC(WINAPI *CompatibleDcFn)(HDC);
typedef HGDIOBJ(WINAPI *SelectFn)(HDC, HGDIOBJ);
typedef BOOL(WINAPI *DeleteDcFn)(HDC);
typedef HFONT(WINAPI *CreateFontFn)(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD,
                                    DWORD, DWORD, DWORD, LPCWSTR);
typedef COLORREF(WINAPI *TextColorFn)(HDC, COLORREF);
typedef int(WINAPI *BkModeFn)(HDC, int);
typedef int(WINAPI *DrawTextFn)(HDC, LPCWSTR, int, LPRECT, UINT);
typedef BOOL(WINAPI *GdiFlushFn)(void);

static struct {
    CompatibleDcFn compatible_dc;
    SelectFn select;
    DeleteDcFn delete_dc;
    CreateFontFn create_font;
    TextColorFn text_color;
    BkModeFn bk_mode;
    DrawTextFn draw_text;
    GdiFlushFn flush;
} text_api;

static int load_text_api(void) {
    static int loaded = -1;
    if (loaded >= 0) {
        return loaded;
    }
    HMODULE user = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE graphics = LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user && graphics && load_cursor_api()) {
        text_api.compatible_dc = (CompatibleDcFn)(uintptr_t)GetProcAddress(graphics, "CreateCompatibleDC");
        text_api.select = (SelectFn)(uintptr_t)GetProcAddress(graphics, "SelectObject");
        text_api.delete_dc = (DeleteDcFn)(uintptr_t)GetProcAddress(graphics, "DeleteDC");
        text_api.create_font = (CreateFontFn)(uintptr_t)GetProcAddress(graphics, "CreateFontW");
        text_api.text_color = (TextColorFn)(uintptr_t)GetProcAddress(graphics, "SetTextColor");
        text_api.bk_mode = (BkModeFn)(uintptr_t)GetProcAddress(graphics, "SetBkMode");
        text_api.flush = (GdiFlushFn)(uintptr_t)GetProcAddress(graphics, "GdiFlush");
        text_api.draw_text = (DrawTextFn)(uintptr_t)GetProcAddress(user, "DrawTextW");
    }
    loaded = text_api.compatible_dc && text_api.select && text_api.delete_dc &&
             text_api.create_font && text_api.text_color && text_api.bk_mode && text_api.flush &&
             text_api.draw_text;
    return loaded;
}

/* Distance from a point to the border of a rounded square (negative inside). */
static float rounded_box(float x, float y, float half, float radius) {
    float qx = (x < 0 ? -x : x) - (half - radius), qy = (y < 0 ? -y : y) - (half - radius);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, outside = ox * ox + oy * oy;
    float root = outside > 1 ? outside : 1;
    for (int k = 0; k < 12; ++k) {
        root = 0.5f * (root + outside / root);
    }
    float inner = qx > qy ? qx : qy;
    return (outside > 0 ? root : 0) + (inner < 0 ? inner : 0) - radius;
}

int kbm_win32_render_keycap(const wchar_t *label, unsigned size, unsigned char *rgba) {
    if (!load_text_api() || size < 16 || size > 512) {
        return 0;
    }
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = (LONG)size;
    info.bmiHeader.biHeight = -(LONG)size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    uint32_t *pixels = NULL;
    HDC dc = text_api.compatible_dc(NULL);
    HBITMAP bitmap = dc ? gdi.create_dib(dc, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0) : NULL;
    if (!bitmap || !pixels) {
        if (bitmap) {
            gdi.delete_object(bitmap);
        }
        if (dc) {
            text_api.delete_dc(dc);
        }
        return 0;
    }
    for (unsigned i = 0; i < size * size; ++i) {
        pixels[i] = 0x00ffffffu;
    }
    unsigned length = 0;
    while (label[length]) {
        ++length;
    }
    /* The label fills the keycap: start large and shrink until it fits. */
    RECT box = {(LONG)(size / 12), (LONG)(size / 12), (LONG)(size - size / 12),
                (LONG)(size - size / 12)};
    int height = (int)((float)size * (length <= 1 ? 0.80f : length == 2 ? 0.66f : 0.58f));
    HFONT font = NULL;
    HGDIOBJ old_bitmap = text_api.select(dc, bitmap);
    HGDIOBJ old_font = NULL;
    for (unsigned attempt = 0; attempt < 12; ++attempt) {
        HFONT next = text_api.create_font(-height, 0, 0, 0, 800, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                          4 /* ANTIALIASED_QUALITY */, 0, L"Segoe UI");
        if (!next) {
            break;
        }
        HGDIOBJ previous = text_api.select(dc, next);
        if (!old_font) {
            old_font = previous;
        }
        if (font) {
            gdi.delete_object(font);
        }
        font = next;
        RECT need = {0, 0, 0, 0};
        text_api.draw_text(dc, label, (int)length, &need, DT_SINGLELINE | DT_CALCRECT);
        if (need.right - need.left <= box.right - box.left &&
            need.bottom - need.top <= (box.bottom - box.top) * 5 / 4) {
            break;
        }
        height = height * 7 / 8;
    }
    text_api.bk_mode(dc, 1 /* TRANSPARENT */);
    text_api.text_color(dc, RGB(15, 15, 15));
    text_api.draw_text(dc, label, (int)length, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    text_api.flush();
    float half = (float)size * 0.47f, radius = (float)size * 0.16f, rim = (float)size * 0.05f;
    for (unsigned y = 0; y < size; ++y) {
        for (unsigned x = 0; x < size; ++x) {
            float px = (float)x + 0.5f - (float)size * 0.5f, py = (float)y + 0.5f - (float)size * 0.5f;
            float distance = rounded_box(px, py, half, radius);
            uint32_t source = pixels[y * size + x];
            unsigned gray = source & 0xff, alpha = 0, value = 0;
            if (distance < 0.5f) {
                alpha = distance > -0.5f ? (unsigned)((0.5f - distance) * 255.0f) : 255;
                value = distance > -rim ? 32 : (gray * 236u) / 255u + 4u; /* rim, then face */
            }
            unsigned char *out = rgba + ((size_t)y * size + x) * 4;
            out[0] = out[1] = out[2] = (unsigned char)value;
            out[3] = (unsigned char)alpha;
        }
    }
    if (old_font) {
        text_api.select(dc, old_font);
    }
    text_api.select(dc, old_bitmap);
    if (font) {
        gdi.delete_object(font);
    }
    gdi.delete_object(bitmap);
    text_api.delete_dc(dc);
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestKbmKeycap(const wchar_t *label, unsigned size, unsigned char *rgba) {
    return kbm_win32_render_keycap(label, size, rgba);
}
#endif
