#include "addon_strings.h"
#include "addons.h"
#include "logging.h"
#include "support.h"

enum { MAX_TABLE_BYTES = 32 * 1024 * 1024, MAX_STRING_UNITS = 65535 };
typedef struct {
    unsigned char *data, *entries;
    wchar_t *text;
    void *stream;
    unsigned position;
} NativeTable;
typedef struct {
    const wchar_t *original;
    const JsonValue *replacement;
    unsigned units;
} StringCell;
typedef NativeTable *(*ReadTable)(void **, const char *, unsigned);
typedef void *(*Allocate)(size_t);

static unsigned char *image;
static unsigned char original[2][12], replacement[2][12];
static const unsigned char *accepted[2][2];
PatchDefinition addon_strings_hook_patches[2];

static unsigned little16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned little32(const unsigned char *p) { return little16(p) | little16(p + 2) << 16; }
static void write16(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}
static void write32(unsigned char *p, unsigned value) {
    write16(p, value);
    write16(p + 2, value >> 16);
}

static int string_id(const char *text, unsigned *value) {
    if (!text || !*text || (text[0] == '0' && text[1]))
        return 0;
    unsigned result = 0;
    for (; *text; ++text) {
        unsigned digit = (unsigned char)*text - '0';
        if (digit > 9 || result > (65534u - digit) / 10)
            return 0;
        result = result * 10 + digit;
    }
    *value = result;
    return 1;
}

static int language_valid(const char *name) {
    static const char *codes[] = {"EN", "GB", "BR", "CHT", "CZ", "DE", "ES", "FR",  "HU",
                                  "IT", "JP", "KO", "MX",  "NL", "PL", "RU", "DEV", "LOC"};
    if (!name)
        return 0;
    for (unsigned i = 0; i < ARRAY_COUNT(codes); ++i)
        if (text_equal_ignore_case(name, codes[i]))
            return 1;
    return 0;
}

static const JsonValue *named(const JsonValue *object, const char *name) {
    if (object && object->kind == JSON_OBJECT)
        for (const JsonValue *value = object->child; value; value = value->next)
            if (text_equal_ignore_case(value->name, name))
                return value;
    return NULL;
}

int addon_strings_validate(const JsonValue *document) {
    unsigned version;
    const JsonValue *languages = addon_json_get(document, "languages");
    const JsonValue *fallback = addon_json_get(document, "fallback_language");
    if (!addon_json_uint(addon_json_get(document, "format_version"), &version) || version != 1 ||
        !languages || languages->kind != JSON_OBJECT ||
        (fallback && (fallback->kind != JSON_STRING || !language_valid(fallback->text))))
        return 0;
    for (const JsonValue *language = languages->child; language; language = language->next) {
        if (!language_valid(language->name) || language->kind != JSON_OBJECT ||
            named(languages, language->name) != language)
            return 0;
        for (const JsonValue *table = language->child; table; table = table->next) {
            size_t length = text_length(table->name);
            if (!length || length > 128 || table->kind != JSON_OBJECT ||
                named(language, table->name) != table)
                return 0;
            for (size_t i = 0; i < length; ++i) {
                char ch = table->name[i];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '_'))
                    return 0;
            }
            for (const JsonValue *value = table->child; value; value = value->next) {
                unsigned id;
                if (!string_id(value->name, &id) || value->kind != JSON_STRING ||
                    text_length(value->text) != value->length)
                    return 0;
                if (value->length &&
                    (value->length > MAX_STRING_UNITS * 4 ||
                     MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value->text,
                                         (int)value->length, NULL, 0) > MAX_STRING_UNITS))
                    return 0;
            }
        }
    }
    return 1;
}

static void select_table(StringCell *cells, const JsonValue *table) {
    if (!table)
        return;
    for (const JsonValue *value = table->child; value; value = value->next) {
        unsigned id;
        if (string_id(value->name, &id))
            cells[id].replacement = value;
    }
}

