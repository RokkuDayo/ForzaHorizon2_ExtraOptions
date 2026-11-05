#include "features.h"
#include "support.h"
#include "wheel_variants.h"

#define QUERY_CAPACITY 8192
#define QUERY_CALLBACK_SLOT 0xecf5d8

typedef void (*LoadQuery)(void *, const char *, void *);
typedef void *(*Allocate)(size_t);
typedef void (*Free)(void *);

typedef struct {
    char storage[16];
    size_t length, capacity;
} GameString;

typedef GameString *(*StringFunction)(GameString *, GameString *, void *);
typedef void (*RegisterStringFunction)(void *, const char *, StringFunction, void *);
typedef void *(*FindFunction)(void *, const char *, int, unsigned char);
typedef unsigned short (*LocalizedTable)(void *, unsigned);
typedef const wchar_t *(*LocalizedText)(void *, unsigned short, unsigned);
typedef GameString *(*AssignText)(GameString *, const char *, size_t);

typedef struct {
    size_t offset, removed_size;
    const char *replacement;
} QueryEdit;

typedef struct {
    size_t source_size;
    unsigned count;
    int stock_names;
    QueryEdit edits[8];
} QueryPlan;

static LoadQuery original_load;
static Allocate allocate;
static Free free_query;
static unsigned char *game;
static unsigned char callback_pointer[8];
static const unsigned char unused_slot[8] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_slot[] = {unused_slot, callback_pointer};

static const char category_prefix[] = "SELECT -1 AS id, WheelCategories.Id AS Category,";
static const char category_source[] =
    " FROM WheelCategories LEFT JOIN List_CarMake ON List_CarMake.Id = ";
static const char category_order[] = " ORDER BY DisplayOrder";
static const char extra_categories[] =
    " UNION ALL SELECT -1, 5, 0, 5, (SELECT Name FROM UpgradeTypes WHERE "
    "PartEnum = 43), "
    "Description, 0, IconPath, ImagePath, '0', '0', '0' FROM WheelCategories "
    "WHERE Id = 2"
    " UNION ALL SELECT -1, 6, 0, 6, Name, Description, 0, IconPath, ImagePath, "
    "'0', '0', '0' "
    "FROM WheelCategories WHERE Id = 1";
static const char rims_prefix[] = "SELECT PartTable.PartId AS id, PartTable.IsStock,";
static const char rims_filter[] = "AND 5 = WheelCategories.Id JOIN UpgradeTypes";
static const char all_rims_filter[] = "AND WheelCategories.IsStock = 0 JOIN UpgradeTypes";
static const char rims_order[] = " ORDER BY WheelAnnotations.DisplayOrder";
static const char all_rims_order[] =
    " ORDER BY WheelCategories.DisplayOrder, WheelAnnotations.DisplayOrder";
static const char stock_parts[] =
    " FROM Upgrades JOIN (SELECT 43 AS PartEnum, ID AS PartId, 1 AS IsException, "
    "0 AS IsStock, 1 AS Level, PartManufacturerID AS ManufacturerId, Price "
    "FROM List_Wheels WHERE IsStock = 1 AND MediaName COLLATE NOCASE NOT IN "
    "('NUL_Car_00', 'PG_BUS_14', 'PG_BoxTruck_14') AND (EXISTS "
    "(SELECT 1 FROM Data_Car WHERE StockWheelID = List_Wheels.ID) OR ID = " FF_WHEEL_ID_TEXT
    ")) AS PartTable";
static const char stock_join_source[] =
    "JOIN WheelAnnotations ON WheelAnnotations.WheelId = List_Wheels.Id JOIN "
    "WheelCategories "
    "ON WheelCategories.Id = WheelAnnotations.CategoryId AND 6 = "
    "WheelCategories.Id ";
