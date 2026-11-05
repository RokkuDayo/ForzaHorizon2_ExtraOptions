#include "carbin_patch.h"
#include "support.h"

enum { LIMIT = 32 * 1024 * 1024, RECORDS = 4096, NODES = 32768 };

typedef struct Allocation {
    struct Allocation *next;
} Allocation;

typedef struct {
    Allocation *allocations;
    const char *error;
    unsigned nodes;
    size_t bytes;
} Context;

typedef struct {
    const unsigned char *data;
    size_t size;
} Slice;

typedef struct {
    Context *context;
    Slice input;
    size_t at;
} Reader;

typedef struct {
    Context *context;
    unsigned char *data;
    size_t size, capacity;
} Writer;

typedef struct Node {
    struct Node *next;
    Slice raw, prefix, tail;
    unsigned key;
    unsigned char guid[16];
    struct Node *a, *b;
} Node;

enum { PART, GROUP, MODEL, SHARED, UPGRADE };

static void fail(Context *c, const char *message) {
    if (!c->error) {
        c->error = message;
    }
}

static void fields(Context *c, const JsonValue *d, const char *allowed) {
    if (!d || d->kind != JSON_OBJECT) {
        fail(c, "expected JSON object");
        return;
    }
    for (const JsonValue *v = d->child; v; v = v->next) {
        if (text_equal_ignore_case(v->name, "_comment")) {
            continue;
        }
        size_t length = text_length(v->name);
        int found = 0;
        const char *p = allowed;
        while (*p) {
            const char *start = p;
            while (*p && *p != ',') {
                ++p;
            }
            if ((size_t)(p - start) == length && memory_equal(start, v->name, length)) {
                found = 1;
            }
            if (*p) {
                ++p;
            }
        }
        if (!found) {
            fail(c, "unknown JSON field (check spelling)");
        }
    }
}

static void *arena(Context *c, size_t size) {
    if (c->error || size > LIMIT || c->bytes + size > LIMIT * 3u) {
        fail(c, "allocation limit exceeded");
        return NULL;
    }
    Allocation *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a) + size);
    if (!a) {
        fail(c, "allocation failed");
        return NULL;
    }
    a->next = c->allocations;
    c->allocations = a;
    c->bytes += size;
    return a + 1;
}

static Slice take(Reader *r, size_t size) {
    if (r->context->error || size > r->input.size - r->at) {
        fail(r->context, "truncated or invalid FH2 carbin");
        return (Slice){0};
    }
    Slice s = {r->input.data + r->at, size};
    r->at += size;
    return s;
}

static unsigned integer(Reader *r, unsigned bytes) {
    Slice s = take(r, bytes);
    unsigned n = 0;
    if (s.data) {
        for (unsigned i = 0; i < bytes; ++i) {
            n |= (unsigned)s.data[i] << (i * 8);
        }
    }
    return n;
}

static Slice string(Reader *r) {
    unsigned n = integer(r, 4);
    return take(r, n);
}

static unsigned count(Reader *r) {
    unsigned n = integer(r, 4);
    if (n > RECORDS) {
        fail(r->context, "too many carbin records");
        return 0;
    }
    return n;
}

static void write(Writer *w, const void *data, size_t size) {
    if (w->context->error) {
        return;
    }
    if (size > LIMIT - w->size) {
        fail(w->context, "carbin exceeds 32 MiB");
        return;
    }
    if (w->size + size > w->capacity) {
        size_t cap = w->capacity ? w->capacity : 1024;
        while (cap < w->size + size) {
            cap *= 2;
        }
        void *p = HeapAlloc(GetProcessHeap(), 0, cap);
        if (!p) {
            fail(w->context, "allocation failed");
            return;
        }
        if (w->data) {
            memory_copy(p, w->data, w->size);
            HeapFree(GetProcessHeap(), 0, w->data);
        }
        w->data = p;
        w->capacity = cap;
    }
    if (size) {
        memory_copy(w->data + w->size, data, size);
    }
    w->size += size;
}

