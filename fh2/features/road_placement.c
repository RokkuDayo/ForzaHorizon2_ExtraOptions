#include "features.h"
#include "game_addresses.h"
#include "logging.h"
#include "support.h"

static unsigned char *road_game_image;
static int road_enabled = 1;

void road_set_enabled(int enabled) {
    __atomic_store_n(&road_enabled, enabled != 0, __ATOMIC_RELAXED);
}

/* NearestRoad destination from the existing car, consumed by the stock
   RestoreCarPosition after loading its replacement. No asynchronous state. */
#define ROAD_STORE_SLOT_RVA 0x2d275d0u
#define ROAD_STORE_RVA 0x93ca40u
typedef void (*RoadStoreFn)(void *self);
typedef void *(*RoadStringFn)(void *parameter);
typedef void *(*RoadPlayerFn)(void *game, int index);
typedef void *(*RoadCarFn)(void *player, void *out);
typedef void *(*RoadOwnerFn)(void *component);
typedef void *(*RoadCacheFn)(void *collection, const void *key);
typedef unsigned char (*RoadQueryFn)(void *car, float *position, float *basis,
                                     unsigned char pop_history);
typedef void (*RoadReleaseFn)(void *car);
static RoadStoreFn original_road_store;
static RoadStringFn parameter_get_string;
static RoadPlayerFn game_get_local_player;
static RoadCarFn player_get_car;
static RoadCacheFn component_get_state_cache;
static RoadQueryFn car_query_nearest_road;
static unsigned char road_original_pointer[8], road_target_pointer[8];

static int road_is_garage_swap(void *self) {
    /* entity_id is inert while locator_name is empty. A locator marker
       would activate native route lookup and dereference a missing component. */
    static const char marker[] = "_fh2_car_road";
    unsigned char sso[32];
    size_t length, capacity;
    void *text;
    if (!self) {
        return 0;
    }
    void *value = parameter_get_string((unsigned char *)self + 0x28);
    if (!memory_read(value, sso, 32)) {
        return 0;
    }
    memory_copy(&length, sso + 16, 8);
    memory_copy(&capacity, sso + 24, 8);
    if (length != sizeof(marker) - 1 || capacity < length) {
        return 0;
    }
    if (capacity < 16) {
        return memory_equal(sso, marker, sizeof(marker));
    }
    memory_copy(&text, sso, 8);
    char bytes[sizeof(marker)];
    return memory_read(text, bytes, sizeof(bytes)) && memory_equal(bytes, marker, sizeof(bytes));
}

/* The native query follows the adjacent points of each road polyline to
   choose a heading. Validate precisely the pointers it will dereference. */
static int road_node_has_heading(unsigned char *node, float position[3]) {
    void *edges;
    int edge_count;
    if (!memory_read(node + 0x14, position, 12) || !memory_read(node + 0x20, &edge_count, 4) ||
        edge_count < 1 || edge_count > 64 || !memory_read_pointer(node, &edges)) {
        return 0;
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!float_is_finite(position[i])) {
            return 0;
        }
    }
    int usable = 0;
    for (int i = 0; i < edge_count; ++i) {
        unsigned char *edge;
        void *points;
        int point_index, point_count;
        unsigned char *entry = (unsigned char *)edges + (size_t)i * 16;
        if (!memory_read_pointer(entry, (void **)&edge) ||
            !memory_read(entry + 8, &point_index, 4) || !memory_read_pointer(edge, &points) ||
            !memory_read(edge + 0x10, &point_count, 4) || point_count < 1 ||
            point_count > 1000000 || point_index < 0 || point_index >= point_count) {
            return 0;
        }
        for (int side = -1; side <= 1; side += 2) {
            int next = point_index + side;
            if (next < 0 || next >= point_count) {
                continue;
            }
            unsigned char *neighbor;
            float neighbor_position[3];
            if (!memory_read((unsigned char *)points + (size_t)next * 8, &neighbor, 8)) {
                return 0;
            }
            if (!neighbor) {
                continue;
            }
            if (!memory_read(neighbor + 0x14, neighbor_position, 12)) {
                return 0;
            }
            for (unsigned j = 0; j < 3; ++j) {
                if (!float_is_finite(neighbor_position[j])) {
                    return 0;
                }
            }
            if (neighbor_position[0] != position[0] || neighbor_position[1] != position[1] ||
                neighbor_position[2] != position[2]) {
                usable = 1;
            }
        }
    }
    return usable;
}

