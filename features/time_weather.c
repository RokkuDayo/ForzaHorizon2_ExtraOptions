#include "time_weather.h"
#include "logging.h"
#include "support.h"

/* Inspected FH2 release: the open world's per-frame update (0x3005b0) ticks
   the weather manager [world+0x488] and the time-of-day manager [world+0x480]
   with "mov rcx, [rsi+manager]; call rel32". Both calls are redirected to
   thunks that apply the menu's choice on the game thread, then call the
   original tick. The world, and so both calls, exists only in the open world;
   menus and loads skip them. While a choice is Game nothing is overridden. */
#define WEATHER_TICK_SITE 0x30071cu
#define TIME_TICK_SITE 0x300933u
#define WEATHER_TICK 0x19848b0u
#define TIME_TICK 0x197b170u
#define SET_TIME 0x197aec0u             /* (tod, float seconds, int remap) */
#define SET_DIRECT_CONTROL 0x1981860u   /* (wx, const WeatherControl *) */
#define CLEAR_DIRECT_CONTROL 0x1981c80u /* (wx) */

typedef struct {
    uint32_t rva;
    unsigned size;
    unsigned char bytes[16];
} Expected;
static const Expected expected[] = {
    /* world = [[[image+0x34aba60]+0x48]+0x98] and its manager getters */
    {0x21e0c0, 16, {0x48, 0x8b, 0x49, 0x48, 0x48, 0x85, 0xc9, 0x0f, 0x85, 0x53, 0x34, 0x0c, 0x00, 0x33, 0xc0, 0xc3}},
    {0x2e1520, 8, {0x48, 0x8b, 0x81, 0x98, 0x00, 0x00, 0x00, 0xc3}},
    {0x2e1f90, 8, {0x48, 0x8b, 0x81, 0x80, 0x04, 0x00, 0x00, 0xc3}},
    {0x2e2e90, 8, {0x48, 0x8b, 0x81, 0x88, 0x04, 0x00, 0x00, 0xc3}},
    {SET_TIME, 16, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0x79, 0x25, 0x00, 0x48, 0x8b, 0xd9, 0x75, 0x0a, 0x45}},
    {0x197afe0, 16, {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x41, 0x10, 0x48, 0x8b, 0xd9, 0xc6, 0x41, 0x28, 0x01}},
    {TIME_TICK, 16, {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x78, 0x10, 0x55, 0x48, 0x8d, 0x68, 0xa1}},
    {SET_DIRECT_CONTROL, 16, {0x40, 0x53, 0x48, 0x81, 0xec, 0xa0, 0x00, 0x00, 0x00, 0xc5, 0xf8, 0x10, 0x81, 0xe0, 0x00, 0x00}},
    {CLEAR_DIRECT_CONTROL, 16, {0x48, 0x8b, 0xc4, 0x48, 0x83, 0xec, 0x68, 0xc5, 0xf8, 0x57, 0xc0, 0x48, 0x8d, 0x50, 0xb8, 0xc6}},
    {0x1981bc0, 16, {0x48, 0x8b, 0xc4, 0x48, 0x83, 0xec, 0x68, 0xc5, 0xfa, 0x10, 0x49, 0x18, 0xc5, 0xf8, 0x57, 0xc0}},
    {0x1985950, 16, {0x40, 0x53, 0x48, 0x83, 0xec, 0x50, 0x80, 0xb9, 0xf0, 0x00, 0x00, 0x00, 0x00, 0xc5, 0xf8, 0x29}},
    {WEATHER_TICK, 10, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30}},
    /* mov rcx, [rsi+0x488]; call weather tick */
    {WEATHER_TICK_SITE - 7, 12, {0x48, 0x8b, 0x8e, 0x88, 0x04, 0x00, 0x00, 0xe8, 0x8f, 0x41, 0x68, 0x01}},
    /* mov rcx, [rsi+0x480]; call time-of-day tick */
    {TIME_TICK_SITE - 7, 12, {0x48, 0x8b, 0x8e, 0x80, 0x04, 0x00, 0x00, 0xe8, 0x38, 0xa8, 0x67, 0x01}},
};

/* The game's own control (SetWeatherControl node, ClearDirectControl). */
typedef struct {
    uint8_t has_preset, _a[3];
    int32_t preset;
    uint8_t has_severity, _b[3];
    int32_t severity; /* 0..3: Clear, Cloudy, Threatening, Storm; sets atmosphere */
    uint8_t has_atmosphere, _c[3];
    float atmosphere;
    uint8_t has_environment, _d[3];
    float environment; /* road wetness */
    uint8_t has_precipitation, _e[3];
    float precipitation;
    uint8_t has_extra, _f[3];
    float extra;
    uint8_t b30, b31, b32, _g;
    float f34;
    uint8_t b38, _h[3];
} WeatherControl;
_Static_assert(sizeof(WeatherControl) == 0x3c, "WeatherControl layout");

#define CONTROL(severity, environment, precipitation)                                         \
    {0, {0}, 0, 1, {0}, severity, 0, {0}, 0, 1, {0}, environment, 1, {0}, precipitation, 0,  \
     {0}, 0, 0, 0, 1, 0, 1.0f, 1, {0}}
static const WeatherControl controls[WEATHER_CHOICE_COUNT] = {
    {0},                   /* game: ClearDirectControl */
    CONTROL(0, 0, 0),      /* sunny */
    CONTROL(1, 1.0f, 0),   /* wet: cloudy, wet roads */
    CONTROL(2, 1.0f, 0.6f), /* rain */
    CONTROL(3, 1.0f, 1.0f), /* storm */
};
/* Seconds after midnight; the game remaps them as its SetTimeOfDay node does. */
static const float seconds[TIME_CHOICE_COUNT] = {0, 23400.0f, 43200.0f, 70200.0f, 0};