static const char stock_join[] =
    "LEFT JOIN Data_Car AS StockCar ON StockCar.ID = CASE WHEN List_Wheels.ID = " FF_WHEEL_ID_TEXT
    " THEN " FF_CAR_ID_TEXT " ELSE "
    "COALESCE((SELECT MIN(ID) FROM Data_Car WHERE StockWheelID = "
    "List_Wheels.ID "
    "AND MediaName COLLATE NOCASE = "
    "FH2_STOCK_RIM_DONOR(List_Wheels.MediaName)), "
    "(SELECT MIN(ID) FROM Data_Car WHERE StockWheelID = List_Wheels.ID)) END "
    "LEFT JOIN List_CarMake AS StockMake ON StockMake.ID = StockCar.MakeID ";
static const char manufacturer_source[] =
    "CASE WHEN PartTable.IsStock = 1 THEN List_CarMake.DisplayName ELSE "
    "List_PartManufacturer.PartManufacturer END AS ManufacturerName";
static const char name_source[] =
    "CASE WHEN PartTable.IsStock = 1 THEN List_CarMake.DisplayName ELSE "
    "List_Wheels.DisplayName END AS Name";
static const char logo_source[] =
    "CASE WHEN List_PartManufacturer.LogoPath IS NULL OR "
    "List_PartManufacturer.LogoPath = \"\" "
    "THEN List_CarMake.IconPath ELSE "
    "'GAME:\\Media\\UI\\Textures\\Data_Bound\\Aftermarket_Logos\\' "
    "|| List_PartManufacturer.LogoPath || '.swatchbin' END AS LogoPath";
static const char icon_source[] = "CASE WHEN PartTable.IsStock = 1 THEN List_CarMake.IconPath ELSE "
                                  "'GAME:\\Media\\UI\\Textures\\Data_Bound\\Upgrade_Parts\\Tire_"
                                  "Rims\\Licensed_Rims\\' "
                                  "|| List_Wheels.IconPath || '.swatchbin' END AS IconPath";
static const char stock_logo[] =
    "COALESCE(StockMake.IconPath, (SELECT IconPath FROM WheelCategories WHERE "
    "ID = 1)) AS LogoPath";
static const char stock_icon[] =
    "COALESCE(StockMake.IconPath, (SELECT IconPath FROM WheelCategories WHERE "
    "ID = 1)) AS IconPath";
static const char stock_order[] =
    " ORDER BY StockCar.MakeID, StockCar.Year, StockCar.ID, List_Wheels.ID";

static GameString *resolve_stock_rim_name(GameString *out, const GameString *input) {
    *out = (GameString){.capacity = 15};
    const char *text = input->capacity < 16 ? input->storage : *(const char *const *)input;
    size_t length = input->length;
    AssignText assign = (AssignText)(game + 0x22c300);
    if (length < 3 || text[0] != '_' || text[1] != '&') {
        return assign(out, text, length);
    }
    unsigned key = 0;
    for (size_t i = 2; i < length; ++i) {
        unsigned digit = (unsigned char)text[i] - '0';
        if (digit > 9 || key > (0xffffffffu - digit) / 10) {
            return out;
        }
        key = key * 10 + digit;
    }
    void *manager;
    if (!memory_read_pointer(game + 0x3662990, &manager)) {
        return out;
    }
    unsigned short table = ((LocalizedTable)(game + 0x123d830))(manager, key);
    const wchar_t *localized = ((LocalizedText)(game + 0x123d780))(manager, table, key & 0xffff);
    if (!localized || !*localized) {
        return out;
    }
    char utf8[1024];
    int size = WideCharToMultiByte(CP_UTF8, 0, localized, -1, utf8, sizeof(utf8), NULL, NULL);
    return size > 0 ? assign(out, utf8, (size_t)size - 1) : out;
}

static GameString *stock_rim_name(GameString *out, GameString *input, void *context) {
    (void)context;
    resolve_stock_rim_name(out, input);
    if (input->capacity >= 16) {
        free_query(*(void **)input);
    }
    return out;
}

static GameString *stock_rim_donor(GameString *out, GameString *input, void *context) {
    (void)context;
    const char *text = input->capacity < 16 ? input->storage : *(const char *const *)input;
    const char *donor = stock_wheel_donor_name(text, input->length);
    *out = (GameString){.capacity = 15};
    ((AssignText)(game + 0x22c300))(out, donor ? donor : text,
                                    donor ? text_length(donor) : input->length);
    if (input->capacity >= 16) {
        free_query(*(void **)input);
    }
    return out;
}

