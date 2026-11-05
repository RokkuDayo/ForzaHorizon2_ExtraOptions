#include "addon_json.h"
#include "support.h"

typedef struct JsonBlock {
    struct JsonBlock *next;
    unsigned count;
    JsonValue values[256];
} JsonBlock;
typedef struct {
    JsonDocument *document;
    char *cursor, *end;
} Parser;

static int fail(Parser *parser, const char *message) {
    if (!parser->document->error) {
        parser->document->error = message;
        parser->document->error_offset = (size_t)(parser->cursor - parser->document->source);
    }
    return 0;
}

static void whitespace(Parser *parser) {
    while (parser->cursor < parser->end && (*parser->cursor == ' ' || *parser->cursor == '\r' ||
                                            *parser->cursor == '\n' || *parser->cursor == '\t'))
        ++parser->cursor;
}

static int hex4(Parser *parser, unsigned *value) {
    *value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (parser->cursor == parser->end)
            return fail(parser, "truncated Unicode escape");
        unsigned char ch = (unsigned char)*parser->cursor++;
        unsigned digit = ch >= '0' && ch <= '9'   ? ch - '0'
                         : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                         : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                                  : 16;
        if (digit == 16)
            return fail(parser, "invalid Unicode escape");
        *value = *value * 16 + digit;
    }
    return 1;
}

static int string(Parser *parser, char **text, size_t *length) {
    char *write = ++parser->cursor;
    *text = write;
    while (parser->cursor < parser->end) {
        unsigned char ch = (unsigned char)*parser->cursor++;
        if (ch == '"') {
            *length = (size_t)(write - *text);
            *write = 0;
            return 1;
        }
        if (ch < 32)
            return fail(parser, "control character in string");
        if (ch != '\\') {
            *write++ = (char)ch;
            continue;
        }
        if (parser->cursor == parser->end)
            return fail(parser, "truncated escape");
        ch = (unsigned char)*parser->cursor++;
        if (ch == '"' || ch == '\\' || ch == '/')
            *write++ = (char)ch;
        else if (ch == 'b')
            *write++ = '\b';
        else if (ch == 'f')
            *write++ = '\f';
        else if (ch == 'n')
            *write++ = '\n';
        else if (ch == 'r')
            *write++ = '\r';
        else if (ch == 't')
            *write++ = '\t';
        else if (ch == 'u') {
            unsigned code;
            if (!hex4(parser, &code))
                return 0;
            if (code >= 0xd800 && code <= 0xdbff) {
                if (parser->end - parser->cursor < 6 || parser->cursor[0] != '\\' ||
                    parser->cursor[1] != 'u')
                    return fail(parser, "missing low surrogate");
                parser->cursor += 2;
                unsigned low;
                if (!hex4(parser, &low) || low < 0xdc00 || low > 0xdfff)
                    return fail(parser, "invalid low surrogate");
                code = 0x10000 + (code - 0xd800) * 1024 + low - 0xdc00;
            } else if (code >= 0xdc00 && code <= 0xdfff)
                return fail(parser, "unpaired surrogate");
            if (code < 0x80)
                *write++ = (char)code;
            else if (code < 0x800) {
                *write++ = (char)(0xc0 | (code >> 6));
                *write++ = (char)(0x80 | (code & 63));
            } else {
                if (code >= 0x10000) {
                    *write++ = (char)(0xf0 | (code >> 18));
                    *write++ = (char)(0x80 | ((code >> 12) & 63));
                } else
                    *write++ = (char)(0xe0 | (code >> 12));
                *write++ = (char)(0x80 | ((code >> 6) & 63));
                *write++ = (char)(0x80 | (code & 63));
            }
        } else
            return fail(parser, "invalid escape");
    }
    return fail(parser, "unterminated string");
}

