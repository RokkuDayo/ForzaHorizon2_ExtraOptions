// Included by addon_database.c: one load-order plan over a private database.
enum { ID_MODS = 128, ID_LIMIT = 32768 };

typedef struct IdMemory {
    struct IdMemory *next;
} IdMemory;

typedef struct IdEdit {
    struct IdEdit *next;
    JsonValue *value;
    char *text;
    size_t length;
} IdEdit;

typedef struct IdSlot {
    struct IdSlot *next;
    const char *table, *column;
    unsigned mod, old, assigned;
    struct IdSlot *leader;
    int shared;
} IdSlot;

typedef struct IdLink {
    struct IdLink *next;
    const char *table, *column, *parent, *key;
} IdLink;

typedef struct IdDomain {
    struct IdDomain *next;
    const char *table, *column;
    unsigned mod, old, assigned;
} IdDomain;

static IdMemory *id_memory;
static IdEdit *id_edits;
static IdSlot *id_slots;
static IdLink *id_links;
static IdDomain *id_domains;
static unsigned char id_blocked[ID_MODS];
static unsigned char id_policy[ID_MODS]; // 0 error, 1 nearest, 2 shared/last_wins
static const char *id_labels[ID_MODS];
static unsigned id_nodes;
static int id_failed;

static void *id_alloc(size_t bytes, IdMemory **owner) {
    IdMemory *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) + bytes);
    if (!p) {
        id_failed = 1;
        return NULL;
    }
    p->next = *owner;
    *owner = p;
    return p + 1;
}

void addon_ids_free_temporary(void *temporary) {
    IdMemory *p = temporary;
    while (p) {
        IdMemory *next = p->next;
        HeapFree(GetProcessHeap(), 0, p);
        p = next;
    }
}

void addon_ids_reset(void) {
    for (IdEdit *p = id_edits; p; p = p->next) {
        p->value->text = p->text;
        p->value->length = p->length;
    }
    addon_ids_free_temporary(id_memory);
    id_memory = NULL;
    id_edits = NULL;
    id_slots = NULL;
    id_links = NULL;
    id_domains = NULL;
    for (unsigned i = 0; i < ID_MODS; ++i) {
        id_blocked[i] = 0;
        id_policy[i] = 0;
        id_labels[i] = NULL;
    }
    id_nodes = 0;
    id_failed = 0;
}

int addon_ids_blocked(unsigned index) {
    return index >= ID_MODS || id_blocked[index];
}

int addon_ids_changed(int index) {
    for (IdDomain *p = id_domains; p; p = p->next) {
        if (index < 0 || p->mod == (unsigned)index) {
            return 1;
        }
    }
    return 0;
}

static char *id_string(const char *text) {
    if (!text || text_length(text) > 255) {
        id_failed = 1;
        return NULL;
    }
    size_t bytes = text_length(text) + 1;
    char *p = id_alloc(bytes, &id_memory);
    if (p) {
        memory_copy(p, text, bytes);
    }
    return p;
}

static int id_equal(const char *a, const char *b) {
    return a && b && text_equal_ignore_case(a, b);
}

static JsonValue *id_field(const JsonValue *object, const char *column) {
    if (!object || object->kind != JSON_OBJECT) {
        return NULL;
    }
    JsonValue *found = NULL;
    for (JsonValue *p = object->child; p; p = p->next) {
        if (id_equal(p->name, column)) {
            if (found) {
                return NULL;
            }
            found = p;
        }
    }
    return found;
}

static int id_number(const JsonValue *value, unsigned *out) {
    if (!value || (value->kind != JSON_NUMBER && value->kind != JSON_STRING)) {
        return 0;
    }
    JsonValue numeric = *value;
    numeric.kind = JSON_NUMBER;
    return addon_json_uint(&numeric, out) && *out > 0 && *out <= INT32_MAX;
}

