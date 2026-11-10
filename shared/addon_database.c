#include "shared/boot_diagnostics.h"
#include "shared/addon_database.h"
#include "shared/addon_cache_io.h"
#include "shared/support.h"
#include "shared/logging.h"

enum { SQL_CAPACITY = 2 * 1024 * 1024, SQLITE_ROW = 100, SQLITE_DONE = 101 };

static AddonSqlite sql;
static const char *applying_addon;
static int applying_shared_ids;
static const JsonValue *validation_scope;
static const char *target_database="media/stripped/gamedbRC.slt";

typedef struct {
    char *text;
    size_t length;
    int valid;
    const char **fields;
    unsigned field_capacity;
} Query;

void addon_database_bind(unsigned char *image) {
    addon_cache_io_bind(image,0);
    target_database="media/stripped/gamedbRC.slt";
    sql = (AddonSqlite){
        (void *)(image + 0x7d6c0), (void *)(image + 0x7af20), (void *)(image + 0x3d710),
        (void *)(image + 0x1eee0), (void *)(image + 0x1ab40), (void *)(image + 0x203b0),
        (void *)(image + 0x20690), (void *)(image + 0x206e0), (void *)(image + 0xe740),
        (void *)(image + 0xec30),  (void *)(image + 0x18810)};
}

void addon_database_bind_profile(const AddonSqlite *api,const char *target){
    memory_copy(&sql,api,sizeof(sql));target_database=target;
}

const AddonSqlite *addon_database_api(void) {
    return &sql;
}