static void put(Writer *w, unsigned n, unsigned size) {
    unsigned char bytes[4];
    for (unsigned i = 0; i < size; ++i) {
        bytes[i] = (unsigned char)(n >> (i * 8));
    }
    write(w, bytes, size);
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int hex_decode(const JsonValue *v, unsigned char *out, size_t size) {
    if (!v || v->kind != JSON_STRING || v->length != size * 2) {
        return 0;
    }
    for (size_t i = 0; i < size; ++i) {
        int a = hex_digit(v->text[i * 2]), b = hex_digit(v->text[i * 2 + 1]);
        if (a < 0 || b < 0) {
            return 0;
        }
        if (out) {
            out[i] = (unsigned char)(a * 16 + b);
        }
    }
    return 1;
}

static int number(const JsonValue *v, unsigned size, int sign, unsigned *out) {
    if (!v || v->kind != JSON_NUMBER || !v->length) {
        return 0;
    }
    size_t at = 0;
    int negative = v->text[0] == '-';
    if (negative) {
        if (!sign) {
            return 0;
        }
        ++at;
    }
    uint64_t n = 0, max = sign ? (negative ? 0x80000000u : 0x7fffffffu)
                               : (size == 4 ? 0xffffffffu : (1u << (size * 8)) - 1);
    if (at == v->length) {
        return 0;
    }
    for (; at < v->length; ++at) {
        char ch = v->text[at];
        if (ch < '0' || ch > '9') {
            return 0;
        }
        n = n * 10 + (unsigned)(ch - '0');
        if (n > max) {
            return 0;
        }
    }
    *out = negative ? 0u - (unsigned)n : (unsigned)n;
    return 1;
}

static unsigned jnum(Writer *w, const JsonValue *d, const char *key, unsigned size, int sign) {
    unsigned n = 0;
    if (!number(addon_json_get(d, key), size, sign, &n)) {
        fail(w->context, "missing or invalid numeric field");
    }
    put(w, n, size);
    return n;
}

static void jstr(Writer *w, const JsonValue *d, const char *key) {
    const JsonValue *v = addon_json_get(d, key);
    if (!v || v->kind != JSON_STRING || v->length > LIMIT ||
        (v->length &&
         !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v->text, (int)v->length, NULL, 0))) {
        fail(w->context, "missing or invalid UTF-8 field");
        return;
    }
    for (size_t i = 0; i < v->length; ++i) {
        if (!v->text[i]) {
            fail(w->context, "embedded NUL in carbin string");
            return;
        }
    }
    put(w, (unsigned)v->length, 4);
    write(w, v->text, v->length);
}

static void jhex(Writer *w, const JsonValue *d, const char *key, size_t size) {
    const JsonValue *v = addon_json_get(d, key);
    if (!hex_decode(v, NULL, size)) {
        fail(w->context, "missing or invalid hexadecimal field");
        return;
    }
    for (size_t i = 0; i < size; ++i) {
        unsigned char b =
            (unsigned char)(hex_digit(v->text[2 * i]) * 16 + hex_digit(v->text[2 * i + 1]));
        write(w, &b, 1);
    }
}

static const JsonValue *jarray(Writer *w, const JsonValue *d, const char *key) {
    const JsonValue *v = addon_json_get(d, key);
    if (!v || v->kind != JSON_ARRAY || v->length > RECORDS) {
        fail(w->context, "missing or invalid record array");
        return NULL;
    }
    put(w, (unsigned)v->length, 4);
    return v->child;
}

// Bounds checked FH2 v15 model scanner; opaque Bundle/float payloads are retained.
static void scan_model(Reader *r, Node *node) {
    if (integer(r, 2) != 15) {
        fail(r->context, "unsupported render model version (expected FH2 v15)");
        return;
    }
    string(r);
    take(r, 64);
    take(r, 2);
    string(r);
    take(r, 2 + 1 + 4);
    unsigned n = count(r);
    for (unsigned i = 0; i < n && !r->context->error; ++i) {
        string(r);
        unsigned len = integer(r, 4);
        take(r, len);
    }
    n = count(r);
    for (unsigned i = 0; i < n && !r->context->error; ++i) {
        string(r);
        take(r, 4);
    }
    unsigned hash = integer(r, 1);
    if (hash > 1) {
        fail(r->context, "invalid model hash flag");
    }
    if (hash) {
        take(r, 8);
    }
    take(r, 4);
    n = count(r);
    for (unsigned i = 0; i < n && !r->context->error; ++i) {
        unsigned v = integer(r, 2);
        if (v < 1 || v > 3) {
            fail(r->context, "unsupported AO map version");
            break;
        }
        string(r);
        take(r, 8 + (v >= 2 ? 16 : 3) + 1 + (v >= 3 ? 2 : 0));
    }
    take(r, 1 + 18);
    string(r);
    Slice guid = take(r, 16);
    if (guid.data) {
        memory_copy(node->guid, guid.data, 16);
    }
    take(r, 20 + 4);
}

