#include "world_menu.h"
#include "logging.h"
#include "settings.h"
#include "support.h"
#include "time_weather.h"
#include "video_options_text.h"
#include "video_options_xaml.h"

/* Inspected FH2 release: PauseOptionsMenu (the pause menu's Settings hub,
   A_PauseOptions.xaml) has its page init 0xdc8a60 in vtable slot 0x2e15940.
   It binds each tile with 0xd9fc10(page, "PART_Button_<tile>", &{handler,
   0}); the binder returns quietly when no element has that name. The Video
   tile's handler 0xdcfcd0 takes the page in rcx and starts the transition to
   the Video page, whose XAML video_options.c then reads and rebuilds. */
#define HUB_INIT_SLOT 0x2e15940u
#define HUB_INIT 0xdc8a60u
#define BIND_BUTTON 0xd9fc10u
#define VIDEO_TILE 0xdcfcd0u
/* The World page must be read within this time after the tile was chosen. */
#define REQUEST_WINDOW_MS 5000u

typedef void (*PageMethod)(void *page);
typedef struct {
    void *method;
    int32_t adjust, unused;
} MemberHandler;
typedef void (*BindButton)(void *page, const char *name, const MemberHandler *handler);

static PageMethod original_init, open_video;
static BindButton bind_button;
static volatile LONG64 requested_at;
static int values[WORLD_SETTING_COUNT];

static void world_tile(void *page) {
    LONG64 now = (LONG64)GetTickCount64();
    InterlockedExchange64(&requested_at, now ? now : 1);
    open_video(page);
}

static void hub_initialize(void *page) {
    original_init(page);
    MemberHandler handler = {(void *)(uintptr_t)world_tile, 0, 0};
    bind_button(page, "PART_Button_EOWorld", &handler);
}

int world_menu_take_request(void) {
    LONG64 at = InterlockedExchange64(&requested_at, 0);
    return at && (ULONGLONG)GetTickCount64() - (ULONGLONG)at < REQUEST_WINDOW_MS;
}

unsigned world_menu_count(unsigned setting) {
    return setting == WORLD_TIME      ? TIME_CHOICE_COUNT
           : setting == WORLD_WEATHER ? WEATHER_CHOICE_COUNT
                                      : 0;
}

int world_menu_get(unsigned setting) {
    return setting < WORLD_SETTING_COUNT ? values[setting] : 0;
}

int world_menu_set(unsigned setting, int value) {
    if (setting >= WORLD_SETTING_COUNT || value < 0 ||
        (unsigned)value >= world_menu_count(setting) || values[setting] == value) {
        return 0;
    }
    values[setting] = value;
    if (setting == WORLD_TIME) {
        time_weather_set_time(value);
    } else {
        time_weather_set_weather(value);
    }
    if (!settings_write_world(setting, value)) {
        log_append("WORLD MENU: FH2_ExtraOptions.ini [World] could not be written.\r\n");
    }
    return 1;
}

/* The hub init slot holds the game's init, or another Settings hub hook of
   this DLL (the World and KEYBOARD & MOUSE tiles each add one): either is
   chained, so the tiles install in any order. Anything else is left alone. */
static int hub_init_known(unsigned char *game_image, void *current) {
    HMODULE ours = NULL, owner = NULL;
    if (current == game_image + HUB_INIT) {
        return 1;
    }
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)(uintptr_t)hub_init_known, &ours) &&
           GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)current, &owner) &&
           owner == ours;
}

void world_menu_install(unsigned char *game_image, int time_choice, int weather_choice) {
    static const unsigned char bind_code[] = {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x81,
                                              0xec, 0x90, 0x00, 0x00, 0x00};
    static const unsigned char video_code[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xd9};
    void **slot = (void **)(game_image + HUB_INIT_SLOT), *current;
    if (!memory_read(slot, &current, sizeof(current)) || !hub_init_known(game_image, current) ||
        !memory_equal(game_image + BIND_BUTTON, bind_code, sizeof(bind_code)) ||
        !memory_equal(game_image + VIDEO_TILE, video_code, sizeof(video_code))) {
        log_append("WORLD MENU: Settings hub not as expected; World tile left out.\r\n");
        return;
    }
    values[WORLD_TIME] = time_choice;
    values[WORLD_WEATHER] = weather_choice;
    if (!time_weather_install(game_image, time_choice, weather_choice)) {
        log_append("WORLD MENU: time of day and weather unavailable; World tile left out.\r\n");
        return;
    }
    bind_button = (BindButton)(void *)(game_image + BIND_BUTTON);
    open_video = (PageMethod)(void *)(game_image + VIDEO_TILE);
    DWORD previous, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) {
        return;
    }
    original_init = (PageMethod)current;
    InterlockedExchangePointer(slot, (void *)(uintptr_t)hub_initialize);
    VirtualProtect(slot, sizeof(*slot), previous, &ignored);
    log_append("WORLD MENU: Settings hub World tile opens the time of day and weather page.\r\n");
}

