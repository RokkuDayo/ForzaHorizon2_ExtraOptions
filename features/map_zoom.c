#include "map_zoom.h"
#include "logging.h"
#include "support.h"

/* Pause (world) map zoom.

   Inspected FH2 release:
   - The pause map is a MapViewControllerInteractive (vtable 0x2d9cd48, ctor
     0xb7bc90, factory 0xb778e0). A_Map.xaml / A_IslandMap.xaml set its
     XAML properties through one-line setters (0xb8a3a0..0xb8a4d0):
       MinMapDistance +0x370 (A_Map 900, island 600)
       MaxMapDistance +0x374 (A_Map 2500, island 1100)
       MinCursorSpeed +0x37c, MaxCursorSpeed +0x380 (pan speed)
       MinCursorZoom +0x384 (0), MaxCursorZoom +0x388 (1)
       ZoomSpeed +0x38c (3.0)
     The initial zoom +0x390 is not a property: the ctor sets it to 1.0.
   - Zoom is a 0..1 value t at +0x2e0. The camera distance (height above the
     map; 90 degree FOV, top-down) is MinMapDistance + (MaxMapDistance -
     MinMapDistance) * t (0xb8db80 at 0xb8dc22). The pan speed is the same
     lerp of Min/MaxCursorSpeed. Input (0xb8d790, fed by the pause map page
     input 0xd867b0) moves t by ZoomSpeed * zoom axis * dt and clamps it to
     [MinCursorZoom, MaxCursorZoom].
   - The pause map page setup 0xd7fd80 calls 0xb803f0 (ResetView: centre on
     the given position, t = [+0x390], reset the springs, update) at
     0xd80442; it is its only caller. With t = 1.0 the map therefore always
     opens at MaxMapDistance, which is also the farthest zoom-out: 2500 m on
     the mainland, 1100 m on Storm Island.
   - Race/story scripts can narrow the t range through the ConstrainMapZoom
     Min/Max component properties (0xd91630 writes +0x384/+0x388).
   - The camera's far plane is +0xc4 (10000, base ctor 0x192fd00), so the
     distance must stay well below it. The map's low tile covers the whole
     world and is always loaded; the high tiles only draw below 2000 m
     (MapProfileTest.xml VisibleRange), so a larger distance shows the
     low-resolution tile, as the game's own max zoom-out already does.

   This module redirects the ResetView call through the int3 padding after
   0xb803f0. Before the game's ResetView runs it raises MaxMapDistance (and
   MaxCursorSpeed with it so the pan speed at a given distance is unchanged),
   scales ZoomSpeed so zooming moves at the game's metres per second, and
   sets the initial t for the requested opening distance. The zoom range is
   still t 0..1, so the zoom controls cover the full new range.

   The Storm Island map (A_IslandMap, MaxMapDistance 1100) has its own
   percentages: its map texture ends close to the island, so the mainland's
   zoom-out shows black around it. */

#define RESET_VIEW 0xb803f0u
#define RESET_VIEW_SIZE 0x54u
#define RESET_VIEW_CALL 0xd80442u
#define CAVE 0xb80444u
#define CONTROLLER_VTABLE 0x2d9cd48u

#define FAR_PLANE 0xc4
#define MIN_DISTANCE 0x370
#define MAX_DISTANCE 0x374
#define MIN_PAN_SPEED 0x37c
#define MAX_PAN_SPEED 0x380
#define MIN_ZOOM 0x384
#define MAX_ZOOM 0x388
#define ZOOM_SPEED 0x38c
#define INITIAL_ZOOM 0x390

/* Above the island map's 1100 m, below the mainland's 2500 m. */
#define ISLAND_MAX_DISTANCE 1500.0f

#define DEFAULT_START_PERCENT 100
#define DEFAULT_MAX_PERCENT 100
#define DEFAULT_ISLAND_START_PERCENT 100
#define DEFAULT_ISLAND_MAX_PERCENT 100
#define MIN_PERCENT 25
#define MAX_PERCENT 400

