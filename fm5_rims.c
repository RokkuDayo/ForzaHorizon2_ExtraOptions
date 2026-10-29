#include "fm5_patch.h"
#include "fm5_stock_wheel_assets.h"
#include "support.h"

#define QUERY_CAPACITY 8192
typedef struct { char storage[16]; size_t length, capacity; } GameString;
typedef GameString *(*AssignText)(GameString *, const char *, size_t);
typedef GameString *(*StringFunction)(GameString *, GameString *, void *);
typedef void (*LoadQuery)(void *, const char *, void *);
typedef void (*Reference)(void *);
typedef struct { size_t offset, removed_size; const char *replacement; } QueryEdit;
typedef struct { size_t source_size; unsigned count; int stock_names; QueryEdit edits[8]; } QueryPlan;

static unsigned char *game;
static LoadQuery original_load;
static void *(*allocate)(size_t);
static void (*free_query)(void *);

static const char *string_data(const GameString *value) {
    return value->capacity < 16 ? value->storage : *(const char *const *)value;
}

static GameString borrowed(const char *text) {
    GameString value = {.length = text_length(text), .capacity = 16};
    memory_copy(&value, &text, sizeof(text));
    return value;
}

static const StockWheelAssets *find_wheel(const GameString *name) {
    const char *text = string_data(name);
    size_t first = 0, end = ARRAY_COUNT(stock_wheels);
    while (first < end) {
        size_t middle = first + (end - first) / 2, i = 0;
        const char *candidate = stock_wheels[middle].name;
        int comparison = 0;
        while (i < name->length && candidate[i]) {
            unsigned char a = (unsigned char)text[i], b = (unsigned char)candidate[i];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if ((comparison = (int)a - b)) break;
            ++i;
        }
        if (!comparison) comparison = i < name->length ? 1 : candidate[i] ? -1 : 0;
        if (!comparison) return stock_wheels + middle;
        if (comparison < 0) end = middle; else first = middle + 1;
    }
    return NULL;
}

static const char *donor_name(const StockWheelAssets *wheel) {
    return wheel->donor ? wheel->donor : wheel->name;
}

static GameString *assign(GameString *out, const char *text, size_t length) {
    *out = (GameString){.capacity = 15};
    return ((AssignText)(game + 0x14a180))(out, text, length);
}

/* FM5's database callback owns its by-value input string. Return a separate
   native string and release that input using the same heap as the native adapter. */
static void release_input(GameString *input) {
    if (input->capacity >= 16) free_query(*(void **)input);
    *input = (GameString){.capacity = 15};
}

static GameString *stock_rim_donor(GameString *out, GameString *input, void *context) {
    (void)context;
    const StockWheelAssets *wheel = find_wheel(input);
    const char *donor = wheel ? donor_name(wheel) : "";
    assign(out, donor, text_length(donor));
    release_input(input);
    return out;
}

static GameString *stock_rim_name(GameString *out, GameString *input, void *context) {
    (void)context;
    const char *text = string_data(input);
    size_t length = input->length;
    if (length >= 3 && text[0] == '_' && text[1] == '&') {
        unsigned key = 0;
        int valid = 1;
        for (size_t i = 2; valid && i < length; ++i) {
            unsigned digit = (unsigned char)text[i] - '0';
            valid = digit <= 9 && key <= (0xffffffffu - digit) / 10;
            if (valid) key = key * 10 + digit;
        }
        void *manager = *(void **)(game + 0x25e7ae8);
        if (valid && manager) {
            unsigned short table = ((unsigned short (*)(void *, unsigned))(game + 0x7ceeb0))(manager, key);
            /* Same table lookup used by CLocalizedString::GetValue (7C6FF0). */
            void *strings = table == 0xffff ? NULL :
                *(void **)((unsigned char *)manager + 0x78 + (size_t)table * 8);
            const wchar_t *localized = strings ? ((const wchar_t *(*)(void *, unsigned))
                (game + 0x7cd1f0))(strings, key & 0xffff) : NULL;
            char utf8[2048];
            int bytes = localized ? WideCharToMultiByte(CP_UTF8, 0, localized, -1,
                utf8, sizeof(utf8), NULL, NULL) : 0;
            assign(out, bytes > 0 ? utf8 : "", bytes > 0 ? (size_t)bytes - 1 : 0);
        } else assign(out, "", 0);
    } else assign(out, text, length);
    release_input(input);
    return out;
}