static NativeTable *merge_table(NativeTable *table, const char *name, const char *language) {
    if (!table || !table->data || table->stream)
        return table;
    unsigned count = little32(table->data + 4), size = little32(table->data);
    size_t pool = 8 + ((size_t)count + 1) * 6;
    if (count > 65535 || size < pool || size > MAX_TABLE_BYTES || (size - pool) % 2)
        return table;
    int present = 0;
    for (unsigned i = 0; i < addons_count(); ++i) {
        const JsonValue *document = addons_strings_document(i);
        if (!document || !addons_media_active(i))
            continue;
        const JsonValue *languages = addon_json_get(document, "languages");
        const JsonValue *fallback = addon_json_get(document, "fallback_language");
        const JsonValue *a = named(named(languages, fallback ? fallback->text : "EN"), name);
        const JsonValue *b = named(named(languages, language), name);
        if ((a && a->child) || (b && b->child)) {
            present = 1;
            break;
        }
    }
    if (!present)
        return table;
    StringCell *cells = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 65535 * sizeof(StringCell));
    wchar_t *wide = HeapAlloc(GetProcessHeap(), 0, (MAX_STRING_UNITS + 1) * sizeof(wchar_t));
    if (!cells || !wide) {
        if (cells)
            HeapFree(GetProcessHeap(), 0, cells);
        if (wide)
            HeapFree(GetProcessHeap(), 0, wide);
        return table;
    }
    NativeTable *result = table;
    int valid = 1, changed = 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char *entry = table->data + 8 + i * 6;
        unsigned id = little16(entry), offset = little32(entry + 2);
        if (id == 65535 || offset >= (size - pool) / 2) {
            valid = 0;
            break;
        }
        cells[id].original = (const wchar_t *)(table->data + pool) + offset;
        unsigned limit = (size - (unsigned)pool) / 2 - offset;
        while (cells[id].units < limit && cells[id].original[cells[id].units])
            ++cells[id].units;
        if (cells[id].units == limit) {
            valid = 0;
            break;
        }
    }
    unsigned mods = addons_count();
    for (unsigned i = 0; valid && i < mods; ++i) {
        const JsonValue *document = addons_strings_document(i);
        if (!document || !addons_media_active(i))
            continue;
        const JsonValue *languages = addon_json_get(document, "languages");
        const JsonValue *fallback = addon_json_get(document, "fallback_language");
        const char *code = fallback ? fallback->text : "EN";
        select_table(cells, named(named(languages, code), name));
        select_table(cells, named(named(languages, language), name));
    }
    count = 0;
    size_t units = 0;
    for (unsigned i = 0; valid && i < 65535; ++i) {
        StringCell *cell = &cells[i];
        if (cell->replacement) {
            int chars =
                cell->replacement->length
                    ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, cell->replacement->text,
                                          (int)cell->replacement->length, wide, MAX_STRING_UNITS)
                    : 0;
            if (cell->replacement->length && !chars) {
                valid = 0;
                break;
            }
            wide[chars] = 0;
            if (!cell->original || cell->units != (unsigned)chars ||
                !memory_equal(wide, cell->original, (size_t)chars * sizeof(wchar_t)))
                changed = 1;
            cell->units = (unsigned)chars;
        }
        if (cell->original || cell->replacement) {
            ++count;
            units += cell->units + 1;
        }
    }
    size_t bytes = 8 + ((size_t)count + 1) * 6 + units * sizeof(wchar_t);
    if (valid && changed && bytes <= MAX_TABLE_BYTES) {
        unsigned char *data = ((Allocate)(image + 0x1090320))(bytes);
        if (data) {
            write32(data, (unsigned)bytes);
            write32(data + 4, count);
            wchar_t *text = (wchar_t *)(data + 8 + ((size_t)count + 1) * 6);
            unsigned index = 0, offset = 0;
            for (unsigned i = 0; i < 65535; ++i) {
                StringCell *cell = &cells[i];
                if (!cell->original && !cell->replacement)
                    continue;
                write16(data + 8 + index * 6, i);
                write32(data + 10 + index++ * 6, offset);
                if (cell->replacement && cell->units)
                    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, cell->replacement->text,
                                        (int)cell->replacement->length, text + offset,
                                        (int)cell->units);
                else if (!cell->replacement)
                    memory_copy(text + offset, cell->original, cell->units * sizeof(wchar_t));
                text[offset + cell->units] = 0;
                offset += cell->units + 1;
            }
            write16(data + 8 + count * 6, 65535);
            write32(data + 10 + count * 6, offset);
            NativeTable *extended = ((Allocate)(image + 0x1090320))(sizeof(NativeTable));
            if (extended) {
                memory_copy(extended, table, sizeof(NativeTable));
                extended->entries = data + 8;
                extended->text = text;
                extended->data = data;
                result = extended;
                log_append("MODS: localized string table extended in memory.\r\n");
            } else {
                typedef void *(*Allocator)(void);
                typedef void (*Free)(void *, void *);
                ((Free)(image + 0x1009740))(((Allocator)(image + 0x125f380))(), data);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, wide);
    HeapFree(GetProcessHeap(), 0, cells);
    return result;
}

