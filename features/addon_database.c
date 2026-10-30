#include "addon_database.h"
#include "support.h"
#include "logging.h"

enum { SQL_CAPACITY = 2 * 1024 * 1024, SQLITE_ROW = 100, SQLITE_DONE = 101 };

static AddonSqlite sql;
static const char *applying_addon;
static int applying_shared_ids;

typedef struct {
    char *text;
    size_t length;
    int valid;
} Query;

void addon_database_bind(unsigned char *image) {
    sql = (AddonSqlite){
        (void *)(image + 0x7d6c0), (void *)(image + 0x7af20), (void *)(image + 0x3d710),
        (void *)(image + 0x1eee0), (void *)(image + 0x1ab40), (void *)(image + 0x203b0),
        (void *)(image + 0x20690), (void *)(image + 0x206e0), (void *)(image + 0xe740),
        (void *)(image + 0xec30),  (void *)(image + 0x18810)};
}

const AddonSqlite *addon_database_api(void) {
    return &sql;
}

void *addon_database_clone(void *source) {
    void *copy = NULL;
    if (sql.open(":memory:", &copy)) {
        if (copy) {
            sql.close(copy);
        }
        return NULL;
    }
    void *backup = sql.backup_init(copy, "main", source, "main");
    int result = backup ? sql.backup_step(backup, -1) : -1;
    int finished = backup ? sql.backup_finish(backup) : -1;
    if (result != SQLITE_DONE || finished) {
        sql.close(copy);
        return NULL;
    }
    return copy;
}

static int reject(char *error, const char *message) {
    size_t size = text_length(message);
    if (size > 255) {
        size = 255;
    }
    memory_copy(error, message, size);
    error[size] = 0;
    return 0;
}

static void append_n(Query *query, const char *text, size_t size) {
    if (!query->valid || size >= SQL_CAPACITY - query->length) {
        query->valid = 0;
        return;
    }
    memory_copy(query->text + query->length, text, size);
    query->length += size;
    query->text[query->length] = 0;
}

static void append(Query *query, const char *text) {
    append_n(query, text, text_length(text));
}

static void reset(Query *query) {
    query->length = 0;
    query->valid = 1;
    query->text[0] = 0;
}

static int equal(const char *a, const char *b) {
    return text_length(a) == text_length(b) && memory_equal(a, b, text_length(a));
}

static int name(Query *query, const char *text) {
    size_t size = text_length(text);
    if (!size || size > 255) {
        return query->valid = 0;
    }
    append(query, "\"");
    for (size_t i = 0; i < size; ++i) {
        if ((unsigned char)text[i] < 32) {
            return query->valid = 0;
        }
        append_n(query, text + i, 1);
        if (text[i] == '"') {
            append(query, "\"");
        }
    }
    append(query, "\"");
    return query->valid;
}

