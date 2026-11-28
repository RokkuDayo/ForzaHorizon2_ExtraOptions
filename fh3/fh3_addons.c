#include "fh3.h"
#include "fh3_internal.h"
#include "shared/addon_json.h"
#include "shared/addon_database.h"
#include "shared/support.h"
#include "shared/logging.h"

/* Add-on packages (cars and other database content) for FH3, with FH2's
   package format: mods\Forza Horizon 3\<Mod>\{mod.json, database.json,
   strings.json, media}. database.json is applied by FH2's engine
   (shared/addon_database.c) to the in-memory copy of gamedbRC.slt that
   fh3.c's sqlite3_open hook makes; strings.json entries are added to the
   game's string tables as they are read.

   Packages made for FH2 (anything whose target does not say "game": "FH3")
   are translated first:
   - FH2 string references are _&<table16:key16>; FH3's are
     _&<table32:key32>. A reference to a string in the package's own
     strings.json becomes _&<FH3 table hash:key>, and the key is added to the
     FH3 table. Data_Car.MakeName is FH3's own name of the make.
   - Rows whose key FH3 already has are FH3's (makes, shared lists) and are
     kept; a clashing row that belongs to one of the package's own cars (a
     paint colour, say) gets a free ID instead.
   - FH3-only columns take the most common FH3 value (the average where
     values vary).
   - update and delete operations are dropped: their keys name FH2's cars.

   A car is drawn from Data_Car(ModelId).MediaName's zip; fh3_mods.c opens
   mod car zips (FH3 opens only those in its zipmanifest.xml). */

enum {
    MAX_PACKS = 64, MAX_CARS = 512, MAX_COLUMNS = 512, MAX_VOTES = 256, MAX_HASHES = 64,
    MAX_STRINGS = 8192, SQL_CAPACITY = 16384, SQLITE_ROW = 100,
    STREAM_CALL = 0x1cb1e8b, MEMORY_STREAM = 0x1c40750,
};

typedef struct {
    const char *id;
    char folder[MAX_PATH * 3];
    JsonDocument manifest, database, strings;
    int fh2, valid, applied;
} Pack;

static Pack packs[MAX_PACKS];
static unsigned pack_count;
static const wchar_t *mods_root;
static const wchar_t (*mod_folders)[MAX_PATH];
static unsigned mod_folder_count;


static void report(const Pack *pack, const char *message) {
    log_append("MODS: ");
    if (pack) {
        log_append(pack->id ? pack->id : pack->folder);
        log_append(": ");
    }
    log_append(message);
    log_append("\r\n");
}

static unsigned read32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static void write32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

static void *allocate(size_t bytes) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes); }

static int equal(const char *a, const char *b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}

static const JsonValue *named(const JsonValue *object, const char *name) {
    if (object && object->kind == JSON_OBJECT && name)
        for (const JsonValue *value = object->child; value; value = value->next)
            if (value->name && text_equal_ignore_case(value->name, name)) return value;
    return NULL;
}

static char *decimal(char *out, uint64_t value) {
    char digits[24];
    unsigned n = 0;
    do digits[n++] = (char)('0' + value % 10), value /= 10;
    while (value);
    while (n) *out++ = digits[--n];
    *out = 0;
    return out;
}

static int parse_decimal(const char *text, size_t length, uint64_t *value) {
    uint64_t result = 0;
    if (!length || length > 20) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned digit = (unsigned char)text[i] - '0';
        if (digit > 9 || result > (UINT64_MAX - digit) / 10) return 0;
        result = result * 10 + digit;
    }
    *value = result;
    return 1;
}

/* --- Package discovery (startup). --- */

/* Startup runs on one thread: shared path scratch. */
static wchar_t scratch_path[PATH_CAPACITY], scratch_folder[PATH_CAPACITY];

static int read_json(const wchar_t *folder, const wchar_t *name, JsonDocument *document) {
    wchar_t *path = scratch_path;
    if (!path_join(path, folder, name)) return -1;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    LARGE_INTEGER size;
    char *data = NULL;
    DWORD read = 0;
    int ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 16 * 1024 * 1024 &&
             (data = HeapAlloc(GetProcessHeap(), 0, (size_t)size.QuadPart + 1)) != NULL &&
             ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL) && read == size.QuadPart;
    CloseHandle(file);
    if (!ok) {
        if (data) HeapFree(GetProcessHeap(), 0, data);
        return -1;
    }
    data[read] = 0;
    return addon_json_parse(document, data, read) ? 1 : -1;
}

static int json_file(const wchar_t *folder, const JsonValue *manifest, const char *key,
                     const wchar_t *fallback, JsonDocument *document) {
    const JsonValue *name = addon_json_get(manifest, key);
    if (name && name->kind == JSON_NULL) return 0;
    if (!name) return read_json(folder, fallback, document);
    wchar_t wide[MAX_PATH];
    int n = name->kind == JSON_STRING
                ? MultiByteToWideChar(CP_UTF8, 0, name->text, (int)name->length, wide, MAX_PATH - 1)
                : 0;
    if (n <= 0) return -1;
    wide[n] = 0;
    for (int i = 0; i < n; ++i)
        if (wide[i] == L':' || (wide[i] == L'.' && i + 1 < n && wide[i + 1] == L'.')) return -1;
    return read_json(folder, wide, document);
}

static int pack_by_id(const char *id, unsigned before) {
    for (unsigned i = 0; i < before; ++i)
        if (packs[i].id && text_equal_ignore_case(packs[i].id, id)) return (int)i;
    return -1;
}

