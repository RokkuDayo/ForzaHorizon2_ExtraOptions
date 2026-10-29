// Time Lock rows on the game's Settings > Video screen (A_VideoOptions).
//
// Two parts, both patterned on FH2 LiveTune's Enhanced Tuning page:
//
// 1. Native properties. Every row on that page binds to a property the UI
//    looks up by name on the page's type (VideoOptions, registry
//    game+0x35470d0, set up by game+0xeca710). We copy that page's own int
//    property (Subtitles) and float property (Gamma, the Brightness slider)
//    templates, give them our names and getter/setter, and link them into the
//    same type's list:
//      TimeLockEnabled  int, 0 = Off, 1 = On (the page's own OnOffOptions list)
//      TimeLockHour     float hours, 0-23.75
//      TimeLockWeather  int, the game's MainlandWeatherOptions order:
//                       Current (= Dynamic), Sunny, Wet, Rain, Storm
//      TimeLockWeatherOptions  the game's own localized list for that row,
//                       built by game+0xd61880 (as the online event options do),
//                       on a copy of the page's OnOffOptions property
//    Nothing existing is changed; the game's setters are untouched.
//
// 2. The page text. The game loads each page's XAML through one call
//    (game+0x16ebb37) that Extra Options redirects to its jump at
//    game+0x13fe354. We chain onto that jump, let the game and Extra Options
//    load and edit the file as usual, then for A_VideoOptions only:
//      - insert two TileMenuDataItems, a converter and a slider style just
//        before the closing </ResourceDictionary> of <Page.Resources>,
//      - insert a SpinnerControl, a Slider and (when the weather hold is
//        ready) a Weather SpinnerControl row just before the closing tag
//        of the OptionsList CommonListBox (a plain list, not a tile menu), so
//        they follow Brightness and Subtitles (and any rows other mods added),
//      - if that list still has the stock Height="110" (two rows), make it
//        Height="220" (or "275" with the Weather row) so every row shows; any
//        other height (Extra Options sets its own) is left alone and the list
//        scrolls.
//    Anything unexpected (Extra Options' hook missing, an anchor missing or
//    ambiguous, our rows already present) leaves the file exactly as loaded.

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "settings_page.h"

enum {
    RESOURCE_CALL = 0x16ebb37,   // call into Extra Options' jump (originally the game's loader)
    RESOURCE_THUNK = 0x13fe354,  // mov rax, imm64; jmp rax (written by Extra Options)
    NATIVE_ALLOCATE = 0x3229c0,
    NATIVE_FREE = 0x31c7b0,
    OBJECT_INIT = 0x143eb80,
    OBJECT_CLEANUP = 0x1442050,
    DELETE_OBJECT = 0x257730,

    PAGE_TYPE_INIT = 0xeca710,   // VideoOptions type set-up (runs once, guarded by its own flag)
    PAGE_REGISTRY = 0x35470d0,
    PAGE_LEA = 0xeca808,         // lea rcx,[PAGE_REGISTRY] ...
    PAGE_CALL = 0xeca80f,        // ... call REGISTER_PROPERTY (registering Gamma)
    REGISTER_PROPERTY = 0x155ee30,
    INT_TEMPLATE = 0xed81f0,     // returns the Subtitles int property
    INT_TEMPLATE_NAME = 0xed825b,
    FLOAT_TEMPLATE = 0xed6e90,   // returns the Gamma (Brightness) float property
    FLOAT_TEMPLATE_NAME = 0xed6efb,
    LIST_TEMPLATE = 0xed77d0,    // returns the page's OnOffOptions (list-valued) property
    LIST_TEMPLATE_NAME = 0xed782e,
    MAKE_WEATHER_LIST = 0xd61880,// (out) -> new MainlandWeatherOptions data source
    MAKE_WEATHER_CALL = 0xd618bf,// ... call WEATHER_LIST_CTOR
    WEATHER_LIST_CTOR = 0xd63bf0,
    WEATHER_LIST_VTABLE = 0x2e0e3c0, // EnumDataSource<MainlandWeatherOptions>
    WEATHER_LIST_COUNT = 0xd63cad,   // cmp ebx,5: five items
    BOX_INT = 0x4702d0,          // (out, &value) -> boxed int
    BOX_FLOAT = 0x3f5990,        // (out, &value) -> boxed float
    PROPERTY_CHANGED = 0xcf9c60, // (page, property): refresh bindings