typedef void (*ResetView)(void *controller, const void *centre);

typedef struct {
    uint32_t rva;
    unsigned size;
    unsigned char bytes[112];
} Expected;

static const Expected expected[] = {
    /* ResetView: [+0x2c0] = centre, [+0x2c4] = 0, [+0x2e0] = [+0x390],
       [+0x2d0] = [+0x2c0], call 0xb80680, jmp 0xb8db80 (dt 0, snap) */
    {RESET_VIEW,
     RESET_VIEW_SIZE,
     {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0xc5, 0xf8, 0x28, 0x02, 0x8b, 0x81, 0x90, 0x03,
      0x00, 0x00, 0x48, 0x8b, 0xd9, 0xc5, 0xf8, 0x29, 0x81, 0xc0, 0x02, 0x00, 0x00, 0xc7,
      0x81, 0xc4, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x81, 0xe0, 0x02, 0x00,
      0x00, 0xc5, 0xf8, 0x28, 0x89, 0xc0, 0x02, 0x00, 0x00, 0xc5, 0xf8, 0x29, 0x89, 0xd0,
      0x02, 0x00, 0x00, 0xe8, 0x50, 0x02, 0x00, 0x00, 0x41, 0xb0, 0x01, 0x48, 0x8b, 0xcb,
      0xc5, 0xf0, 0x57, 0xc9, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0x3c, 0xd7, 0x00, 0x00}},
    /* page setup: lea rdx,[rbp-0x30]; mov rcx,[r14+0x410]; call 0xb803f0 */
    {RESET_VIEW_CALL - 11,
     16,
     {0x48, 0x8d, 0x55, 0xd0, 0x49, 0x8b, 0x8e, 0x10, 0x04, 0x00, 0x00, 0xe8, 0xa9, 0xff, 0xdf,
      0xff}},
    /* camera distance = [+0x370] + ([+0x374] - [+0x370]) * [+0x2e0] */
    {0xb8dc22,
     0x24,
     {0xc5, 0xfa, 0x10, 0x99, 0x70, 0x03, 0x00, 0x00, 0xc5, 0xfa, 0x10, 0x81, 0x74, 0x03, 0x00,
      0x00, 0xc5, 0xfa, 0x5c, 0xcb, 0xc5, 0xf2, 0x59, 0x91, 0xe0, 0x02, 0x00, 0x00, 0xc5, 0xea,
      0x58, 0xdb, 0xc5, 0xe2, 0x59, 0x81}},
    /* input: t -= [+0x38c] * axis * dt, clamped to [+0x384], [+0x388] */
    {0xb8d937,
     0x6f,
     {0xc5, 0xfa, 0x10, 0x8b, 0x8c, 0x03, 0x00, 0x00, 0xc5, 0xf2, 0x59, 0x93, 0x74, 0x02, 0x00,
      0x00, 0xc5, 0xfa, 0x10, 0x8b, 0x88, 0x03, 0x00, 0x00, 0xc5, 0xf8, 0x29, 0x83, 0xc0, 0x02,
      0x00, 0x00, 0xc5, 0xfa, 0x10, 0x83, 0xe0, 0x02, 0x00, 0x00, 0xc4, 0xc1, 0x6a, 0x59, 0xda,
      0xc5, 0xfa, 0x10, 0x93, 0x84, 0x03, 0x00, 0x00, 0xc5, 0xfa, 0x5c, 0xe3, 0xc5, 0xe0, 0x57,
      0xdb, 0xc5, 0xda, 0x5c, 0xc1, 0xc5, 0xfa, 0x11, 0xa3, 0xe0, 0x02, 0x00, 0x00, 0xc5, 0xf8,
      0x2f, 0xc3, 0x73, 0x04, 0xc5, 0xf8, 0x28, 0xcc, 0xc5, 0xf2, 0x5c, 0xc2, 0xc5, 0xf8, 0x2f,
      0xc3, 0x72, 0x0a, 0xc5, 0xfa, 0x11, 0x8b, 0xe0, 0x02, 0x00, 0x00, 0xeb, 0x08, 0xc5, 0xfa,
      0x11, 0x93, 0xe0, 0x02, 0x00, 0x00}},
    /* XAML setters: mov eax,[r8+0x28]; mov [rdx+field],eax; ret */
    {0xb8a480, 11, {0x41, 0x8b, 0x40, 0x28, 0x89, 0x82, 0x70, 0x03, 0x00, 0x00, 0xc3}},
    {0xb8a450, 11, {0x41, 0x8b, 0x40, 0x28, 0x89, 0x82, 0x74, 0x03, 0x00, 0x00, 0xc3}},
    {0xb8a460, 11, {0x41, 0x8b, 0x40, 0x28, 0x89, 0x82, 0x7c, 0x03, 0x00, 0x00, 0xc3}},
    {0xb8a420, 11, {0x41, 0x8b, 0x40, 0x28, 0x89, 0x82, 0x80, 0x03, 0x00, 0x00, 0xc3}},
    {0xb8a470, 11, {0x41, 0x8b, 0x40, 0x28, 0x89, 0x82, 0x84, 0x03, 0x00, 0x00, 0xc3}},
    {0xb8a430, 11, {0x41, 0x8b, 0x40, 0x28, 0x89, 0x82, 0x88, 0x03, 0x00, 0x00, 0xc3}},
    {0xb8a4d0, 11, {0x41, 0x8b, 0x40, 0x28, 0x89, 0x82, 0x8c, 0x03, 0x00, 0x00, 0xc3}},
    /* ctor: initial zoom [+0x390] = 1.0 */
    {0xb7be44, 10, {0xc7, 0x83, 0x90, 0x03, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f}},
    /* far plane read by the camera projection (cam +0x44 = controller +0xc4) */
    {0x19440c1, 5, {0xc5, 0xfa, 0x10, 0x63, 0x44}},
};