static Node *read_node(Reader *r, int kind);

static int same_key(const Node *a, const Node *b, int kind) {
    return (kind == MODEL || kind == SHARED) ? memory_equal(a->guid, b->guid, 16)
                                             : a->key == b->key;
}

static Node *read_list(Reader *r, int kind) {
    unsigned n = count(r);
    Node *head = NULL, **end = &head;
    for (unsigned i = 0; i < n && !r->context->error; ++i) {
        Node *node = read_node(r, kind);
        if (!node) {
            break;
        }
        for (Node *p = head; p; p = p->next) {
            if (same_key(p, node, kind)) {
                fail(r->context, "ambiguous duplicate record identity");
            }
        }
        *end = node;
        end = &node->next;
    }
    return head;
}

static Node *read_node(Reader *r, int kind) {
    Context *c = r->context;
    if (++c->nodes > NODES) {
        fail(c, "too many total carbin records");
        return NULL;
    }
    Node *node = arena(c, sizeof(*node));
    if (!node) {
        return NULL;
    }
    size_t start = r->at;
    if (kind == PART) {
        take(r, 1);
        unsigned v = integer(r, 2);
        if (v < 1 || v > 2) {
            fail(c, "unsupported part version");
        }
        node->key = integer(r, 4);
        node->prefix = (Slice){r->input.data + start, r->at - start};
        node->a = read_list(r, MODEL);
        node->tail = take(r, v >= 2 ? 32 : 0);
    } else if (kind == GROUP) {
        if (integer(r, 2) != 3) {
            fail(c, "unsupported upgrade group version");
        }
        node->key = integer(r, 4);
        node->prefix = (Slice){r->input.data + start, r->at - start};
        node->a = read_list(r, UPGRADE);
        node->b = read_list(r, SHARED);
    } else if (kind == UPGRADE) {
        unsigned v = integer(r, 2);
        if (v < 1 || v > 3) {
            fail(c, "unsupported upgrade version");
        }
        take(r, 2);
        node->key = integer(r, 4);
        take(r, 5);
        if (v >= 2) {
            take(r, 32);
        }
    } else {
        if (kind == SHARED) {
            unsigned n = count(r);
            take(r, (size_t)n * 4);
        }
        scan_model(r, node);
    }
    node->raw = (Slice){r->input.data + start, r->at - start};
    return node;
}