    MAX_XAML = 1024 * 1024,
};

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    void *getter, *setter, *unused;
    const char *name;
    void *type;
    uint64_t flags;
} NativeProperty;

typedef struct Link {
    NativeProperty *property;
    struct Link *next;
} Link;

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    char *bytes;
    int length;
    int capacity;
} NativeResource;

_Static_assert(sizeof(NativeProperty) == 0x58, "native property layout");

typedef void **(*BoxFn)(void **, const void *);
typedef void (*PropertyChangedFn)(void *, void *);
typedef void **(*GetResourceFn)(void *, void **);
typedef void *(*AllocateFn)(size_t);
typedef void (*ObjectFn)(void *);

static unsigned char *game;
static LogFn log_message;
static NativeProperty enabled_property, hour_property, weather_property, weather_list_property;
static Link enabled_link, hour_link, weather_link, weather_list_link;
static int weather_row = 1;          // settings_page_install turns it off when the weather hold isn't ready
static void *volatile weather_list;  // the one data source we hand out (we keep a reference forever)


static volatile LONG logged_mismatch, set_logs;
static wchar_t dump_file[MAX_PATH];

static const float HOUR_MIN = 0.0f, HOUR_MAX = 23.75f, HOUR_STEP = 0.25f;

// ---------------------------------------------------------------- XAML text

static const char screen_class[] = "x:Class=\"ForzaUI.A_VideoOptions\"";
static const char our_marker[] = "{Binding TimeLockEnabled}";
static const char list_name[] = "x:Name=\"OptionsList\"";
static const char list_open[] = "<local:CommonListBox";
static const char list_close[] = "</local:CommonListBox>";
static const char resources_close[] = "</Page.Resources>";
static const char dictionary_close[] = "</ResourceDictionary>";
static const char stock_height[] = "Height=\"110\"";  // the stock list fits Brightness + Subtitles
static const char our_height[] = "Height=\"220\"";
static const char our_height_weather[] = "Height=\"275\"";