static void open_pack(const wchar_t *root, const wchar_t *name) {
    if (pack_count == MAX_PACKS) return;
    Pack *pack = &packs[pack_count];
    static const Pack empty;
    memory_copy(pack, &empty, sizeof(empty));
    wchar_t *folder = scratch_folder;
    if (!path_join(folder, root, name) ||
        WideCharToMultiByte(CP_UTF8, 0, name, -1, pack->folder, sizeof(pack->folder), NULL, NULL) <= 0)
        return;
    int manifest = read_json(folder, L"mod.json", &pack->manifest);
    if (!manifest) manifest = read_json(folder, L"addon.json", &pack->manifest);
    if (manifest < 0) {
        report(pack, "invalid mod.json; mod skipped");
        addon_json_free(&pack->manifest);
        return;
    }
    const JsonValue *root_value = pack->manifest.root;
    int database = json_file(folder, root_value, "database", L"database.json", &pack->database);
    int strings = json_file(folder, root_value, "strings", L"strings.json", &pack->strings);
    if (!database && !strings) {
        addon_json_free(&pack->manifest);
        return; /* a plugin or media-only mod */
    }
    ++pack_count;
    const JsonValue *id = addon_json_get(root_value, "id");
    pack->id = id && id->kind == JSON_STRING && id->length ? id->text : pack->folder;
    if (database < 0 || strings < 0) {
        report(pack, database < 0 ? "invalid database JSON; mod skipped" : "invalid strings JSON; mod skipped");
        return;
    }
    if (pack_by_id(pack->id, pack_count - 1) >= 0) {
        report(pack, "duplicate mod ID; mod skipped");
        return;
    }
    const JsonValue *dependencies = addon_json_get(root_value, "dependencies");
    for (const JsonValue *d = dependencies && dependencies->kind == JSON_ARRAY ? dependencies->child : NULL;
         d; d = d->next) {
        int at = d->kind == JSON_STRING ? pack_by_id(d->text, pack_count - 1) : -1;
        if (at < 0 || !packs[at].valid) {
            report(pack, "a dependency is missing, disabled or later in the load order; mod skipped");
            return;
        }
    }
    const JsonValue *target = addon_json_get(pack->database.root, "target");
    pack->fh2 = !addon_json_is(addon_json_get(target, "game"), "FH3") &&
                !addon_json_is(addon_json_get(target, "game"), "fh3");
    pack->valid = 1;
    report(pack, !pack->database.root ? "strings ready"
                 : pack->fh2          ? "FH2 database queued (translated for FH3 when the game database opens)"
                                      : "database queued");
}

static void install_strings_hook(void);

void fh3_addons_configure(const wchar_t *root, const wchar_t (*names)[MAX_PATH], unsigned count) {
    mods_root = root, mod_folders = names, mod_folder_count = count;
    for (unsigned i = 0; i < count; ++i) open_pack(root, names[i]);
    for (unsigned i = 0; i < pack_count; ++i)
        if (packs[i].valid && packs[i].strings.root) {
            install_strings_hook();
            break;
        }
}

int fh3_addons_pending(void) {
    for (unsigned i = 0; i < pack_count; ++i)
        if (packs[i].valid && packs[i].database.root) return 1;
    return 0;
}

/* --- SQL helpers on the game's SQLite (bound in fh3_addons_database). --- */

static const AddonSqlite *sql;
static char query[SQL_CAPACITY];
static unsigned query_used;
static int query_ok;