static int prepare_stock_names(void *database) {
    void *object = *(void **)database;
    if (!object) return 0;
    typedef void (*Register)(void *, const char *, StringFunction, void *);
    Register register_string = (Register)(*(void ***)object)[0xe8 / 8];
    typedef void *(*Find)(void *, const char *, int, unsigned char);
    Find find = (Find)(game + 0x692a80);
    static const char *const names[] = {"FM5_RIM_NAME", "FM5_RIM_DONOR"};
    const StringFunction callbacks[] = {stock_rim_name, stock_rim_donor};
    /* Query this connection's native registry. Registering an existing name
       allocates an unused native wrapper; caching a connection address would
       instead risk stale state after a profile reload. */
    for (unsigned i = 0; i < ARRAY_COUNT(names); ++i) {
        if (!find(object, names[i], 1, 0))
            register_string(object, names[i], callbacks[i], NULL);
        if (!find(object, names[i], 1, 0)) return 0;
    }
    return 1;
}

/* Bounded SQL planner follows below. Only the two wheel-list call sites use it. */
static const char category_prefix[] = "SELECT -1 AS id, WheelCategories.Id AS Category,";
static const char category_source[] =
    " FROM WheelCategories LEFT JOIN List_CarMake ON List_CarMake.Id = ";
static const char category_order[] = " ORDER BY DisplayOrder";
static const char extra_categories[] =
    " UNION ALL SELECT -1, 5, 0, 5, 'All aftermarket rims', Description, "
    "IconPath, ImagePath, '0', '0', '0' FROM WheelCategories WHERE Id = 2"
    " UNION ALL SELECT -1, 6, 0, 6, 'All stock rims', Description, "
    "IconPath, ImagePath, '0', '0', '0' FROM WheelCategories WHERE Id = 1";
static const char rims_prefix[] = "SELECT PartTable.PartId AS id, PartTable.IsStock,";
static const char rims_filter[] = "AND 5 = WheelCategories.Id JOIN UpgradeTypes";
static const char all_rims_filter[] = "AND WheelCategories.IsStock = 0 JOIN UpgradeTypes";
static const char rims_order[] = " ORDER BY WheelAnnotations.DisplayOrder";
static const char all_rims_order[] =
    " ORDER BY WheelCategories.DisplayOrder, WheelAnnotations.DisplayOrder";
static const char stock_parts[] =
    " FROM Upgrades JOIN (SELECT 43 AS PartEnum, ID AS PartId, 1 AS IsException, "
    "0 AS IsStock, 1 AS Level, PartManufacturerID AS ManufacturerId, Price "
    "FROM List_Wheels WHERE IsStock = 1 AND FM5_RIM_DONOR(MediaName) != '') AS PartTable";
static const char stock_join_source[] =
    "JOIN WheelAnnotations ON WheelAnnotations.WheelId = List_Wheels.Id JOIN "
    "WheelCategories ON WheelCategories.Id = WheelAnnotations.CategoryId AND 6 = "
    "WheelCategories.Id ";
