#include "features.h"
#include "game_addresses.h"
#include "support.h"

typedef void (*MapUpdate)(void *, const float *, const float *, float, float);
typedef unsigned char (*MapRotates)(void *);

static unsigned char *image;
static int follow_camera;

void minimap_set_follow_camera(int enabled) {
    follow_camera = enabled != 0;
}

static MapUpdate original_update;
static MapRotates rotates;
static unsigned char callback_pointer[8];
static const unsigned char unused_slot[8] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_slot[] = {unused_slot, callback_pointer};

static int has_vtable(void *object, unsigned rva) {
    void *vtable;
    return memory_read_pointer(object, &vtable) && vtable == image + rva;
}

static int camera_heading(float direction[4]) {
    void *game, *simulation, *manager, *views, *view, *camera;
    float matrix[16];
    if (!memory_read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game) ||
        !memory_read_pointer((unsigned char *)game + 0x48, &simulation) ||
        !memory_read_pointer((unsigned char *)simulation + 0x98, &manager) ||
        !memory_read_pointer((unsigned char *)manager + 0x2d0, &views) ||
        !memory_read_pointer(views, &view) ||
        (!has_vtable(view, 0x2c66188) && !has_vtable(view, 0x2e6e890)) ||
        !memory_read_pointer((unsigned char *)view + 0xc0, &camera) ||
        !has_vtable(camera, 0x2c6c840) ||
        !memory_read((unsigned char *)camera + 0x260, matrix, sizeof(matrix))) {
        return 0;
    }
    // The cached view matrix stores the look direction in its third column.
    float x = matrix[2], y = matrix[6], z = matrix[10];
    if (!float_is_finite(x) || !float_is_finite(y) || !float_is_finite(z)) {
        return 0;
    }
    float horizontal = x * x + z * z;
    float length = horizontal + y * y;
    if (!(horizontal > 0.000001f && length > 0.5f && length < 1.5f)) {
        return 0;
    }
    direction[0] = x;
    direction[1] = 0;
    direction[2] = z;
    direction[3] = 0;
    return 1;
}

static void update_minimap(void *controller, const float *position, const float *direction,
                           float speed, float zoom) {
    _Alignas(16) float heading[4];
    if (follow_camera && rotates(controller) && camera_heading(heading)) {
        direction = heading;
    }
    original_update(controller, position, direction, speed, zoom);
}

PatchDefinition minimap_hook_patch = {0xbaae68,
                                      0,
                                      8,
                                      GROUP_MINIMAP,
                                      "minimap_camera_heading_callback",
                                      unused_slot,
                                      callback_pointer,
                                      callback_pointer,
                                      accepted_slot,
                                      2};

void minimap_prepare_hook(unsigned char *game_image) {
    image = game_image;
    original_update = (MapUpdate)(image + 0xbabb90);
    rotates = (MapRotates)(image + 0xbadff0);
    uintptr_t callback = (uintptr_t)update_minimap;
    memory_copy(callback_pointer, &callback, sizeof(callback));
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestMinimapHookAddress(void) {
    return (void *)update_minimap;
}
#endif