static void q_reset(void) { query_used = 0, query_ok = 1, query[0] = 0; }
static void q_add_n(const char *text, size_t length) {
    if (query_used + length + 1 > SQL_CAPACITY) {
        query_ok = 0;
        return;
    }
    memory_copy(query + query_used, text, length);
    query_used += (unsigned)length;
    query[query_used] = 0;
}
static void q_add(const char *text) { q_add_n(text, text_length(text)); }
static void q_quoted(const char *text, size_t length, char quote) {
    q_add_n(&quote, 1);
    for (size_t i = 0; i < length; ++i) {
        q_add_n(text + i, 1);
        if (text[i] == quote) q_add_n(&quote, 1);
    }
    q_add_n(&quote, 1);
}
static void q_name(const char *name) { q_quoted(name, text_length(name), '"'); }
static void q_value(const JsonValue *value) {
    if (!value || value->kind == JSON_NULL) q_add("NULL");
    else if (value->kind == JSON_NUMBER) {
        for (size_t i = 0; i < value->length; ++i) {
            char c = value->text[i];
            if (!((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E'))
                query_ok = 0;
        }
        q_add_n(value->text, value->length);
    } else if (value->kind == JSON_STRING) q_quoted(value->text, value->length, '\'');
    else if (value->kind == JSON_BOOL) q_add(value->text[0] == 't' ? "1" : "0");
    else query_ok = 0;
}
static void *q_prepare(void *database) {
    void *statement = NULL;
    if (!query_ok || sql->prepare(database, query, -1, &statement, NULL) || !statement) {
        if (statement) sql->finalize(statement);
        return NULL;
    }
    return statement;
}

static char *copy_text(const char *text, size_t length) {
    char *copy = allocate(length + 1);
    if (copy && length) memory_copy(copy, text, length);
    return copy;
}

/* A table's columns: name and declared type. */
typedef struct {
    char *name[MAX_COLUMNS];
    int text[MAX_COLUMNS];
    unsigned count;
} Columns;

static int read_columns(void *database, const char *table, Columns *columns) {
    for (unsigned i = 0; i < columns->count; ++i) HeapFree(GetProcessHeap(), 0, columns->name[i]);
    columns->count = 0;
    q_reset();
    q_add("PRAGMA table_info(");
    q_name(table);
    q_add(")");
    void *statement = q_prepare(database);
    if (!statement) return 0;
    while (columns->count < MAX_COLUMNS && sql->step(statement) == SQLITE_ROW) {
        const char *name = sql->column_text(statement, 1), *type = sql->column_text(statement, 2);
        if (!name) continue;
        columns->name[columns->count] = copy_text(name, text_length(name));
        columns->text[columns->count] = type && text_equal_ignore_case(type, "TEXT");
        if (columns->name[columns->count]) ++columns->count;
    }
    sql->finalize(statement);
    return columns->count > 0;
}

static int column_index(const Columns *columns, const char *name) {
    for (unsigned i = 0; i < columns->count; ++i)
        if (text_equal_ignore_case(columns->name[i], name)) return (int)i;
    return -1;
}

/* FH3 string-table hash of a database table: its own string columns refer
   to it, so the most common high half of their _& references. */
static struct {
    char *table;
    unsigned hash;
} fh3_hashes[MAX_HASHES];
static unsigned fh3_hash_count;

static unsigned fh3_table_hash(void *database, const char *table) {
    for (unsigned i = 0; i < fh3_hash_count; ++i)
        if (text_equal_ignore_case(fh3_hashes[i].table, table)) return fh3_hashes[i].hash;
    static Columns columns;
    static unsigned hashes[32], votes[32];
    unsigned kinds = 0, best = 0;
    for (unsigned k = 0; k < 32; ++k) hashes[k] = votes[k] = 0;
    if (read_columns(database, table, &columns))
        for (unsigned c = 0; c < columns.count; ++c) {
            if (!columns.text[c]) continue;
            q_reset();
            q_add("SELECT ");
            q_name(columns.name[c]);
            q_add(" FROM ");
            q_name(table);
            q_add(" WHERE substr(");
            q_name(columns.name[c]);
            q_add(",1,2)='_&' LIMIT 50");
            void *statement = q_prepare(database);
            while (statement && sql->step(statement) == SQLITE_ROW) {
                const char *text = sql->column_text(statement, 0);
                uint64_t value;
                if (!text || !parse_decimal(text + 2, text_length(text + 2), &value) || value >> 32 == 0)
                    continue;
                unsigned hash = (unsigned)(value >> 32), k = 0;
                while (k < kinds && hashes[k] != hash) ++k;
                if (k == kinds && kinds < 32) hashes[kinds++] = hash;
                if (k < kinds) ++votes[k];
            }
            if (statement) sql->finalize(statement);
        }
    for (unsigned k = 1; k < kinds; ++k)
        if (votes[k] > votes[best]) best = k;
    unsigned hash = kinds ? hashes[best] : 0;
    if (fh3_hash_count < MAX_HASHES && (fh3_hashes[fh3_hash_count].table = copy_text(table, text_length(table))))
        fh3_hashes[fh3_hash_count++].hash = hash;
    return hash;
}

/* FH2 table ids (the high 16 bits of an FH2 reference), learned from which
   table's rows use them. */
static struct {
    unsigned id, votes;
    const char *table;
} fh2_votes[MAX_VOTES];
static unsigned fh2_vote_count;

static int fh2_reference(const JsonValue *value, unsigned *table, unsigned *key) {
    uint64_t number;
    if (!value || value->kind != JSON_STRING || value->length < 3 || value->text[0] != '_' ||
        value->text[1] != '&' || !parse_decimal(value->text + 2, value->length - 2, &number) || number >> 32)
        return 0;
    *table = (unsigned)(number >> 16), *key = (unsigned)(number & 0xffff);
    return 1;
}

static int package_string(const char *table, unsigned key) {
    char name[12];
    decimal(name, key);
    for (unsigned p = 0; p < pack_count; ++p) {
        const JsonValue *languages = packs[p].valid ? addon_json_get(packs[p].strings.root, "languages") : NULL;
        for (const JsonValue *language = languages ? languages->child : NULL; language; language = language->next)
            if (named(named(language, table), name)) return 1;
    }
    return 0;
}

static void learn_fh2_tables(void) {
    fh2_vote_count = 0;
    for (unsigned p = 0; p < pack_count; ++p) {
        if (!packs[p].valid || !packs[p].fh2) continue;
        const JsonValue *tables = addon_json_get(packs[p].database.root, "tables");
        for (const JsonValue *table = tables ? tables->child : NULL; table; table = table->next) {
            const JsonValue *rows = addon_json_get(table, "insert");
            for (const JsonValue *row = rows ? rows->child : NULL; row; row = row->next)
                for (const JsonValue *field = row->child; field; field = field->next) {
                    unsigned id, key, i = 0;
                    /* Only references the package defines say which table an id is. */
                    if (!fh2_reference(field, &id, &key) || !package_string(table->name, key)) continue;
                    while (i < fh2_vote_count &&
                           !(fh2_votes[i].id == id && text_equal_ignore_case(fh2_votes[i].table, table->name)))
                        ++i;
                    if (i == fh2_vote_count && i < MAX_VOTES) {
                        fh2_votes[i].id = id, fh2_votes[i].votes = 0, fh2_votes[i].table = table->name;
                        ++fh2_vote_count;
                    }
                    if (i < fh2_vote_count) ++fh2_votes[i].votes;
                }
        }
    }
}

static const char *fh2_table(unsigned id) {
    const char *table = NULL;
    unsigned best = 0;
    for (unsigned i = 0; i < fh2_vote_count; ++i)
        if (fh2_votes[i].id == id && fh2_votes[i].votes > best) best = fh2_votes[i].votes, table = fh2_votes[i].table;
    return table;
}

static void set_text(JsonValue *value, JsonKind kind, const char *text, size_t length) {
    char *copy = copy_text(text, length);
    if (!copy) return;
    value->kind = kind;
    value->text = copy;
    value->length = length;
}

static JsonValue *field_named(const JsonValue *row, const char *name) {
    for (JsonValue *field = row->child; field; field = field->next)
        if (field->name && text_equal_ignore_case(field->name, name)) return field;
    return NULL;
}

/* Data_Car.MakeName: FH3's own name of the car's make. */
static int make_name(void *database, const JsonValue *row, JsonValue *field) {
    const JsonValue *make = field_named(row, "MakeID");
    if (!make || make->kind != JSON_NUMBER) return 0;
    q_reset();
    q_add("SELECT DisplayName FROM List_CarMake WHERE ID=");
    q_value(make);
    void *statement = q_prepare(database);
    int found = 0;
    if (statement && sql->step(statement) == SQLITE_ROW) {
        const char *text = sql->column_text(statement, 0);
        if (text && text[0] == '_' && text[1] == '&') {
            set_text(field, JSON_STRING, text, text_length(text));
            found = 1;
        }
    }
    if (statement) sql->finalize(statement);
    return found;
}

static int translate_string(void *database, const char *table, const JsonValue *row, JsonValue *field) {
    unsigned id, key;
    if (!fh2_reference(field, &id, &key)) return 1;
    if (equal(table, "Data_Car") && text_equal_ignore_case(field->name, "MakeName") &&
        make_name(database, row, field))
        return 1;
    const char *strings = fh2_table(id);
    unsigned hash = strings && package_string(strings, key) ? fh3_table_hash(database, strings) : 0;
    if (!hash) return 0;
    char text[32];
    text[0] = '_', text[1] = '&';
    char *end = decimal(text + 2, (uint64_t)hash << 32 | key);
    set_text(field, JSON_STRING, text, (size_t)(end - text));
    return 1;
}

/* Fill value of an FH3-only column. */
static JsonValue *fill_value(void *database, const char *table, const char *column) {
    JsonValue *value = allocate(sizeof(JsonValue));
    if (!value) return NULL;
    value->name = (char *)column;
    value->kind = JSON_NULL;
    value->text = (char *)"null";
    value->length = 4;
    int64_t total = 0;
    q_reset();
    q_add("SELECT count(*) FROM ");
    q_name(table);
    void *statement = q_prepare(database);
    if (statement && sql->step(statement) == SQLITE_ROW) total = sql->column_int64(statement, 0);
    if (statement) sql->finalize(statement);
    q_reset();
    q_add("SELECT typeof(");
    q_name(column);
    q_add("), ");
    q_name(column);
    q_add(", count(*) n FROM ");
    q_name(table);
    q_add(" GROUP BY 2 ORDER BY n DESC LIMIT 1");
    statement = q_prepare(database);
    int done = 0;
    if (statement && sql->step(statement) == SQLITE_ROW) {
        const char *type = sql->column_text(statement, 0), *text = sql->column_text(statement, 1);
        int64_t n = sql->column_int64(statement, 2);
        int number = type && (equal(type, "integer") || equal(type, "real"));
        if (type && text && (equal(type, "text") || (number && n * 20 >= total))) {
            set_text(value, number ? JSON_NUMBER : JSON_STRING, text, text_length(text));
            done = 1;
        } else if (number) {
            done = -1; /* values vary: their average */
        }
    }
    if (statement) sql->finalize(statement);
    if (done < 0) {
        q_reset();
        q_add("SELECT avg(");
        q_name(column);
        q_add(") FROM ");
        q_name(table);
        statement = q_prepare(database);
        if (statement && sql->step(statement) == SQLITE_ROW) {
            const char *text = sql->column_text(statement, 0);
            if (text) set_text(value, JSON_NUMBER, text, text_length(text));
        }
        if (statement) sql->finalize(statement);
    }
    return value;
}

static int row_exists(void *database, const char *table, const JsonValue *keys, const JsonValue *row) {
    q_reset();
    q_add("SELECT 1 FROM ");
    q_name(table);
    q_add(" WHERE ");
    for (const JsonValue *key = keys->child; key; key = key->next) {
        const JsonValue *field = key->kind == JSON_STRING ? field_named(row, key->text) : NULL;
        if (!field) return 0;
        if (key != keys->child) q_add(" AND ");
        q_name(key->text);
        q_add(" IS ");
        q_value(field);
    }
    q_add(" LIMIT 1");
    void *statement = q_prepare(database);
    int found = statement && sql->step(statement) == SQLITE_ROW;
    if (statement) sql->finalize(statement);
    return found;
}

static int is_integer(const JsonValue *value) {
    uint64_t unused;
    return value && value->kind == JSON_NUMBER && parse_decimal(value->text, value->length, &unused);
}

static JsonValue *make_value(const char *name, JsonKind kind, const char *text, size_t length) {
    JsonValue *value = allocate(sizeof(JsonValue));
    if (!value) return NULL;
    value->name = copy_text(name, text_length(name));
    value->kind = kind;
    value->text = copy_text(text, length); /* document texts are not NUL-terminated */
    value->length = length;
    return value->name && value->text ? value : NULL;
}

static void append_child(JsonValue *parent, JsonValue *child) {
    JsonValue **tail = &parent->child;
    while (*tail) tail = &(*tail)->next;
    *tail = child;
    child->next = NULL;
    ++parent->length;
}

/* FH3 keeps a car's stock wheel in List_Wheels (MediaName and IconPath are
   the car's, IsStock 1); the game reads its Mass and MassLevel and crashes
   without the row. An FH2 car whose StockWheelID names FH2's own wheel gets
   one; the other columns take FH3's usual values. */
static void stock_wheels(void *database, Pack *pack, JsonValue *tables) {
    JsonValue *cars = addon_json_get(addon_json_get(tables, "Data_Car"), "insert");
    if (!cars) return;
    JsonValue *wheels = addon_json_get(tables, "List_Wheels");
    unsigned added = 0;
    for (JsonValue *car = cars->child; car; car = car->next) {
        JsonValue *wheel = field_named(car, "StockWheelID"), *media = field_named(car, "MediaName");
        if (!is_integer(wheel) || !media || media->kind != JSON_STRING) continue;
        int present = 0;
        JsonValue *rows = addon_json_get(wheels, "insert");
        for (JsonValue *row = rows ? rows->child : NULL; row && !present; row = row->next) {
            JsonValue *id = field_named(row, "ID");
            present = id && id->kind == JSON_NUMBER && id->length == wheel->length &&
                      memory_equal(id->text, wheel->text, id->length);
        }
        if (present) continue;
        q_reset();
        q_add("SELECT 1 FROM List_Wheels WHERE ID=");
        q_value(wheel);
        void *statement = q_prepare(database);
        present = statement && sql->step(statement) == SQLITE_ROW;
        if (statement) sql->finalize(statement);
        if (present) continue;
        if (!wheels) {
            wheels = make_value("List_Wheels", JSON_OBJECT, "", 0);
            JsonValue *key = make_value("key", JSON_ARRAY, "", 0), *id = make_value("", JSON_STRING, "ID", 2);
            JsonValue *insert = make_value("insert", JSON_ARRAY, "", 0);
            if (!wheels || !key || !id || !insert) return;
            wheels->length = key->length = insert->length = 0;
            append_child(key, id);
            append_child(wheels, key);
            append_child(wheels, insert);
            append_child(tables, wheels);
            rows = insert;
        }
        JsonValue *row = make_value("", JSON_OBJECT, "", 0);
        if (!row) return;
        row->length = 0;
        JsonValue *fields[] = {make_value("ID", JSON_NUMBER, wheel->text, wheel->length),
                               make_value("MediaName", JSON_STRING, media->text, media->length),
                               make_value("IconPath", JSON_STRING, media->text, media->length),
                               make_value("DisplayName", JSON_STRING, "", 0),
                               make_value("IsStock", JSON_NUMBER, "1", 1), make_value("Mass", JSON_NUMBER, "25.0", 4)};
        for (unsigned i = 0; i < ARRAY_COUNT(fields); ++i)
            if (fields[i]) append_child(row, fields[i]);
        append_child(rows, row);
        ++added;
    }
    if (added) report(pack, "stock wheel rows added for cars whose wheel is FH2's own");
}

static void translate(void *database, Pack *pack) {
    static Columns columns;
    static unsigned cars[MAX_CARS];
    unsigned car_count = 0, dropped_ops = 0, unresolved = 0;
    JsonValue *root = pack->database.root, *tables = addon_json_get(root, "tables");
    /* The package's own cars. */
    const JsonValue *car_rows = addon_json_get(addon_json_get(tables, "Data_Car"), "insert");
    for (const JsonValue *row = car_rows ? car_rows->child : NULL; row && car_count < MAX_CARS; row = row->next) {
        uint64_t id;
        const JsonValue *field = field_named(row, "Id");
        if (is_integer(field) && parse_decimal(field->text, field->length, &id)) cars[car_count++] = (unsigned)id;
    }
    if (tables) stock_wheels(database, pack, tables);
    for (JsonValue *table = tables ? tables->child : NULL; table; table = table->next) {
        if (table->kind != JSON_OBJECT || !read_columns(database, table->name, &columns)) continue;
        /* update/delete name FH2's own rows. */
        for (JsonValue **link = &table->child; *link;) {
            if ((*link)->name && (equal((*link)->name, "update") || equal((*link)->name, "delete"))) {
                *link = (*link)->next;
                --table->length;
                ++dropped_ops;
            } else {
                link = &(*link)->next;
            }
        }
        JsonValue *keys = addon_json_get(table, "key"), *rows = addon_json_get(table, "insert");
        if (!rows || rows->kind != JSON_ARRAY) continue;
        unsigned kept = 0, renumbered = 0, removed_columns = 0;
        uint64_t next_id = 0;
        static JsonValue *fills[MAX_COLUMNS];
        for (unsigned c = 0; c < MAX_COLUMNS; ++c) fills[c] = NULL;
        for (JsonValue **link = &rows->child; *link;) {
            JsonValue *row = *link;
            if (row->kind != JSON_OBJECT) {
                link = &row->next;
                continue;
            }
            /* Columns FH3 doesn't have. */
            for (JsonValue **field = &row->child; *field;) {
                if ((*field)->name && column_index(&columns, (*field)->name) < 0) {
                    *field = (*field)->next;
                    --row->length;
                    ++removed_columns;
                } else {
                    field = &(*field)->next;
                }
            }
            for (JsonValue *field = row->child; field; field = field->next)
                if (!translate_string(database, table->name, row, field)) ++unresolved;
            if (keys && keys->kind == JSON_ARRAY && keys->length && row_exists(database, table->name, keys, row)) {
                JsonValue *key = keys->length == 1 ? field_named(row, keys->child->text) : NULL;
                int own = 0;
                for (const JsonValue *field = row->child; field && !own; field = field->next) {
                    uint64_t value;
                    if (!(text_equal_ignore_case(field->name, "Ordinal") || text_equal_ignore_case(field->name, "CarId")) ||
                        !is_integer(field) || !parse_decimal(field->text, field->length, &value))
                        continue;
                    for (unsigned c = 0; c < car_count && !own; ++c) own = cars[c] == value;
                }
                if (own && is_integer(key) && !equal(table->name, "Data_Car")) {
                    if (!next_id) {
                        q_reset();
                        q_add("SELECT max(");
                        q_name(key->name);
                        q_add(") FROM ");
                        q_name(table->name);
                        void *statement = q_prepare(database);
                        if (statement && sql->step(statement) == SQLITE_ROW)
                            next_id = (uint64_t)sql->column_int64(statement, 0);
                        if (statement) sql->finalize(statement);
                        for (const JsonValue *other = rows->child; other; other = other->next) {
                            const JsonValue *value = field_named(other, key->name);
                            uint64_t number;
                            if (is_integer(value) && parse_decimal(value->text, value->length, &number) &&
                                number > next_id)
                                next_id = number;
                        }
                    }
                    char text[24];
                    char *end = decimal(text, ++next_id);
                    set_text(key, JSON_NUMBER, text, (size_t)(end - text));
                    ++renumbered;
                } else {
                    *link = row->next;
                    --rows->length;
                    ++kept;
                    continue;
                }
            }
            /* FH3-only columns. */
            JsonValue **tail = &row->child;
            while (*tail) tail = &(*tail)->next;
            for (unsigned c = 0; c < columns.count; ++c) {
                if (field_named(row, columns.name[c])) continue;
                if (!fills[c]) fills[c] = fill_value(database, table->name, columns.name[c]);
                JsonValue *value = allocate(sizeof(JsonValue));
                if (!value || !fills[c]) continue;
                memory_copy(value, fills[c], sizeof(JsonValue));
                value->name = copy_text(columns.name[c], text_length(columns.name[c]));
                value->next = NULL;
                *tail = value;
                tail = &value->next;
                ++row->length;
            }
            link = &row->next;
        }
        if (kept || renumbered || removed_columns) {
            static char line[512];
            char number[24];
            unsigned at = 0;
            const char *parts[8];
            parts[0] = "FH2 rows in ", parts[1] = table->name, parts[2] = ": ", parts[3] = NULL,
            parts[4] = " already in FH3 (FH3's kept), ", parts[5] = NULL, parts[6] = " given free IDs, ",
            parts[7] = NULL;
            unsigned values[3];
            values[0] = kept, values[1] = renumbered, values[2] = removed_columns;
            line[0] = 0;
            for (unsigned i = 0, v = 0; i < 9; ++i) {
                const char *part = i == 8 ? " FH2-only values left out"
                                   : parts[i] ? parts[i] : (decimal(number, values[v++]), number);
                size_t n = text_length(part);
                if (at + n >= sizeof(line)) break;
                memory_copy(line + at, part, n);
                at += (unsigned)n;
                line[at] = 0;
            }
            report(pack, line);
        }
    }
    /* The FH2 base database hash never matches FH3's. */
    JsonValue *target = addon_json_get(root, "target");
    for (JsonValue **link = target ? &target->child : NULL; link && *link;) {
        if ((*link)->name && equal((*link)->name, "base_sha256")) {
            *link = (*link)->next;
            --target->length;
        } else {
            link = &(*link)->next;
        }
    }
    if (dropped_ops) report(pack, "FH2 update/delete operations left out (they name FH2's own rows)");
    if (unresolved) report(pack, "some FH2 string references have no text in strings.json and stay as they are");
}

/* media\cars\<media>.zip in any mod folder. */
static int car_zip_present(const JsonValue *media) {
    static wchar_t name[MAX_PATH], path[PATH_CAPACITY], folder[PATH_CAPACITY];
    int n = media && media->kind == JSON_STRING && media->length && media->length < MAX_PATH - 20
                ? MultiByteToWideChar(CP_UTF8, 0, media->text, (int)media->length, name, MAX_PATH - 20)
                : 0;
    if (n <= 0) return 0;
    for (int i = 0; i < n; ++i)
        if (name[i] == L'\\' || name[i] == L'/' || name[i] == L':') return 0;
    memory_copy(name + n, L".zip", 5 * sizeof(wchar_t));
    for (unsigned m = 0; m < mod_folder_count; ++m) {
        if (!path_join(folder, mods_root, mod_folders[m]) || !path_join(path, folder, L"media") ||
            !path_join(folder, path, L"cars") || !path_join(path, folder, name))
            continue;
        DWORD attributes = GetFileAttributesW(path);
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return 1;
    }
    return 0;
}

static void record_cars(void) {
    unsigned count = 0, missing = 0;
    for (unsigned p = 0; p < pack_count; ++p) {
        if (!packs[p].applied) continue;
        const JsonValue *rows = addon_json_get(addon_json_get(addon_json_get(packs[p].database.root, "tables"),
                                                              "Data_Car"), "insert");
        for (const JsonValue *row = rows ? rows->child : NULL; row && count < MAX_CARS; row = row->next) {
            const JsonValue *id = field_named(row, "Id");
            uint64_t value;
            if (!is_integer(id) || !parse_decimal(id->text, id->length, &value) || value > 0x7fffffff) continue;
            if (!car_zip_present(field_named(row, "MediaName"))) {
                ++missing;
                continue;
            }
            ++count;
        }
    }
    char line[128], number[16];
    unsigned at = 0;
    const char *parts[4];
    parts[0] = "add-on cars with their car zip: ", parts[1] = NULL, parts[2] = "; without (drawn as FH3's placeholder): ", parts[3] = NULL;
    unsigned values[2];
    values[0] = (unsigned)count, values[1] = missing;
    for (unsigned i = 0, v = 0; i < 4; ++i) {
        const char *part = parts[i] ? parts[i] : (decimal(number, values[v++]), number);
        for (const char *c = part; *c && at + 1 < sizeof(line); ++c) line[at++] = *c;
    }
    line[at] = 0;
    report(NULL, line);
}

int fh3_addons_database(void *database) {
    if (!fh3_addons_pending()) return 0;
    static AddonSqlite api;
    static const unsigned offsets[] = {0x84ad0, 0x82330, 0x44b20, 0x262f0, 0x21f50, 0x277c0,
                                       0x27aa0, 0x27af0, 0x15b50, 0x16040, 0x1fc20};
    for (unsigned i = 0; i < ARRAY_COUNT(offsets); ++i) ((void **)&api)[i] = fh3_image() + offsets[i];
    addon_database_bind_profile(&api, "media/stripped/gamedbRC.slt");
    sql = addon_database_api();
    learn_fh2_tables();
    int applied = 0;
    for (unsigned i = 0; i < pack_count; ++i) {
        Pack *pack = &packs[i];
        if (!pack->valid || !pack->database.root || pack->applied) continue;
        const JsonValue *dependencies = addon_json_get(pack->manifest.root, "dependencies");
        int ready = 1;
        for (const JsonValue *d = dependencies && dependencies->kind == JSON_ARRAY ? dependencies->child : NULL;
             d && ready; d = d->next) {
            int at = d->kind == JSON_STRING ? pack_by_id(d->text, i) : -1;
            ready = at >= 0 && (packs[at].applied || !packs[at].database.root);
        }
        if (!ready) {
            report(pack, "a dependency's database did not load; mod skipped");
            continue;
        }
        if (pack->fh2) translate(database, pack);
        /* FH3's own database has hundreds of dangling upgrade references
           (UpgradePresetPackages), more than FH2's foreign-key baseline
           holds: each package is its own savepoint, and SQLite's integrity
           check runs once on the result. */
        char error[256];
        if (addon_database_apply_queued(database, pack->database.root, NULL, pack->id, error, 1)) {
            pack->applied = 1;
            ++applied;
            report(pack, "database rows added (in memory)");
        } else {
            report(pack, error);
            report(pack, "database rolled back; mod skipped");
        }
    }
    if (applied && !addon_database_check(database)) {
        report(NULL, "the merged database failed SQLite's integrity check; the original database is used");
        for (unsigned i = 0; i < pack_count; ++i) packs[i].applied = 0;
        return -1;
    }
    if (applied) record_cars();
    return applied;
}

/* --- String tables. ---
   FH3 reads a .str file whole and wraps it in a memory stream
   (0x1c40750(stream, data, size)) at 0x1cb1e8b; the path is the std::string
   at rbp+0x78. Layout: u32 version, u32 12, u32 offset of the name section,
   u32 entries*8+text bytes, u32 text bytes, u32 entries, entries {u32 key,
   u32 byte offset}, UTF-16 text; then the name section: u32 size-12, u32 text
   bytes, u32 entries, entries, UTF-16 ID names. */

typedef struct {
    union {
        char buffer[16];
        char *pointer;
    };
    size_t size, capacity;
} GameString;


typedef struct {
    unsigned key;
    const JsonValue *text;
} Entry;

static unsigned gather(Entry *entries, const char *table, const char *language) {
    unsigned count = 0;
    for (unsigned p = 0; p < pack_count; ++p) {
        if (!packs[p].valid || !packs[p].strings.root) continue;
        const JsonValue *languages = addon_json_get(packs[p].strings.root, "languages"),
                        *fallback = addon_json_get(packs[p].strings.root, "fallback_language");
        const JsonValue *sources[2] = {
            named(named(languages, fallback && fallback->kind == JSON_STRING ? fallback->text : "EN"), table),
            named(named(languages, language), table)};
        for (unsigned s = 0; s < 2; ++s)
            for (const JsonValue *value = sources[s] ? sources[s]->child : NULL; value; value = value->next) {
                uint64_t key;
                if (value->kind != JSON_STRING || !value->name ||
                    !parse_decimal(value->name, text_length(value->name), &key) || key > 0xffffffffu)
                    continue;
                unsigned at = 0;
                while (at < count && entries[at].key != key) ++at;
                if (at == count && count < MAX_STRINGS) entries[count++].key = (unsigned)key;
                if (at < count) entries[at].text = value;
            }
    }
    return count;
}

static unsigned wide_length(const JsonValue *text) {
    return text->length ? (unsigned)MultiByteToWideChar(CP_UTF8, 0, text->text, (int)text->length, NULL, 0) : 0;
}

static unsigned char *extend_table(const unsigned char *data, unsigned size, const char *path, unsigned *out_size) {
    /* <...>\<language>\<table>.str */
    const char *name = path, *previous = path;
    for (const char *at = path; *at; ++at)
        if (*at == '\\' || *at == '/') previous = name, name = at + 1;
    size_t length = text_length(name), code = name > previous ? (size_t)(name - previous - 1) : 0;
    if (length < 5 || length > 132 || !code || code > 8 || !text_equal_ignore_case(name + length - 4, ".str"))
        return NULL;
    char table[129], language[9];
    memory_copy(table, name, length - 4);
    table[length - 4] = 0;
    memory_copy(language, previous, code);
    language[code] = 0;
    static SRWLOCK lock = SRWLOCK_INIT;
    static Entry entries[MAX_STRINGS];
    AcquireSRWLockExclusive(&lock);
    unsigned char *result = NULL;
    unsigned count = gather(entries, table, language);
    if (!count || size < 24) goto done;
    unsigned names = read32(data + 8), body = read32(data + 12), text = read32(data + 16), n = read32(data + 20);
    if (n > 0x1000000 || (uint64_t)24 + (uint64_t)n * 8 + text != names || body != n * 8 + text ||
        (uint64_t)names + 12 > size)
        goto done;
    unsigned names_size = read32(data + names), names_text = read32(data + names + 4), m = read32(data + names + 8);
    if (m != n || names_size != size - names - 12 || (uint64_t)names + 12 + (uint64_t)m * 8 + names_text != size)
        goto done;
    const unsigned char *keys = data + 24, *pool = keys + n * 8;
    /* Which entries replace an existing key, and the new text size. */
    uint64_t new_text = 0, new_names = 0;
    unsigned added = 0;
    static unsigned char replaced[MAX_STRINGS];
    for (unsigned e = 0; e < count; ++e) {
        replaced[e] = 0;
        for (unsigned i = 0; i < n && !replaced[e]; ++i) replaced[e] = read32(keys + i * 8) == entries[e].key;
        new_text += ((uint64_t)wide_length(entries[e].text) + 1) * 2;
        if (!replaced[e]) ++added, new_names += 16 * 2; /* IDS_EO_XXXXXXXX + 0 */
    }
    /* Existing text, rebuilt without the replaced strings. */
    for (unsigned i = 0; i < n; ++i) {
        unsigned offset = read32(keys + i * 8 + 4), key = read32(keys + i * 8), chars = 0;
        int skip = 0;
        for (unsigned e = 0; e < count && !skip; ++e) skip = replaced[e] && entries[e].key == key;
        if (offset >= text || offset % 2) goto done;
        while (offset + chars * 2 + 1 < text && (pool[offset + chars * 2] || pool[offset + chars * 2 + 1])) ++chars;
        if (offset + chars * 2 + 1 >= text) goto done;
        if (!skip) new_text += ((uint64_t)chars + 1) * 2;
    }
    uint64_t total_n = (uint64_t)n + added;
    uint64_t new_names_offset = 24 + total_n * 8 + new_text;
    uint64_t total = new_names_offset + 12 + total_n * 8 + names_text + new_names;
    if (total > 0x7fffffff || !(result = HeapAlloc(GetProcessHeap(), 0, (size_t)total))) goto done;
    memory_copy(result, data, 8);
    write32(result + 8, (unsigned)new_names_offset);
    write32(result + 12, (unsigned)(total_n * 8 + new_text));
    write32(result + 16, (unsigned)new_text);
    write32(result + 20, (unsigned)total_n);
    unsigned char *out_keys = result + 24, *out_pool = out_keys + total_n * 8;
    unsigned written = 0;
    for (unsigned i = 0; i < n; ++i) {
        unsigned key = read32(keys + i * 8), offset = read32(keys + i * 8 + 4), chars = 0;
        const JsonValue *mod = NULL;
        for (unsigned e = 0; e < count && !mod; ++e)
            if (replaced[e] && entries[e].key == key) mod = entries[e].text;
        write32(out_keys + i * 8, key);
        write32(out_keys + i * 8 + 4, written);
        if (mod) {
            chars = wide_length(mod);
            if (chars) MultiByteToWideChar(CP_UTF8, 0, mod->text, (int)mod->length, (wchar_t *)(out_pool + written), (int)chars);
        } else {
            while (pool[offset + chars * 2] || pool[offset + chars * 2 + 1]) ++chars;
            memory_copy(out_pool + written, pool + offset, chars * 2);
        }
        out_pool[written + chars * 2] = out_pool[written + chars * 2 + 1] = 0;
        written += (chars + 1) * 2;
    }
    unsigned slot = n;
    for (unsigned e = 0; e < count; ++e) {
        if (replaced[e]) continue;
        unsigned chars = wide_length(entries[e].text);
        write32(out_keys + slot * 8, entries[e].key);
        write32(out_keys + slot * 8 + 4, written);
        if (chars)
            MultiByteToWideChar(CP_UTF8, 0, entries[e].text->text, (int)entries[e].text->length,
                                (wchar_t *)(out_pool + written), (int)chars);
        out_pool[written + chars * 2] = out_pool[written + chars * 2 + 1] = 0;
        written += (chars + 1) * 2;
        ++slot;
    }
    /* ID names: the originals, then IDS_EO_<key> for the new keys. */
    unsigned char *section = result + new_names_offset;
    write32(section, (unsigned)(total - new_names_offset - 12));
    write32(section + 4, (unsigned)(names_text + new_names));
    write32(section + 8, (unsigned)total_n);
    unsigned char *name_keys = section + 12, *name_pool = name_keys + total_n * 8;
    memory_copy(name_keys, data + names + 12, (size_t)m * 8);
    memory_copy(name_pool, data + names + 12 + m * 8, names_text);
    unsigned name_written = names_text;
    slot = n;
    for (unsigned e = 0; e < count; ++e) {
        if (replaced[e]) continue;
        static const char hex[] = "0123456789ABCDEF";
        char label[16];
        for (unsigned d = 0; d < 7; ++d) label[d] = "IDS_EO_"[d];
        for (unsigned d = 0; d < 8; ++d) label[7 + d] = hex[entries[e].key >> (28 - d * 4) & 15];
        write32(name_keys + slot * 8, entries[e].key);
        write32(name_keys + slot * 8 + 4, name_written);
        for (unsigned c = 0; c < 16; ++c) {
            name_pool[name_written + c * 2] = c < 15 ? (unsigned char)label[c] : 0;
            name_pool[name_written + c * 2 + 1] = 0;
        }
        name_written += 32;
        ++slot;
    }
    *out_size = (unsigned)total;
    log_append("MODS: ");
    log_append(language);
    log_append("\\");
    log_append(table);
    log_append(".str: mod strings added in memory.\r\n");
done:
    ReleaseSRWLockExclusive(&lock);
    return result;
}

typedef void *(*MemoryStream)(void *, const void *, unsigned);

void *fh3_addons_string_stream(void *stream, const unsigned char *data, unsigned size, const GameString *path) {
    const char *text = path ? (path->capacity >= 16 ? path->pointer : path->buffer) : NULL;
    unsigned extended_size = 0;
    unsigned char *extended = text && path->size < 1024 && data ? extend_table(data, size, text, &extended_size) : NULL;
    /* The extended copy stays allocated: the stream reads it after this call. */
    return ((MemoryStream)(fh3_image() + MEMORY_STREAM))(stream, extended ? extended : data,
                                                          extended ? extended_size : size);
}

static void install_strings_hook(void) {
    unsigned char *site = fh3_image() + STREAM_CALL;
    static const unsigned char mov_rcx_rax[3] = {0x48, 0x8b, 0xc8};
    int32_t relative;
    memory_copy(&relative, site + 1, sizeof(relative));
    unsigned char *stub = NULL;
    if (site[0] != 0xe8 || site + 5 + relative != fh3_image() + MEMORY_STREAM ||
        !memory_equal(site - 3, mov_rcx_rax, 3) || !(stub = fh3_code_stub())) {
        log_append("MODS: the string table loader is not the expected one; mod strings skipped.\r\n");
        return;
    }
    /* lea r9, [rbp+0x78]; mov rax, imm64; jmp rax */
    static unsigned char code[16] = {0x4c, 0x8d, 0x4d, 0x78, 0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t callback = (uintptr_t)fh3_addons_string_stream;
    memory_copy(code + 6, &callback, sizeof(callback));
    intptr_t distance = stub - (site + 5);
    static unsigned char call[5] = {0xe8};
    int32_t rel32 = (int32_t)distance;
    memory_copy(call + 1, &rel32, sizeof(rel32));
    if (distance > 0x7fffffff || distance < -0x7fffffffLL - 1 || !fh3_write_code(stub, code, sizeof(code)) ||
        !fh3_write_code(site, call, sizeof(call))) {
        log_append("MODS: could not hook the string table loader; mod strings skipped.\r\n");
        return;
    }
    log_append("MODS: mod strings are added to the game's string tables as they load.\r\n");
}

/* --- Engine audio donors. --- */

/* mod.json: "fh3_engine_audio": {"<car MediaName>": "<FH3 car or engine>", "*": "<default>"}.
   fn(car, donor, explicit) for every Data_Car row the pack inserts that gets a donor;
   explicit is 0 for the "*" default (which never replaces an engine FH3 already maps). */
void fh3_addons_engine_audio(void (*fn)(const char *, const char *, int)) {
    for (unsigned p = 0; p < pack_count; ++p) {
        Pack *pack = &packs[p];
        const JsonValue *map = addon_json_get(pack->manifest.root, "fh3_engine_audio");
        if (!map || map->kind != JSON_OBJECT || !pack->database.root) continue;
        const JsonValue *fallback = addon_json_get(map, "*");
        if (fallback && (fallback->kind != JSON_STRING || !fallback->length)) fallback = NULL;
        const JsonValue *cars = addon_json_get(
            addon_json_get(addon_json_get(pack->database.root, "tables"), "Data_Car"), "insert");
        if (!cars || cars->kind != JSON_ARRAY) continue;
        for (const JsonValue *row = cars->child; row; row = row->next) {
            const JsonValue *media = addon_json_get(row, "MediaName");
            if (!media || media->kind != JSON_STRING || !media->length) continue;
            const JsonValue *donor = addon_json_get(map, media->text);
            int explicit_donor = donor && donor->kind == JSON_STRING && donor->length;
            if (!explicit_donor) donor = fallback;
            if (!donor) continue;
            fn(media->text, donor->text, explicit_donor);
        }
    }
}
