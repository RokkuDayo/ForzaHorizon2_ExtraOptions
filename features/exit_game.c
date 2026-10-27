#include "exit_game.h"
#include "logging.h"
#include "support.h"
#include "video_options_text.h"

/* Inspected FH2 release: PauseMenuTiled's page init (0xdc6e30) binds each
   tile with `lea rax,<handler>` then 0xd9f9f0(page, "PART_Button_<tile>",
   &{handler, 0}). Car Packs' lea is at 0xdc8155 (48 8D 05 C4 55 FF FF, to
   0xdbd720); its handler takes the page in rcx. The int3 bytes at 0xdc6a21
   are unused padding: a jump to the new handler goes there and the lea's
   rel32 is pointed at it. The tile itself stays where it is (the
   Marketplace tab); only its caption and icon change. */
#define CAR_PACKS_LEA 0xdc8155u
#define EXIT_THUNK 0xdc6a21u

typedef HWND(WINAPI *FindWindowFn)(LPCWSTR, LPCWSTR);
typedef BOOL(WINAPI *PostMessageFn)(HWND, UINT, WPARAM, LPARAM);
typedef DWORD(WINAPI *WindowProcessFn)(HWND, LPDWORD);

static int installed;
static unsigned char *image;

/* Closes Azimuth's game window, as its own close button does. */
static void exit_game(void *page) {
    (void)page;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    FindWindowFn find = user32 ? (FindWindowFn)(void *)GetProcAddress(user32, "FindWindowW") : NULL;
    PostMessageFn post = user32 ? (PostMessageFn)(void *)GetProcAddress(user32, "PostMessageW") : NULL;
    WindowProcessFn owner =
        user32 ? (WindowProcessFn)(void *)GetProcAddress(user32, "GetWindowThreadProcessId") : NULL;
    HWND window = find ? find(L"Azimuth.Game.Win32", NULL) : NULL;
    DWORD process = 0;
    if (window && owner && post && owner(window, &process) && process == GetCurrentProcessId()) {
        log_append("EXIT GAME: chosen in the pause menu; closing the game window.\r\n");
        post(window, 0x10 /* WM_CLOSE */, 0, 0);
    }
}

void exit_game_install(unsigned char *game_image) {
    static const unsigned char lea[7] = {0x48, 0x8d, 0x05, 0xc4, 0x55, 0xff, 0xff};
    static const unsigned char padding[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                              0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
    image = game_image;
    unsigned char *site = game_image + CAR_PACKS_LEA, *thunk = game_image + EXIT_THUNK;
    if (!memory_equal(site, lea, sizeof(lea)) || !memory_equal(thunk, padding, sizeof(padding))) {
        log_append("EXIT GAME: Car Packs tile not as expected; Exit Game left out.\r\n");
        return;
    }
    /* mov rax, exit_game; jmp rax */
    unsigned char code[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t target = (uintptr_t)exit_game;
    memory_copy(code + 2, &target, sizeof(target));
    int32_t relative = (int32_t)(thunk - (site + sizeof(lea)));
    DWORD previous, ignored;
    if (!VirtualProtect(thunk, sizeof(code), PAGE_EXECUTE_READWRITE, &previous)) {
        return;
    }
    memory_copy(thunk, code, sizeof(code));
    VirtualProtect(thunk, sizeof(code), previous, &ignored);
    if (!VirtualProtect(site + 3, 4, PAGE_EXECUTE_READWRITE, &previous)) {
        return;
    }
    memory_copy(site + 3, &relative, 4);
    VirtualProtect(site + 3, 4, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), game_image + EXIT_THUNK,
                          CAR_PACKS_LEA + 7 - EXIT_THUNK);
    installed = 1;
    log_append("EXIT GAME: the Marketplace's Car Packs tile is now Exit Game.\r\n");
}

/* The whole <local:TileMenuDataItem ... /> element holding `key`, as [begin, end). */
static int find_item(const char *text, int length, const char *key, int *begin, int *end) {
    static const char tag[] = "<local:TileMenuDataItem";
    const char *at = text_find_unique(text, (size_t)length, key);
    if (!at) {
        return 0;
    }
    int start = (int)(at - text), limit = start - 1000;
    while (start > 0 && start > limit && !memory_equal(text + start, tag, sizeof(tag) - 1)) {
        --start;
    }
    const char *close = text_find(at, (size_t)length - (size_t)(at - text), "/>");
    if (start <= 0 || start <= limit || !close) {
        return 0;
    }
    *begin = start;
    *end = (int)(close - text) + 2;
    return 1;
}

static int append(char *out, int used, int capacity, const char *text) {
    int size = (int)text_length(text);
    if (used < 0 || used + size > capacity) {
        return -1;
    }
    memory_copy(out + used, text, (size_t)size);
    return used + size;
}

/* The pause page's XAML is read as single-byte text, so letters beyond ASCII
   are written as XML character references. */
static int append_caption(char *out, int used, int capacity, const wchar_t *caption) {
    static const char digits[] = "0123456789ABCDEF";
    for (; *caption && used >= 0; ++caption) {
        unsigned code = (unsigned)*caption;
        if (code >= 0x20 && code < 0x7f && code != '"' && code != '&' && code != '<') {
            if (used + 1 > capacity) {
                return -1;
            }
            out[used++] = (char)code;
            continue;
        }
        char reference[12] = "&#x";
        int n = 3;
        for (int shift = 12; shift >= 0; shift -= 4) {
            if ((code >> shift) || shift == 0 || n > 3) {
                reference[n++] = digits[(code >> shift) & 15];
            }
        }
        reference[n++] = ';';
        reference[n] = 0;
        used = append(out, used, capacity, reference);
    }
    return used;
}

int exit_game_relabel(char *bytes, int length, int capacity) {
    static char layout[262144];
    int begin, end;
    if (!installed || length <= 0 || length > (int)sizeof(layout) ||
        !text_find(bytes, (size_t)length, "x:Name=\"PART_SCOPE_Marketplace\"") ||
        !find_item(bytes, length, "x:Key=\"A_MenuDataItem_CarPacks\"", &begin, &end)) {
        return length;
    }
    memory_copy(layout, bytes, (size_t)begin);
    int out = begin;
    out = append(layout, out, (int)sizeof(layout), "<local:TileMenuDataItem Heading=\"");
    out = append_caption(layout, out, (int)sizeof(layout), video_options_text(image)->exit_game);
    out = append(layout, out, (int)sizeof(layout),
                 "\" Icon=\"/ForzaUI;component/Textures/Anthem/Icons/PauseMenu/Quit.png\""
                 " x:Key=\"A_MenuDataItem_CarPacks\" />");
    if (out < 0 || out + (length - end) >= capacity) {
        log_append("EXIT GAME: the pause menu text does not fit; Car Packs caption kept.\r\n");
        return length;
    }
    memory_copy(layout + out, bytes + end, (size_t)(length - end));
    out += length - end;
    if (!memory_is_writable(bytes, (size_t)out + 1)) {
        return length;
    }
    memory_copy(bytes, layout, (size_t)out);
    bytes[out] = 0;
    log_append("EXIT GAME: Car Packs tile shown as Exit Game.\r\n");
    return out;
}

#ifdef FH2_TWEAKS_TEST
/* Offline test: the relabel as installed, with the English caption. */
__declspec(dllexport) int Fh2TestExitGameRelabel(char *bytes, int length, int capacity) {
    installed = 1;
    image = NULL;
    return exit_game_relabel(bytes, length, capacity);
}
#endif