static int number_valid(const JsonValue *value) {
    int real = 0;
    for (size_t i = 0; i < value->length; ++i) {
        real |= value->text[i] == '.' || value->text[i] == 'e' || value->text[i] == 'E';
    }
    if (value->length > 128) {
        return 0;
    }
    size_t index = value->text[0] == '-' ? 1 : 0;
    if (!real) {
        uint64_t result = 0, limit = index ? UINT64_C(9223372036854775808) : INT64_MAX;
        for (; index < value->length; ++index) {
            unsigned digit = (unsigned char)value->text[index] - '0';
            if (digit > 9 || result > (limit - digit) / 10) {
                return 0;
            }
            result = result * 10 + digit;
        }
        return 1;
    }
    double magnitude = 0;
    int decimals = 0, fraction = 0, exponent = 0, sign = 1;
    for (; index < value->length && value->text[index] != 'e' && value->text[index] != 'E';
         ++index) {
        if (value->text[index] == '.') {
            fraction = 1;
            continue;
        }
        magnitude = magnitude * 10 + value->text[index] - '0';
        decimals += fraction;
    }
    if (index < value->length) {
        ++index;
        if (value->text[index] == '-' || value->text[index] == '+') {
            if (value->text[index] == '-') {
                sign = -1;
            }
            ++index;
        }
        for (; index < value->length; ++index) {
            if (exponent > 1000) {
                return 0;
            }
            exponent = exponent * 10 + value->text[index] - '0';
        }
    }
    exponent = sign * exponent - decimals;
    if (exponent > 400 || exponent < -1000) {
        return 0;
    }
    while (exponent > 0) {
        magnitude *= 10;
        --exponent;
    }
    while (exponent < 0) {
        magnitude /= 10;
        ++exponent;
    }

    union {
        double value;
        uint64_t bits;
    } result = {.value = magnitude};

    return (result.bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}

static void literal(Query *query, const JsonValue *value) {
    static const char hex[] = "0123456789abcdef";
    if (!value) {
        query->valid = 0;
        return;
    }
    if (value->kind == JSON_NULL) {
        append(query, "NULL");
    } else if (value->kind == JSON_BOOL) {
        append(query, value->text[0] == 't' ? "1" : "0");
    } else if (value->kind == JSON_NUMBER) {
        if (!number_valid(value)) {
            query->valid = 0;
            return;
        }
        append_n(query, value->text, value->length);
    } else if (value->kind == JSON_STRING) {
        append(query, "CAST(X'");
        for (size_t i = 0; i < value->length; ++i) {
            unsigned char ch = (unsigned char)value->text[i];
            char digits[] = {hex[ch >> 4], hex[ch & 15]};
            append_n(query, digits, 2);
        }
        append(query, "' AS TEXT)");
    } else if (value->kind == JSON_OBJECT && value->length == 1) {
        JsonValue *blob = addon_json_get(value, "blob_hex");
        if (!blob || blob->kind != JSON_STRING || blob->length % 2) {
            query->valid = 0;
            return;
        }
        for (size_t i = 0; i < blob->length; ++i) {
            unsigned char ch = (unsigned char)blob->text[i];
            if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                  (ch >= 'A' && ch <= 'F'))) {
                query->valid = 0;
                return;
            }
        }
        append(query, "X'");
        append_n(query, blob->text, blob->length);
        append(query, "'");
    } else {
        query->valid = 0;
    }
}

static int prepare(void *database, Query *query, void **statement) {
    const char *tail = NULL;
    *statement = NULL;
    return query->valid &&
           !sql.prepare(database, query->text, (int)query->length, statement, &tail) &&
           *statement && tail == query->text + query->length;
}

static int execute(void *database, Query *query) {
    void *statement = NULL;
    int valid = prepare(database, query, &statement) && sql.step(statement) == SQLITE_DONE;
    if (statement && sql.finalize(statement)) {
        valid = 0;
    }
    return valid;
}

static int command(void *database, Query *query, const char *text) {
    reset(query);
    append(query, text);
    return execute(database, query);
}

static int64_t count(void *database, Query *query) {
    void *statement = NULL;
    int64_t result = -1;
    if (prepare(database, query, &statement) && sql.step(statement) == SQLITE_ROW) {
        result = sql.column_int64(statement, 0);
        if (sql.step(statement) != SQLITE_DONE) {
            result = -1;
        }
    }
    if (statement && sql.finalize(statement)) {
        result = -1;
    }
    return result;
}

static int predicate(Query *query, const JsonValue *row, int typed) {
    if (!row || row->kind != JSON_OBJECT || !row->length) {
        return query->valid = 0;
    }
    for (JsonValue *field = row->child; field; field = field->next) {
        if (field != row->child) {
            append(query, " AND ");
        }
        name(query, field->name);
        append(query, " IS ");
        literal(query, field);
        if (typed) {
            append(query, " AND typeof(");
            name(query, field->name);
            append(query, ")=typeof(");
            literal(query, field);
            append(query, ")");
        }
    }
    return query->valid;
}

static int64_t matches(void *database, Query *query, const char *table, const JsonValue *row,
                       const JsonValue *expect) {
    reset(query);
    append(query, "SELECT count(*) FROM ");
    name(query, table);
    append(query, " WHERE ");
    predicate(query, row, 0);
    if (expect) {
        append(query, " AND ");
        predicate(query, expect, 1);
    }
    return count(database, query);
}