// Same slider template as the game's A_Style_Slider_Options, but the value is
// shown as hours (e.g. 18.50) instead of a percentage.
static const char resources_text[] =
    "    <local:TileMenuDataItem x:Key=\"TimeLock_Item_Enabled\" Heading=\"Time of Day Lock\" "
    "Description=\"Holds the free-roam clock at the Locked Hour. Races and events that set their own time still use it; "
    "free roam returns to the locked hour afterwards. Saved to EO_WeatherControl.ini.\"/>\r\n"
    "            <local:TileMenuDataItem x:Key=\"TimeLock_Item_Hour\" Heading=\"Locked Hour\" "
    "Description=\"Hour of day to hold, 0 to 23.75 (6 = dawn, 12 = noon, 17.75 = sunset, 23 = night; 18.5 = 18:30). "
    "Applies right away while Time of Day Lock is on.\"/>\r\n"
    "            <local:TileMenuDataItem x:Key=\"TimeLock_Item_Weather\" Heading=\"Weather\" "
    "Description=\"Holds free-roam weather at one of the game's own weather options. Current keeps the normal weather cycle. "
    "Races, car meets and events that set their own weather still do; free roam returns to this afterwards. Saved to EO_WeatherControl.ini.\"/>\r\n"
    "            <local:NumUnitConverter x:Key=\"TimeLock_HourFormat\" FractionDigits=\"2\" ShowUnit=\"False\"/>\r\n"
    "            <Style x:Key=\"TimeLock_HourSlider\" TargetType=\"Slider\" BasedOn=\"{StaticResource A_Style_Slider_Options}\">\r\n"
    "                <Setter Property=\"Minimum\" Value=\"0\"/>\r\n"
    "                <Setter Property=\"Maximum\" Value=\"23.75\"/>\r\n"
    "                <Setter Property=\"SmallChange\" Value=\"0.25\"/>\r\n"
    "                <Setter Property=\"LargeChange\" Value=\"0\"/>\r\n"
    "                <Setter Property=\"Template\">\r\n"
    "                    <Setter.Value>\r\n"
    "                        <ControlTemplate TargetType=\"Slider\">\r\n"
    "                            <Grid FocusManager.IsFocusScope=\"True\">\r\n"
    "                                <Grid.ColumnDefinitions>\r\n"
    "                                    <ColumnDefinition />\r\n"
    "                                    <ColumnDefinition />\r\n"
    "                                </Grid.ColumnDefinitions>\r\n"
    "                                <local:FZTextBlock x:Name=\"PART_Label\" Text=\"{Binding Tag.Heading, RelativeSource={RelativeSource TemplatedParent}}\" Style=\"{StaticResource A_Style_SliderControlText}\" HorizontalAlignment=\"Left\" />\r\n"
    "                                <DockPanel Grid.Column=\"1\">\r\n"
    "                                    <local:FZTextBlock DockPanel.Dock=\"Right\" Width=\"60\" TextAlignment=\"Right\" HorizontalAlignment=\"Right\" Text=\"{Binding Value, RelativeSource={RelativeSource TemplatedParent}, Converter={StaticResource TimeLock_HourFormat}}\" Style=\"{StaticResource A_Style_SliderControlText}\" />\r\n"
    "                                    <Grid Margin=\"32, 0, 8, 0\" Height=\"22\">\r\n"
    "                                        <local:FZMaterial x:Name=\"PART_SliderMaterial\" Color=\"{StaticResource A_Color_MediumGreyText}\" />\r\n"
    "                                        <Track x:Name=\"PART_Track\">\r\n"
    "                                            <Track.DecreaseRepeatButton>\r\n"
    "                                                <RepeatButton x:Name=\"Left\" Style=\"{StaticResource A_Style_RepeatButton_Left_Unfocused}\"/>\r\n"
    "                                            </Track.DecreaseRepeatButton>\r\n"
    "                                        </Track>\r\n"
    "                                    </Grid>\r\n"
    "                                </DockPanel>\r\n"
    "                            </Grid>\r\n"
    "                            <ControlTemplate.Triggers>\r\n"
    "                                <DataTrigger Binding=\"{Binding RelativeSource={RelativeSource Mode=FindAncestor, AncestorType=ListBoxItem}, Path=IsKeyboardFocusWithin}\" Value=\"True\">\r\n"
    "                                    <Setter TargetName=\"PART_SliderMaterial\" Property=\"Color\" Value=\"{StaticResource A_Color_Header_Background}\"/>\r\n"
    "                                    <Setter TargetName=\"Left\" Property=\"Style\" Value=\"{StaticResource A_Style_RepeatButton_Left_Focused}\"/>\r\n"
    "                                </DataTrigger>\r\n"
    "                            </ControlTemplate.Triggers>\r\n"
    "                        </ControlTemplate>\r\n"
    "                    </Setter.Value>\r\n"
    "                </Setter>\r\n"
    "            </Style>\r\n"
    "        ";

static const char rows_text[] =
    "    <local:SpinnerControl Label=\"Time of Day Lock\" Tag=\"{StaticResource TimeLock_Item_Enabled}\" "
    "SelectedIndex=\"{Binding TimeLockEnabled}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\"/>\r\n"
    "                                        <Slider Value=\"{Binding TimeLockHour, Mode=TwoWay}\" "
    "Tag=\"{StaticResource TimeLock_Item_Hour}\" Style=\"{StaticResource TimeLock_HourSlider}\"/>\r\n"
    "                                ";

static const char weather_row_text[] =
    "    <local:SpinnerControl Label=\"Weather\" Tag=\"{StaticResource TimeLock_Item_Weather}\" "
    "SelectedIndex=\"{Binding TimeLockWeather}\" ItemsSource=\"{Binding TimeLockWeatherOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\"/>\r\n"
    "                                ";

static const char *find(const char *text, size_t length, const char *needle, size_t needle_length) {
    if (needle_length > length) return NULL;
    for (size_t i = 0; i + needle_length <= length; ++i)
        if (text[i] == needle[0] && !memcmp(text + i, needle, needle_length)) return text + i;
    return NULL;
}

static const char *find_unique(const char *text, size_t length, const char *needle) {
    size_t n = strlen(needle);
    const char *first = find(text, length, needle, n);
    if (!first) return NULL;
    size_t rest = length - (size_t)(first - text) - 1;
    return find(first + 1, rest, needle, n) ? NULL : first;
}

static const char *find_last_before(const char *text, const char *limit, const char *needle) {
    size_t n = strlen(needle);
    const char *last = NULL;
    for (const char *at = text; at + n <= limit; ++at)
        if (*at == needle[0] && !memcmp(at, needle, n)) last = at;
    return last;
}

