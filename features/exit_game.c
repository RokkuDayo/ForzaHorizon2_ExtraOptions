#include "exit_game.h"
#include "logging.h"
#include "support.h"
#include "video_options_text.h"

/* Inspected FH2 release: PauseMenuTiled's page init (0xdc6e30) binds each
   tile with `lea rax,<handler>` then 0xd9f9f0(page, "PART_Button_<tile>",
   &{handler, 0}); Car Packs' lea is at 0xdc8155 (48 8D 05 C4 55 FF FF). The
   Exit Game tile is a new button, PART_Button_EOExitGame, bound the same way
   right after the native init (return_garage.c's pause init hook calls
   exit_game_page_ready). Its handler posts WM_CLOSE to Azimuth's game window,
   as the window's own close button does.

   On the Home tab the wide tile under the map (Online Road Trip, or Horizon
   Solo in its place) becomes a square one, its picture cropped to the middle,
   and Exit Game takes the square beside it. */
#define CAR_PACKS_LEA 0xdc8155u
#define BIND_BUTTON 0xd9f9f0u

typedef HWND(WINAPI *FindWindowFn)(LPCWSTR, LPCWSTR);
typedef BOOL(WINAPI *PostMessageFn)(HWND, UINT, WPARAM, LPARAM);
typedef DWORD(WINAPI *WindowProcessFn)(HWND, LPDWORD);
typedef struct {
    void *method;
    int32_t adjust, unused;
} MemberHandler;
typedef void (*BindButton)(void *page, const char *name, const MemberHandler *handler);

static int installed;
static unsigned char *image;
static BindButton bind_button;

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
    image = game_image;
    unsigned char *site = game_image + CAR_PACKS_LEA;
    if (!memory_equal(site, lea, sizeof(lea))) {
        log_append("EXIT GAME: pause menu tile binding not as expected; Exit Game left out.\r\n");
        return;
    }
    /* The binder is the call that follows Car Packs' lea. */
    for (unsigned i = sizeof(lea); i < 64; ++i) {
        int32_t relative;
        memory_copy(&relative, site + i + 1, sizeof(relative));
        if (site[i] == 0xe8 && site + i + 5 + relative == game_image + BIND_BUTTON) {
            bind_button = (BindButton)(void *)(game_image + BIND_BUTTON);
            break;
        }
    }
    if (!bind_button) {
        log_append("EXIT GAME: pause menu button binder not found; Exit Game left out.\r\n");
        return;
    }
    installed = 1;
    log_append("EXIT GAME: Exit Game tile added to the pause menu's Home tab.\r\n");
}

void exit_game_page_ready(void *page) {
    if (installed && page) {
        MemberHandler handler = {(void *)(uintptr_t)exit_game, 0, 0};
        bind_button(page, "PART_Button_EOExitGame", &handler);
    }
}

/* The game's tile look (as A_ControlTemplate_IconTile and the image tiles
   draw it), with the picture cropped: a wide 558x276 tile picture shows its
   middle 276x276. */
#define EO_TILE_HEAD(key)                                                                          \
    "<ControlTemplate x:Key=\"" key "\" TargetType=\"{x:Type Button}\">"                          \
    "<Grid Style=\"{StaticResource A_Style_FrameworkElement_DefaultTransition}\""                  \
    " local:UIPageTransitionManager.TransitionIndex=\"{Binding "                                   \
    "(local:UIPageTransitionManager.TransitionIndex), RelativeSource={RelativeSource "             \
    "TemplatedParent}}\">"                                                                         \
    "<Grid Style=\"{StaticResource A_Style_Grid_HiddenWhenLocked}\">"                              \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileShadow}\"/></Grid>"          \
    "<Grid Style=\"{StaticResource A_Style_Grid_MotionBlur}\">"                                    \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileBackground}\"/>"
