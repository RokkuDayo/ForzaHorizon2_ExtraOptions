#include "features.h"
#include "support.h"
#include <xmmintrin.h>

#define NATIVE_DENSITY 295u
#define SAMPLE_DENSITY_RVA 0x2e834e4u

static const uint32_t native_density = NATIVE_DENSITY;
static uint32_t default_density = NATIVE_DENSITY;
static unsigned char *prepared_image;
static uint32_t selected_density = NATIVE_DENSITY;
static uint32_t applied_density = NATIVE_DENSITY;
static unsigned selected_samples = 129;
#ifdef FH2_AZIMUTH
static const uint32_t launcher_densities[] = {587u, 1170u};
#endif

static const unsigned char *const accepted_densities[] = {
    (const unsigned char *)&native_density,        (const unsigned char *)&selected_density,
    (const unsigned char *)&applied_density,
#ifdef FH2_AZIMUTH
    (const unsigned char *)&launcher_densities[0], (const unsigned char *)&launcher_densities[1],
#endif
};

static unsigned grid_sample_count(unsigned density) {
    float step = 2.0f / _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss((float)density)));
    unsigned count = 0;
    for (float y = -1.0f; y < 1.0f; y += step) {
        float y_squared = y * y;
        for (float x = -1.0f; x < 1.0f; x += step) {
            float radius = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x * x + y_squared)));
            if (radius < 0.75f) {
                ++count;
            }
        }
    }
    return count;
}

PatchDefinition photo_sampling_patch = {
    SAMPLE_DENSITY_RVA,
    0,
    sizeof(native_density),
    GROUP_PHOTO_SAMPLING,
    "photo_mode_sample_density",
    (const unsigned char *)&default_density,
    (const unsigned char *)&selected_density,
    (const unsigned char *)&selected_density,
    accepted_densities,
    ARRAY_COUNT(accepted_densities),
};

void photo_sampling_prepare_patch(unsigned char *game_image, const ModOptions *options) {
    photo_capture_prepare_hooks(game_image);
    if (prepared_image != game_image) {
        default_density = NATIVE_DENSITY;
#ifdef FH2_AZIMUTH
        uint32_t loaded_density;
        memory_copy(&loaded_density, game_image + SAMPLE_DENSITY_RVA, sizeof(loaded_density));
        for (unsigned i = 0; i < ARRAY_COUNT(launcher_densities); ++i) {
            if (loaded_density == launcher_densities[i]) {
                default_density = loaded_density;
                break;
            }
        }
#endif
        applied_density = default_density;
        prepared_image = game_image;
    }
    selected_density = options->photo_mode_samples ? NATIVE_DENSITY : default_density;
    selected_samples = grid_sample_count(selected_density);
    unsigned target = (unsigned)options->photo_mode_samples;
    if (!target || target == 129) {
        return;
    }
    unsigned center = (unsigned)((float)target * 2.263537f + 0.5f);
    unsigned smallest_error = ~0u;
    unsigned first = center > 16 ? center - 16 : 1;
    for (unsigned density = first; density <= center + 16; ++density) {
        unsigned samples = grid_sample_count(density);
        unsigned error = samples > target ? samples - target : target - samples;
        if (error < smallest_error) {
            selected_density = density;
            selected_samples = samples;
            smallest_error = error;
        }
        if (!error) {
            break;
        }
    }
}