static unsigned char *image;
static ResetView reset_view;
static int start_percent = DEFAULT_START_PERCENT;
static int max_percent = DEFAULT_MAX_PERCENT;
static int island_start_percent = DEFAULT_ISLAND_START_PERCENT;
static int island_max_percent = DEFAULT_ISLAND_MAX_PERCENT;

/* Controllers already adjusted, with the distance written, so a second
   ResetView on the same controller does not scale it again. */
typedef struct {
    unsigned char *controller;
    float max_distance;
    float initial_zoom;
} Adjusted;
static Adjusted adjusted[8];
static unsigned adjusted_next;
/* Original max distances already logged (mainland, island). */
static float logged[4];
static unsigned logged_count;
static int warned_constrained, warned_values;

void map_zoom_config_defaults(void) {
    start_percent = DEFAULT_START_PERCENT;
    max_percent = DEFAULT_MAX_PERCENT;
    island_start_percent = DEFAULT_ISLAND_START_PERCENT;
    island_max_percent = DEFAULT_ISLAND_MAX_PERCENT;
}

static int parse_percent(const char *text, int *value) {
    int result = 0;
    if (!*text) {
        return 0;
    }
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || result > 100000) {
            return 0;
        }
        result = result * 10 + (*text - '0');
    }
    *value = result;
    return 1;
}

int map_zoom_parse(const char *key, const char *value) {
    int parsed;
    if (text_equal_ignore_case(key, "MapStartZoom")) {
        if (!parse_percent(value, &parsed) || parsed < MIN_PERCENT || parsed > MAX_PERCENT) {
            return 0;
        }
        start_percent = parsed;
        return 1;
    }
    if (text_equal_ignore_case(key, "MapMaxZoomOut")) {
        if (!parse_percent(value, &parsed) || parsed < 100 || parsed > MAX_PERCENT) {
            return 0;
        }
        max_percent = parsed;
        return 1;
    }
    if (text_equal_ignore_case(key, "MapIslandStartZoom")) {
        if (!parse_percent(value, &parsed) || parsed < MIN_PERCENT || parsed > MAX_PERCENT) {
            return 0;
        }
        island_start_percent = parsed;
        return 1;
    }
    if (text_equal_ignore_case(key, "MapIslandMaxZoomOut")) {
        if (!parse_percent(value, &parsed) || parsed < 100 || parsed > MAX_PERCENT) {
            return 0;
        }
        island_max_percent = parsed;
        return 1;
    }
    return -1;
}