#define EO_TILE_TAIL                                                                               \
    "<Grid Margin=\"{StaticResource A_Thickness_TilePadding}\">"                                   \
    "<local:FZTextBlock x:Name=\"NameText\""                                                       \
    " Text=\"{Binding DataContext.Heading, RelativeSource={RelativeSource TemplatedParent}}\""     \
    " Style=\"{StaticResource A_Style_FZTextBlock_TileTitle}\""                                    \
    " MaxWidth=\"{Binding Width, RelativeSource={RelativeSource TemplatedParent}}\"/>"             \
    "<local:FZMaterial Style=\"{StaticResource A_Style_Tile_LockIcon}\"/></Grid>"                  \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileSelectionBar}\"/>"           \
    "</Grid></Grid></ControlTemplate>"
static const char square_template[] =
    EO_TILE_HEAD("EO_ControlTemplate_SquareImageTile")
    "<Grid ClipToBounds=\"True\"><local:FZMaterial"
    " Style=\"{StaticResource A_Style_FZMaterial_LockableImage}\""
    " ImageSource=\"{Binding DataContext.Icon, RelativeSource={RelativeSource TemplatedParent}}\""
    " Width=\"558\" Height=\"276\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\""
    " Margin=\"-141,0,0,0\" StretchDirection=\"Both\"/></Grid>" EO_TILE_TAIL;
static const char exit_item_head[] = "<local:TileMenuDataItem x:Key=\"EO_MenuDataItem_ExitGame\" Heading=\"";
static const char exit_item_tail[] =
    "\" Icon=\"/ForzaUI;component/Textures/Anthem/Icons/PauseMenu/Quit.png\"/>";
static const char exit_tile[] =
    "<Button Canvas.Left=\"{StaticResource A_Double_TileCoord_X_1}\""
    " Canvas.Top=\"{StaticResource A_Double_TileCoord_Y_1}\""
    " DataContext=\"{StaticResource EO_MenuDataItem_ExitGame}\""
    " Style=\"{StaticResource A_Style_Button_Tile_1x1}\""
    " Template=\"{StaticResource A_ControlTemplate_IconTile}\""
    " local:TileMenuNavigationManager.TileState=\"Enabled\""
    " local:TileMenuNavigationManager.GridX=\"1\" local:TileMenuNavigationManager.GridY=\"1\""
    " local:UIPageTransitionManager.TransitionIndex=\"2\" x:Name=\"PART_Button_EOExitGame\" />";
static const char wide_style[] = "Style=\"{StaticResource A_Style_Button_Tile_2x1}\"";
static const char wide_template[] = "Template=\"{StaticResource A_ControlTemplate_HorizontalImageTile}\"";
static const char square_style[] = "Style=\"{StaticResource A_Style_Button_Tile_1x1}\"";
static const char square_look[] = "Template=\"{StaticResource EO_ControlTemplate_SquareImageTile}\"";

typedef struct {
    const char *at;
    size_t skip;
    const char *text;
    int item;
} Edit;

/* The <Button ... /> with that x:Name: its style and template attributes. */
static int find_wide_tile(const char *text, size_t length, const char *name, const char **style,
                          const char **look, const char **end) {
    const char *at = text_find_unique(text, length, name);
    const char *begin = at;
    while (begin && begin > text && !memory_equal(begin, "<Button", 7)) {
        --begin;
    }
    const char *close = at ? text_find(at, length - (size_t)(at - text), "/>") : NULL;
    if (!at || begin == text || !close) {
        return 0;
    }
    size_t span = (size_t)(close - begin);
    *style = text_find(begin, span, wide_style);
    *look = text_find(begin, span, wide_template);
    *end = close + 2;
    return *style && *look;
}

static int append(char *out, int used, int capacity, const char *text, size_t size) {
    if (used < 0 || (size_t)used + size > (size_t)capacity) {
        return -1;
    }
    memory_copy(out + used, text, size);
    return used + (int)size;
}

/* The pause page's XAML is read as single-byte text, so letters beyond ASCII
   are written as XML character references. */
