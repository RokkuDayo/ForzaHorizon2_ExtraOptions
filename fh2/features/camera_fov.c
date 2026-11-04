#include "camera_views.h"
#include "features.h"
#include "support.h"
#include "video_settings.h"

enum { FOV_SLOT = 0x70, NATIVE_CHASE_FOV = 0x1034120, NATIVE_MOUNTED_FOV = 0x10340f0 };

static const unsigned camera_vtables[] = {0x2e783a0, 0x2e78610, 0x2e78880,
                                          0x2e78120, 0x2e77ea8, 0x2e703b0};
typedef float (*GetFov)(void *);
static unsigned char *image;
static GetFov native_chase, native_mounted;
static unsigned char original_chase[8], original_mounted[8], replacement[8];
static const unsigned char *const accepted_chase[] = {original_chase, replacement};
static const unsigned char *const accepted_mounted[] = {original_mounted, replacement};

static float get_fov(void *camera) {
    void *vtable;
    if (!memory_read_pointer(camera, &vtable)) {
        return 0;
    }
    unsigned view = CAMERA_UNKNOWN;
    for (unsigned index = 0; index < ARRAY_COUNT(camera_vtables); ++index) {
        if (vtable == image + camera_vtables[index]) {
            view = index + CAMERA_CHASE;
            break;
        }
    }
    if (!view) {
        return 0;
    }
    int chase = view <= CAMERA_CHASE_FAR;
    float result = (chase ? native_chase : native_mounted)(camera);
    float degrees = camera_fov_degrees(view);
    if (degrees == camera_fov_default(view) || !float_is_finite(result)) {
        return result;
    }
    float base;
    if (chase) {
        void *settings;
        if (!memory_read_pointer((unsigned char *)camera + 0x180, &settings) ||
            !memory_read((unsigned char *)settings + 0x1c, &base, sizeof(base))) {
            return result;
        }
        if (!float_is_finite(base) || base <= 0) {
            return result;
        }
        return result * (degrees / base);
    }
    if (!memory_read((unsigned char *)camera + 0x1d0, &base, sizeof(base)) ||
        !float_is_finite(base) || base <= 0) {
        return result;
    }
    return result * (degrees * 0.017453292519943295f / base);
}

#define FOV_PATCH(table, name, original, accepted)                                                 \
    {table + FOV_SLOT, 0,           8,           GROUP_CAMERA_FOV, name,                           \
     original,         replacement, replacement, accepted,         2}
PatchDefinition camera_fov_hook_patches[6] = {
    FOV_PATCH(0x2e783a0u, "fov_follow", original_chase, accepted_chase),
    FOV_PATCH(0x2e78610u, "fov_chase", original_chase, accepted_chase),
    FOV_PATCH(0x2e78880u, "fov_far_chase", original_chase, accepted_chase),
    FOV_PATCH(0x2e78120u, "fov_driver", original_mounted, accepted_mounted),
    FOV_PATCH(0x2e77ea8u, "fov_hood", original_mounted, accepted_mounted),
    FOV_PATCH(0x2e703b0u, "fov_bumper", original_mounted, accepted_mounted),
};

void camera_fov_prepare_hooks(unsigned char *game_image) {
    image = game_image;
    native_chase = (GetFov)(image + NATIVE_CHASE_FOV);
    native_mounted = (GetFov)(image + NATIVE_MOUNTED_FOV);
    uintptr_t pointer = (uintptr_t)native_chase;
    memory_copy(original_chase, &pointer, sizeof(pointer));
    pointer = (uintptr_t)native_mounted;
    memory_copy(original_mounted, &pointer, sizeof(pointer));
    pointer = (uintptr_t)get_fov;
    memory_copy(replacement, &pointer, sizeof(pointer));
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestFovHookAddress(void) {
    return (void *)get_fov;
}
#endif