static int only_space(const char *from, const char *to) {
    for (; from < to; ++from)
        if (*from != ' ' && *from != '\t' && *from != '\r' && *from != '\n') return 0;
    return 1;
}

static void report_once(const char *message) {
    if (!InterlockedExchange(&logged_mismatch, 1) && log_message) log_message(message);
}

int settings_page_is_xaml(const char *text, size_t length) {
    return find(text, length, screen_class, sizeof screen_class - 1) != NULL;
}

size_t settings_page_edit(const char *text, size_t length, char *out, size_t capacity) {
    if (!settings_page_is_xaml(text, length)) return 0;
    if (find(text, length, our_marker, sizeof our_marker - 1)) return 0;  // already there

    // Resources: the last </ResourceDictionary> before the only </Page.Resources>.
    const char *page_resources_end = find_unique(text, length, resources_close);
    const char *resources_at = page_resources_end ? find_last_before(text, page_resources_end, dictionary_close) : NULL;
    if (!resources_at || !only_space(resources_at + sizeof dictionary_close - 1, page_resources_end)) {
        report_once("TIME LOCK PAGE: Video settings resources did not match; rows not added.\r\n");
        return 0;
    }
    // Rows: the closing tag of the only OptionsList, with no list nested inside it.
    const char *list = find_unique(text, length, list_name);
    const char *rest = list ? list + sizeof list_name - 1 : NULL;
    const char *rows_at = rest ? find(rest, length - (size_t)(rest - text), list_close, sizeof list_close - 1) : NULL;
    if (!rows_at || find(rest, (size_t)(rows_at - rest), list_open, sizeof list_open - 1) || rows_at < resources_at) {
        report_once("TIME LOCK PAGE: Video settings list did not match; rows not added.\r\n");
        return 0;
    }
    // The list must really be a CommonListBox: its tag starts before the name.
    const char *tag = list;
    while (tag > text && *tag != '<') --tag;
    if ((size_t)(list - tag) < sizeof list_open - 1 || memcmp(tag, list_open, sizeof list_open - 1)) {
        report_once("TIME LOCK PAGE: OptionsList is not a CommonListBox; rows not added.\r\n");
        return 0;
    }

    // The list's own start tag: grow the stock two-row height (same length, in place).
    const char *tag_end = find(list, length - (size_t)(list - text), ">", 1);
    const char *height = NULL;
    if (tag_end && tag_end < rows_at) {
        size_t tag_length = (size_t)(tag_end - tag);
        const char *h = find(tag, tag_length, stock_height, sizeof stock_height - 1);
        if (h && h > resources_at &&
            !find(h + 1, tag_length - (size_t)(h + 1 - tag), stock_height, sizeof stock_height - 1))
            height = h;
    }

    size_t a = (size_t)(resources_at - text), b = (size_t)(rows_at - text);
    size_t ra = sizeof resources_text - 1, rb = sizeof rows_text - 1, rw = weather_row ? sizeof weather_row_text - 1 : 0;
    if (length + ra + rb + rw + 1 > capacity) return 0;
    char *w = out;
    memcpy(w, text, a), w += a;
    memcpy(w, resources_text, ra), w += ra;
    memcpy(w, text + a, b - a), w += b - a;
    memcpy(w, rows_text, rb), w += rb;
    memcpy(w, weather_row_text, rw), w += rw;
    memcpy(w, text + b, length - b), w += length - b;
    *w = 0;
    if (height)  // after the resources insert; same length, in place
        memcpy(out + (height - text) + ra, weather_row ? our_height_weather : our_height, sizeof our_height - 1);
    return length + ra + rb + rw;
}

// ---------------------------------------------------------------- properties

static void **get_enabled(void **output, NativeProperty *property, void *page) {
    (void)property, (void)page;
    int value = config_enabled();
    *output = NULL;
    return ((BoxFn)(game + BOX_INT))(output, &value);
}

static void **get_hour(void **output, NativeProperty *property, void *page) {
    (void)property, (void)page;
    float value = config_hour();
    if (value > HOUR_MAX) value = HOUR_MAX;  // an ini hour like 23.9 still shows on the slider
    *output = NULL;
    return ((BoxFn)(game + BOX_FLOAT))(output, &value);
}

