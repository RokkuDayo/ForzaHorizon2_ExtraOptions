#include "features.h"
#include "patches_generated.h"
#include "support.h"

typedef struct {
    const PatchDefinition *patch;
    unsigned char bytes[MAX_PATCH_SIZE];
} ShadowDefault;

static unsigned char *prepared_image;
static ShadowDefault layer_defaults[3];
static unsigned default_count;

void sun_shadows_prepare_defaults(unsigned char *game_image) {
    if (prepared_image == game_image) {
        return;
    }
    prepared_image = game_image;
    default_count = 0;
    for (unsigned index = 0; index < FIXED_PATCH_COUNT; ++index) {
        const PatchDefinition *patch = &fixed_patches[index];
        if (patch->group != GROUP_SUN_SHADOWS || default_count == ARRAY_COUNT(layer_defaults)) {
            continue;
        }
        ShadowDefault *saved = &layer_defaults[default_count++];
        saved->patch = patch;
        memory_copy(saved->bytes, patch->original, patch->size);
        for (unsigned variant = 0; variant < patch->accepted_count; ++variant) {
            if (memory_equal(game_image + patch->rva, patch->accepted[variant], patch->size)) {
                memory_copy(saved->bytes, patch->accepted[variant], patch->size);
                break;
            }
        }
    }
}

const unsigned char *sun_shadows_layer_default(const PatchDefinition *patch) {
    for (unsigned index = 0; index < default_count; ++index) {
        if (layer_defaults[index].patch == patch) {
            return layer_defaults[index].bytes;
        }
    }
    return patch->original;
}
