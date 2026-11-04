#include "features.h"
#include "support.h"
#include "video_settings.h"
#include "autodrive_game.h"

// Only EntityModel::FreeroamDrivatarComponent is hooked. The race opponent
// acquisition/generation and ordinary traffic components keep their native code.
typedef struct {
    void *component;
    int minimum, maximum;
    unsigned char force;
} SuppressedPopulation;

static unsigned char *image;
static SuppressedPopulation suppressed[32];

static SuppressedPopulation *find_population(void *component, int create) {
    SuppressedPopulation *empty = NULL;
    for (unsigned i = 0; i < ARRAY_COUNT(suppressed); ++i) {
        if (suppressed[i].component == component) {
            return &suppressed[i];
        }
        if (!suppressed[i].component && !empty) {
            empty = &suppressed[i];
        }
    }
    return create ? empty : NULL;
}

static void update_population(void *component, float delta) {
    unsigned char *p = component;
    int disabled = !video_setting_get(OPEN_WORLD_DRIVATARS) && autodrive_game_is_freeroam();
    SuppressedPopulation *saved = find_population(component, disabled);
    if (disabled && saved) {
        if (!saved->component) {
            *saved = (SuppressedPopulation){component, *(int *)(p + 0xb0), *(int *)(p + 0xf0),
                                            *(unsigned char *)(p + 0x130)};
        }
        *(int *)(p + 0xb0) = 0;            // MinDrivatarsForFreeroam
        *(int *)(p + 0xf0) = 0;            // MaxDrivatarsToAcquire
        *(unsigned char *)(p + 0x130) = 0; // ForceDrivatarsDuringIE
        // The native publication routine clears the same vector<int> at +218
        // before filling it up to MaxDrivatarsToAcquire, and marks +210 ready.
        // No owned references are discarded: this vector contains integer IDs.
        *(void **)(p + 0x220) = *(void **)(p + 0x218);
        *(unsigned char *)(p + 0x210) = 1;
    } else if (saved && saved->component) {
        *(int *)(p + 0xb0) = saved->minimum;
        *(int *)(p + 0xf0) = saved->maximum;
        *(unsigned char *)(p + 0x130) = saved->force;
        *(float *)(p + 0x188) = 1e30f; // Native refresh/reacquisition on return.
        saved->component = NULL;
    }
    ((void (*)(void *, float))(image + 0x55e9a0))(component, delta);
}

static void *delete_population(void *component, unsigned flags) {
    SuppressedPopulation *saved = find_population(component, 0);
    if (saved) {
        saved->component = NULL;
    }
    return ((void *(*)(void *, unsigned))(image + 0x547e20))(component, flags);
}

PatchDefinition world_drivatars_hook_patches[2];
static unsigned char originals[2][8], replacements[2][8];
static const unsigned char *accepted[2][2];

void world_drivatars_prepare_hooks(unsigned char *base) {
    if (image != base) {
        for (unsigned i = 0; i < ARRAY_COUNT(suppressed); ++i) {
            suppressed[i].component = NULL;
        }
    }
    image = base;
    static const unsigned slots[] = {0x2c98c18, 0x2c98c00};
    static const unsigned native[] = {0x55e9a0, 0x547e20};
    static const uintptr_t callbacks[] = {(uintptr_t)update_population,
                                          (uintptr_t)delete_population};
    for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
        uintptr_t original = (uintptr_t)(base + native[i]);
        memory_copy(originals[i], &original, 8);
        memory_copy(replacements[i], &callbacks[i], 8);
        accepted[i][0] = originals[i];
        accepted[i][1] = replacements[i];
        world_drivatars_hook_patches[i] = (PatchDefinition){slots[i],
                                                            0,
                                                            8,
                                                            GROUP_VIDEO_OPTIONS,
                                                            "open_world_drivatars",
                                                            originals[i],
                                                            replacements[i],
                                                            replacements[i],
                                                            accepted[i],
                                                            2};
    }
}
