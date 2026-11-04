#include "display_units.h"
#include "support.h"
#include "video_settings.h"

enum { SYSTEMS = 7, DIMENSIONS = 45, POWER = 16, SPRING = 26,
       MAPPING_RVA = 0x3662998, TABLE_BYTES = 0x18a8,
       STORE_RVA = 0x123dd6a, THUNK_RVA = 0x123ab01 };

typedef struct { int dimension, unit, format, label, decimals; } UnitMapping;
_Static_assert(sizeof(UnitMapping) == 20, "Native unit mapping row");
typedef struct { void *vtable; UnitMapping rows[SYSTEMS][DIMENSIONS]; unsigned padding; } UnitTable;
_Static_assert(sizeof(UnitTable) == TABLE_BYTES, "Native unit mapping table");

static unsigned char *image;
static UnitTable *vanilla;
// Immutable snapshots: changing a selector swaps one pointer, never a partially
// updated conversion/label row. Old readers can finish using their snapshot.
static UnitTable snapshots[4][4];
static unsigned char original[2][15], replacement[2][15];
static const unsigned char *accepted[2][2];
PatchDefinition display_units_hook_patches[2];

static UnitMapping mapping(unsigned dimension, unsigned unit, unsigned format, unsigned label,
                           int decimals) {
    UnitMapping row = {(int)dimension, (int)unit, 0, 0, decimals};
    memory_copy(&row.format, image + format, 4);
    memory_copy(&row.label, image + label, 4);
    return row;
}

void display_units_refresh(void) {
    if (!image || !vanilla) return;
    unsigned power = (unsigned)video_setting_get(POWER_UNITS);
    unsigned spring = (unsigned)video_setting_get(SPRING_UNITS);
    if (power > 3 || spring > 3) return;
    UnitTable *selected = power || spring ? &snapshots[power][spring] : vanilla;
    InterlockedExchangePointer((void *volatile *)(image + MAPPING_RVA), selected);
}

static void capture_mapping(UnitTable *table) {
    vanilla = table;
    // The native constructor has finished and all dynamic localization IDs are
    // ready. Reuse native unit IDs/factors in every existing unit-system preset.
    UnitMapping power[] = {{0}, mapping(POWER, 25, 0x36628cc, 0x3662790, 0),
                          mapping(POWER, 28, 0x3662774, 0x3662850, 0),
                          mapping(POWER, 27, 0x3662900, 0x36626c0, 0)};
    UnitMapping spring[] = {{0}, mapping(SPRING, 49, 0x366271c, 0x3662778, 2),
                           mapping(SPRING, 48, 0x36626bc, 0x36626ec, 1),
                           mapping(SPRING, 50, 0x3662920, 0x36626f4, 2)};
    for (unsigned p = 0; p < 4; ++p) {
        for (unsigned s = 0; s < 4; ++s) {
            UnitTable *copy = &snapshots[p][s];
            memory_copy(copy, table, sizeof(*copy));
            for (unsigned system = 0; system < SYSTEMS; ++system) {
                if (p) copy->rows[system][POWER] = power[p];
                if (s) copy->rows[system][SPRING] = spring[s];
            }
        }
    }
    display_units_refresh();
}

void display_units_prepare_hooks(unsigned char *base) {
    if (image != base) vanilla = NULL;
    image = base;
    static const unsigned addresses[] = {THUNK_RVA, STORE_RVA};
    static const unsigned sizes[] = {15, 7};
    static const char *const names[] = {"display_units_capture_thunk", "display_units_mapping"};
    static const unsigned char store[] = {0x48, 0x89, 0x1d, 0x27, 0x4c, 0x42, 0x02};
    // The replaced MOV publishes RBX. Pass that same table to the callback;
    // all volatile registers are reloaded by the following native instructions.
    static const unsigned char thunk[] = {0x48, 0x89, 0xd9, 0x48, 0xb8,
        0,0,0,0,0,0,0,0, 0xff,0xe0};
    for (unsigned i = 0; i < 15; ++i) original[0][i] = 0xcc;
    memory_copy(replacement[0], thunk, sizeof(thunk));
    uintptr_t callback = (uintptr_t)capture_mapping;
    memory_copy(replacement[0] + 5, &callback, 8);
    memory_copy(original[1], store, sizeof(store));
    replacement[1][0] = 0xe8;
    int32_t displacement = THUNK_RVA - STORE_RVA - 5;
    memory_copy(replacement[1] + 1, &displacement, 4);
    replacement[1][5] = replacement[1][6] = 0x90;
    for (unsigned i = 0; i < 2; ++i) {
        accepted[i][0] = original[i]; accepted[i][1] = replacement[i];
        display_units_hook_patches[i] = (PatchDefinition){addresses[i], 0, sizes[i],
            GROUP_VIDEO_OPTIONS, names[i], original[i], replacement[i], replacement[i], accepted[i], 2};
    }
}