static void **get_weather(void **output, NativeProperty *property, void *page) {
    (void)property, (void)page;
    int value = config_weather();
    *output = NULL;
    return ((BoxFn)(game + BOX_INT))(output, &value);
}

// Same shape as the game's own list getters: the object itself, one more reference for the caller.
static void **get_weather_list(void **output, NativeProperty *property, void *page) {
    (void)property, (void)page;
    void *list = weather_list;
    if (!list) {
        void *made = NULL;
        ((void (*)(void **))(game + MAKE_WEATHER_LIST))(&made);
        if (made) {
            if (InterlockedCompareExchangePointer((void *volatile *)&weather_list, made, NULL)) {
                if(InterlockedDecrement((volatile LONG *)((unsigned char *)made+0x10))==0)
                    ((ObjectFn)(game+DELETE_OBJECT))(made);
            }
        }
        list = weather_list;
    }
    *output = list;
    if (list) InterlockedIncrement((volatile LONG *)((unsigned char *)list + 0x10));
    return output;
}

static void log_set(const char *what, double value, int ok) {
    if (InterlockedIncrement(&set_logs) > 40 || !log_message) return;
    char line[160];
    snprintf(line, sizeof line, "TIME LOCK PAGE: set %s %.2f -> %s\r\n", what, value, ok ? "saved" : "refused");
    log_message(line);
}

static void set_enabled(NativeProperty *property, void *page, void *boxed) {
    int value = -1;
    if (boxed) memcpy(&value, (unsigned char *)boxed + 0x28, sizeof value);  // boxed int payload
    int ok = (value == 0 || value == 1) && config_set_enabled(value);
    if (ok) ((PropertyChangedFn)(game + PROPERTY_CHANGED))(page, property);
    log_set("TimeLockEnabled", value, ok);
}

static void set_weather(NativeProperty *property, void *page, void *boxed) {
    int value = -1;
    if (boxed) memcpy(&value, (unsigned char *)boxed + 0x28, sizeof value);  // boxed int payload
    int ok = value >= 0 && value < WEATHER_COUNT && config_set_weather(value);
    if (ok) ((PropertyChangedFn)(game + PROPERTY_CHANGED))(page, property);
    log_set("TimeLockWeather", value, ok);
}

static void set_hour(NativeProperty *property, void *page, void *boxed) {
    float value = -1;
    if (boxed) memcpy(&value, (unsigned char *)boxed + 0x28, sizeof value);  // boxed float payload
    int ok = boxed && value == value && value >= HOUR_MIN - 0.01f && value <= HOUR_MAX + 0.01f;
    if (ok) {
        value = (float)(int)(value / HOUR_STEP + 0.5f) * HOUR_STEP;
        if (value < HOUR_MIN) value = HOUR_MIN;
        if (value > HOUR_MAX) value = HOUR_MAX;
        ok = config_set_hour(value);
        if (ok) ((PropertyChangedFn)(game + PROPERTY_CHANGED))(page, property);
    }
    log_set("TimeLockHour", value, ok);
}

// ---------------------------------------------------------------- install

static int code_at(const unsigned char *image, size_t image_size, uint32_t rva, const char *bytes, size_t length) {
    return rva + length <= image_size && !memcmp(image + rva, bytes, length);
}

// `lea reg,[rip+X]` (7 bytes: 48 8d modrm disp32) at `at`; modrm 0x0d = rcx, 0x05 = rax.
static int is_lea(const unsigned char *image, size_t image_size, uint32_t at, unsigned char modrm) {
    return at + 7 <= image_size && image[at] == 0x48 && image[at + 1] == 0x8d && image[at + 2] == modrm;
}

static uint32_t lea_target(const unsigned char *image, uint32_t at) {
    int32_t disp;
    memcpy(&disp, image + at + 3, 4);
    return (uint32_t)((int64_t)at + 7 + disp);
}

static int call_to(const unsigned char *image, size_t image_size, uint32_t at, uint32_t target) {
    int32_t rel;
    if (at + 5 > image_size || image[at] != 0xe8) return 0;
    memcpy(&rel, image + at + 1, 4);
    return (int64_t)at + 5 + rel == target;
}

