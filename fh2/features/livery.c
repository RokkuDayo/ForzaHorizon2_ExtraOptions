#include "features.h"
#include "patches_generated.h"
#include "support.h"

enum { LIVERY_PLAYER_TEXTURE_SIZE = 0x121f112, LIVERY_OTHER_TEXTURE_SIZE = 0x121f0da };

typedef struct {
    const PatchDefinition *patch;
    unsigned char bytes[MAX_PATCH_SIZE];
} LiveryDefault;

static unsigned char *prepared_image;
static LiveryDefault layer_defaults[9];
static unsigned default_count;

void livery_prepare_defaults(unsigned char *image) {
    if (prepared_image == image) {
        return;
    }
    prepared_image = image;
    default_count = 0;
    for (unsigned index = 0; index < FIXED_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = &fixed_patches[index];
        if (patch->group != GROUP_LIVERY && patch->group != GROUP_MSAA) {
            continue;
        }
        LiveryDefault *saved = &layer_defaults[default_count++];
        saved->patch = patch;
        memory_copy(saved->bytes, patch->original, patch->size);
        for (unsigned variant = 0; variant < patch->accepted_count; ++variant) {
            if (memory_equal(image + patch->rva, patch->accepted[variant], patch->size)) {
                memory_copy(saved->bytes, patch->accepted[variant], patch->size);
                break;
            }
        }
    }
}

const unsigned char *livery_patch_target(const PatchDefinition *patch, const ModOptions *options) {
    const unsigned char *baseline = patch->original;
    for (unsigned index = 0; index < default_count; ++index) {
        if (layer_defaults[index].patch == patch) {
            baseline = layer_defaults[index].bytes;
            break;
        }
    }
    if (patch->group == GROUP_MSAA) {
        return options->livery_msaa == 4 ? patch->standard : baseline;
    }
    if (patch->rva == LIVERY_PLAYER_TEXTURE_SIZE || patch->rva == LIVERY_OTHER_TEXTURE_SIZE) {
        return options->enable_4k_liveries ? patch->standard : baseline;
    }
    if (!options->enable_4k_liveries && !options->livery_supersampling) {
        return baseline;
    }
    return options->enable_4k_liveries && options->livery_supersampling ? patch->supersampled
                                                                        : patch->standard;
}
