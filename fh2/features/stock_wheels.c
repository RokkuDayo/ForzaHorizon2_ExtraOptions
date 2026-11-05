#include "addon_wheels.h"
#include "features.h"
#include "stock_wheel_assets.h"
#include "support.h"

typedef struct {
    const char *data;
    uintptr_t reserved;
    size_t length, capacity;
} GameString;

typedef GameString *(*AssignText)(GameString *, const char *, size_t);
typedef GameString *(*ConcatStrings)(GameString *, const GameString *, const GameString *);
typedef GameString *(*ConcatText)(GameString *, const char *, const GameString *);
typedef GameString *(*AssignString)(GameString *, const GameString *, size_t, size_t);
typedef int (*CompareStrings)(const GameString *, const GameString *);
typedef unsigned char (*LoadWheels)(void *, const GameString *);
typedef unsigned char (*ReplaceModels)(void *, const GameString *, const GameString *);
typedef unsigned char (*FileExists)(void *, const GameString *);
typedef unsigned char (*ModelsReady)(void *);

static unsigned char *game_image;
static AssignText assign_text;
static ConcatStrings concat_strings;
static ConcatText concat_text;
static AssignString assign_string;
static CompareStrings compare_strings;
static LoadWheels original_load;
static ReplaceModels replace_models;
static unsigned char thunks[3][12];
static const unsigned char unused[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                         0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *accepted[3][2];
PatchDefinition stock_wheels_hook_patches[3];

static const char *string_data(const GameString *string) {
    return string->capacity < 16 ? (const char *)string : string->data;
}

static GameString borrowed_string(const char *text) {
    size_t length = text_length(text);
    return (GameString){text, 0, length, length < 16 ? 16 : length};
}

static char lower_ascii(char value) {
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

static int compare_name(const GameString *name, const char *candidate) {
    const char *text = string_data(name);
    size_t index = 0;
    while (index < name->length && candidate[index]) {
        int difference = lower_ascii(text[index]) - lower_ascii(candidate[index]);
        if (difference) {
            return difference;
        }
        ++index;
    }
    return index < name->length ? 1 : candidate[index] ? -1 : 0;
}

static const StockWheelAssets *find_wheel(const GameString *name) {
    size_t first = 0, end = ARRAY_COUNT(stock_wheels);
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        int comparison = compare_name(name, stock_wheels[middle].name);
        if (!comparison) {
            return &stock_wheels[middle];
        }
        if (comparison < 0) {
            end = middle;
        } else {
            first = middle + 1;
        }
    }
    return NULL;
}

static const char *donor_name(const StockWheelAssets *wheel) {
    return wheel->donor ? wheel->donor : wheel->name;
}

const char *stock_wheel_donor_name(const char *name, size_t length) {
    GameString key = {name, 0, length, length < 16 ? 16 : length};
    const StockWheelAssets *wheel = find_wheel(&key);
    return wheel ? donor_name(wheel) : addon_wheels_donor(name, length);
}

int stock_wheel_has_authored_route(const char *name, size_t length) {
    GameString key = {name, 0, length, length < 16 ? 16 : length};
    return find_wheel(&key) != NULL;
}

static int append_path(char *output, size_t capacity, size_t *length, const char *text,
                       int lowercase) {
    while (*text) {
        if (*length + 1 >= capacity) {
            return 0;
        }
        output[(*length)++] = lowercase ? lower_ascii(*text) : *text;
        ++text;
    }
    output[*length] = 0;
    return 1;
}

static int build_asset_path(char *output, size_t capacity, const StockWheelAssets *wheel,
                            const char *pattern) {
    if (!capacity) {
        return 0;
    }
    output[0] = 0;
    if (!*pattern) {
        return 1;
    }
    size_t length = 0;
    if (!append_path(output, capacity, &length, "game:\\media\\cars\\", 0)) {
        return 0;
    }
    size_t remaining = text_length(pattern);
    while (remaining) {
        const char *replacement = NULL;
        size_t consumed = 1;
        if (remaining >= 5 && memory_equal(pattern, "{car}", 5)) {
            replacement = donor_name(wheel);
            consumed = 5;
        } else if (remaining >= 7 && memory_equal(pattern, "{wheel}", 7)) {
            replacement = wheel->name;
            consumed = 7;
        }
        if (replacement) {
            if (!append_path(output, capacity, &length, replacement, 1)) {
                return 0;
            }
        } else {
            if (*pattern == '{' || length + 1 >= capacity) {
                return 0;
            }
            output[length++] = *pattern;
        }
        pattern += consumed;
        remaining -= consumed;
    }
    output[length] = 0;
    return 1;
}

static GameString *create_archive_path(GameString *output, const StockWheelAssets *wheel) {
    char path[ADDON_WHEEL_PATH_CAPACITY];
    size_t length = 0;
    if (!append_path(path, sizeof(path), &length, "game:\\media\\cars\\", 0) ||
        !append_path(path, sizeof(path), &length, donor_name(wheel), 0)) {
        return NULL;
    }
    *output = (GameString){.capacity = 15};
    return assign_text(output, path, length);
}

static GameString *stock_archive_text(GameString *output, const char *prefix,
                                      const GameString *name) {
    const StockWheelAssets *wheel = find_wheel(name);
    StockWheelAssets dynamic;
    if (!wheel) {
        const char *donor = addon_wheels_donor(string_data(name), name->length);
        if (donor) {
            dynamic = (StockWheelAssets){donor, NULL, 0, 0};
            wheel = &dynamic;
        }
    }
    if (wheel && create_archive_path(output, wheel)) {
        return output;
    }
    return concat_text(output, prefix, name);
}

static GameString *stock_archive_string(GameString *output, const GameString *prefix,
                                        const GameString *name) {
    const StockWheelAssets *wheel = find_wheel(name);
    StockWheelAssets dynamic;
    if (!wheel) {
        const char *donor = addon_wheels_donor(string_data(name), name->length);
        if (donor) {
            dynamic = (StockWheelAssets){donor, NULL, 0, 0};
            wheel = &dynamic;
        }
    }
    if (wheel && create_archive_path(output, wheel)) {
        return output;
    }
    return concat_strings(output, prefix, name);
}

static int file_exists(void *filesystem, const GameString *path) {
    return path->length && ((FileExists)(*(void ***)filesystem)[0xe8 / 8])(filesystem, path);
}

static unsigned char models_ready(void *part) {
    return ((ModelsReady)(*(void ***)part)[0x10 / 8])(part);
}

static unsigned char load_stock_wheels(void *part, const GameString *name) {
    const StockWheelAssets *wheel = find_wheel(name);
    GameString *current = (GameString *)((unsigned char *)part + 0x198);
    const char *addon_models[4], *addon_textures[4];
    int dynamic =
        !wheel && addon_wheels_paths(string_data(name), name->length, addon_models, addon_textures);
    if ((!wheel && !dynamic) || !compare_strings(current, name)) {
        return original_load(part, name);
    }
    void *filesystem = *(void **)(game_image + 0x36614f0);
    if (!filesystem) {
        return models_ready(part);
    }
    GameString models[4], textures[4];
    char model_paths[4][STOCK_WHEEL_PATH_CAPACITY];
    char texture_paths[4][STOCK_WHEEL_PATH_CAPACITY];
    const StockWheelLayout *model_layout = wheel ? &stock_model_layouts[wheel->model_layout] : NULL;
    const StockWheelLayout *texture_layout =
        wheel ? &stock_texture_layouts[wheel->texture_layout] : NULL;
    int textures_available = 1;
    for (unsigned index = 0; index < ARRAY_COUNT(models); ++index) {
        if (!dynamic && (!build_asset_path(model_paths[index], sizeof(model_paths[index]), wheel,
                                           model_layout->paths[index]) ||
                         !build_asset_path(texture_paths[index], sizeof(texture_paths[index]),
                                           wheel, texture_layout->paths[index]))) {
            return models_ready(part);
        }
        models[index] = borrowed_string(dynamic ? addon_models[index] : model_paths[index]);
        if (!file_exists(filesystem, &models[index])) {
            return models_ready(part);
        }
        textures[index] = borrowed_string(dynamic ? addon_textures[index] : texture_paths[index]);
        if (!file_exists(filesystem, &textures[index])) {
            textures_available = 0;
        }
    }
    assign_string(current, name, 0, (size_t)-1);
    *((unsigned char *)part + 0x190) = !textures_available;
    // The native replacement copies paths and owns the loaded model resources.
    return replace_models(part, models, textures);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestStockWheelPath(const char *name, unsigned kind, unsigned corner,
                                                char *output, size_t capacity) {
    GameString key = borrowed_string(name);
    const StockWheelAssets *wheel = find_wheel(&key);
    if (!wheel || kind > 1 || corner >= 4) {
        return 0;
    }
    const StockWheelLayout *layout = kind ? &stock_texture_layouts[wheel->texture_layout]
                                          : &stock_model_layouts[wheel->model_layout];
    return build_asset_path(output, capacity, wheel, layout->paths[corner]);
}
#endif

void stock_wheels_prepare_hooks(unsigned char *image) {
    game_image = image;
    addon_wheels_bind(image);
    assign_text = (AssignText)(image + 0x22c300);
    concat_strings = (ConcatStrings)(image + 0x211e10);
    concat_text = (ConcatText)(image + 0x2b3750);
    assign_string = (AssignString)(image + 0x22c1d0);
    compare_strings = (CompareStrings)(image + 0x22c7e0);
    original_load = (LoadWheels)(image + 0x11b0fb0);
    replace_models = (ReplaceModels)(image + 0x11b9da0);
    const uint32_t locations[] = {0xecd6a4, 0xecde31, 0xecfc22};
    const char *names[] = {"stock_wheel_archive_text_thunk", "stock_wheel_archive_string_thunk",
                           "stock_wheel_models_thunk"};
    const uintptr_t callbacks[] = {(uintptr_t)stock_archive_text, (uintptr_t)stock_archive_string,
                                   (uintptr_t)load_stock_wheels};
    const unsigned char jump[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    for (unsigned index = 0; index < ARRAY_COUNT(thunks); ++index) {
        memory_copy(thunks[index], jump, sizeof(jump));
        memory_copy(thunks[index] + 2, &callbacks[index], sizeof(callbacks[index]));
        accepted[index][0] = unused;
        accepted[index][1] = thunks[index];
        stock_wheels_hook_patches[index] = (PatchDefinition){
            locations[index], 0, 12, GROUP_RIMS, names[index], unused, thunks[index], thunks[index],
            accepted[index],  2};
    }
}