static int prepare_stock_names(void *database) {
    void *object;
    if (!memory_read_pointer(database, &object)) {
        return 0;
    }
    static const char *const names[] = {"FH2_STOCK_RIM_NAME", "FH2_STOCK_RIM_DONOR"};
    const StringFunction callbacks[] = {stock_rim_name, stock_rim_donor};
    FindFunction find = (FindFunction)(game + 0x1090460);
    for (unsigned i = 0; i < ARRAY_COUNT(names); ++i) {
        if (!find(object, names[i], 1, 0)) {
            ((RegisterStringFunction)(game + 0x1093440))(object, names[i], callbacks[i], NULL);
        }
        if (!find(object, names[i], 1, 0)) {
            return 0;
        }
    }
    return 1;
}

static int add_edit(QueryPlan *plan, size_t offset, size_t removed, const char *replacement) {
    if (plan->count == ARRAY_COUNT(plan->edits) || offset > plan->source_size ||
        removed > plan->source_size - offset) {
        return 0;
    }
    unsigned index = plan->count;
    while (index && plan->edits[index - 1].offset > offset) {
        plan->edits[index] = plan->edits[index - 1];
        --index;
    }
    if ((index && plan->edits[index - 1].offset + plan->edits[index - 1].removed_size > offset) ||
        (index < plan->count && offset + removed > plan->edits[index].offset)) {
        return 0;
    }
    plan->edits[index] = (QueryEdit){offset, removed, replacement};
    ++plan->count;
    return 1;
}

static int replace_fragment(const char *query, QueryPlan *plan, const char *source,
                            const char *replacement) {
    const char *found = text_find_unique(query, plan->source_size, source);
    return found && add_edit(plan, (size_t)(found - query), text_length(source), replacement);
}

static int plan_stock_query(const char *query, QueryPlan *plan) {
    const char *source = text_find_unique(query, plan->source_size, " FROM Upgrades JOIN ");
    const char *table_end = text_find_unique(query, plan->source_size, " AS PartTable ON ");
    if (!source || !table_end || table_end <= source ||
        !text_find(source, (size_t)(table_end - source), "View_UpgradeParts")) {
        return 0;
    }
    size_t source_size = (size_t)(table_end - source) + text_length(" AS PartTable");
    plan->stock_names = 1;
    return add_edit(plan, (size_t)(source - query), source_size, stock_parts) &&
           replace_fragment(query, plan, stock_join_source, stock_join) &&
           replace_fragment(query, plan, manufacturer_source,
                            "COALESCE(NULLIF(FH2_STOCK_RIM_NAME(COALESCE("
                            "StockMake.DisplayName, '')), ''), "
                            "StockCar.MediaName, List_Wheels.MediaName) AS "
                            "ManufacturerName") &&
           replace_fragment(query, plan, name_source,
                            "COALESCE(NULLIF(StockCar.DisplayName, ''), "
                            "StockCar.MediaName, List_Wheels.MediaName) AS Name") &&
           replace_fragment(query, plan, logo_source, stock_logo) &&
           replace_fragment(query, plan, icon_source, stock_icon) &&
           replace_fragment(query, plan, rims_order, stock_order) &&
           replace_fragment(query, plan, "0 UseDarkBg", "1 UseDarkBg");
}

