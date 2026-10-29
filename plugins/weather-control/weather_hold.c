// Holds free-roam weather at one of the game's own weather options.
//
// The world (the same object whose +0x480 is the time-of-day clock) owns one
// WeatherManager at world+0x488. Scripts steer it with one call,
// WeatherManager::SetControl(W, &control) at game+0x1981860, which copies a
// 0x3c-byte control block to W+0xe0 (see research/time_of_day.md):
//   +0x00 has preset   +0x04 preset index (from FindPreset, game+0x1981e00)
//   +0x08 has severity +0x0c severity (Clear, Cloudy, Threatening, Storm)
//   +0x10 has atmospheric wetness  +0x14 value
//   +0x18 has environmental (road) wetness  +0x1c value
//   +0x20 has precipitation  +0x24 value
//   +0x28 / +0x2c unused by scripts, +0x30 word 0, +0x32 = 1, +0x34 = 1.0f, +0x38 = 1
// A block with no "has" flag set is "no control": dynamic weather. That is
// what Release (game+0x1981c80) passes, and it is what free roam runs on
// (global.unpause_weather and SetWeatherControl enable="false" both release).
// Races with their own weather (SetWeatherFromGameOptions), car meets,
// cutscenes and the first-time flows all set flags.
//
// Each frame, from the clock hook (the world update has just run the weather
// manager's own update on this thread), we look at W's current block:
//   - mode chosen and the block is released: SetControl with the same block
//     the game builds for that option in its online-event code (0xd6f110):
//     the preset by name, its environmental wetness and precipitation, so
//     the weather blends there the way the game's own changes do;
//   - a block we set (still byte-for-byte what SetControl left) and the mode
//     changed: set the new one, or Release when the mode is Dynamic;
//   - anything else (the game's own control) is left alone.
// Every game function and offset used is checked against the expected bytes first.

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "weather_hold.h"

enum {
    WORLD_GLOBAL = 0x34aba60,       // [WORLD_GLOBAL]->[0x48]->[0x98] = world
    WORLD_ACCESSOR = 0x6ae290,      // mov rcx,[WORLD_GLOBAL]; mov edx,1; jmp 0x21e0c0
    WORLD_STEP1 = 0x21e0c0,         // mov rcx,[rcx+0x48]; test; jne 0x2e1520
    WORLD_STEP2 = 0x2e1520,         // mov rax,[rcx+0x98]
    WORLD_TOD = 0x2e1f90,           // mov rax,[rcx+0x480]
    WORLD_WEATHER = 0x2e2e90,       // mov rax,[rcx+0x488]
    WORLD_UPDATE_WEATHER = 0x300715,// world update: mov rcx,[rsi+0x488]; call WeatherManager update
    SET_CONTROL = 0x1981860,
    RELEASE = 0x1981c80,
    FIND_PRESET = 0x1981e00,
    OPTIONS = 0x2959a0,             // game options; [+0x3b7d] makes the weather behaviours do nothing
    OPTIONS_CHECK = 0x92f784,       // SetWeatherControl: cmp byte [rax+0x3b7d],0
    PAUSE_CHECK = 0x8e9a9b,         // PauseWeather: the same check

    CONTROL = 0xe0,                 // W+0xe0: current control block
    CONTROL_SIZE = 0x3c,
    NO_WEATHER_SCRIPTS = 0x3b7d,
};

// The game's own weather options (online-event code at 0xd6f110, values from
// the UI/OnlineEventOptions/Weather/* tunables). Index = WeatherMode.
static const struct {
    const char *preset;
    float environmental, precipitation;
} modes[WEATHER_COUNT] = {
    {NULL, 0, 0},                           // Dynamic: never set
    {"Clear", 0.0f, 0.0f},                  // Sunny
    {"ThreateningStormCCloudyA", 0.6f, 0.0f}, // Wet
    {"ThreateningStormACloudyB", 1.0f, 0.8f}, // Rain
    {"StormBCloudyA", 1.0f, 1.0f},          // Storm
};

// MSVC std::string as the game passes it: 16-byte buffer (or pointer), size, capacity.
typedef struct {
    union {
        char buffer[16];
        const char *pointer;
    };
    size_t size, capacity;
} GameString;
_Static_assert(sizeof(GameString) == 0x20, "std::string layout");

static unsigned char *game;
static LogFn log_message;
static WeatherSetControlFn set_control;
static WeatherReleaseFn release;
static WeatherFindPresetFn find_preset;
static unsigned char *(*weather_of)(unsigned char *tod);
static int ready;

// Game thread only.
static unsigned char *last_weather;
static unsigned char ours[CONTROL_SIZE];  // W's block right after our SetControl
static int have_ours;
static long applied_version = -1;
static unsigned logs;

