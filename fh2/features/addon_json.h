#ifndef FH2_ADDON_JSON_H
#define FH2_ADDON_JSON_H
#include "extra_options.h"

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } JsonKind;
typedef struct JsonValue {
    JsonKind kind;
    size_t length;
    char *text, *name;
    struct JsonValue *child, *next;
} JsonValue;
typedef struct {
    char *source;
    void *blocks;
    JsonValue *root;
    const char *error;
    size_t error_offset;
    unsigned nodes;
} JsonDocument;

int addon_json_parse(JsonDocument *document, char *owned_source, size_t size);
void addon_json_free(JsonDocument *document);
JsonValue *addon_json_get(const JsonValue *object, const char *name);
int addon_json_is(const JsonValue *value, const char *text);
int addon_json_uint(const JsonValue *value, unsigned *number);
#endif