static float *field(unsigned char *controller, unsigned offset) {
    return (float *)(controller + offset);
}

static int valid_number(float value) {
    return float_is_finite(value);
}

static Adjusted *find_adjusted(unsigned char *controller) {
    for (unsigned i = 0; i < ARRAY_COUNT(adjusted); ++i) {
        if (adjusted[i].controller == controller) {
            return &adjusted[i];
        }
    }
    return NULL;
}

static void log_once(float original_max, float minimum, float open_distance, float start,
                     float new_max) {
    for (unsigned i = 0; i < logged_count; ++i) {
        if (logged[i] == original_max) {
            return;
        }
    }
    if (logged_count < ARRAY_COUNT(logged)) {
        logged[logged_count++] = original_max;
    }
    log_append_value("MAP ZOOM: pause map opened; game opening distance (m) ", (int)open_distance);
    log_append_value("MAP ZOOM: new opening distance (m) ", (int)start);
    log_append_value("MAP ZOOM: max zoom-out (m), game ", (int)original_max);
    log_append_value("MAP ZOOM: max zoom-out (m), now ", (int)new_max);
    log_append_value("MAP ZOOM: closest zoom-in (m), unchanged ", (int)minimum);
}

static void adjust(unsigned char *controller) {
    Adjusted *previous = find_adjusted(controller);
    if (previous && *field(controller, MAX_DISTANCE) == previous->max_distance) {
        *field(controller, INITIAL_ZOOM) = previous->initial_zoom;
        return;
    }
    float minimum = *field(controller, MIN_DISTANCE);
    float maximum = *field(controller, MAX_DISTANCE);
    float min_speed = *field(controller, MIN_PAN_SPEED);
    float max_speed = *field(controller, MAX_PAN_SPEED);
    float min_zoom = *field(controller, MIN_ZOOM);
    float max_zoom = *field(controller, MAX_ZOOM);
    float zoom_speed = *field(controller, ZOOM_SPEED);
    float initial = *field(controller, INITIAL_ZOOM);
    float far_plane = *field(controller, FAR_PLANE);
    if (!valid_number(minimum) || !valid_number(maximum) || !valid_number(min_speed) ||
        !valid_number(max_speed) || !valid_number(min_zoom) || !valid_number(max_zoom) ||
        !valid_number(zoom_speed) || !valid_number(initial) || !valid_number(far_plane) ||
        minimum <= 0.0f || maximum <= minimum || far_plane <= maximum || zoom_speed <= 0.0f ||
        initial < 0.0f || initial > 1.0f) {
        if (!warned_values) {
            warned_values = 1;
            log_append("MAP ZOOM: map view values out of the expected range; game zoom kept.\r\n");
        }
        return;
    }
    if (min_zoom != 0.0f || max_zoom != 1.0f) {
        // A script narrowed the zoom range in t; leave the distances it meant.
        if (!warned_constrained) {
            warned_constrained = 1;
            log_append("MAP ZOOM: zoom range constrained by the game; game zoom kept.\r\n");
        }
        return;
    }
    int island = maximum < ISLAND_MAX_DISTANCE;
    int start_wanted = island ? island_start_percent : start_percent;
    int max_wanted = island ? island_max_percent : max_percent;
    if (start_wanted == 100 && max_wanted == 100) {
        return;
    }
    float open_distance = minimum + (maximum - minimum) * initial;
    float start = open_distance * (float)start_wanted / 100.0f;
    float new_max = maximum * (float)max_wanted / 100.0f;
    if (new_max < start) {
        new_max = start;
    }
    // Keep the map plane well inside the camera's far plane.
    float limit = far_plane * 0.9f;
    if (new_max > limit) {
        new_max = limit;
    }
    if (new_max < maximum) {
        new_max = maximum;
    }
    if (start > new_max) {
        start = new_max;
    }
    if (start < minimum) {
        start = minimum;
    }
    float ratio = (new_max - minimum) / (maximum - minimum);
    float start_zoom = (start - minimum) / (new_max - minimum);
    *field(controller, MAX_DISTANCE) = new_max;
    *field(controller, MAX_PAN_SPEED) = min_speed + (max_speed - min_speed) * ratio;
    *field(controller, ZOOM_SPEED) = zoom_speed / ratio;
    *field(controller, INITIAL_ZOOM) = start_zoom;
    Adjusted *entry = previous ? previous : &adjusted[adjusted_next++ % ARRAY_COUNT(adjusted)];
    entry->controller = controller;
    entry->max_distance = new_max;
    entry->initial_zoom = start_zoom;
    log_once(maximum, minimum, open_distance, start, new_max);
}