static JsonValue *value(Parser *parser, unsigned depth) {
    if (depth > 64 || parser->document->nodes >= 262144) {
        fail(parser, "JSON structure limit exceeded");
        return NULL;
    }
    JsonBlock *block = parser->document->blocks;
    if (!block || block->count == ARRAY_COUNT(block->values)) {
        block = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*block));
        if (!block) {
            fail(parser, "out of memory");
            return NULL;
        }
        block->next = parser->document->blocks;
        parser->document->blocks = block;
    }
    JsonValue *item = &block->values[block->count++];
    ++parser->document->nodes;
    whitespace(parser);
    if (parser->cursor == parser->end) {
        fail(parser, "missing value");
        return NULL;
    }
    char ch = *parser->cursor;
    if (ch == '{' || ch == '[') {
        item->kind = ch == '{' ? JSON_OBJECT : JSON_ARRAY;
        char close = ch == '{' ? '}' : ']';
        ++parser->cursor;
        whitespace(parser);
        JsonValue **tail = &item->child;
        if (parser->cursor < parser->end && *parser->cursor == close) {
            ++parser->cursor;
            return item;
        }
        for (;;) {
            char *name = NULL;
            size_t length = 0;
            if (item->kind == JSON_OBJECT) {
                if (parser->cursor == parser->end || *parser->cursor != '"' ||
                    !string(parser, &name, &length)) {
                    fail(parser, "object key must be a string");
                    return NULL;
                }
                if (text_length(name) != length || addon_json_get(item, name)) {
                    fail(parser, "duplicate or invalid object key");
                    return NULL;
                }
                whitespace(parser);
                if (parser->cursor == parser->end || *parser->cursor++ != ':') {
                    fail(parser, "missing colon");
                    return NULL;
                }
            }
            JsonValue *child = value(parser, depth + 1);
            if (!child)
                return NULL;
            child->name = name;
            *tail = child;
            tail = &child->next;
            ++item->length;
            whitespace(parser);
            if (parser->cursor == parser->end) {
                fail(parser, "unterminated container");
                return NULL;
            }
            ch = *parser->cursor++;
            if (ch == close)
                return item;
            if (ch != ',') {
                fail(parser, "missing comma");
                return NULL;
            }
            whitespace(parser);
        }
    }
    if (ch == '"') {
        item->kind = JSON_STRING;
        return string(parser, &item->text, &item->length) ? item : NULL;
    }
    item->text = parser->cursor;
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
        item->kind = JSON_NUMBER;
        if (ch == '-')
            ++parser->cursor;
        if (parser->cursor < parser->end && *parser->cursor == '0')
            ++parser->cursor;
        else {
            if (parser->cursor == parser->end || *parser->cursor < '1' || *parser->cursor > '9') {
                fail(parser, "invalid number");
                return NULL;
            }
            do {
                ++parser->cursor;
            } while (parser->cursor < parser->end && *parser->cursor >= '0' &&
                     *parser->cursor <= '9');
        }
        for (unsigned stage = 0; stage < 2; ++stage) {
            if (parser->cursor < parser->end &&
                (stage == 0 ? *parser->cursor == '.'
                            : (*parser->cursor == 'e' || *parser->cursor == 'E'))) {
                ++parser->cursor;
                if (stage && parser->cursor < parser->end &&
                    (*parser->cursor == '+' || *parser->cursor == '-'))
                    ++parser->cursor;
                char *first = parser->cursor;
                while (parser->cursor < parser->end && *parser->cursor >= '0' &&
                       *parser->cursor <= '9')
                    ++parser->cursor;
                if (first == parser->cursor) {
                    fail(parser, "invalid number");
                    return NULL;
                }
            }
        }
    } else {
        const char *literal = ch == 'n' ? "null" : ch == 't' ? "true" : ch == 'f' ? "false" : "";
        size_t size = text_length(literal);
        if (!size || (size_t)(parser->end - parser->cursor) < size ||
            !memory_equal(parser->cursor, literal, size)) {
            fail(parser, "invalid value");
            return NULL;
        }
        item->kind = ch == 'n' ? JSON_NULL : JSON_BOOL;
        parser->cursor += size;
    }
    item->length = (size_t)(parser->cursor - item->text);
    return item;
}

int addon_json_parse(JsonDocument *document, char *source, size_t size) {
    *document = (JsonDocument){.source = source};
    Parser parser = {document, source, source + size};
    if (!size || size > 16 * 1024 * 1024 ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, (int)size, NULL, 0))
        return fail(&parser, "JSON must be valid UTF-8, at most 16 MiB");
    if (size >= 3 && memory_equal(source, "\xef\xbb\xbf", 3))
        parser.cursor += 3;
    document->root = value(&parser, 0);
    whitespace(&parser);
    if (!document->root || parser.cursor != parser.end)
        return fail(&parser, "trailing or invalid JSON data");
    return 1;
}

void addon_json_free(JsonDocument *document) {
    JsonBlock *block = document->blocks;
    while (block) {
        JsonBlock *next = block->next;
        HeapFree(GetProcessHeap(), 0, block);
        block = next;
    }
    if (document->source)
        HeapFree(GetProcessHeap(), 0, document->source);
    *document = (JsonDocument){0};
}

JsonValue *addon_json_get(const JsonValue *object, const char *name) {
    if (!object || object->kind != JSON_OBJECT)
        return NULL;
    for (JsonValue *item = object->child; item; item = item->next)
        if (text_length(item->name) == text_length(name) &&
            memory_equal(item->name, name, text_length(name)))
            return item;
    return NULL;
}

int addon_json_is(const JsonValue *value, const char *text) {
    return value && value->kind == JSON_STRING && value->length == text_length(text) &&
           memory_equal(value->text, text, value->length);
}

int addon_json_uint(const JsonValue *value, unsigned *number) {
    if (!value || value->kind != JSON_NUMBER || !value->length)
        return 0;
    unsigned result = 0;
    for (size_t i = 0; i < value->length; ++i) {
        unsigned digit = (unsigned char)value->text[i] - '0';
        if (digit > 9 || result > (0xffffffffu - digit) / 10)
            return 0;
        result = result * 10 + digit;
    }
    *number = result;
    return 1;
}