static void log_line(const char *text) {
    if (log_message && logs < 40) {
        ++logs;
        log_message(text);
    }
}

const char *weather_hold_preset_name(int mode) {
    return mode > 0 && mode < WEATHER_COUNT ? modes[mode].preset : NULL;
}

void weather_hold_build_control(int mode, int preset_index, unsigned char *control) {
    memset(control, 0, CONTROL_SIZE);
    if (mode <= 0 || mode >= WEATHER_COUNT) return;
    float one = 1.0f;
    control[0x00] = 1;
    memcpy(control + 0x04, &preset_index, 4);
    control[0x18] = 1;
    memcpy(control + 0x1c, &modes[mode].environmental, 4);
    control[0x20] = 1;
    memcpy(control + 0x24, &modes[mode].precipitation, 4);
    control[0x32] = 1;  // the same tail every script call passes
    memcpy(control + 0x34, &one, 4);
    control[0x38] = 1;
}

static int released(const unsigned char *weather) {
    const unsigned char *c = weather + CONTROL;
    return !c[0x00] && !c[0x08] && !c[0x10] && !c[0x18] && !c[0x20] && !c[0x28];
}

static int still_ours(const unsigned char *weather) {
    return have_ours && !memcmp(weather + CONTROL, ours, CONTROL_SIZE);
}

static void apply(unsigned char *weather, int mode, const char *why) {
    GameString name;
    memset(&name, 0, sizeof name);
    const char *preset = modes[mode].preset;
    name.size = strlen(preset);
    if (name.size < 16) {
        memcpy(name.buffer, preset, name.size + 1);
        name.capacity = 15;
    } else {
        name.pointer = preset;  // read only: FindPreset copies it
        name.capacity = name.size;
    }
    int index = find_preset(weather, &name);
    unsigned char control[0x40];
    weather_hold_build_control(mode, index, control);
    set_control(weather, control);
    memcpy(ours, weather + CONTROL, CONTROL_SIZE);
    have_ours = 1;
    char line[200];
    snprintf(line, sizeof line, "TIME LOCK: %s; weather set to %s (preset %s #%d, road wetness %.2f, rain %.2f).\r\n", why,
             config_weather_name(mode), preset, index, (double)modes[mode].environmental, (double)modes[mode].precipitation);
    log_line(line);
}

static unsigned char *game_weather_of(unsigned char *tod) {
    unsigned char *global = *(unsigned char **)(game + WORLD_GLOBAL);
    if (!global) return NULL;
    unsigned char *holder = *(unsigned char **)(global + 0x48);
    if (!holder) return NULL;
    unsigned char *world = *(unsigned char **)(holder + 0x98);
    if (!world || *(unsigned char **)(world + 0x480) != tod) return NULL;  // only the clock's own world
    return *(unsigned char **)(world + 0x488);
}

static int scripts_disabled(void) {
    if (!game) return 0;
    unsigned char *options = ((unsigned char *(*)(void))(game + OPTIONS))();
    return options && options[NO_WEATHER_SCRIPTS];
}

void weather_hold_frame(unsigned char *tod) {
    if (!ready || !tod) return;
    unsigned char *weather = weather_of(tod);
    if (!weather) return;
    if (weather != last_weather) {  // a new world: forget the old manager
        last_weather = weather;
        have_ours = 0;
    }
    long version = config_version();
    int changed = version != applied_version;
    applied_version = version;
    int mode = config_weather();
    if (scripts_disabled()) return;
    if (mode != WEATHER_DYNAMIC) {
        if (released(weather))
            apply(weather, mode, have_ours ? "the game released the weather again" : "free-roam weather was dynamic");
        else if (changed && still_ours(weather))
            apply(weather, mode, "weather setting changed");
    } else if (changed && still_ours(weather)) {
        release(weather);
        have_ours = 0;
        log_line("TIME LOCK: weather set to Dynamic; the game's own weather cycle is back.\r\n");
    }
}

// ---------------------------------------------------------------- guards

static int code_at(const unsigned char *image, size_t size, uint32_t rva, const char *bytes, size_t length) {
    return rva + length <= size && !memcmp(image + rva, bytes, length);
}

static int rel32_to(const unsigned char *image, size_t size, uint32_t at, unsigned length, uint32_t target) {
    int32_t rel;
    if (at + length > size) return 0;
    memcpy(&rel, image + at + length - 4, 4);
    return (int64_t)at + length + rel == target;
}

