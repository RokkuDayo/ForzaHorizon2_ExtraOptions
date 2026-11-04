#include "features.h"
#include "support.h"

typedef struct {
    unsigned available;
    float default_position, drag_min, downforce_min, drag_max, downforce_max;
    float remaining[14];
} AeroDefinition;

typedef struct {
    char storage[16];
    size_t length, capacity;
} GameString;

typedef void (*LoadAero)(AeroDefinition *, void **, void **);
typedef void (*ReferenceAction)(void *);
typedef int (*ReadInteger)(void *, unsigned, const GameString *, int);
typedef void **(*Query)(void *, void **, const char *);
static unsigned char *image;

_Static_assert(sizeof(AeroDefinition) == 0x50, "Native aero definition");

static uintptr_t method(void *object, unsigned offset) {
    void *vtable;
    uintptr_t function;
    return memory_read_pointer(object, &vtable) &&
                   memory_read((unsigned char *)vtable + offset, &function, sizeof(function)) &&
                   function >= (uintptr_t)image + 0x1000 && function < (uintptr_t)image + 0x1dcc000
               ? function
               : 0;
}

static void release(void *object) {
    uintptr_t function = method(object, 0x18);
    if (function) {
        ((ReferenceAction)function)(object);
    }
}

static int append(char *text, size_t *length, const char *value) {
    while (*value) {
        if (*length >= 767) {
            return 0;
        }
        text[(*length)++] = *value++;
    }
    text[*length] = 0;
    return 1;
}

static int make_query(char text[768], int part_id, int rear, int matching) {
    const char *table = rear ? "List_UpgradeRearWing" : "List_UpgradeCarBodyFrontBumper";
    const char *family = rear ? "Ordinal" : "CarBodyID";
    size_t length = 0;
    if (!append(text, &length, "SELECT Race.* FROM ") || !append(text, &length, table) ||
        !append(text, &length,
                " Race JOIN List_AeroPhysics A ON "
                "A.AeroPhysicsID=Race.AeroPhysicsID ")) {
        return 0;
    }
    if (matching) {
        char digits[12];
        unsigned count = 0, value = (unsigned)part_id;
        do {
            digits[count++] = (char)('0' + value % 10);
            value /= 10;
        } while (value);
        if (!append(text, &length, "JOIN ") || !append(text, &length, table) ||
            !append(text, &length, " Current ON Current.") || !append(text, &length, family) ||
            !append(text, &length, "=Race.") || !append(text, &length, family) ||
            !append(text, &length, " WHERE Current.Id=")) {
            return 0;
        }
        while (count) {
            char digit[2] = {digits[--count], 0};
            if (!append(text, &length, digit)) {
                return 0;
            }
        }
        if (!append(text, &length, " AND ")) {
            return 0;
        }
    } else if (!append(text, &length, "WHERE ")) {
        return 0;
    }
    return append(text, &length,
                  "Race.Level=3 AND A.Downforce1>A.Downforce0 ORDER BY "
                  "Race.Sequence,Race.Id LIMIT 1");
}

static void *race_part(void *database, int id, int rear) {
    Query query = (Query)method(database, 0x40);
    if (!query) {
        return NULL;
    }
    for (int matching = 1; matching >= 0; --matching) {
        char sql[768];
        if (!make_query(sql, id, rear, matching)) {
            return NULL;
        }
        void *rows = NULL;
        query(database, &rows, sql);
        uintptr_t count = method(rows, 0x148);
        if (count && ((int (*)(void *))count)(rows) > 0) {
            return rows;
        }
        release(rows);
    }
    return NULL;
}

static int fixed_downforce(const AeroDefinition *definition) {
    return definition->available && float_is_finite(definition->downforce_min) &&
           definition->downforce_min == definition->downforce_max &&
           float_is_finite(definition->drag_min) && float_is_finite(definition->drag_max);
}

static void load_aero(AeroDefinition *output, void **part, void **database, int rear) {
    void *source = part ? *part : NULL;
    void *db = database ? *database : NULL;
    uintptr_t read_integer = method(source, 0xa0);
    uintptr_t retain = method(db, 0x10);
    static const GameString id_column = {.storage = "Id", .length = 2, .capacity = 15};
    int id = read_integer ? ((ReadInteger)read_integer)(source, 0, &id_column, -1) : -1;
    if (retain) {
        ((ReferenceAction)retain)(db);
    } else {
        db = NULL;
    }
    LoadAero original = (LoadAero)(image + 0x1061240);
    original(output, part, database);
    if (db && id > 0 && fixed_downforce(output)) {
        void *alternative = race_part(db, id, rear);
        if (alternative) {
            AeroDefinition race = {0};
            original(&race, &alternative, &db);
            db = NULL;
            float span = race.downforce_max - race.downforce_min;
            if (race.available && float_is_finite(span) && span > 0.0f &&
                float_is_finite(race.drag_min) && float_is_finite(race.drag_max)) {
                output->default_position = race.default_position;
                output->downforce_min = race.downforce_min;
                output->downforce_max = race.downforce_max;
                output->drag_min = race.drag_min;
                output->drag_max = race.drag_max;
            }
        }
    }
    release(db);
}

static void load_front(AeroDefinition *output, void **part, void **database) {
    load_aero(output, part, database, 0);
}

static void load_rear(AeroDefinition *output, void **part, void **database) {
    load_aero(output, part, database, 1);
}

static unsigned char thunks[2][12];
static const unsigned char padding[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                          0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted[2][2] = {{padding, thunks[0]}, {padding, thunks[1]}};
PatchDefinition aero_tuning_hook_patches[2];

void aero_tuning_prepare_hooks(unsigned char *game_image) {
    image = game_image;
    static const unsigned rvas[2] = {0x1061234, 0x1061064};
    static const char *const names[2] = {"front_aero_tuning_ranges", "rear_aero_tuning_ranges"};
    static const unsigned char code[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    const uintptr_t callbacks[2] = {(uintptr_t)load_front, (uintptr_t)load_rear};
    for (unsigned i = 0; i < 2; ++i) {
        memory_copy(thunks[i], code, sizeof(code));
        memory_copy(thunks[i] + 2, &callbacks[i], sizeof(callbacks[i]));
        aero_tuning_hook_patches[i] = (PatchDefinition){
            rvas[i], 0,         12,        GROUP_AERO_TUNING, names[i],
            padding, thunks[i], thunks[i], accepted[i],       ARRAY_COUNT(accepted[i])};
    }
}