static void reset_view_hook(void *controller, const void *centre) {
    void *table = NULL;
    if (controller && memory_read_pointer(controller, &table) &&
        table == image + CONTROLLER_VTABLE) {
        adjust((unsigned char *)controller);
    }
    reset_view(controller, centre);
}

static int write_code(unsigned rva, const void *data, size_t size) {
    DWORD previous, ignored;
    if (!VirtualProtect(image + rva, size, PAGE_EXECUTE_READWRITE, &previous)) {
        return 0;
    }
    memory_copy(image + rva, data, size);
    VirtualProtect(image + rva, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), image + rva, size);
    return 1;
}

void map_zoom_install(unsigned char *game_image) {
    image = game_image;
    if (start_percent == 100 && max_percent == 100 && island_start_percent == 100 &&
        island_max_percent == 100) {
        log_append("MAP ZOOM: all map zoom percentages are 100; game map zoom kept.\r\n");
        return;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(expected); ++i) {
        unsigned char actual[sizeof(expected[i].bytes)];
        if (!memory_read(image + expected[i].rva, actual, expected[i].size) ||
            !memory_equal(actual, expected[i].bytes, expected[i].size)) {
            log_append_value("MAP ZOOM: game code did not match; map zoom not changed. RVA ",
                             (int)expected[i].rva);
            return;
        }
    }
    unsigned char cave[12];
    if (!memory_read(image + CAVE, cave, sizeof(cave))) {
        return;
    }
    for (unsigned i = 0; i < sizeof(cave); ++i) {
        if (cave[i] != 0xcc) {
            log_append("MAP ZOOM: code cave is in use; map zoom not changed.\r\n");
            return;
        }
    }
    reset_view = (ResetView)(image + RESET_VIEW);
    unsigned char thunk[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t target = (uintptr_t)reset_view_hook;
    memory_copy(thunk + 2, &target, sizeof(target));
    int32_t displacement = (int32_t)(CAVE - (RESET_VIEW_CALL + 5));
    unsigned char call[5] = {0xe8, 0, 0, 0, 0};
    memory_copy(call + 1, &displacement, 4);
    if (!write_code(CAVE, thunk, sizeof(thunk)) ||
        !write_code(RESET_VIEW_CALL, call, sizeof(call))) {
        log_append("MAP ZOOM: game code could not be written; map zoom not changed.\r\n");
        return;
    }
    log_append_value("MAP ZOOM: installed; pause map opens at percent of the game distance ",
                     start_percent);
    log_append_value("MAP ZOOM: max zoom-out percent of the game's ", max_percent);
    log_append_value("MAP ZOOM: Storm Island opens at percent ", island_start_percent);
    log_append_value("MAP ZOOM: Storm Island max zoom-out percent ", island_max_percent);
}