static int append_caption(char *out, int used, int capacity, const wchar_t *caption) {
    static const char digits[] = "0123456789ABCDEF";
    for (; *caption && used >= 0; ++caption) {
        unsigned code = (unsigned)*caption;
        if (code >= 0x20 && code < 0x7f && code != '"' && code != '&' && code != '<') {
            char c = (char)code;
            used = append(out, used, capacity, &c, 1);
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
        used = append(out, used, capacity, reference, (size_t)n);
    }
    return used;
}

int exit_game_is_xaml(const char *bytes, size_t length) {
    return installed && length > 0 && text_find(bytes, length, "x:Name=\"PART_SCOPE_Home\"") &&
           text_find(bytes, length, "x:Name=\"PART_Button_HorizonOnline\"") &&
           !text_find(bytes, length, "PART_Button_EOExitGame");
}

size_t exit_game_extend_xaml(char *bytes, size_t length, size_t capacity) {
    static char layout[262144];
    const size_t size = length;
    if (!exit_game_is_xaml(bytes, length) || length > sizeof(layout)) {
        return length;
    }
    const char *resources = text_find_unique(bytes, size, "</ResourceDictionary>");
    const char *online_style, *online_look, *online_end, *solo_style, *solo_look, *solo_end;
    if (!resources ||
        !find_wide_tile(bytes, size, "x:Name=\"PART_Button_HorizonOnline\"", &online_style,
                        &online_look, &online_end) ||
        !find_wide_tile(bytes, size, "x:Name=\"PART_Button_HorizonSolo\"", &solo_style, &solo_look,
                        &solo_end)) {
        log_append("EXIT GAME: pause menu layout did not match; original tiles kept.\r\n");
        return length;
    }
    const char *after = online_end > solo_end ? online_end : solo_end;
    Edit edits[] = {
        {resources, 0, square_template, 1},
        {online_style, sizeof(wide_style) - 1, square_style, 0},
        {online_look, sizeof(wide_template) - 1, square_look, 0},
        {solo_style, sizeof(wide_style) - 1, square_style, 0},
        {solo_look, sizeof(wide_template) - 1, square_look, 0},
        {after, 0, exit_tile, 0},
    };
    for (unsigned i = 1; i < ARRAY_COUNT(edits); ++i) {
        for (unsigned j = i; j > 0 && edits[j - 1].at > edits[j].at; --j) {
            Edit swap = edits[j];
            edits[j] = edits[j - 1];
            edits[j - 1] = swap;
        }
    }
    int out = 0, room = (int)sizeof(layout);
    const char *from = bytes;
    for (unsigned i = 0; i < ARRAY_COUNT(edits) && out >= 0; ++i) {
        out = append(layout, out, room, from, (size_t)(edits[i].at - from));
        out = append(layout, out, room, edits[i].text, text_length(edits[i].text));
        if (edits[i].item) {
            out = append(layout, out, room, exit_item_head, sizeof(exit_item_head) - 1);
            out = append_caption(layout, out, room, video_options_text(image)->exit_game);
            out = append(layout, out, room, exit_item_tail, sizeof(exit_item_tail) - 1);
        }
        from = edits[i].at + edits[i].skip;
    }
    out = append(layout, out, room, from, size - (size_t)(from - bytes));
    if (out < 0 || (size_t)out >= capacity) {
        log_append("EXIT GAME: the new pause menu does not fit; original tiles kept.\r\n");
        return length;
    }
    memory_copy(bytes, layout, (size_t)out);
    bytes[out] = 0;
    log_append("EXIT GAME: Exit Game placed beside Online Road Trip on the Home tab.\r\n");
    return (size_t)out;
}

#ifdef FH2_TWEAKS_TEST
/* Offline test: the layout edit as installed, with the English caption. */
__declspec(dllexport) size_t Fh2TestExitGameXaml(char *bytes, size_t length, size_t capacity) {
    installed = 1;
    image = NULL;
    return exit_game_extend_xaml(bytes, length, capacity);
}
#endif