static void encode_model(Writer *w, const JsonValue *d) {
    fields(w->context, d,
           "version,path,transform_hex,lod,bone_name,bone_index,flags,draw_groups,material_"
           "overrides,material_indices,has_hash,hash_hex,unknown8_hex,ao_maps,unknown10_hex,"
           "unknown11_hex,part_name,guid,unknown14_hex,unknown15_hex,upgrade_ids");
    if (jnum(w, d, "version", 2, 0) != 15) {
        fail(w->context, "expected FH2 render model v15");
    }
    jstr(w, d, "path");
    jhex(w, d, "transform_hex", 64);
    jnum(w, d, "lod", 2, 0);
    jstr(w, d, "bone_name");
    jnum(w, d, "bone_index", 2, 0);
    jnum(w, d, "flags", 1, 0);
    jnum(w, d, "draw_groups", 4, 1);
    for (const JsonValue *m = jarray(w, d, "material_overrides"); m && !w->context->error;
         m = m->next) {
        fields(w->context, m, "name,data_hex");
        jstr(w, m, "name");
        const JsonValue *v = addon_json_get(m, "data_hex");
        if (!v || v->kind != JSON_STRING || v->length % 2) {
            fail(w->context, "invalid material Bundle");
            break;
        }
        put(w, (unsigned)(v->length / 2), 4);
        jhex(w, m, "data_hex", v->length / 2);
    }
    for (const JsonValue *m = jarray(w, d, "material_indices"); m && !w->context->error;
         m = m->next) {
        fields(w->context, m, "name,value");
        jstr(w, m, "name");
        jnum(w, m, "value", 4, 0);
    }
    unsigned hash = jnum(w, d, "has_hash", 1, 0);
    if (hash > 1) {
        fail(w->context, "has_hash must be 0 or 1");
    }
    jhex(w, d, "hash_hex", hash ? 8 : 0);
    jhex(w, d, "unknown8_hex", 4);
    for (const JsonValue *m = jarray(w, d, "ao_maps"); m && !w->context->error; m = m->next) {
        fields(w->context, m, "version,path,data_hex");
        unsigned v = jnum(w, m, "version", 2, 0);
        if (v < 1 || v > 3) {
            fail(w->context, "unsupported AO map version");
        }
        jstr(w, m, "path");
        jhex(w, m, "data_hex", 8 + (v >= 2 ? 16 : 3) + 1 + (v >= 3 ? 2 : 0));
    }
    jhex(w, d, "unknown10_hex", 1);
    jhex(w, d, "unknown11_hex", 18);
    jstr(w, d, "part_name");
    jhex(w, d, "guid", 16);
    jhex(w, d, "unknown14_hex", 20);
    jhex(w, d, "unknown15_hex", 4);
}

static void encode_record(Writer *w, const JsonValue *d, int kind);

static void encode_array(Writer *w, const JsonValue *d, const char *key, int kind) {
    for (const JsonValue *p = jarray(w, d, key); p && !w->context->error; p = p->next) {
        encode_record(w, p, kind);
    }
}

static void encode_record(Writer *w, const JsonValue *d, int kind) {
    if (!d || d->kind != JSON_OBJECT) {
        fail(w->context, "expected record object");
        return;
    }
    if (kind == PART || kind == GROUP) {
        fields(w->context, d,
               kind == PART ? "dispatch,version,part_type,models,data_hex"
                            : "version,part_type,upgrades,models");
        if (kind == PART) {
            jnum(w, d, "dispatch", 1, 0);
        }
        unsigned v = jnum(w, d, "version", 2, 0);
        if ((kind == PART && (v < 1 || v > 2)) || (kind == GROUP && v != 3)) {
            fail(w->context, "unsupported part/group version");
        }
        jnum(w, d, "part_type", 4, 0);
        if (kind == GROUP) {
            encode_array(w, d, "upgrades", UPGRADE);
        }
        encode_array(w, d, "models", kind == PART ? MODEL : SHARED);
        if (kind == PART) {
            jhex(w, d, "data_hex", v >= 2 ? 32 : 0);
        }
    } else if (kind == UPGRADE) {
        fields(w->context, d, "version,level,is_stock,id,car_body_id,parent_is_stock,data_hex");
        unsigned v = jnum(w, d, "version", 2, 0);
        if (v < 1 || v > 3) {
            fail(w->context, "unsupported upgrade version");
        }
        jnum(w, d, "level", 1, 0);
        jnum(w, d, "is_stock", 1, 0);
        jnum(w, d, "id", 4, 1);
        jnum(w, d, "car_body_id", 4, 1);
        jnum(w, d, "parent_is_stock", 1, 0);
        jhex(w, d, "data_hex", v >= 2 ? 32 : 0);
    } else {
        if (kind == SHARED) {
            for (const JsonValue *p = jarray(w, d, "upgrade_ids"); p && !w->context->error;
                 p = p->next) {
                unsigned n;
                if (!number(p, 4, 0, &n)) {
                    fail(w->context, "invalid shared upgrade ID");
                    break;
                }
                put(w, n, 4);
            }
        }
        encode_model(w, d);
    }
}

static Node *compile(Context *c, const JsonValue *d, int kind) {
    Writer w = {.context = c};
    encode_record(&w, d, kind);
    Node *node = NULL;
    if (!c->error) {
        void *data = arena(c, w.size);
        if (data) {
            memory_copy(data, w.data, w.size);
            Reader r = {c, {data, w.size}, 0};
            node = read_node(&r, kind);
            if (r.at != w.size) {
                fail(c, "invalid compiled record");
            }
        }
    }
    if (w.data) {
        HeapFree(GetProcessHeap(), 0, w.data);
    }
    return node;
}