int world_menu_installed(void) {
    return original_init != NULL;
}

int world_menu_is_hub_xaml(const char *text, size_t length) {
    return original_init && length > 0 &&
           text_find(text, length, "x:Class=\"ForzaUI.A_PauseOptions\"") &&
           text_find(text, length, "x:Name=\"PART_Button_TeamCredits\"");
}

/* The page's tile navigation grid is fixed at 5 x 2 cells; tiles outside it
   cannot take focus. Team Credits (column 4, two rows tall) is cut to its top
   cell and World takes the bottom one, so World is reached down from Team
   Credits and right from Video.
   The tile pictures are drawn at their own size (an FZMaterial without a size
   collapses when aligned, which left these tiles blank): the World picture
   is fitted whole and Team Credits (1x2) shows its bottom half, where its
   drawn part is. */
#define EO_TILE_HEAD(key)                                                                          \
    "<ControlTemplate x:Key=\"" key "\" TargetType=\"{x:Type Button}\">"                          \
    "<Grid Style=\"{StaticResource A_Style_FrameworkElement_DefaultTransition}\""                  \
    " local:UIPageTransitionManager.TransitionIndex=\"{Binding "                                   \
    "(local:UIPageTransitionManager.TransitionIndex),"                                             \
    " RelativeSource={RelativeSource TemplatedParent}}\">"                                         \
    "<Grid Style=\"{StaticResource A_Style_Grid_HiddenWhenLocked}\">"                              \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileShadow}\"/></Grid>"          \
    "<Grid Style=\"{StaticResource A_Style_Grid_MotionBlur}\">"                                    \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileBackground}\"/>"
#define EO_TILE_PICTURE                                                                            \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_LockableImage}\""                \
    " ImageSource=\"{Binding DataContext.Icon, RelativeSource={RelativeSource TemplatedParent}}\""
#define EO_TILE_TAIL                                                                               \
    "<Grid Margin=\"{StaticResource A_Thickness_TilePadding}\">"                                   \
    "<local:FZTextBlock x:Name=\"NameText\""                                                       \
    " Text=\"{Binding DataContext.Heading, RelativeSource={RelativeSource TemplatedParent}}\""     \
    " Style=\"{StaticResource A_Style_FZTextBlock_TileTitle}\""                                    \
    " MaxWidth=\"{Binding Width, RelativeSource={RelativeSource TemplatedParent}}\"/>"             \
    "<local:FZMaterial Style=\"{StaticResource A_Style_Tile_LockIcon}\"/></Grid>"                  \
    "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_TileSelectionBar}\"/>"           \
    "</Grid></Grid></ControlTemplate>"
static const char tile_templates[] =
    EO_TILE_HEAD("EO_ControlTemplate_FitIconTile") EO_TILE_PICTURE
    " Width=\"276\" Height=\"276\" Stretch=\"Uniform\" StretchDirection=\"Both\"/>" EO_TILE_TAIL
    EO_TILE_HEAD("EO_ControlTemplate_BottomHalfTile") "<Grid ClipToBounds=\"True\">" EO_TILE_PICTURE
    " Width=\"276\" Height=\"558\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Top\""
    " Margin=\"0,-282,0,0\" StretchDirection=\"Both\"/></Grid>" EO_TILE_TAIL;
#undef EO_TILE_HEAD
#undef EO_TILE_PICTURE
#undef EO_TILE_TAIL
static const char world_item[] = "<local:TileMenuDataItem x:Key=\"EO_MenuDataItem_World\" Heading=\"";
static const char world_icon[] =
    "\" Icon=\"/ForzaUI;component/Textures/Anthem/Icons/PauseMenu/RoadTripProgress.png\"/>";
static const char world_tile_xaml[] =
    "<Button x:Name=\"PART_Button_EOWorld\" local:TileMenuNavigationManager.TileState=\"Enabled\""
    " DataContext=\"{StaticResource EO_MenuDataItem_World}\""
    " local:TileMenuNavigationManager.GridX=\"4\" local:TileMenuNavigationManager.GridY=\"1\""
    " Style=\"{DynamicResource A_Style_Button_Tile_1x1}\""
    " Canvas.Left=\"{StaticResource A_Double_TileCoord_X_4}\""
    " Canvas.Top=\"{StaticResource A_Double_TileCoord_Y_1}\""
    " Template=\"{StaticResource EO_ControlTemplate_FitIconTile}\""
    " local:UIPageTransitionManager.TransitionIndex=\"6\" local:BindingProxy.C=\"DisableMotionBlur\"/>";