static int plan_query(const char *query, QueryPlan *plan) {
    if (!query) {
        return 0;
    }
    size_t length = 0;
    while (length < QUERY_CAPACITY && query[length]) {
        ++length;
    }
    if (length == QUERY_CAPACITY) {
        return 0;
    }
    *plan = (QueryPlan){.source_size = length};
    size_t prefix = sizeof(category_prefix) - 1;
    size_t order = sizeof(category_order) - 1;
    if (length >= prefix + order && memory_equal(query, category_prefix, prefix) &&
        memory_equal(query + length - order, category_order, order) &&
        text_find(query, length, category_source) && !text_find(query, length, " UNION ")) {
        return add_edit(plan, length - order, 0, extra_categories);
    }
    prefix = sizeof(rims_prefix) - 1;
    order = sizeof(rims_order) - 1;
    if (length < prefix + order || !memory_equal(query, rims_prefix, prefix) ||
        !memory_equal(query + length - order, rims_order, order)) {
        return 0;
    }
    if (text_find(query, length - order, rims_filter)) {
        return replace_fragment(query, plan, rims_filter, all_rims_filter) &&
               replace_fragment(query, plan, rims_order, all_rims_order);
    }
    return text_find(query, length - order, stock_join_source) && plan_stock_query(query, plan);
}

static size_t edit_query(const char *query, const QueryPlan *plan, char *output, size_t capacity) {
    size_t length = plan->source_size;
    for (unsigned index = 0; index < plan->count; ++index) {
        length -= plan->edits[index].removed_size;
        length += text_length(plan->edits[index].replacement);
    }
    if (length >= capacity) {
        return 0;
    }
    size_t read = 0, written = 0;
    for (unsigned index = 0; index < plan->count; ++index) {
        const QueryEdit *edit = &plan->edits[index];
        size_t unchanged = edit->offset - read;
        memory_copy(output + written, query + read, unchanged);
        written += unchanged;
        size_t replacement = text_length(edit->replacement);
        memory_copy(output + written, edit->replacement, replacement);
        written += replacement;
        read = edit->offset + edit->removed_size;
    }
    memory_copy(output + written, query + read, plan->source_size - read);
    output[length] = 0;
    return length;
}

static void load_rim_query(void *collection, const char *query, void *database) {
    QueryPlan edit;
    if (!plan_query(query, &edit)) {
        original_load(collection, query, database);
        return;
    }
    char *combined = allocate(QUERY_CAPACITY);
    if (!combined) {
        original_load(collection, query, database);
        return;
    }
    int changed = edit_query(query, &edit, combined, QUERY_CAPACITY) != 0;
    if (changed && edit.stock_names && !prepare_stock_names(database)) {
        changed = 0;
    }
    // The native loader copies the SQL before preparing the statement.
    original_load(collection, changed ? combined : query, database);
    free_query(combined);
}

static void **selected_stock_rim_name(void *page, void **output) {
    void *original = *(void **)((unsigned char *)page + 0x4f0);
    if (*(int *)((unsigned char *)page + 0x500) == 6 && original) {
        const wchar_t *text = *(const wchar_t **)((unsigned char *)original + 0x28);
        int length = *(int *)((unsigned char *)original + 0x30);
        unsigned key = 0;
        int valid = text && length >= 3 && length <= 12 && text[0] == L'_' && text[1] == L'&';
        for (int i = 2; valid && i < length; ++i) {
            unsigned digit = text[i] - L'0';
            valid = digit <= 9 && key <= (0xffffffffu - digit) / 10;
            if (valid) {
                key = key * 10 + digit;
            }
        }
        void *manager;
        if (valid && memory_read_pointer(game + 0x3662990, &manager)) {
            unsigned short table = ((LocalizedTable)(game + 0x123d830))(manager, key);
            const wchar_t *name = ((LocalizedText)(game + 0x123d780))(manager, table, key & 0xffff);
            if (name && *name) {
                return ((void **(*)(void **, const wchar_t *))(game + 0x13fe8e0))(output, name);
            }
        }
    }
    return ((void **(*)(void *, void **))(game + 0xed7d90))(page, output);
}

typedef void **(*PartName)(void *, void **, int);
static unsigned char original_part_name[8], replacement_part_name[8];
static const unsigned char *const accepted_part_name[] = {original_part_name,
                                                          replacement_part_name};
PatchDefinition rim_part_name_hook_patches[2] = {
    {0x2d826a0, 0, 8, GROUP_RIMS, "stock_rim_part_name_everyman", original_part_name,
     replacement_part_name, replacement_part_name, accepted_part_name, 2},
    {0x2e5ef20, 0, 8, GROUP_RIMS, "stock_rim_part_name", original_part_name, replacement_part_name,
     replacement_part_name, accepted_part_name, 2},
};