static void *clone_impl(void *source) {
    void *copy = NULL;
    if (sql.open(":memory:", &copy)) {
        if (copy) {
            sql.close(copy);
        }
        return NULL;
    }
    // SQLite 3.6 cannot copy a different page size into an in-memory target.
    // Set it before the first target page is allocated (custom bases may differ).
    void *page=NULL;
    if(!sql.prepare(source,"PRAGMA page_size",-1,&page,NULL)){
        if(sql.step(page)==100){
            unsigned size=(unsigned)sql.column_int64(page,0);
            char query[48]="PRAGMA page_size=";unsigned at=17;char digits[12];unsigned n=0;
            do{digits[n++]=(char)('0'+size%10);size/=10;}while(size && n<12);
            while(n)query[at++]=digits[--n];query[at]=0;void *setting=NULL;
            if(!sql.prepare(copy,query,-1,&setting,NULL)){sql.step(setting);sql.finalize(setting);}
        }
        sql.finalize(page);
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

void *addon_database_clone(void *source){
    uint64_t begin=boot_diag_now();void *result=clone_impl(source);
    boot_diag_end("database.clone",NULL,begin);return result;
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

#include "shared/addon_ids_plan.h"

static int shared_insert(const JsonValue *table,const JsonValue *row) {
    if(applying_shared_ids)return 1;
    for(unsigned i=0;i<ID_MODS;++i){
        if(!id_equal(id_labels[i],applying_addon))continue;
        if(id_policy[i]==2)return 1;
        for(IdSlot *p=id_mod_slots[i];p;p=p->mod_next){
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
    // Reuse bounded scratch storage across rows, including the game's very wide
    // torque curves (250 columns). Previously each row compared every column
    // with all earlier columns. Hash collisions still get a full name comparison.
    unsigned capacity=16;
    while(capacity<row->length*2 && capacity<8192)capacity*=2;
    if(capacity>=row->length*2 && capacity>query->field_capacity){
        const char **fields=HeapAlloc(GetProcessHeap(),0,capacity*sizeof(*fields));
        if(fields){
            if(query->fields)HeapFree(GetProcessHeap(),0,query->fields);
            query->fields=fields;query->field_capacity=capacity;
        }
    }
    if(query->field_capacity>=row->length*2){
        for(unsigned i=0;i<query->field_capacity;++i)query->fields[i]=NULL;
        for(JsonValue *field=row->child;field;field=field->next){
            unsigned at=id_bucket("",field->name,0)&(query->field_capacity-1);
            while(query->fields[at]){
                if(id_equal(query->fields[at],field->name))return 0;
                at=(at+1)&(query->field_capacity-1);
            }
            query->fields[at]=field->name;
        }
    }else{
        // Allocation failure or unusually wide custom schemas retain validation.
        for(JsonValue *field=row->child;field;field=field->next)
            for(JsonValue *prior=row->child;prior!=field;prior=prior->next)
                if(id_equal(field->name,prior->name))return 0;
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

static int columns_changed(const char *table,char *const *columns,unsigned count){
    if(!validation_scope)return 1;
    const JsonValue *tables=addon_json_get(validation_scope,"tables");
    for(const JsonValue *p=tables?tables->child:NULL;p;p=p->next){
        if(!text_equal_ignore_case(p->name,table))continue;
        const JsonValue *inserted=addon_json_get(p,"insert"),*deleted=addon_json_get(p,"delete");
        if((inserted && inserted->child)||(deleted && deleted->child))return 1;
        const JsonValue *updates=addon_json_get(p,"update");
        for(const JsonValue *row=updates?updates->child:NULL;row;row=row->next){
            const JsonValue *set=addon_json_get(row,"set");
            for(const JsonValue *field=set?set->child:NULL;field;field=field->next){
                for(unsigned i=0;i<count;i++)if(!columns[i] || text_equal_ignore_case(field->name,columns[i]))return 1;
                // Rowid participates in violation identity even when no FK column changes.
                if(text_equal_ignore_case(field->name,"rowid") || text_equal_ignore_case(field->name,"_rowid_") || text_equal_ignore_case(field->name,"oid"))return 1;
            }
        }
    }
    return 0;
}

static int check_rule(void *database, Query *query, const char *table, ForeignRule *rule,
                      uint64_t hashes[256], unsigned *size) {
    if (!rule->columns || !rule->parent) {
        return 0;
    }
    if(!columns_changed(table,rule->from,rule->columns) && !columns_changed(rule->parent,rule->to,rule->columns))return 1;
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
        uint64_t table_begin=boot_diag_now();
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
        boot_diag_end("database.apply_table",table->name,table_begin);
    }
    return 1;
}

static int apply_database(void *database, const JsonValue *document, const char *base_hash,
                         const char *addon_id, char error[256],int defer_integrity,int defer_foreign) {
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
    if (!addon_json_is(path, target_database)) {
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
    uint64_t *baseline = defer_foreign?NULL:HeapAlloc(GetProcessHeap(), 0, 512 * sizeof(uint64_t));
    if (!query.text || (!defer_foreign && !baseline)) {
        if (query.text) {
            HeapFree(GetProcessHeap(), 0, query.text);
        }
        if (baseline) {
            HeapFree(GetProcessHeap(), 0, baseline);
        }
        return reject(error, "out of memory");
    }
    // Data-only overrides cannot alter unrelated relations. Triggers can do so,
    // therefore databases containing any trigger retain the full FK scan.
    if(!defer_foreign){
        reset(&query);append(&query,"SELECT count(*) FROM sqlite_master WHERE type='trigger'");
        validation_scope=count(database,&query)==0?document:NULL;
    }
    unsigned before = 0, after = 0;
    uint64_t phase_begin=boot_diag_now();
    int success = (defer_foreign || allow_foreign_keys || foreign_keys(database, &query, baseline, &before)) &&
                  command(database, &query, "SAVEPOINT fh2_addon");
    boot_diag_end(defer_foreign?"database.begin_mod_transaction":"database.foreign_keys_before",addon_id,phase_begin);
    int started = success;
    if (success) {
        phase_begin=boot_diag_now();
        success = apply_rows(database, &query, document, version, strict, error);
        boot_diag_end("database.apply_rows",addon_id,phase_begin);
    }
    if (success && !defer_foreign && !allow_foreign_keys) {
        phase_begin=boot_diag_now();
        success = foreign_keys(database, &query, baseline + 256, &after);
        boot_diag_end("database.foreign_keys_after",addon_id,phase_begin);
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
    if (success && !defer_integrity) {
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
    if(baseline)HeapFree(GetProcessHeap(), 0, baseline);
    if(query.fields)HeapFree(GetProcessHeap(),0,query.fields);
    HeapFree(GetProcessHeap(), 0, query.text);
    return success;
}

int addon_database_apply(void *database,const JsonValue *document,const char *hash,
                         const char *id,char error[256]){
    return apply_database(database,document,hash,id,error,0,0);
}
int addon_database_apply_queued(void *database,const JsonValue *document,const char *hash,
                                const char *id,char error[256],int defer_foreign){
    uint64_t begin=boot_diag_now();int result=apply_database(database,document,hash,id,error,1,defer_foreign);
    boot_diag_end("database.mod_transaction",id,begin);return result;
}
static int foreign_snapshot_impl(void *database,AddonForeignBaseline *baseline){
    Query query={.text=HeapAlloc(GetProcessHeap(),0,SQL_CAPACITY),.valid=1};
    baseline->count=0;validation_scope=NULL;
    int valid=query.text && foreign_keys(database,&query,baseline->hashes,&baseline->count);
    if(query.text)HeapFree(GetProcessHeap(),0,query.text);
    return valid;
}
int addon_database_foreign_snapshot(void *database,AddonForeignBaseline *baseline){
    uint64_t begin=boot_diag_now();int valid=foreign_snapshot_impl(database,baseline);
    boot_diag_end("database.foreign_keys_batch_baseline",valid?"ready":"per_mod",begin);return valid;
}
int addon_database_foreign_unchanged(void *database,const AddonForeignBaseline *baseline){
    uint64_t begin=boot_diag_now();AddonForeignBaseline after;
    int valid=foreign_snapshot_impl(database,&after);
    unsigned char matched[256];for(unsigned i=0;i<256;++i)matched[i]=0;
    for(unsigned i=0;valid && i<after.count;++i){
        unsigned j=0;while(j<baseline->count && (matched[j] || after.hashes[i]!=baseline->hashes[j]))++j;
        if(j==baseline->count)valid=0;else matched[j]=1;
    }
    boot_diag_end("database.foreign_keys_batch_final",valid?"passed":"retry_per_mod",begin);return valid;
}
static int database_check_impl(void *database){
    void *statement=NULL;
    int ok=!sql.prepare(database,"PRAGMA quick_check",-1,&statement,NULL) &&
           sql.step(statement)==SQLITE_ROW;
    const char *text=ok?sql.column_text(statement,0):NULL;
    ok=ok && text && equal(text,"ok") && sql.step(statement)==SQLITE_DONE;
    if(statement && sql.finalize(statement))ok=0;
    return ok;
}

int addon_database_check(void *database){
    uint64_t begin=boot_diag_now();int result=database_check_impl(database);
    boot_diag_end("database.final_integrity_check",NULL,begin);return result;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fh2TestIdLookup(unsigned mod,const char *table,const char *column,unsigned value){
    return addon_ids_lookup(mod,table,column,value);
}
__declspec(dllexport) unsigned Fh2TestIdCarbinValue(unsigned mod,unsigned part,unsigned value){
    return addon_ids_carbin_value((void *)(uintptr_t)mod,part,value);
}
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