static int road_store_nearest_destination(unsigned char *car) {
    void *vtable, *component, *owner, *collection, *bindings, *outer, *inner;
    unsigned char *cache, *node;
    uintptr_t method;
    int previous_reset_mode;
    if (!memory_read_pointer(car, &vtable) ||
        vtable != road_game_image + GAME_RVA_PLAYER_CAR_VTABLE ||
        !memory_read(car + 0x4f94, &previous_reset_mode, 4) ||
        !memory_is_writable(car + 0x4f94, 4) || !memory_read_pointer(car + 0x4f70, &bindings) ||
        !memory_read_pointer(bindings, &vtable) ||
        vtable != road_game_image + GAME_RVA_ROAD_BINDINGS_VTABLE ||
        !memory_read_pointer((unsigned char *)bindings + 0xa8, &outer) ||
        !memory_read_pointer(outer, &inner) ||
        !memory_read_pointer((unsigned char *)inner + 0x10, (void **)&node)) {
        return 0;
    }
    float road_position[3];
    if (!road_node_has_heading(node, road_position)) {
        return 0;
    }
    if (!memory_read_pointer(car + 0x4d00, &component) ||
        !memory_read_pointer(component, &vtable) ||
        !memory_read((unsigned char *)vtable + 0xc8, &method, 8) ||
        method < (uintptr_t)road_game_image || method >= (uintptr_t)road_game_image + 0x3a18000) {
        return 0;
    }
    owner = ((RoadOwnerFn)method)(component);
    if (!owner || !memory_read_pointer((unsigned char *)owner + 0xd0, &collection)) {
        return 0;
    }
    unsigned char cache_key[32] = {0};
    size_t length = 11, capacity = 15;
    memory_copy(cache_key, "PlayerState", 12);
    memory_copy(cache_key + 16, &length, 8);
    memory_copy(cache_key + 24, &capacity, 8);
    cache = component_get_state_cache(collection, cache_key);
    unsigned char facing_event;
    if (!memory_read_pointer(cache, &vtable) ||
        vtable != road_game_image + GAME_RVA_PLAYER_STATE_VTABLE ||
        !memory_read(cache + 0x30, &facing_event, 1) || facing_event ||
        !memory_is_writable(cache + 0x10, 32)) {
        return 0;
    }
    float position[4] __attribute__((aligned(16))) = {0};
    float basis[12] __attribute__((aligned(16))) = {0};
    /* The original mode and reset-history stack belong to normal gameplay.
       Only borrow mode 0 for this synchronous query, without popping history. */
    *(volatile int *)(car + 0x4f94) = 0;
    unsigned char found = car_query_nearest_road(car, position, basis, 0);
    *(volatile int *)(car + 0x4f94) = previous_reset_mode;
    if (!found) {
        return 0;
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (!float_is_finite(position[i]) || !float_is_finite(basis[8 + i])) {
            return 0;
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        float delta = position[i] - road_position[i];
        if (delta > 0.01f || delta < -0.01f) {
            return 0;
        }
    }
    float heading_length_squared =
        basis[8] * basis[8] + basis[9] * basis[9] + basis[10] * basis[10];
    if (heading_length_squared < 0.9f || heading_length_squared > 1.1f) {
        return 0;
    }
    memory_copy(cache + 0x10, position, 16);
    memory_copy(cache + 0x20, basis + 8, 16);
    return 1;
}

static void road_store_hook(void *self) {
    original_road_store(self);
    if (!__atomic_load_n(&road_enabled, __ATOMIC_RELAXED) || !road_is_garage_swap(self)) {
        return;
    }
    log_append("GARAGE: native position stored; calculating nearest road.\r\n");
    void *game, *player;
    unsigned char *car = 0;
    void *vtable;
    uintptr_t release;
    int changed = 0;
    if (memory_read_pointer(road_game_image + GAME_RVA_PLAYER_MANAGER, &game) &&
        (player = game_get_local_player(game, 0))) {
        player_get_car(player, &car);
        if (car && memory_read_pointer(car, &vtable) &&
            memory_read((unsigned char *)vtable + 0x18, &release, 8) &&
            release >= (uintptr_t)road_game_image &&
            release < (uintptr_t)road_game_image + 0x3a18000) {
            changed = road_store_nearest_destination(car);
            ((RoadReleaseFn)release)(car);
        }
    }
    log_append(changed ? "GARAGE: nearest-road position and heading stored for "
                         "loading-screen placement.\r\n"
                       : "GARAGE: nearest-road data unavailable; keeping "
                         "original car position.\r\n");
}

static const unsigned char *const road_hook_accepted[] = {road_original_pointer,
                                                          road_target_pointer};
PatchDefinition road_hook_patch = {ROAD_STORE_SLOT_RVA,
                                   0,
                                   8,
                                   GROUP_ROAD,
                                   "garage_nearest_road_store_hook",
                                   road_original_pointer,
                                   road_target_pointer,
                                   road_target_pointer,
                                   road_hook_accepted,
                                   2};

void road_prepare_hook(unsigned char *game_image) {
    road_game_image = game_image;
    uintptr_t original_callback = (uintptr_t)(game_image + ROAD_STORE_RVA),
              replacement_callback = (uintptr_t)road_store_hook;
    memory_copy(road_original_pointer, &original_callback, 8);
    memory_copy(road_target_pointer, &replacement_callback, 8);
    original_road_store = (RoadStoreFn)original_callback;
    parameter_get_string = (RoadStringFn)(game_image + GAME_RVA_PARAMETER_STRING);
    game_get_local_player = (RoadPlayerFn)(game_image + GAME_RVA_GET_LOCAL_PLAYER);
    player_get_car = (RoadCarFn)(game_image + GAME_RVA_GET_PLAYER_CAR);
    component_get_state_cache = (RoadCacheFn)(game_image + GAME_RVA_GET_STATE_CACHE);
    car_query_nearest_road = (RoadQueryFn)(game_image + GAME_RVA_NEAREST_ROAD);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestRoadHookAddress(void) {
    return (void *)road_store_hook;
}
#endif