static NativeTable *load_table(void **stream, const char *path, unsigned flags) {
    NativeTable *table = ((ReadTable)(image + 0x1246f30))(stream, path, flags);
    const char *name = path, *previous = path;
    for (const char *at = path; *at; ++at)
        if (*at == '/' || *at == '\\') {
            previous = name;
            name = at + 1;
        }
    size_t size = text_length(name),
           code_size = name > previous ? (size_t)(name - previous - 1) : 0;
    char title[129], code[4];
    if (size < 5 || size - 4 > 128 || code_size < 2 || code_size > 3 ||
        !text_equal_ignore_case(name + size - 4, ".str"))
        return table;
    memory_copy(title, name, size - 4);
    title[size - 4] = 0;
    memory_copy(code, previous, code_size);
    code[code_size] = 0;
    return language_valid(code) ? merge_table(table, title, code) : table;
}

void addon_strings_refresh(void) {
    void *manager;
    if (!image || !memory_read_pointer(image + 0x3662990, &manager))
        return;
    char code[4];
    size_t length;
    if (!memory_read((unsigned char *)manager + 0x40, &length, sizeof(length)) || length < 2 ||
        length > 3 || !memory_read((unsigned char *)manager + 0x30, code, length))
        return;
    code[length] = 0;
    const char **seen = HeapAlloc(GetProcessHeap(), 0, 2048 * sizeof(char *));
    if (!seen)
        return;
    unsigned total = 0;
    EnterCriticalSection((CRITICAL_SECTION *)((unsigned char *)manager + 8));
    for (unsigned i = 0; i < addons_count(); ++i) {
        const JsonValue *document = addons_strings_document(i);
        const JsonValue *languages = addon_json_get(document, "languages");
        if (!languages || !addons_media_active(i))
            continue;
        for (const JsonValue *language = languages->child; language; language = language->next)
            for (const JsonValue *table = language->child; table; table = table->next) {
                unsigned at = 0;
                while (at < total && !text_equal_ignore_case(seen[at], table->name))
                    ++at;
                if (at < total || total == 2048)
                    continue;
                seen[total++] = table->name;
                typedef unsigned short (*TableId)(void *, const char *);
                unsigned short id = ((TableId)(image + 0x123d940))(manager, table->name);
                if (id != 65535) {
                    NativeTable *native;
                    if (memory_read_pointer((unsigned char *)manager + 0x78 + id * 8,
                                            (void **)&native)) {
                        NativeTable *extended = merge_table(native, table->name, code);
                        if (extended != native)
                            InterlockedExchangePointer(
                                (void *volatile *)((unsigned char *)manager + 0x78 + id * 8),
                                extended);
                    }
                }
            }
    }
    LeaveCriticalSection((CRITICAL_SECTION *)((unsigned char *)manager + 8));
    HeapFree(GetProcessHeap(), 0, seen);
}

void addon_strings_prepare_hooks(unsigned char *base) {
    image = base;
    const unsigned slots[] = {0x1245514, 0x1245441};
    for (unsigned i = 0; i < 2; ++i) {
        unsigned size = i ? 5 : 12;
        if (!i) {
            for (unsigned j = 0; j < 12; ++j)
                original[i][j] = 0xcc;
            const unsigned char jump[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
            uintptr_t callback = (uintptr_t)load_table;
            memory_copy(replacement[i], jump, 12);
            memory_copy(replacement[i] + 2, &callback, 8);
        } else {
            original[i][0] = replacement[i][0] = 0xe8;
            int32_t before = 0x1246f30 - slots[i] - 5, after = slots[0] - slots[i] - 5;
            memory_copy(original[i] + 1, &before, 4);
            memory_copy(replacement[i] + 1, &after, 4);
        }
        accepted[i][0] = original[i];
        accepted[i][1] = replacement[i];
        addon_strings_hook_patches[i] =
            (PatchDefinition){slots[i],
                              0,
                              size,
                              GROUP_ADDONS,
                              i ? "addon_string_table_load" : "addon_string_table_thunk",
                              original[i],
                              replacement[i],
                              replacement[i],
                              accepted[i],
                              2};
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestStringsMerge(void *table, const char *name,
                                                const char *language) {
    return merge_table(table, name, language);
}
__declspec(dllexport) void *Fh2TestStringsLoad(void **stream, const char *path) {
    return load_table(stream, path, 1);
}
#endif
