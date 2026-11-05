#include "roof_fade.h"
#include "support.h"

typedef void *(*FadeColorGetter)(void *, float *);

static unsigned char *game_image;
static unsigned char original_callback[8], replacement_callback[8];
static const unsigned char *accepted_callbacks[] = {original_callback, replacement_callback};
static volatile LONG opacity_bits, black_frame_presented;
PatchDefinition roof_fade_hook_patch;

void roof_fade_set_opacity(float opacity) {
    LONG bits;
    memory_copy(&bits, &opacity, sizeof(bits));
    InterlockedExchange(&opacity_bits, bits);
    if (opacity < 1.0f) {
        InterlockedExchange(&black_frame_presented, 0);
    }
}

int roof_fade_black_frame_presented(void) {
    return InterlockedCompareExchange(&black_frame_presented, 0, 0) != 0;
}

static void *roof_fade_color(void *self, float *color) {
    void *result = ((FadeColorGetter)(game_image + 0x477c50))(self, color);
    LONG bits = InterlockedCompareExchange(&opacity_bits, 0, 0);
    float opacity;
    memory_copy(&opacity, &bits, sizeof(opacity));
    if (opacity > 0.0f && opacity >= color[3]) {
        color[0] = color[1] = color[2] = 0.0f;
        color[3] = opacity;
    }
    if (opacity >= 1.0f) {
        InterlockedExchange(&black_frame_presented, 1);
    }
    return result;
}

void roof_fade_prepare_hook(unsigned char *image) {
    game_image = image;
    uintptr_t original = (uintptr_t)(image + 0x477c50);
    uintptr_t replacement = (uintptr_t)roof_fade_color;
    memory_copy(original_callback, &original, 8);
    memory_copy(replacement_callback, &replacement, 8);
    roof_fade_hook_patch = (PatchDefinition){0x2c81388,
                                             0,
                                             8,
                                             GROUP_VEHICLE_CONTROLS,
                                             "roof_black_fade_color",
                                             original_callback,
                                             replacement_callback,
                                             replacement_callback,
                                             accepted_callbacks,
                                             ARRAY_COUNT(accepted_callbacks)};
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestVehicleFadeHookAddress(void) {
    return (void *)roof_fade_color;
}
#endif