static const char stock_join[] =
    "LEFT JOIN Data_Car AS StockCar ON StockCar.ID = "
    "COALESCE((SELECT MIN(ID) FROM Data_Car WHERE StockWheelID = List_Wheels.ID "
    "AND MediaName COLLATE NOCASE = FM5_RIM_DONOR(List_Wheels.MediaName)), "
    "(SELECT MIN(ID) FROM Data_Car WHERE StockWheelID = List_Wheels.ID)) "
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
                            "COALESCE(NULLIF(FM5_RIM_NAME(COALESCE("
                            "StockMake.DisplayName, '')), ''), "
                            "StockCar.MediaName, List_Wheels.MediaName) AS "
                            "ManufacturerName") &&
           replace_fragment(query, plan, name_source,
                            "COALESCE(NULLIF(FM5_RIM_NAME(COALESCE(StockCar.DisplayName, '')), ''), "
                            "StockCar.MediaName, List_Wheels.MediaName) AS Name") &&
           replace_fragment(query, plan, logo_source, stock_logo) &&
           replace_fragment(query, plan, icon_source, stock_icon) &&
           replace_fragment(query, plan, rims_order, stock_order);
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

static int append_path(char *out, size_t *length, const char *text, int lowercase) {
    while (*text) {
        char value = *text++;
        if (*length + 1 >= STOCK_WHEEL_PATH_CAPACITY) return 0;
        if (lowercase && value >= 'A' && value <= 'Z') value += 'a' - 'A';
        out[(*length)++] = value;
    }
    out[*length] = 0;
    return 1;
}

static int asset_path(char *out, const StockWheelAssets *wheel, const char *pattern) {
    out[0] = 0;
    if (!*pattern) return 1;
    size_t length = 0;
    if (!append_path(out, &length, "game:\\media\\cars\\", 0)) return 0;
    size_t remaining = text_length(pattern);
    while (remaining) {
        const char *replacement = NULL;
        size_t consumed = 1;
        if (remaining >= 5 && memory_equal(pattern, "{car}", 5)) {
            replacement = donor_name(wheel); consumed = 5;
        } else if (remaining >= 7 && memory_equal(pattern, "{wheel}", 7)) {
            replacement = wheel->name; consumed = 7;
        }
        if (replacement) {
            if (!append_path(out, &length, replacement, 1)) return 0;
        } else {
            if (*pattern == '{' || length + 1 >= STOCK_WHEEL_PATH_CAPACITY) return 0;
            out[length++] = *pattern;
        }
        pattern += consumed; remaining -= consumed;
    }
    out[length] = 0;
    return 1;
}

static GameString *archive_path(GameString *out, const GameString *name) {
    const StockWheelAssets *wheel = find_wheel(name);
    if (!wheel) return NULL;
    char path[STOCK_WHEEL_PATH_CAPACITY];
    size_t length = 0;
    if (!append_path(path, &length, "game:\\media\\cars\\", 0) ||
        !append_path(path, &length, donor_name(wheel), 0)) return NULL;
    return assign(out, path, length);
}

static GameString *archive_text(GameString *out, const char *prefix, const GameString *name) {
    if (archive_path(out, name)) return out;
    return ((GameString *(*)(GameString *, const char *, const GameString *))
        (game + 0x1bad80))(out, prefix, name);
}

static GameString *archive_string(GameString *out, const GameString *prefix, const GameString *name) {
    if (archive_path(out, name)) return out;
    return ((GameString *(*)(GameString *, const GameString *, const GameString *))
        (game + 0x14dbd0))(out, prefix, name);
}

static unsigned char ready(void *part) {
    return ((unsigned char (*)(void *))(*(void ***)part)[8 / 8])(part);
}