static void **stock_part_name(void *page, void **output, int index) {
    if (*(int *)((unsigned char *)page + 0x500) == 6) {
        void *collection = *(void **)((unsigned char *)page + 0x420);
        void *rows = collection ? *(void **)((unsigned char *)collection + 0xa0) : NULL;
        if (rows) {
            GameString field = {.storage = "Name", .length = 4, .capacity = 15};
            const GameString *value =
                ((const GameString *(*)(void *, int, const GameString *, int))(
                    *(void ***)rows)[0x80 / 8])(rows, index, &field, 0);
            const char *text = value->capacity < 16 ? value->storage : *(const char *const *)value;
            size_t length = value->length;
            unsigned key = 0;
            int valid = length >= 3 && length <= 12 && text[0] == '_' && text[1] == '&';
            for (size_t i = 2; valid && i < length; ++i) {
                unsigned digit = (unsigned char)text[i] - '0';
                valid = digit <= 9 && key <= (0xffffffffu - digit) / 10;
                if (valid) {
                    key = key * 10 + digit;
                }
            }
            void *manager;
            if (valid && memory_read_pointer(game + 0x3662990, &manager)) {
                unsigned short table = ((LocalizedTable)(game + 0x123d830))(manager, key);
                const wchar_t *name =
                    ((LocalizedText)(game + 0x123d780))(manager, table, key & 0xffff);
                if (name && *name) {
                    return ((void **(*)(void **, unsigned))(game + 0x123a4d0))(output, key);
                }
            }
        }
    }
    return ((PartName)(game + 0xeb70b0))(page, output, index);
}

static unsigned char rim_name_thunk[12];
static const unsigned char unused_name_thunk[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                                    0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_name_thunk[] = {unused_name_thunk, rim_name_thunk};
PatchDefinition rim_name_hook_patch = {0xeb0274,
                                       0,
                                       12,
                                       GROUP_RIMS,
                                       "stock_rim_localized_name_thunk",
                                       unused_name_thunk,
                                       rim_name_thunk,
                                       rim_name_thunk,
                                       accepted_name_thunk,
                                       2};

PatchDefinition rims_hook_patch = {QUERY_CALLBACK_SLOT,
                                   0,
                                   8,
                                   GROUP_RIMS,
                                   "all_rims_query_callback",
                                   unused_slot,
                                   callback_pointer,
                                   callback_pointer,
                                   accepted_slot,
                                   2};

void rims_prepare_hook(unsigned char *image) {
    game = image;
    const unsigned char jump[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t name_callback = (uintptr_t)selected_stock_rim_name;
    memory_copy(rim_name_thunk, jump, sizeof(jump));
    memory_copy(rim_name_thunk + 2, &name_callback, 8);
    uintptr_t native_name = (uintptr_t)(image + 0xeb70b0);
    uintptr_t part_name = (uintptr_t)stock_part_name;
    memory_copy(original_part_name, &native_name, 8);
    memory_copy(replacement_part_name, &part_name, 8);
    stock_wheels_prepare_hooks(image);
    wheel_variants_prepare_hook(image);
    original_load = (LoadQuery)(image + 0x140c3f0);
    allocate = (Allocate)(image + 0x3229c0);
    free_query = (Free)(image + 0x31c7b0);
    uintptr_t callback = (uintptr_t)load_rim_query;
    memory_copy(callback_pointer, &callback, sizeof(callback));
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestRimNameGetter(void) { return selected_stock_rim_name; }

__declspec(dllexport) void *Fh2TestRimsHookAddress(void) { return (void *)load_rim_query; }

__declspec(dllexport) size_t Fh2TestRimsQuery(const char *query, char *output, size_t capacity) {
    QueryPlan edit;
    return plan_query(query, &edit) ? edit_query(query, &edit, output, capacity) : 0;
}
#endif