typedef uint8_t (*Tick)(void *manager, float dt, void *context);
static Tick original_time_tick, original_weather_tick;
static void (*set_time)(void *tod, float seconds, int remap);
static void (*set_direct_control)(void *wx, const WeatherControl *control);
static void (*clear_direct_control)(void *wx);

/* Written by the menu; read by the thunks on the game thread. */
static volatile LONG wanted_time = -1, wanted_weather = -1;
/* Game thread only: the managers last seen and the choices applied to them. */
static void *time_manager, *weather_manager;
static int applied_time, applied_weather;

static uint8_t time_tick(void *tod, float dt, void *context) {
    int choice = (int)wanted_time;
    if (tod && choice >= 0) {
        unsigned char *stopped = (unsigned char *)tod + 0x24;
        // A new world starts on the game's clock.
        if (tod != time_manager) {
            time_manager = tod;
            applied_time = TIME_GAME;
        }
        // Re-applied when something (an event) restarts the clock.
        if (choice != TIME_GAME && (choice != applied_time || !*stopped)) {
            set_time(tod, seconds[choice], 1);
            *stopped = 1;
        } else if (choice == TIME_GAME && applied_time != TIME_GAME) {
            *stopped = 0;
        }
        if (choice != applied_time) log_append_value("TIME OF DAY: applied ", choice);
        applied_time = choice;
    }
    return original_time_tick(tod, dt, context);
}

static uint8_t weather_tick(void *wx, float dt, void *context) {
    int choice = (int)wanted_weather;
    if (wx && choice >= 0) {
        const unsigned char *active = (const unsigned char *)wx + 0xe0;
        if (wx != weather_manager) {
            weather_manager = wx;
            applied_weather = WEATHER_GAME;
        }
        // Re-applied when an event clears the control (atmosphere and
        // precipitation both released).
        if (choice != WEATHER_GAME &&
            (choice != applied_weather || (!active[0x10] && !active[0x20]))) {
            set_direct_control(wx, &controls[choice]);
        } else if (choice == WEATHER_GAME && applied_weather != WEATHER_GAME) {
            clear_direct_control(wx);
        }
        if (choice != applied_weather) log_append_value("WEATHER: applied ", choice);
        applied_weather = choice;
    }
    return original_weather_tick(wx, dt, context);
}

static unsigned char *allocate_near(unsigned char *image, SIZE_T size) {
    /* Within +-2 GB of the image so the call sites reach the stubs. */
    for (uintptr_t offset = 0x4000000; offset < 0x70000000; offset += 0x100000) {
        void *block = VirtualAlloc(image + offset, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (block) return block;
    }
    return 0;
}

int time_weather_install(unsigned char *image, int time_choice, int weather_choice) {
    for (unsigned i = 0; i < ARRAY_COUNT(expected); ++i) {
        unsigned char actual[16];
        if (!memory_read(image + expected[i].rva, actual, expected[i].size) ||
            !memory_equal(actual, expected[i].bytes, expected[i].size)) {
            log_append("TIME/WEATHER: world update not as expected; not installed.\r\n");
            return 0;
        }
    }
    unsigned char *block = allocate_near(image, 0x1000);
    if (!block) {
        log_append("TIME/WEATHER: no memory near the game image; not installed.\r\n");
        return 0;
    }
    original_time_tick = (Tick)(void *)(image + TIME_TICK);
    original_weather_tick = (Tick)(void *)(image + WEATHER_TICK);
    set_time = (void (*)(void *, float, int))(void *)(image + SET_TIME);
    set_direct_control =
        (void (*)(void *, const WeatherControl *))(void *)(image + SET_DIRECT_CONTROL);
    clear_direct_control = (void (*)(void *))(void *)(image + CLEAR_DIRECT_CONTROL);
    time_weather_set_time(time_choice);
    time_weather_set_weather(weather_choice);
    /* Stubs: mov rax, thunk; jmp rax. The thunks take the tick's arguments
       (rcx manager, xmm1 dt, r8 context) unchanged. */
    static const uint32_t sites[2] = {TIME_TICK_SITE, WEATHER_TICK_SITE};
    const uintptr_t thunks[2] = {(uintptr_t)time_tick, (uintptr_t)weather_tick};
    for (unsigned i = 0; i < 2; ++i) {
        static const unsigned char code[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
        memory_copy(block + i * 16, code, sizeof(code));
        memory_copy(block + i * 16 + 2, &thunks[i], 8);
    }
    DWORD previous, ignored;
    if (!VirtualProtect(block, 0x1000, PAGE_EXECUTE_READ, &previous)) return 0;
    FlushInstructionCache(GetCurrentProcess(), block, 0x1000);
    for (unsigned i = 0; i < 2; ++i) {
        unsigned char *code = image + sites[i];
        int32_t d = (int32_t)(block + i * 16 - (code + 5));
        if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &previous)) return 0;
        memory_copy(code + 1, &d, 4);
        VirtualProtect(code, 5, previous, &ignored);
        FlushInstructionCache(GetCurrentProcess(), code, 5);
    }
    log_append("TIME/WEATHER: installed; applied in the open world.\r\n");
    return 1;
}

void time_weather_set_time(int choice) {
    if (choice >= 0 && choice < TIME_CHOICE_COUNT) InterlockedExchange(&wanted_time, choice);
}

void time_weather_set_weather(int choice) {
    if (choice >= 0 && choice < WEATHER_CHOICE_COUNT) InterlockedExchange(&wanted_weather, choice);
}