static unsigned char load_stock_wheels(void *part, const GameString *name) {
    const StockWheelAssets *wheel = find_wheel(name);
    GameString *current = (GameString *)((unsigned char *)part + 0x188);
    if (!wheel || !((int (*)(const GameString *, const GameString *))(game + 0x146570))(current, name))
        return ((unsigned char (*)(void *, const GameString *))(game + 0x6cedf0))(part, name);
    void *filesystem = *(void **)(game + 0x2470a18);
    if (!filesystem) return ready(part);
    typedef unsigned char (*Exists)(void *, const GameString *);
    Exists exists = (Exists)(*(void ***)filesystem)[0xc8 / 8];
    GameString models[4], textures[4];
    char model_paths[4][STOCK_WHEEL_PATH_CAPACITY], texture_paths[4][STOCK_WHEEL_PATH_CAPACITY];
    int has_textures = 1;
    for (unsigned i = 0; i < 4; ++i) {
        if (!asset_path(model_paths[i], wheel, stock_model_layouts[wheel->model_layout].paths[i]) ||
            !asset_path(texture_paths[i], wheel, stock_texture_layouts[wheel->texture_layout].paths[i]))
            return ready(part);
        models[i] = borrowed(model_paths[i]); textures[i] = borrowed(texture_paths[i]);
        if (!exists(filesystem, models + i)) return ready(part);
        if (!textures[i].length || !exists(filesystem, textures + i)) has_textures = 0;
    }
    ((GameString *(*)(GameString *, const GameString *, size_t, size_t))(game + 0x149190))
        (current, name, 0, (size_t)-1);
    *((unsigned char *)part + 0x180) = !has_textures;
    /* Native WheelPart still owns its queued loads, replacements and release. */
    return ((unsigned char (*)(void *, const GameString *, const GameString *))
        (game + 0x6cf610))(part, models, textures);
}

static Fm5Site bridge(unsigned rva, uintptr_t callback) {
    Fm5Site site = {rva, 12,
        {0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
        {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}, 0};
    memory_copy(site.replacement + 2, &callback, sizeof(callback));
    return site;
}

static Fm5Site call(unsigned rva, unsigned original, unsigned target) {
    Fm5Site site = {rva, 5, {0xe8,0,0,0,0}, {0xe8,0,0,0,0}, 0};
    int32_t before = (int32_t)(original - rva - 5), after = (int32_t)(target - rva - 5);
    memory_copy(site.original + 1, &before, 4);
    memory_copy(site.replacement + 1, &after, 4);
    return site;
}

unsigned fm5_rims_prepare(unsigned char *image, Fm5Site *sites) {
    game = image;
    allocate = (void *(*)(size_t))(image + 0x1cdbb0);
    free_query = (void (*)(void *))(image + 0x1cdc30);
    original_load = (LoadQuery)(image + 0x8dc6c0);
    sites[0] = bridge(0x49c0b2, (uintptr_t)load_rim_query);
    sites[1] = bridge(0x49c284, (uintptr_t)archive_text);
    sites[2] = bridge(0x49c522, (uintptr_t)archive_string);
    sites[3] = bridge(0x49d521, (uintptr_t)load_stock_wheels);
    sites[4] = call(0x49c056, 0x8dc6c0, 0x49c0b2);
    sites[5] = call(0x49d46e, 0x8dc6c0, 0x49c0b2);
    sites[6] = call(0x5b1d84, 0x1bad80, 0x49c284);
    sites[7] = call(0x8708fe, 0x14dbd0, 0x49c522);
    sites[8] = call(0x877da1, 0x14dbd0, 0x49c522);
    sites[9] = call(0x6e8112, 0x6cedf0, 0x49d521);
    sites[10] = call(0x6ed856, 0x6cedf0, 0x49d521);
    return 11;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) size_t Fm5TestRimsQuery(const char *query, char *out, size_t capacity) {
    QueryPlan plan;
    return plan_query(query, &plan) ? edit_query(query, &plan, out, capacity) : 0;
}
__declspec(dllexport) int Fm5TestWheelPath(const char *name, unsigned kind, unsigned corner,
                                        char *out, size_t capacity) {
    GameString key = borrowed(name);
    const StockWheelAssets *wheel = find_wheel(&key);
    if (!wheel || kind > 1 || corner > 3 || capacity < STOCK_WHEEL_PATH_CAPACITY) return 0;
    return asset_path(out, wheel, kind ? stock_texture_layouts[wheel->texture_layout].paths[corner]
                                      : stock_model_layouts[wheel->model_layout].paths[corner]);
}
#endif