static int operation_key(const JsonValue *op, int kind, Node *key) {
    const JsonValue *v = addon_json_get(op, "key");
    return (kind == MODEL || kind == SHARED) ? hex_decode(v, key->guid, 16)
                                             : number(v, 4, kind == UPGRADE, &key->key);
}

static void merge(Context *c, Node **head, const JsonValue *ops, int kind) {
    if (!ops) {
        return;
    }
    if (ops->kind != JSON_ARRAY || ops->length > RECORDS) {
        fail(c, "invalid operation list");
        return;
    }
    for (const JsonValue *op = ops->child; op && !c->error; op = op->next) {
        Node key = {0};
        if (!operation_key(op, kind, &key)) {
            fail(c, "invalid operation key");
            break;
        }
        for (const JsonValue *prior = ops->child; prior != op; prior = prior->next) {
            Node k = {0};
            if (operation_key(prior, kind, &k) && same_key(&key, &k, kind)) {
                fail(c, "duplicate operation key");
            }
        }
        Node **slot = head;
        while (*slot && !same_key(*slot, &key, kind)) {
            slot = &(*slot)->next;
        }
        const JsonValue *action = addon_json_get(op, "op");
        if (addon_json_is(action, "remove")) {
            fields(c, op, "op,key");
            if (*slot) {
                *slot = (*slot)->next;
            }
        } else if (addon_json_is(action, "upsert")) {
            fields(c, op, "op,key,value");
            Node *replacement = compile(c, addon_json_get(op, "value"), kind);
            if (!replacement || !same_key(replacement, &key, kind)) {
                fail(c, "record identity differs from operation key");
                break;
            }
            replacement->next = *slot ? (*slot)->next : NULL;
            *slot = replacement;
        } else if (addon_json_is(action, "merge") && (kind == PART || kind == GROUP) && *slot) {
            fields(c, op, kind == PART ? "op,key,models" : "op,key,models,upgrades");
            merge(c, &(*slot)->a, addon_json_get(op, kind == PART ? "models" : "upgrades"),
                  kind == PART ? MODEL : UPGRADE);
            if (kind == GROUP) {
                merge(c, &(*slot)->b, addon_json_get(op, "models"), SHARED);
            }
        } else {
            fail(c, "invalid operation or missing merge target");
        }
    }
}

static void serialize(Writer *w, const Node *head, int kind) {
    unsigned n = 0;
    for (const Node *p = head; p; p = p->next) {
        ++n;
    }
    if (n > RECORDS) {
        fail(w->context, "too many merged records");
        return;
    }
    put(w, n, 4);
    for (const Node *p = head; p && !w->context->error; p = p->next) {
        if (kind == PART || kind == GROUP) {
            write(w, p->prefix.data, p->prefix.size);
            serialize(w, p->a, kind == PART ? MODEL : UPGRADE);
            if (kind == GROUP) {
                serialize(w, p->b, SHARED);
            } else {
                write(w, p->tail.data, p->tail.size);
            }
        } else {
            write(w, p->raw.data, p->raw.size);
        }
    }
}