static int table_columns(void *database, Query *query, const char *table) {
    if (!table || text_length(table) < 1) {
        return -1;
    }
    if (text_length(table) >= 7) {
        char prefix[8];
        memory_copy(prefix, table, 7);
        prefix[7] = 0;
        if (text_equal_ignore_case(prefix, "sqlite_")) {
            return -1;
        }
    }
    reset(query);
    append(query, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name=");
    JsonValue label = {.kind = JSON_STRING, .text = (char *)table, .length = text_length(table)};
    literal(query, &label);
    if (count(database, query) != 1) {
        return -1;
    }
    reset(query);
    append(query, "SELECT * FROM ");
    name(query, table);
    append(query, " LIMIT 0");
    void *statement = NULL;
    int columns = -1;
    if (prepare(database, query, &statement)) {
        columns = sql.column_count(statement);
    }
    if (statement) {
        sql.finalize(statement);
    }
    return columns;
}

#include "addon_ids_plan.h"

static int shared_insert(const JsonValue *table,const JsonValue *row) {
    if(applying_shared_ids)return 1;
    for(unsigned i=0;i<ID_MODS;++i){
        if(!id_equal(id_labels[i],applying_addon))continue;
        if(id_policy[i]==2)return 1;
        for(IdSlot *p=id_slots;p;p=p->next){
            unsigned value;
            if(p->mod==i && p->shared && id_equal(p->table,table->name) &&
               id_number(id_field(row,p->column),&value) && value==p->assigned)return 1;
        }
    }
    return 0;
}

static int insert(void *database, Query *query, const JsonValue *table, const JsonValue *row,
                  const JsonValue *keys, int columns) {
    if (row->kind != JSON_OBJECT || row->length != (unsigned)columns) {
        return 0;
    }
    for (JsonValue *field = row->child; field; field = field->next) {
        for (JsonValue *prior = row->child; prior != field; prior = prior->next) {
            if (id_equal(field->name, prior->name)) {
                return 0;
            }
        }
    }
    JsonValue native_key = {.kind = JSON_STRING},
              native_keys = {.kind = JSON_ARRAY, .length = 1, .child = &native_key};
    if (!keys || !keys->length) {
        native_key.text = (char *)id_primary_key(database, query, table->name);
        if (native_key.text) {
            native_key.length = text_length(native_key.text);
            keys = &native_keys;
        }
    }
    if (keys && keys->length) {
        reset(query);
        append(query, "SELECT count(*) FROM ");
        name(query, table->name);
        append(query, " WHERE ");
        for (JsonValue *key = keys->child; key; key = key->next) {
            JsonValue *field = id_field(row, key->text);
            if (!field || field->kind == JSON_NULL) {
                return 0;
            }
            if (key != keys->child) {
                append(query, " AND ");
            }
            name(query, key->text);
            append(query, " IS ");
            literal(query, field);
        }
        int64_t existing = count(database, query);
        if (existing < 0) {
            return 0;
        }
        if (existing > 0) {
            if(existing==1 && shared_insert(table,row)){
                // UPDATE preserves the existing identity and foreign-key dependents;
                // SQLite REPLACE would delete the row and could cascade deletions.
                reset(query);append(query,"UPDATE ");name(query,table->name);append(query," SET ");
                for(JsonValue *field=row->child;field;field=field->next){
                    if(field!=row->child)append(query,",");
                    name(query,field->name);append(query,"=");literal(query,field);
                }
                append(query," WHERE ");
                size_t key_offset=query->length;
                for(JsonValue *key=keys->child;key;key=key->next){
                    if(key!=keys->child)append(query," AND ");
                    name(query,key->text);append(query," IS ");literal(query,id_field(row,key->text));
                }
                log_append("MODS: ");log_append(applying_addon);
                log_append(": shared ID override in ");log_append(table->name);
                log_append("; key: ");
                log_append(query->text+key_offset);
                log_append("; higher-priority row replaces earlier data.\r\n");
                return execute(database,query);
            }
            log_append("MODS: ");
            log_append(applying_addon ? applying_addon : "[database]");
            log_append(": ID conflict in ");
            log_append(table->name);
            log_append("; key: ");
            reset(query);
            for (JsonValue *key = keys->child; key; key = key->next) {
                if (key != keys->child) {
                    append(query, ", ");
                }
                append(query, key->text);
                append(query, "=");
                JsonValue *field = id_field(row, key->text);
                if (field->kind == JSON_STRING) {
                    append(query, "\"");
                    for (size_t i = 0; i < field->length; ++i) {
                        char c = field->text[i];
                        if (c == '\r') {
                            append(query, "\\r");
                        } else if (c == '\n') {
                            append(query, "\\n");
                        } else if (c == '\t') {
                            append(query, "\\t");
                        } else {
                            if (c == '"' || c == '\\') {
                                append(query, "\\");
                            }
                            append_n(query, &c, 1);
                        }
                    }
                    append(query, "\"");
                } else {
                    literal(query, field);
                }
            }
            log_append(query->text);
            log_append("\r\n");
            return 0;
        }
    }
    reset(query);
    append(query, "INSERT INTO ");
    name(query, table->name);
    append(query, " (");
    for (JsonValue *field = row->child; field; field = field->next) {
        if (field != row->child) {
            append(query, ",");
        }
        name(query, field->name);
    }
    append(query, ") VALUES (");
    for (JsonValue *field = row->child; field; field = field->next) {
        if (field != row->child) {
            append(query, ",");
        }
        literal(query, field);
    }
    append(query, ")");
    return execute(database, query);
}

static int mutate(void *database, Query *query, const JsonValue *table, const JsonValue *operation,
                  const JsonValue *keys, int strict, int remove, int columns) {
    JsonValue *where = addon_json_get(operation, "where"),
              *expect = addon_json_get(operation, "expect");
    JsonValue *set = addon_json_get(operation, "set");
    if (operation->kind != JSON_OBJECT || !keys || !keys->length || !where ||
        where->kind != JSON_OBJECT || where->length != keys->length ||
        (!remove && (!set || set->kind != JSON_OBJECT || !set->length)) ||
        (expect && (expect->kind != JSON_OBJECT || !expect->length)) || (strict && !expect)) {
        return 0;
    }
    for (JsonValue *key = keys->child; key; key = key->next) {
        JsonValue *field = addon_json_get(where, key->text);
        if (!field || field->kind == JSON_NULL || (!remove && addon_json_get(set, key->text))) {
            return 0;
        }
    }
    if (expect && !remove && strict) {
        if (expect->length != set->length) {
            return 0;
        }
        for (JsonValue *field = set->child; field; field = field->next) {
            if (!addon_json_get(expect, field->name)) {
                return 0;
            }
        }
    }
    if (strict && remove && expect->length != (unsigned)columns) {
        return 0;
    }
    if (matches(database, query, table->name, where, NULL) != 1 ||
        (strict && matches(database, query, table->name, where, expect) != 1)) {
        return 0;
    }
    reset(query);
    append(query, remove ? "DELETE FROM " : "UPDATE ");
    name(query, table->name);
    if (!remove) {
        append(query, " SET ");
        for (JsonValue *field = set->child; field; field = field->next) {
            if (field != set->child) {
                append(query, ",");
            }
            name(query, field->name);
            append(query, "=");
            literal(query, field);
        }
    }
    append(query, " WHERE ");
    predicate(query, where, 0);
    return execute(database, query);
}

static int keys_valid(const JsonValue *keys) {
    if (!keys) {
        return 1;
    }
    if (keys->kind != JSON_ARRAY) {
        return 0;
    }
    for (JsonValue *key = keys->child; key; key = key->next) {
        if (key->kind != JSON_STRING || !key->length || text_length(key->text) != key->length) {
            return 0;
        }
        for (JsonValue *previous = keys->child; previous != key; previous = previous->next) {
            if (text_equal_ignore_case(previous->text, key->text)) {
                return 0;
            }
        }
    }
    return 1;
}

typedef struct {
    char *parent, *from[16], *to[16];
    unsigned columns;
    int64_t id;
} ForeignRule;

static const ForeignRule empty_rule;

static char *copy_column(void *statement, int column) {
    const char *text = sql.column_text(statement, column);
    if (!text || text_length(text) > 255) {
        return NULL;
    }
    size_t size = text_length(text) + 1;
    char *copy = HeapAlloc(GetProcessHeap(), 0, size);
    if (copy) {
        memory_copy(copy, text, size);
    }
    return copy;
}

static void free_rule(ForeignRule *rule) {
    if (rule->parent) {
        HeapFree(GetProcessHeap(), 0, rule->parent);
    }
    for (unsigned i = 0; i < rule->columns; ++i) {
        if (rule->from[i]) {
            HeapFree(GetProcessHeap(), 0, rule->from[i]);
        }
        if (rule->to[i]) {
            HeapFree(GetProcessHeap(), 0, rule->to[i]);
        }
    }
    memory_copy(rule, &empty_rule, sizeof(*rule));
}

static int check_rule(void *database, Query *query, const char *table, ForeignRule *rule,
                      uint64_t hashes[256], unsigned *size) {
    if (!rule->columns || !rule->parent) {
        return 0;
    }
    for (unsigned i = 0; i < rule->columns; ++i) {
        if (rule->to[i]) {
            continue;
        }
        // FH2 names its parent columns. Also accept implicit single-column PKs.
        if (rule->columns != 1) {
            return 0;
        }
        reset(query);
        append(query, "PRAGMA table_info(");
        name(query, rule->parent);
        append(query, ")");
        void *info = NULL;
        if (!prepare(database, query, &info)) {
            if (info) {
                sql.finalize(info);
            }
            return 0;
        }
        int result, found = 0;
        while ((result = sql.step(info)) == SQLITE_ROW) {
            if (sql.column_int64(info, 5)) {
                ++found;
                if (found == 1) {
                    rule->to[i] = copy_column(info, 1);
                }
            }
        }
        sql.finalize(info);
        if (found != 1 || !rule->to[i] || result != SQLITE_DONE) {
            return 0;
        }
    }
    reset(query);
    append(query, "SELECT c.rowid FROM ");
    name(query, table);
    append(query, " AS c WHERE ");
    for (unsigned i = 0; i < rule->columns; ++i) {
        if (i) {
            append(query, " AND ");
        }
        append(query, "c.");
        name(query, rule->from[i]);
        append(query, " IS NOT NULL");
    }
    append(query, " AND NOT EXISTS (SELECT 1 FROM ");
    name(query, rule->parent);
    append(query, " AS p WHERE ");
    for (unsigned i = 0; i < rule->columns; ++i) {
        if (i) {
            append(query, " AND ");
        }
        append(query, "p.");
        name(query, rule->to[i]);
        append(query, "=c.");
        name(query, rule->from[i]);
    }
    append(query, ")");
    void *statement = NULL;
    if (!prepare(database, query, &statement)) {
        if (statement) {
            sql.finalize(statement);
        }
        return 0;
    }
    int result;
    while ((result = sql.step(statement)) == SQLITE_ROW && *size < 256) {
        uint64_t hash = UINT64_C(14695981039346656037);
        for (const unsigned char *p = (const unsigned char *)table; *p; ++p) {
            hash = (hash ^ *p) * UINT64_C(1099511628211);
        }
        hash = (hash ^ 255) * UINT64_C(1099511628211);
        for (const unsigned char *p = (const unsigned char *)rule->parent; *p; ++p) {
            hash = (hash ^ *p) * UINT64_C(1099511628211);
        }
        uint64_t fields[] = {(uint64_t)rule->id, (uint64_t)sql.column_int64(statement, 0)};
        for (unsigned i = 0; i < sizeof(fields); ++i) {
            hash = (hash ^ ((const unsigned char *)fields)[i]) * UINT64_C(1099511628211);
        }
        hashes[(*size)++] = hash;
    }
    int finalized = sql.finalize(statement);
    return result == SQLITE_DONE && !finalized;
}

static int foreign_keys(void *database, Query *query, uint64_t hashes[256], unsigned *size) {
    // SQLite 3.6.19 has foreign_key_list but no foreign_key_check pragma.
    reset(query);
    append(query, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
    void *tables = NULL;
    *size = 0;
    if (!prepare(database, query, &tables)) {
        if (tables) {
            sql.finalize(tables);
        }
        return 0;
    }
    int result, valid = 1;
    while (valid && (result = sql.step(tables)) == SQLITE_ROW) {
        const char *table = sql.column_text(tables, 0);
        if (!table) {
            valid = 0;
            break;
        }
        reset(query);
        append(query, "PRAGMA foreign_key_list(");
        name(query, table);
        append(query, ")");
        void *list = NULL;
        if (!prepare(database, query, &list)) {
            if (list) {
                sql.finalize(list);
            }
            valid = 0;
            break;
        }
        ForeignRule rule;
        memory_copy(&rule, &empty_rule, sizeof(rule));
        int row;
        while ((row = sql.step(list)) == SQLITE_ROW) {
            int64_t id = sql.column_int64(list, 0);
            if (rule.columns && id != rule.id) {
                valid = check_rule(database, query, table, &rule, hashes, size);
                free_rule(&rule);
                if (!valid) {
                    break;
                }
            }
            if (rule.columns == 16 || sql.column_int64(list, 1) != rule.columns) {
                valid = 0;
                break;
            }
            rule.id = id;
            if (!rule.columns) {
                rule.parent = copy_column(list, 2);
            }
            unsigned index = rule.columns++;
            rule.from[index] = copy_column(list, 3);
            rule.to[index] = copy_column(list, 4);
            if (!rule.from[index] || !rule.parent) {
                valid = 0;
                break;
            }
        }
        if (valid && row != SQLITE_DONE) {
            valid = 0;
        }
        if (valid && rule.columns) {
            valid = check_rule(database, query, table, &rule, hashes, size);
        }
        free_rule(&rule);
        if (sql.finalize(list)) {
            valid = 0;
        }
    }
    if (valid && result != SQLITE_DONE) {
        valid = 0;
    }
    if (sql.finalize(tables)) {
        valid = 0;
    }
    return valid;
}

static int apply_rows(void *database, Query *query, const JsonValue *document, unsigned version,
                      int strict, char *error) {
    JsonValue *requirements = addon_json_get(document, "requires");
    JsonValue *records = requirements ? addon_json_get(requirements, "records") : NULL;
    if ((requirements && requirements->kind != JSON_OBJECT) ||
        (records && records->kind != JSON_OBJECT)) {
        return reject(error, "invalid requires.records");
    }
    for (JsonValue *table = records ? records->child : NULL; table; table = table->next) {
        if (table->kind != JSON_ARRAY || table_columns(database, query, table->name) < 0) {
            return reject(error, "unknown required table");
        }
        for (JsonValue *row = table->child; row; row = row->next) {
            if (matches(database, query, table->name, row, NULL) != 1) {
                return reject(error, "required database record missing or ambiguous");
            }
        }
    }
    JsonValue *tables = addon_json_get(document, "tables");
    if (!tables || tables->kind != JSON_OBJECT) {
        return reject(error, "tables must be an object");
    }
    for (JsonValue *table = tables->child; table; table = table->next) {
        int columns = table_columns(database, query, table->name);
        JsonValue *keys = addon_json_get(table, "key");
        if (table->kind != JSON_OBJECT || columns <= 0 || !keys_valid(keys) ||
            (version == 2 && !keys)) {
            return reject(error, "unknown table or invalid/missing key");
        }
        for (JsonValue *operation = table->child; operation; operation = operation->next) {
            if (!equal(operation->name, "key") && !equal(operation->name, "insert") &&
                !equal(operation->name, "update") && !equal(operation->name, "delete")) {
                return reject(error, "unknown table operation");
            }
        }
        static const char *const operations[] = {"delete", "update", "insert"};
        for (unsigned index = 0; index < ARRAY_COUNT(operations); ++index) {
            JsonValue *rows = addon_json_get(table, operations[index]);
            if (!rows) {
                continue;
            }
            if (rows->kind != JSON_ARRAY || (version == 1 && index != 2)) {
                return reject(error, "invalid operation array/format version");
            }
            for (JsonValue *row = rows->child; row; row = row->next) {
                int success = index == 2 ? insert(database, query, table, row, keys, columns)
                                         : mutate(database, query, table, row, keys, strict,
                                                  index == 0, columns);
                if (!success) {
                    reject(error,
                           "conflict, invalid value or database operation failed in table: ");
                    size_t used = text_length(error), length = text_length(table->name);
                    if (length > 255 - used) {
                        length = 255 - used;
                    }
                    memory_copy(error + used, table->name, length);
                    error[used + length] = 0;
                    return 0;
                }
            }
        }
    }
    return 1;
}

int addon_database_apply(void *database, const JsonValue *document, const char *base_hash,
                         const char *addon_id, char error[256]) {
    error[0] = 0;
    applying_addon = addon_id;
    applying_shared_ids=0;
    unsigned version;
    if (!document || document->kind != JSON_OBJECT ||
        !addon_json_uint(addon_json_get(document, "format_version"), &version) ||
        (version != 1 && version != 2)) {
        return reject(error, "unsupported database JSON format (expected 1 or 2)");
    }
    JsonValue *target = addon_json_get(document, "target"),
              *path = addon_json_get(target, "database");
    if (!addon_json_is(path, "media/stripped/gamedbRC.slt")) {
        return reject(error, "unsupported target database");
    }
    JsonValue *hash = addon_json_get(target, "base_sha256");
    if (hash && hash->kind != JSON_STRING) {
        return reject(error, "target base_sha256 must be a string");
    }
    if (hash && (!base_hash || !addon_json_is(hash, base_hash))) {
        log_append("MODS: ");
        log_append(addon_id);
        log_append(": WARNING: base database SHA-256 does not match addon target; "
                   "continuing with database validation. Expected: ");
        log_append(hash->text);
        log_append("; actual: ");
        log_append(base_hash ? base_hash : "unavailable");
        log_append("\r\n");
    }
    JsonValue *policy = addon_json_get(document, "policy"),
              *conflicts = addon_json_get(policy, "update_conflicts");
    JsonValue *id_conflicts=addon_json_get(policy,"id_conflicts");
    if(!id_conflicts)id_conflicts=addon_json_get(document,"id_conflicts");
    applying_shared_ids=addon_json_is(id_conflicts,"last_wins");
    if ((policy && policy->kind != JSON_OBJECT) ||
        (conflicts && !addon_json_is(conflicts, "error") &&
         !addon_json_is(conflicts, "last_wins"))) {
        return reject(error, "invalid update conflict policy");
    }
    JsonValue *foreign_policy=addon_json_get(policy,"foreign_keys");
    if(foreign_policy && !addon_json_is(foreign_policy,"error") && !addon_json_is(foreign_policy,"allow"))
        return reject(error,"invalid foreign-key policy (expected error or allow)");
    int allow_foreign_keys=addon_json_is(foreign_policy,"allow");
    if(allow_foreign_keys){
        log_append("MODS: ");log_append(addon_id);
        log_append(": policy.foreign_keys=allow; preserving declared references as-is. "
                   "Operation and SQLite integrity checks remain enabled.\r\n");
    }
    int strict = !addon_json_is(conflicts, "last_wins");
    Query query = {.text = HeapAlloc(GetProcessHeap(), 0, SQL_CAPACITY), .valid = 1};
    uint64_t *baseline = HeapAlloc(GetProcessHeap(), 0, 512 * sizeof(uint64_t));
    if (!query.text || !baseline) {
        if (query.text) {
            HeapFree(GetProcessHeap(), 0, query.text);
        }
        if (baseline) {
            HeapFree(GetProcessHeap(), 0, baseline);
        }
        return reject(error, "out of memory");
    }
    unsigned before = 0, after = 0;
    int success = (allow_foreign_keys || foreign_keys(database, &query, baseline, &before)) &&
                  command(database, &query, "SAVEPOINT fh2_addon");
    int started = success;
    if (success) {
        success = apply_rows(database, &query, document, version, strict, error);
    }
    if (success && !allow_foreign_keys) {
        success = foreign_keys(database, &query, baseline + 256, &after);
    }
    for (unsigned i = 0; success && i < after; ++i) {
        unsigned found = before;
        for (unsigned j = 0; j < before; ++j) {
            if (baseline[j] == baseline[256 + i]) {
                found = j;
                break;
            }
        }
        if (found == before) {
            reject(error,"addon introduces declared foreign-key violations (policy.foreign_keys=error)");
            success = 0;
            break;
        }
        baseline[found] = baseline[--before];
    }
    if (success) {
        reset(&query);
        append(&query, "PRAGMA quick_check");
        void *statement = NULL;
        success = prepare(database, &query, &statement) && sql.step(statement) == SQLITE_ROW;
        const char *check = success ? sql.column_text(statement, 0) : NULL;
        success = success && check && equal(check, "ok") && sql.step(statement) == SQLITE_DONE;
        if (statement && sql.finalize(statement)) {
            success = 0;
        }
    }
    if (success) {
        success = command(database, &query, "RELEASE fh2_addon");
    }
    if (!success && started) {
        command(database, &query, "ROLLBACK TO fh2_addon");
        command(database, &query, "RELEASE fh2_addon");
    }
    if (!success && !error[0]) {
        reject(error, "addon transaction/integrity/foreign-key validation failed");
    }
    HeapFree(GetProcessHeap(), 0, baseline);
    HeapFree(GetProcessHeap(), 0, query.text);
    return success;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestAddonSqlite(const AddonSqlite *api) {
    memory_copy(&sql, api, sizeof(sql));
}

__declspec(dllexport) int Fh2TestAddonApply(void *database, const char *text, size_t length,
                                            const char *hash, const char *id, char *error) {
    char *copy = HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (!copy) {
        return 0;
    }
    memory_copy(copy, text, length);
    copy[length] = 0;
    JsonDocument document;
    int valid = addon_json_parse(&document, copy, length);
    int result = valid ? addon_database_apply(database, document.root, hash, id, error)
                       : reject(error, document.error);
    addon_json_free(&document);
    return result;
}
#endif