int settings_page_verify(const unsigned char *image, size_t image_size, LogFn log) {
    static const struct {
        uint32_t rva;
        const char *bytes;
        size_t length;
        const char *what;
    } functions[] = {
        {REGISTER_PROPERTY, "\x48\x8b\x41\x40\x48\x89\x42\x08\x48\x89\x51\x40\xc3", 13, "register property"},
        {PROPERTY_CHANGED, "\x40\x57\x48\x83\xec\x60\x48\xc7\x44\x24\x20\xfe", 12, "property changed"},
        {BOX_INT, "\x40\x53\x48\x83\xec\x30\x48\x8b\xd9\xc7\x44\x24", 12, "box int"},
        {BOX_FLOAT, "\x40\x53\x48\x83\xec\x30\x48\x8b\xd9\xc7\x44\x24", 12, "box float"},
        {NATIVE_ALLOCATE, "\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x8b\x15", 12, "allocate"},
        {NATIVE_FREE, "\x48\x85\xc9\x74\x1d\x53\x48\x83\xec\x20\x48\x8b", 12, "free"},
        {OBJECT_INIT, "\x48\x89\x5c\x24\x08\x57\x48\x83\xec\x20\x48\x8d", 12, "object init"},
        {OBJECT_CLEANUP, "\x40\x53\x48\x83\xec\x20\x48\x8d\x05\xb3\xd0\x9f", 12, "object cleanup"},
        {DELETE_OBJECT, "\x40\x53\x48\x83\xec\x20\x48\x8b\xd9\xe8\xb2\xa2", 12, "delete object"},
        {PAGE_TYPE_INIT, "\x40\x53\x48\x81\xec\xd0\x00\x00\x00\x48\xc7\x44\x24\x20\xfe\xff", 16, "VideoOptions type set-up"},
        {INT_TEMPLATE, "\x40\x57\x48\x83\xec\x30\x48\xc7\x44\x24\x20\xfe\xff\xff\xff\x48", 16, "int property template"},
        {FLOAT_TEMPLATE, "\x40\x57\x48\x83\xec\x30\x48\xc7\x44\x24\x20\xfe\xff\xff\xff\x48", 16, "float property template"},
        {LIST_TEMPLATE, "\x40\x57\x48\x83\xec\x30\x48\xc7\x44\x24\x20\xfe\xff\xff\xff\x48", 16, "list property template"},
        {MAKE_WEATHER_LIST, "\x40\x57\x48\x83\xec\x30\x48\xc7\x44\x24\x28\xfe\xff\xff\xff\x48", 16, "weather list maker"},
        {WEATHER_LIST_COUNT, "\x83\xfb\x05", 3, "weather list size"},
    };
    char line[200];
    for (unsigned i = 0; i < ARRAYSIZE(functions); ++i)
        if (!code_at(image, image_size, functions[i].rva, functions[i].bytes, functions[i].length)) {
            snprintf(line, sizeof line, "TIME LOCK PAGE: unexpected game code at game+0x%x (%s); no Settings rows.\r\n",
                     functions[i].rva, functions[i].what);
            if (log) log(line);
            return 0;
        }
    if (!is_lea(image, image_size, PAGE_LEA, 0x0d) || lea_target(image, PAGE_LEA) != PAGE_REGISTRY ||
        !call_to(image, image_size, PAGE_CALL, REGISTER_PROPERTY) || image_size <= PAGE_REGISTRY + 0x48) {
        if (log) log("TIME LOCK PAGE: VideoOptions type didn't match; no Settings rows.\r\n");
        return 0;
    }
    // The templates must still be the properties we expect, by the name they set.
    if (!is_lea(image, image_size, INT_TEMPLATE_NAME, 0x05) || !is_lea(image, image_size, FLOAT_TEMPLATE_NAME, 0x05)) {
        if (log) log("TIME LOCK PAGE: property templates didn't match; no Settings rows.\r\n");
        return 0;
    }
    if (!is_lea(image, image_size, LIST_TEMPLATE_NAME, 0x05) || lea_target(image, LIST_TEMPLATE_NAME) + 13 > image_size ||
        memcmp(image + lea_target(image, LIST_TEMPLATE_NAME), "OnOffOptions", 13) ||
        !call_to(image, image_size, MAKE_WEATHER_CALL, WEATHER_LIST_CTOR) || !is_lea(image, image_size, WEATHER_LIST_CTOR + 0x1d, 0x05) ||
        lea_target(image, WEATHER_LIST_CTOR + 0x1d) != WEATHER_LIST_VTABLE) {
        if (log) log("TIME LOCK PAGE: weather list didn't match; no Settings rows.\r\n");
        return 0;
    }
    uint32_t int_name = lea_target(image, INT_TEMPLATE_NAME), float_name = lea_target(image, FLOAT_TEMPLATE_NAME);
    if (int_name + 10 > image_size || float_name + 6 > image_size || memcmp(image + int_name, "Subtitles", 10) ||
        memcmp(image + float_name, "Gamma", 6)) {
        if (log) log("TIME LOCK PAGE: property template names didn't match; no Settings rows.\r\n");
        return 0;
    }
    return 1;
}