static char *id_digits(unsigned value, IdMemory **memory) {
    char reverse[16];
    unsigned size = 0;
    do {
        reverse[size++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    char *p = id_alloc(size + 1, memory);
    if (p) {
        for (unsigned i = 0; i < size; ++i) {
            p[i] = reverse[size - i - 1];
        }
    }
    return p;
}

static int id_replace(JsonValue *value, unsigned assigned, IdMemory **memory, int remember) {
    char *text = id_digits(assigned, memory);
    if (!text) {
        return 0;
    }
    if (remember) {
        IdEdit *p = id_alloc(sizeof(*p), &id_memory);
        if (!p) {
            return 0;
        }
        *p = (IdEdit){id_edits, value, value->text, value->length};
        id_edits = p;
    }
    value->text = text;
    value->length = text_length(text);
    return 1;
}

static void id_log(const char *label, const char *table, const char *key, unsigned old,
                   const char *action, unsigned assigned) {
    char a[16], b[16];
    unsigned n = 0, m = 0, v = old;
    do {
        a[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    log_append("MODS IDS: ");
    log_append(label ? label : "[mod]");
    log_append(" | ");
    log_append(table);
    log_append(".");
    log_append(key);
    log_append("=");
    while (n) {
        char one[2] = {a[--n], 0};
        log_append(one);
    }
    log_append(" | ");
    log_append(action);
    if (assigned) {
        v = assigned;
        do {
            b[m++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        log_append(" -> ");
        while (m) {
            char one[2] = {b[--m], 0};
            log_append(one);
        }
    }
    log_append("\r\n");
}

void addon_ids_committed(unsigned index) {
    for (IdSlot *p = id_slots; p; p = p->next) {
        if (p->mod == index && !id_blocked[index] && p->old != p->assigned) {
            id_log(id_labels[index], p->table, p->column, p->old, "committed ID reassignment",
                   p->assigned);
        }
    }
}

static int id_column_exists(void *database, Query *q, const char *table, const char *column) {
    reset(q);
    append(q, "PRAGMA table_info(");
    name(q, table);
    append(q, ")");
    void *s = NULL;
    int valid = prepare(database, q, &s), found = 0, row = SQLITE_DONE;
    while (valid && (row = sql.step(s)) == SQLITE_ROW) {
        found |= id_equal(column, sql.column_text(s, 1));
    }
    if (s) {
        sql.finalize(s);
    }
    return valid && row == SQLITE_DONE && found;
}

static const char *id_primary_key(void *database, Query *q, const char *table) {
    reset(q);
    append(q, "PRAGMA table_info(");
    name(q, table);
    append(q, ")");
    void *s = NULL;
    if (!prepare(database, q, &s)) {
        if (s) {
            sql.finalize(s);
        }
        return NULL;
    }
    const char *key = NULL;
    unsigned keys = 0;
    int result;
    while ((result = sql.step(s)) == SQLITE_ROW) {
        if (sql.column_int64(s, 5)) {
            ++keys;
            key = id_string(sql.column_text(s, 1));
        }
    }
    sql.finalize(s);
    return result == SQLITE_DONE && keys == 1 ? key : NULL;
}

static int id_exists(void *database, Query *q, const char *table, const char *key, unsigned value) {
    char number[16];
    unsigned n = 0, v = value;
    do {
        number[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    reset(q);
    append(q, "SELECT count(*) FROM ");
    name(q, table);
    append(q, " WHERE ");
    name(q, key);
    append(q, "=");
    while (n) {
        append_n(q, &number[--n], 1);
    }
    int64_t result = count(database, q);
    if (result < 0) {
        id_failed = 1;
    }
    return result != 0;
}

static int id_reserved(const IdSlot *slot, unsigned value) {
    for (IdSlot *p = id_slots; p; p = p->next) {
        if (id_blocked[p->mod] || !id_equal(p->table, slot->table) ||
            !id_equal(p->column, slot->column)) {
            continue;
        }
        if (p->old == value || p->assigned == value) {
            return 1;
        }
    }
    return 0;
}

static IdSlot *id_root(IdSlot *slot) {
    while (slot->leader != slot) {
        slot = slot->leader;
    }
    return slot;
}

static int id_group_free(void *database, Query *q, IdSlot *leader, unsigned value) {
    for (IdSlot *p = id_slots; p; p = p->next) {
        if (id_root(p) == leader) {
            if (id_reserved(p, value) || id_exists(database, q, p->table, p->column, value)) {
                return 0;
            }
        }
    }
    return 1;
}

static int id_add_link(const char *table, const char *column, const char *parent, const char *key) {
    for (IdLink *p = id_links; p; p = p->next) {
        if (id_equal(table, p->table) && id_equal(column, p->column) &&
            id_equal(parent, p->parent) && id_equal(key, p->key)) {
            return 1;
        }
    }
    if (++id_nodes > ID_LIMIT) {
        return 0;
    }
    IdLink *p = id_alloc(sizeof(*p), &id_memory);
    if (!p) {
        return 0;
    }
    p->table = id_string(table);
    p->column = id_string(column);
    p->parent = id_string(parent);
    p->key = id_string(key);
    p->next = id_links;
    id_links = p;
    return !id_failed;
}

static int id_schema_links(void *database, Query *q) {
    reset(q);
    append(q, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
    void *tables = NULL;
    if (!prepare(database, q, &tables)) {
        if (tables) {
            sql.finalize(tables);
        }
        return 0;
    }
    int result, valid = 1;
    while (valid && (result = sql.step(tables)) == SQLITE_ROW) {
        const char *table = id_string(sql.column_text(tables, 0));
        if (!table) {
            valid = 0;
            break;
        }
        reset(q);
        append(q, "PRAGMA foreign_key_list(");
        name(q, table);
        append(q, ")");
        void *list = NULL;
        valid = prepare(database, q, &list);
        int row = SQLITE_DONE;
        while (valid && (row = sql.step(list)) == SQLITE_ROW) {
            const char *parent = id_string(sql.column_text(list, 2)),
                       *from = id_string(sql.column_text(list, 3));
            const char *target = sql.column_text(list, 4);
            const char *to = target ? id_string(target) : id_primary_key(database, q, parent);
            if (!parent || !from || !to) {
                valid = 0;
            } else {
                valid = id_add_link(table, from, parent, to);
            }
        }
        if (list && sql.finalize(list)) {
            valid = 0;
        }
        if (row != SQLITE_DONE) {
            valid = 0;
        }
    }
    if (sql.finalize(tables)) {
        valid = 0;
    }
    return valid && result == SQLITE_DONE && !id_failed;
}

static int id_add_domain(unsigned mod, const char *table, const char *column, unsigned old,
                         unsigned assigned) {
    for (IdDomain *p = id_domains; p; p = p->next) {
        if (p->mod == mod && p->old == old && id_equal(p->table, table) &&
            id_equal(p->column, column)) {
            if (p->assigned != assigned) {
                id_blocked[mod] = 1;
                id_log(id_labels[mod], table, column, old,
                       "ambiguous shared identity; mod rejected rather than guessing", 0);
            }
            return 0;
        }
    }
    if (++id_nodes > ID_LIMIT) {
        return -1;
    }
    IdDomain *p = id_alloc(sizeof(*p), &id_memory);
    if (!p) {
        return -1;
    }
    *p = (IdDomain){id_domains, table, column, mod, old, assigned};
    id_domains = p;
    return 1;
}

static int id_game_links(void *database, Query *q) {
    // FH2 omits SQL FOREIGN KEY declarations on several established ID fields.
    static const struct {
        const char *column, *parent, *key;
    } known[] = {{"Ordinal", "Data_Car", "Id"},
                 {"CarID", "Data_Car", "Id"},
                 {"EngineID", "Data_Engine", "EngineID"},
                 {"CarBodyID", "Data_CarBody", "Id"},
                 {"MakeID", "List_CarMake", "ID"},
                 {"ManufacturerID", "List_PartManufacturer", "Id"},
                 {"PartManufacturerID", "List_PartManufacturer", "Id"},
                 {"StockWheelID", "List_Wheels", "ID"},
                 {"WheelID", "List_Wheels", "ID"},
                 {"WheelStyleID", "List_WheelStyle", "WheelStyleID"}};

    int available[ARRAY_COUNT(known)];
    for (unsigned i = 0; i < ARRAY_COUNT(known); ++i) {
        available[i] = id_column_exists(database, q, known[i].parent, known[i].key);
    }
    reset(q);
    append(q, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
    void *tables = NULL;
    if (!prepare(database, q, &tables)) {
        if (tables) {
            sql.finalize(tables);
        }
        return 0;
    }
    int result, valid = 1;
    while (valid && (result = sql.step(tables)) == SQLITE_ROW) {
        const char *table = id_string(sql.column_text(tables, 0));
        if (!table) {
            valid = 0;
            break;
        }
        reset(q);
        append(q, "PRAGMA table_info(");
        name(q, table);
        append(q, ")");
        void *columns = NULL;
        valid = prepare(database, q, &columns);
        int row = SQLITE_DONE;
        while (valid && (row = sql.step(columns)) == SQLITE_ROW) {
            const char *column = sql.column_text(columns, 1);
            for (unsigned i = 0; i < ARRAY_COUNT(known); ++i) {
                if (available[i] && id_equal(column, known[i].column) &&
                    !(id_equal(table, known[i].parent) && id_equal(column, known[i].key))) {
                    valid = id_add_link(table, column, known[i].parent, known[i].key);
                }
            }
        }
        if (columns && sql.finalize(columns)) {
            valid = 0;
        }
        if (row != SQLITE_DONE) {
            valid = 0;
        }
    }
    if (sql.finalize(tables)) {
        valid = 0;
    }
    return valid && result == SQLITE_DONE;
}

unsigned addon_ids_lookup(unsigned mod, const char *table, const char *column, unsigned value) {
    for (IdDomain *p = id_domains; p; p = p->next) {
        if (p->mod == mod && p->old == value && id_equal(p->table, table) &&
            id_equal(p->column, column)) {
            return p->assigned;
        }
    }
    return value;
}

static int id_rewrite_row(unsigned mod, const char *table, JsonValue *row) {
    if (!row || row->kind != JSON_OBJECT) {
        return 1;
    }
    for (JsonValue *v = row->child; v; v = v->next) {
        unsigned old;
        if (!id_number(v, &old)) {
            continue;
        }
        unsigned assigned = addon_ids_lookup(mod, table, v->name, old);
        if (assigned != old && !id_replace(v, assigned, &id_memory, 1)) {
            return 0;
        }
    }
    return 1;
}

int addon_ids_plan(void *database, JsonValue *documents[128], const char *labels[128],
                   const unsigned char excluded[128], unsigned total) {
    addon_ids_reset();
    if (total > ID_MODS) {
        return 0;
    }
    for (unsigned i = 0; i < total; ++i) {
        id_labels[i] = labels[i];
    }
    Query q = {.text = HeapAlloc(GetProcessHeap(), 0, SQL_CAPACITY), .valid = 1};
    if (!q.text) {
        return 0;
    }
    unsigned char automatic[ID_MODS] = {0};
    int valid = 1;
    // Reserve all requested keys before allocating replacements, including later mods.
    for (unsigned i = 0; i < total && valid; ++i) {
        if (excluded[i] || !documents[i]) {
            continue;
        }
        JsonValue *group = addon_json_get(documents[i], "policy");
        JsonValue *policy = addon_json_get(group, "id_conflicts");
        JsonValue *legacy = addon_json_get(documents[i], "id_conflicts");
        if ((group && group->kind != JSON_OBJECT) ||
            (policy && legacy && (policy->kind != JSON_STRING || !addon_json_is(legacy, policy->text)))) {
            id_blocked[i] = 1;
            log_append("MODS IDS: invalid policy object or contradictory legacy id_conflicts.\r\n");
            continue;
        }
        if (!policy) policy = legacy; // Backward compatibility with older addons.
        if (policy && !addon_json_is(policy, "error") && !addon_json_is(policy, "nearest") &&
            !addon_json_is(policy, "last_wins")) {
            id_blocked[i] = 1;
            log_append("MODS IDS: invalid policy.id_conflicts; expected error, nearest or last_wins.\r\n");
            continue;
        }
        automatic[i] = addon_json_is(policy, "nearest");
        id_policy[i] = addon_json_is(policy,"last_wins")?2:automatic[i];
        JsonValue *tables = addon_json_get(documents[i], "tables");
        if (!tables || tables->kind != JSON_OBJECT) {
            continue;
        }
        for (JsonValue *t = tables->child; t && valid; t = t->next) {
            const char *key = id_primary_key(database, &q, t->name);
            if (!key) {
                continue;
            }
            JsonValue *rows = addon_json_get(t, "insert");
            for (JsonValue *r = rows && rows->kind == JSON_ARRAY ? rows->child : NULL; r;
                 r = r->next) {
                unsigned value;
                if (!id_number(id_field(r, key), &value)) {
                    continue;
                }
                if (++id_nodes > ID_LIMIT) {
                    valid = 0;
                    break;
                }
                for (IdSlot *p = id_slots; p; p = p->next) {
                    if (p->mod == i && p->old == value && id_equal(p->table, t->name)) {
                        id_blocked[i] = 1;
                        id_log(labels[i], t->name, key, value,
                               "duplicate insert ID inside the same mod; rejected", 0);
                    }
                }
                IdSlot *p = id_alloc(sizeof(*p), &id_memory);
                if (!p) {
                    valid = 0;
                    break;
                }
                *p = (IdSlot){id_slots, t->name, key, i, value, value, p, 0};
                id_slots = p;
            }
        }
    }
    int automatic_plan = 0;
    for (unsigned i = 0; i < total; ++i) {
        automatic_plan |= automatic[i] && !excluded[i] && !id_blocked[i];
    }
    if (valid && automatic_plan) {
        valid = id_schema_links(database, &q) && id_game_links(database, &q);
    }
    // Explicit declarations supplement relationships absent from the game's schema.
    for (unsigned i = 0; i < total && valid; ++i) {
        if (excluded[i] || id_blocked[i] || !documents[i]) {
            continue;
        }
        JsonValue *links = addon_json_get(documents[i], "id_references");
        if (links && links->kind != JSON_ARRAY) {
            id_blocked[i] = 1;
            continue;
        }
        for (JsonValue *p = links ? links->child : NULL; p; p = p->next) {
            JsonValue *t = addon_json_get(p, "table"), *c = addon_json_get(p, "column"),
                      *parent = addon_json_get(p, "target_table"),
                      *key = addon_json_get(p, "target_column");
            if (!t || t->kind != JSON_STRING || !c || c->kind != JSON_STRING || !parent ||
                parent->kind != JSON_STRING || !key || key->kind != JSON_STRING ||
                !id_column_exists(database, &q, t->text, c->text) ||
                !id_column_exists(database, &q, parent->text, key->text)) {
                id_blocked[i] = 1;
                break;
            }
            // Declarations are shared schema facts, not numeric replacement rules.
            if (!id_add_link(t->text, c->text, parent->text, key->text)) {
                valid = 0;
                break;
            }
        }
    }
    // A PK that also references another new row is the same identity, not a
    // separately allocatable number (e.g. CarListFilters.CarID -> Data_Car.Id).
    for (IdSlot *p = id_slots; valid && p; p = p->next) {
        for (IdLink *link = id_links; link; link = link->next) {
            if (id_equal(p->table, link->table) && id_equal(p->column, link->column)) {
                for (IdSlot *parent = id_slots; parent; parent = parent->next) {
                    if (parent->mod == p->mod && parent->old == p->old &&
                        id_equal(parent->table, link->parent) &&
                        id_equal(parent->column, link->key)) {
                        IdSlot *a = id_root(p), *b = id_root(parent);
                        if (a != b) {
                            a->leader = b;
                        }
                    }
                }
            }
        }
    }
    // Resolve sharing before nearest allocation. A higher-priority contributor
    // may otherwise move away from a base ID before the lower contributor's
    // explicit sharing policy is visited.
    for(IdSlot *p=id_slots;p;p=p->next){
        if(id_policy[p->mod]!=2 || id_blocked[p->mod] || excluded[p->mod])continue;
        p->shared=1;
        for(IdSlot *other=id_slots;other;other=other->next)
            if(!id_blocked[other->mod] && !excluded[other->mod] && other->old==p->old &&
               id_equal(other->table,p->table) && id_equal(other->column,p->column))other->shared=1;
    }
    for (unsigned priority = total; priority && valid; --priority) {
        unsigned i = priority - 1;
        if (excluded[i] || id_blocked[i]) {
            continue;
        }
        for (IdSlot *p = id_slots; p && valid; p = p->next) {
            if (p->mod != i || id_root(p) != p) {
                continue;
            }
            const char *winner = NULL;
            int vanilla = 0;
            int shared = id_policy[i]==2 || p->shared;
            IdSlot *conflict = p;
            for (IdSlot *member = id_slots; member; member = member->next) {
                if (id_root(member) == p) {
                    shared |= member->shared;
                    if (id_exists(database, &q, member->table, member->column, member->old)) {
                        vanilla = 1;
                        conflict = member;
                    }
                    for (IdSlot *later = id_slots; later; later = later->next) {
                        if (later->mod > i && !id_blocked[later->mod] && !excluded[later->mod] &&
                            later->old == member->old && id_equal(member->table, later->table) &&
                            id_equal(member->column, later->column)) {
                            if(!winner)winner = labels[later->mod];
                            shared |= id_policy[later->mod]==2 || later->shared;
                            conflict = member;
                        }
                    }
                }
            }
            if (!vanilla && !winner) {
                continue;
            }
            id_log(labels[i], conflict->table, conflict->column, conflict->old,
                   vanilla ? "ID conflict with base database"
                           : "ID conflict with higher-priority mod",
                   0);
            if (winner) {
                log_append("MODS IDS: higher-priority owner: ");
                log_append(winner);
                log_append("\r\n");
            }
            if(shared){
                // Either contributor may explicitly opt into a common identity.
                // Do not remap references; the ascending transaction order makes
                // the highest successful contributor supply the final row.
                for(IdSlot *member=id_slots;member;member=member->next){
                    if(id_root(member)!=p)continue;
                    member->shared=1;
                    for(IdSlot *later=id_slots;later;later=later->next)
                        if(later->mod>i && !id_blocked[later->mod] && !excluded[later->mod] &&
                           later->old==member->old && id_equal(later->table,member->table) &&
                           id_equal(later->column,member->column))later->shared=1;
                }
                id_log(labels[i],conflict->table,conflict->column,conflict->old,
                       "shared ID; highest-priority successful row wins",0);
                continue;
            }
            if (!automatic[i]) {
                id_blocked[i] = 1;
                break;
            }
            unsigned chosen = 0;
            // Positive signed 32-bit IDs, nearest distance; higher ID wins a tie.
            for (unsigned delta = 1; delta <= ID_LIMIT && !chosen && !id_failed; ++delta) {
                unsigned candidates[2] = {p->old <= INT32_MAX - delta ? p->old + delta : 0,
                                          p->old > delta ? p->old - delta : 0};
                for (unsigned side = 0; side < 2; ++side) {
                    if (candidates[side] && id_group_free(database, &q, p, candidates[side])) {
                        chosen = candidates[side];
                        break;
                    }
                }
            }
            if (!chosen) {
                id_log(labels[i], p->table, p->column, p->old,
                       "no free ID within supported search range; mod rejected", 0);
                id_blocked[i] = 1;
                break;
            }
            for (IdSlot *member = id_slots; member; member = member->next) {
                if (id_root(member) == p) {
                    member->assigned = chosen;
                    id_log(labels[i], member->table, member->column, member->old,
                           "planned nearest ID", chosen);
                }
            }
        }
    }
    for (IdSlot *p = id_slots; p && valid; p = p->next) {
        if (!id_blocked[p->mod] && !excluded[p->mod] && p->old != p->assigned) {
            valid = id_add_domain(p->mod, p->table, p->column, p->old, p->assigned) >= 0;
        }
    }
    int changed = 1;
    while (valid && changed) {
        changed = 0;
        for (IdDomain *p = id_domains; p && valid; p = p->next) {
            for (IdLink *link = id_links; link; link = link->next) {
                if (!id_blocked[p->mod] && id_equal(p->table, link->parent) &&
                    id_equal(p->column, link->key)) {
                    int added =
                        id_add_domain(p->mod, link->table, link->column, p->old, p->assigned);
                    if (added < 0) {
                        valid = 0;
                        break;
                    }
                    changed |= added;
                }
            }
        }
    }
    for (unsigned i = 0; i < total && valid; ++i) {
        if (excluded[i] || id_blocked[i] || !documents[i]) {
            continue;
        }
        JsonValue *tables = addon_json_get(documents[i], "tables");
        for (JsonValue *t = tables && tables->kind == JSON_OBJECT ? tables->child : NULL;
             t && valid; t = t->next) {
            JsonValue *rows = addon_json_get(t, "insert");
            for (JsonValue *r = rows && rows->kind == JSON_ARRAY ? rows->child : NULL; r && valid;
                 r = r->next) {
                valid = id_rewrite_row(i, t->name, r);
            }
            rows = addon_json_get(t, "update");
            for (JsonValue *r = rows && rows->kind == JSON_ARRAY ? rows->child : NULL; r && valid;
                 r = r->next) {
                // Targets/expectations refer to existing rows, not this mod's new IDs.
                valid = id_rewrite_row(i, t->name, addon_json_get(r, "set"));
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, q.text);
    return valid && !id_failed;
}

static const struct {
    unsigned part;
    const char *table;
} id_carbin_tables[] = {{0, "List_UpgradeEngine"},
                        {1, "List_UpgradeDrivetrain"},
                        {2, "List_UpgradeCarBody"},
                        {3, "List_UpgradeBrakes"},
                        {4, "List_UpgradeSpringDamper"},
                        {6, "List_UpgradeAntiSwayFront"},
                        {7, "List_UpgradeAntiSwayRear"},
                        {8, "List_UpgradeTireCompound"},
                        {9, "List_UpgradeRearWing"},
                        {10, "List_UpgradeRimSizeFront"},
                        {11, "List_UpgradeRimSizeRear"},
                        {12, "List_UpgradeEngineCamshaft"},
                        {13, "List_UpgradeEngineValves"},
                        {14, "List_UpgradeEngineDisplacement"},
                        {15, "List_UpgradeEnginePistonsCompression"},
                        {16, "List_UpgradeEngineFuelSystem"},
                        {17, "List_UpgradeEngineIgnition"},
                        {18, "List_UpgradeEngineExhaust"},
                        {19, "List_UpgradeEngineIntake"},
                        {20, "List_UpgradeEngineFlywheel"},
                        {21, "List_UpgradeEngineManifold"},
                        {22, "List_UpgradeEngineRestrictorPlate"},
                        {23, "List_UpgradeEngineOilCooling"},
                        {24, "List_UpgradeEngineIntercooler"},
                        {25, "List_UpgradeEngineTurboTwin"},
                        {26, "List_UpgradeEngineTurboSingle"},
                        {27, "List_UpgradeEngineCSC"},
                        {28, "List_UpgradeEngineDSC"},
                        {29, "List_UpgradeDrivetrainTransmission"},
                        {30, "List_UpgradeDrivetrainClutch"},
                        {32, "List_UpgradeDrivetrainDriveline"},
                        {33, "List_UpgradeDrivetrainDifferential"},
                        {34, "List_UpgradeCarBodyFrontBumper"},
                        {35, "List_UpgradeCarBodyRearBumper"},
                        {36, "List_UpgradeCarBodyHood"},
                        {37, "List_UpgradeCarBodySideSkirt"},
                        {38, "List_UpgradeCarBodyTireWidthFront"},
                        {39, "List_UpgradeCarBodyTireWidthRear"},
                        {40, "List_UpgradeCarBodyWeight"},
                        {41, "List_UpgradeCarBodyChassisStiffness"},
                        {42, "List_UpgradeMotor"},
                        {43, "List_Wheels"}};

unsigned addon_ids_carbin_value(void *context, unsigned part, unsigned value) {
    unsigned index = (unsigned)(uintptr_t)context;
    if (part == UINT32_MAX) {
        return addon_ids_lookup(index, "Data_Car", "Id", value);
    }
    if (part == UINT32_MAX - 1) {
        return addon_ids_lookup(index, "Data_CarBody", "Id", value);
    }
    for (unsigned i = 0; i < ARRAY_COUNT(id_carbin_tables); ++i) {
        if (id_carbin_tables[i].part == part) {
            return addon_ids_lookup(index, id_carbin_tables[i].table, "Id", value);
        }
    }
    return value;
}

static int id_carbin_field(unsigned index, unsigned part, JsonValue *value, IdMemory **memory) {
    unsigned old;
    if (!id_number(value, &old)) {
        return 1;
    }
    unsigned assigned = addon_ids_carbin_value((void *)(uintptr_t)index, part, old);
    return assigned == old || id_replace(value, assigned, memory, 0);
}

static int id_carbin_models(unsigned index, unsigned part, JsonValue *models, IdMemory **memory,
                            int operations) {
    for (JsonValue *m = models && models->kind == JSON_ARRAY ? models->child : NULL; m;
         m = m->next) {
        JsonValue *value = operations ? addon_json_get(m, "value") : m;
        JsonValue *ids = addon_json_get(value, "upgrade_ids");
        for (JsonValue *v = ids && ids->kind == JSON_ARRAY ? ids->child : NULL; v; v = v->next) {
            if (!id_carbin_field(index, part, v, memory)) {
                return 0;
            }
        }
    }
    return 1;
}

int addon_ids_carbin_json(unsigned index, JsonValue *document, void **temporary) {
    IdMemory **memory = (IdMemory **)temporary;
    JsonValue *groups = addon_json_get(document, "groups");
    for (JsonValue *g = groups && groups->kind == JSON_ARRAY ? groups->child : NULL; g;
         g = g->next) {
        unsigned part;
        if (!addon_json_uint(addon_json_get(g, "key"), &part)) {
            continue;
        }
        JsonValue *value = addon_json_get(g, "value");
        int whole = value != NULL;
        if (!value) {
            value = g;
        }
        JsonValue *upgrades = addon_json_get(value, "upgrades");
        for (JsonValue *u = upgrades && upgrades->kind == JSON_ARRAY ? upgrades->child : NULL; u;
             u = u->next) {
            if (!whole && !id_carbin_field(index, part, addon_json_get(u, "key"), memory)) {
                return 0;
            }
            JsonValue *record = whole ? u : addon_json_get(u, "value");
            if (!id_carbin_field(index, part, addon_json_get(record, "id"), memory) ||
                !id_carbin_field(index, UINT32_MAX - 1, addon_json_get(record, "car_body_id"),
                                 memory)) {
                return 0;
            }
        }
        if (!id_carbin_models(index, part, addon_json_get(value, "models"), memory, !whole)) {
            return 0;
        }
    }
    // ordinal remains the source identity while applying a diff; the final
    // compiled binary's ordinal is remapped after all patches have merged.
    return 1;
}