int weather_hold_verify(const unsigned char *image, size_t size, LogFn log) {
    static const struct {
        uint32_t rva;
        const char *bytes;
        size_t length;
        const char *what;
    } code[] = {
        {WORLD_ACCESSOR, "\x48\x8b\x0d", 3, "world accessor"},
        {WORLD_ACCESSOR + 7, "\xba\x01\x00\x00\x00\xe9", 6, "world accessor"},
        {WORLD_STEP1, "\x48\x8b\x49\x48\x48\x85\xc9\x0f\x85", 9, "world lookup"},
        {WORLD_STEP2, "\x48\x8b\x81\x98\x00\x00\x00\xc3", 8, "world lookup"},
        {WORLD_TOD, "\x48\x8b\x81\x80\x04\x00\x00\xc3", 8, "world clock"},
        {WORLD_WEATHER, "\x48\x8b\x81\x88\x04\x00\x00\xc3", 8, "world weather manager"},
        {WORLD_UPDATE_WEATHER, "\x48\x8b\x8e\x88\x04\x00\x00\xe8", 8, "weather update in the world update"},
        // SetControl: saves the old block, then copies 0x38 + 4 bytes of the new one to W+0xe0.
        {SET_CONTROL, "\x40\x53\x48\x81\xec\xa0\x00\x00\x00\xc5\xf8\x10\x81\xe0\x00\x00\x00\xc5\xf8\x10\x89\xf0\x00\x00\x00"
                      "\x8b\x81\x18\x01\x00\x00", 31, "WeatherManager::SetControl"},
        // Release: an all-clear block with the standard tail, passed to SetControl.
        {RELEASE, "\x48\x8b\xc4\x48\x83\xec\x68\xc5\xf8\x57\xc0\x48\x8d\x50\xb8\xc6\x40\xb8\x00\xc6\x40\xc0\x00\xc7\x40\xc4"
                  "\x00\x00\x00\x00\xc6\x40\xc8\x00", 34, "WeatherManager::Release"},
        {RELEASE + 0x4f, "\x66\xc7\x40\xe8\x00\x00\xc6\x40\xea\x01\xc6\x40\xf0\x01\xe8", 15, "WeatherManager::Release"},
        {FIND_PRESET, "\x40\x53\x48\x83\xec\x40\x48\x83\xb9\x58\x01\x00\x00\x00\x48\x8b\xd9\x74\x47", 19, "FindPreset"},
        {FIND_PRESET + 0x51, "\x8b\x40\x60\x48\x83\xc4\x40\x5b\xc3", 9, "FindPreset"},
        {OPTIONS, "\x40\x53\x48\x83\xec\x30\x48\xc7\x44\x24\x20\xfe\xff\xff\xff\x8b", 16, "game options"},
        {OPTIONS_CHECK, "\x80\xb8\x7d\x3b\x00\x00\x00", 7, "SetWeatherControl options check"},
        {PAUSE_CHECK, "\x80\xb8\x7d\x3b\x00\x00\x00", 7, "PauseWeather options check"},
    };
    char line[200];
    for (unsigned i = 0; i < ARRAYSIZE(code); ++i)
        if (!code_at(image, size, code[i].rva, code[i].bytes, code[i].length)) {
            snprintf(line, sizeof line, "TIME LOCK: unexpected game code at game+0x%x (%s); weather not held.\r\n", code[i].rva,
                     code[i].what);
            if (log) log(line);
            return 0;
        }
    if (!rel32_to(image, size, WORLD_ACCESSOR, 7, WORLD_GLOBAL) || !rel32_to(image, size, WORLD_ACCESSOR + 12, 5, WORLD_STEP1) ||
        !rel32_to(image, size, WORLD_STEP1 + 7, 6, WORLD_STEP2) || !rel32_to(image, size, RELEASE + 0x5d, 5, SET_CONTROL) ||
        !rel32_to(image, size, OPTIONS_CHECK - 5, 5, OPTIONS)) {
        if (log) log("TIME LOCK: weather call chain didn't match; weather not held.\r\n");
        return 0;
    }
    return 1;
}

int weather_hold_init(unsigned char *image, size_t image_size, LogFn log) {
    game = image;
    log_message = log;
    if (!weather_hold_verify(image, image_size, log)) return 0;
    set_control = (WeatherSetControlFn)(void *)(image + SET_CONTROL);
    release = (WeatherReleaseFn)(void *)(image + RELEASE);
    find_preset = (WeatherFindPresetFn)(void *)(image + FIND_PRESET);
    weather_of = game_weather_of;
    ready = 1;
    log("TIME LOCK: weather hold ready (WeatherManager::SetControl at game+0x1981860).\r\n");
    return 1;
}

void weather_hold_test_setup(WeatherSetControlFn set, WeatherReleaseFn rel, WeatherFindPresetFn find,
                             unsigned char *(*of)(unsigned char *tod), LogFn log) {
    game = NULL;
    log_message = log;
    set_control = set;
    release = rel;
    find_preset = find;
    weather_of = of;
    last_weather = NULL;
    have_ours = 0;
    applied_version = -1;
    logs = 0;
    ready = 1;
}