static int make_property(NativeProperty *property, Link *link, uint32_t template_rva, const char *template_name,
                         const char *name, void *getter, void *setter) {
    NativeProperty *template_property = ((NativeProperty * (*)(void))(game + template_rva))();
    if (!template_property || !template_property->vtable || !template_property->name ||
        strcmp(template_property->name, template_name) || !template_property->type)
        return 0;
    ((void (*)(void *))(game + OBJECT_INIT))(property);
    property->vtable = template_property->vtable;
    memcpy((unsigned char *)property + 0x28, (unsigned char *)template_property + 0x28, 0x30);
    property->name = name;
    property->getter = getter;
    property->setter = setter;
    link->property = property;
    link->next = NULL;
    return 1;
}

int settings_page_install(unsigned char *image, size_t image_size, LogFn log, const wchar_t *dump_path, int weather) {
    game = image;
    log_message = log;
    weather_row = weather != 0;
    if (dump_path) {
        wcsncpy(dump_file, dump_path, MAX_PATH - 1);
        dump_file[MAX_PATH - 1] = 0;
    }
    if (!settings_page_verify(image, image_size, log)) return 0;

    ((void (*)(void))(image + PAGE_TYPE_INIT))();  // idempotent: guarded by the game's own flag
    if (!make_property(&enabled_property, &enabled_link, INT_TEMPLATE, "Subtitles", "TimeLockEnabled",
                       (void *)get_enabled, (void *)set_enabled) ||
        !make_property(&hour_property, &hour_link, FLOAT_TEMPLATE, "Gamma", "TimeLockHour", (void *)get_hour,
                       (void *)set_hour)) {
        log("TIME LOCK PAGE: property templates looked wrong at run time; no Settings rows.\r\n");
        return 0;
    }
    ((void (*)(void *, Link *))(image + REGISTER_PROPERTY))(image + PAGE_REGISTRY, &enabled_link);
    ((void (*)(void *, Link *))(image + REGISTER_PROPERTY))(image + PAGE_REGISTRY, &hour_link);
    if (weather_row) {
        if (make_property(&weather_property, &weather_link, INT_TEMPLATE, "Subtitles", "TimeLockWeather",
                          (void *)get_weather, (void *)set_weather) &&
            make_property(&weather_list_property, &weather_list_link, LIST_TEMPLATE, "OnOffOptions",
                          "TimeLockWeatherOptions", (void *)get_weather_list, NULL)) {
            ((void (*)(void *, Link *))(image + REGISTER_PROPERTY))(image + PAGE_REGISTRY, &weather_link);
            ((void (*)(void *, Link *))(image + REGISTER_PROPERTY))(image + PAGE_REGISTRY, &weather_list_link);
        } else {
            weather_row = 0;
            log("TIME LOCK PAGE: list property template looked wrong; no Weather row.\r\n");
        }
    }
    log(weather_row ? "TIME LOCK PAGE: TimeLockEnabled/TimeLockHour/TimeLockWeather registered on the VideoOptions type.\r\n"
                    : "TIME LOCK PAGE: TimeLockEnabled/TimeLockHour registered on the VideoOptions type.\r\n");

    return 1;
}

void settings_page_refresh(void *page){
    ((PropertyChangedFn)(game+PROPERTY_CHANGED))(page,&enabled_property);
    ((PropertyChangedFn)(game+PROPERTY_CHANGED))(page,&hour_property);
    ((PropertyChangedFn)(game+PROPERTY_CHANGED))(page,&weather_property);
}

#ifdef WEATHER_CONTROL_TEST
void settings_page_test_bind(void *base){game=base;}
#endif