int carbin_patch_apply(const unsigned char *base, size_t bytes, const JsonValue *patch,
                       unsigned char **output, size_t *output_bytes, const char **error) {
    Context c = {0};
    Reader r = {&c, {base, bytes}, 0};
    Writer w = {.context = &c};
    *output = NULL;
    *output_bytes = 0;
    unsigned format_version = 0;
    fields(&c, patch,
           "format,format_version,base_sha256,ordinal,media_name,parts,groups,description");
    if (!base || bytes > LIMIT ||
        !addon_json_is(addon_json_get(patch, "format"), "fh2-carbin-patch") ||
        !addon_json_uint(addon_json_get(patch, "format_version"), &format_version) ||
        format_version != 1) {
        fail(&c, "unsupported carbin patch format");
    }
    if (integer(&r, 2) != 5) {
        fail(&c, "unsupported CarScene version (expected FH2 v5)");
    }
    take(&r, 16 + 1);
    unsigned ordinal = integer(&r, 4);
    Slice media = string(&r);
    string(&r);
    take(&r, 2);
    size_t prefix = r.at;
    unsigned target = 0;
    const JsonValue *name = addon_json_get(patch, "media_name");
    if (!number(addon_json_get(patch, "ordinal"), 4, 0, &target) || target != ordinal || !name ||
        name->kind != JSON_STRING || name->length != media.size) {
        fail(&c, "patch targets another car");
    }
    if (!c.error) {
        for (size_t i = 0; i < media.size; ++i) {
            unsigned a = media.data[i], b = (unsigned char)name->text[i];
            if (a >= 'A' && a <= 'Z') {
                a += 'a' - 'A';
            }
            if (b >= 'A' && b <= 'Z') {
                b += 'a' - 'A';
            }
            if (a != b) {
                fail(&c, "patch targets another media name");
            }
        }
    }
    Node *parts = read_list(&r, PART), *groups = read_list(&r, GROUP);
    if (r.at != bytes) {
        fail(&c, "unrecognized trailing carbin bytes");
    }
    if (!c.error) {
        merge(&c, &parts, addon_json_get(patch, "parts"), PART);
        merge(&c, &groups, addon_json_get(patch, "groups"), GROUP);
    }
    if (!c.error) {
        write(&w, base, prefix);
        serialize(&w, parts, PART);
        serialize(&w, groups, GROUP);
    }
    if (!c.error) {
        *output = w.data;
        *output_bytes = w.size;
    } else if (w.data) {
        HeapFree(GetProcessHeap(), 0, w.data);
    }
    *error = c.error;
    while (c.allocations) {
        Allocation *next = c.allocations->next;
        HeapFree(GetProcessHeap(), 0, c.allocations);
        c.allocations = next;
    }
    return !c.error;
}

static void remap_word(unsigned char *p, CarbinIdMap map, void *context, unsigned part) {
    unsigned old = 0;
    memory_copy(&old, p, 4);
    unsigned replacement = map(context, part, old);
    memory_copy(p, &replacement, 4);
}

int carbin_remap_ids(unsigned char *data, size_t bytes, CarbinIdMap map, void *context) {
    Context c = {0};
    Reader r = {&c, {data, bytes}, 0};
    if (!data || bytes > LIMIT || integer(&r, 2) != 5) {
        fail(&c, "unsupported CarScene");
    }
    take(&r, 16 + 1 + 4);
    string(&r);
    string(&r);
    take(&r, 2);
    read_list(&r, PART);
    Node *groups = read_list(&r, GROUP);
    if (r.at != bytes) {
        fail(&c, "invalid carbin length");
    }
    if (!c.error) {
        for (Node *g = groups; g; g = g->next) {
            for (Node *u = g->a; u; u = u->next) {
                remap_word((unsigned char *)u->raw.data + 4, map, context, g->key);
                remap_word((unsigned char *)u->raw.data + 8, map, context, UINT32_MAX - 1);
            }
            for (Node *m = g->b; m; m = m->next) {
                unsigned n = 0;
                memory_copy(&n, m->raw.data, 4);
                for (unsigned i = 0; i < n; ++i) {
                    remap_word((unsigned char *)m->raw.data + 4 + i * 4, map, context, g->key);
                }
            }
        }
    }
    while (c.allocations) {
        Allocation *next = c.allocations->next;
        HeapFree(GetProcessHeap(), 0, c.allocations);
        c.allocations = next;
    }
    return !c.error;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestCarbinPatch(const unsigned char *base, size_t bytes,
                                             const char *json, size_t length,
                                             unsigned char **output, size_t *output_bytes,
                                             const char **error) {
    char *source = HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (!source) {
        return 0;
    }
    memory_copy(source, json, length);
    source[length] = 0;
    JsonDocument d;
    int result = addon_json_parse(&d, source, length);
    if (result) {
        result = carbin_patch_apply(base, bytes, d.root, output, output_bytes, error);
    } else {
        *output = NULL;
        *output_bytes = 0;
        *error = "invalid JSON";
    }
    addon_json_free(&d);
    return result;
}

__declspec(dllexport) void Fh2TestCarbinFree(void *data) {
    if (data) {
        HeapFree(GetProcessHeap(), 0, data);
    }
}
#endif