void photo_sampling_commit_options(void) {
    applied_density = selected_density;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fh2TestPhotoSampleCount(void) {
    return selected_samples;
}
#endif

// PhotoModeScene states 3..6 are PreSave, Watermark and the capture/save wait.
// UI invalidation (including Save's watermark transition) must not replace the
// full photo with the passive one-sample/five-frame preview. The explicit full
// capture request at DD0ACF remains native, as do effect and camera restarts.
static int capture_is_frozen(const unsigned char *page) {
    unsigned state = *(const unsigned *)(page + 0x1194);
    return state >= 3 && state <= 6;
}

static void restart_photo(unsigned char *page, int preview) {
    if (capture_is_frozen(page)) {
        return;
    }
    void *manager=NULL;
    if(!memory_read_pointer(prepared_image+0x34aba60,&manager))return;
    void *renderer = ((void *(*)(void *))(prepared_image + 0x180a1b0))(manager);
    if(!renderer)return;
    unsigned count = 1;
    if (!preview) {
        void *sampling = ((void *(*)(void))(prepared_image + 0x11923e0))();
        count = ((unsigned (*)(void *))(prepared_image + 0x11938e0))(sampling);
    }
    ((void (*)(void *, unsigned, unsigned, unsigned char))(prepared_image + 0x34dcf0))(renderer,
                                                                                       count, 5, 0);
    page[0x11c5] = (unsigned char)preview;
}

void photo_capture_invalidate(void *page){
    if(prepared_image && page)restart_photo(page,0);
}

static void restart_photo_preview(unsigned char *page) {
    restart_photo(page, 1);
}

static void restart_photo_settled(unsigned char *page) {
    restart_photo(page, 0);
}

PatchDefinition photo_capture_hook_patches[2];
static unsigned char capture_originals[2][56], capture_replacements[2][56];
static const unsigned char *capture_accepted[2][2];

void photo_capture_prepare_hooks(unsigned char *base) {
    static const unsigned char original0[] = {
        0x48, 0x8b, 0x0d, 0x48, 0xc4, 0x6d, 0x02, 0xe8, 0x93, 0xab, 0xa3, 0x00, 0x45,
        0x33, 0xc9, 0x41, 0x8d, 0x51, 0x01, 0x45, 0x8d, 0x41, 0x05, 0x48, 0x8b, 0xc8,
        0xe8, 0xc0, 0xe6, 0x57, 0xff, 0xc6, 0x87, 0xc5, 0x11, 0x00, 0x00, 0x01};
    static const unsigned char original1[] = {
        0x48, 0x8b, 0x0d, 0xf8, 0xc3, 0x6d, 0x02, 0xe8, 0x43, 0xab, 0xa3, 0x00, 0x48, 0x8b,
        0xd8, 0xe8, 0x6b, 0x2d, 0x3c, 0x00, 0x48, 0x8b, 0xc8, 0xe8, 0x63, 0x42, 0x3c, 0x00,
        0x45, 0x33, 0xc9, 0x48, 0x8b, 0xcb, 0x45, 0x8d, 0x41, 0x05, 0x48, 0x8b, 0xd0, 0xe8,
        0x61, 0xe6, 0x57, 0xff, 0x48, 0x8b, 0xcf, 0xc6, 0x87, 0xc5, 0x11, 0x00, 0x00, 0x00};
    static const unsigned addresses[] = {0xdcf611, 0xdcf661};
    static const unsigned lengths[] = {sizeof(original0), sizeof(original1)};
    static const unsigned char *const native[] = {original0, original1};
    static const uintptr_t callbacks[] = {(uintptr_t)restart_photo_preview,
                                          (uintptr_t)restart_photo_settled};
    for (unsigned i = 0; i < 2; ++i) {
        unsigned length = lengths[i];
        memory_copy(capture_originals[i], native[i], length);
        for (unsigned j = 0; j < length; ++j) {
            capture_replacements[i][j] = 0x90;
        }
        // RDI is the native PhotoModeScene; preserve the surrounding update's
        // prologue, stack alignment, registers and continuation/epilogue.
        static const unsigned char call[] = {0x48, 0x8b, 0xcf, 0x48, 0xb8, 0,    0,   0,
                                             0,    0,    0,    0,    0,    0xff, 0xd0};
        memory_copy(capture_replacements[i], call, sizeof(call));
        memory_copy(capture_replacements[i] + 5, &callbacks[i], 8);
        if (i == 1) {
            // This branch tail-calls the base scene update after its epilogue.
            // The native block explicitly leaves RCX=RDI for that call.
            static const unsigned char restore_this[] = {0x48, 0x8b, 0xcf};
            memory_copy(capture_replacements[i] + length - 3, restore_this, 3);
        }
        capture_accepted[i][0] = capture_originals[i];
        capture_accepted[i][1] = capture_replacements[i];
        photo_capture_hook_patches[i] = (PatchDefinition){addresses[i],
                                                          0,
                                                          length,
                                                          GROUP_VIDEO_OPTIONS,
                                                          "photo_capture_preserve_accumulation",
                                                          capture_originals[i],
                                                          capture_replacements[i],
                                                          capture_replacements[i],
                                                          capture_accepted[i],
                                                          2};
    }
    (void)base;
}