static const char credits_size[] = "Style=\"{DynamicResource A_Style_Button_Tile_1x2}\"";
static const char credits_template[] = "Template=\"{StaticResource A_ControlTemplate_VerticalImageTile}\"";

typedef struct {
    const char *at;
    size_t skip;
    const char *parts[3];
} HubEdit;

size_t world_menu_extend_hub_xaml(unsigned char *game_image, const char *text, size_t length,
                                  char *output, size_t capacity) {
    const char *resources = text_find_unique(text, length, "</ResourceDictionary>");
    const char *tiles = text_find_unique(text, length, "</local:AutoSizeCanvas>");
    const char *credits = text_find_unique(text, length, "x:Name=\"PART_Button_TeamCredits\"");
    const char *credits_end =
        credits ? text_find(credits, length - (size_t)(credits - text), "/>") : NULL;
    size_t span = credits_end ? (size_t)(credits_end - credits) : 0;
    const char *size = credits_end ? text_find(credits, span, credits_size) : NULL;
    const char *look = credits_end ? text_find(credits, span, credits_template) : NULL;
    if (!capacity || !resources || !tiles || !size || !look || resources >= credits ||
        credits_end >= tiles || text_find(text, length, "PART_Button_EOWorld")) {
        log_append("WORLD MENU: Settings hub layout did not match; World tile left out.\r\n");
        return 0;
    }
    // The caption in capitals, as the game's tiles, written as XML character
    // references beyond ASCII.
    char heading[160];
    if (!video_options_xml_text(heading, sizeof(heading), video_options_text(game_image)->world)) {
        return 0;
    }
    HubEdit edits[] = {
        {resources, 0, {tile_templates, world_item, heading}},
        {size, sizeof(credits_size) - 1, {"Style=\"{DynamicResource A_Style_Button_Tile_1x1}\"", "", ""}},
        {look, sizeof(credits_template) - 1,
         {"Template=\"{StaticResource EO_ControlTemplate_BottomHalfTile}\"", "", ""}},
        {tiles, 0, {world_tile_xaml, "", ""}},
    };
    for (unsigned i = 1; i < ARRAY_COUNT(edits); ++i) {
        for (unsigned j = i; j > 0 && edits[j - 1].at > edits[j].at; --j) {
            HubEdit swap = edits[j];
            edits[j] = edits[j - 1];
            edits[j - 1] = swap;
        }
    }
    size_t out = 0;
    int fits = 1;
#define HUB_APPEND(bytes, count)                                                                   \
    do {                                                                                           \
        size_t n_ = (count);                                                                       \
        if (out + n_ >= capacity) {                                                                \
            fits = 0;                                                                              \
        } else {                                                                                   \
            memory_copy(output + out, (bytes), n_);                                                \
            out += n_;                                                                             \
        }                                                                                          \
    } while (0)
    // FH2's XML reader only enables UTF-8 decoding after seeing its byte-order
    // mark.
    if (length < 3 || !memory_equal(text, "\xef\xbb\xbf", 3)) {
        HUB_APPEND("\xef\xbb\xbf", 3);
    }
    const char *from = text;
    for (unsigned i = 0; i < ARRAY_COUNT(edits) && fits; ++i) {
        HUB_APPEND(from, (size_t)(edits[i].at - from));
        for (unsigned p = 0; p < 3; ++p) {
            HUB_APPEND(edits[i].parts[p], text_length(edits[i].parts[p]));
        }
        // The data item's closing part follows its heading.
        if (edits[i].at == resources) {
            HUB_APPEND(world_icon, sizeof(world_icon) - 1);
        }
        from = edits[i].at + edits[i].skip;
    }
    HUB_APPEND(from, length - (size_t)(from - text));
#undef HUB_APPEND
    if (!fits) {
        return 0;
    }
    output[out] = 0;
    log_append("WORLD MENU: World tile placed under Team Credits in the Settings hub.\r\n");
    return out;
}

#ifdef FH2_TWEAKS_TEST
/* The hub XAML as rebuilt in game, with the English caption. */
__declspec(dllexport) size_t Fh2TestWorldHubXaml(const char *text, size_t length, char *out,
                                                 size_t capacity) {
    return world_menu_extend_hub_xaml(NULL, text, length, out, capacity);
}

__declspec(dllexport) int Fh2TestWorldMenuSet(unsigned setting, int value) {
    return world_menu_set(setting, value);
}
#endif
