#include "pause_menu.h"
#include "support.h"

static int cars_next_to_home;

void pause_menu_configure(int enabled) {
    cars_next_to_home = enabled != 0;
}

int pause_menu_is_xaml(const char *text, size_t length) {
    return cars_next_to_home && text_find(text, length, "x:Class=\"ForzaUI.A_PauseMenu\"") != NULL;
}

typedef struct {
    const char *begin, *end;
} Element;

static int boundary(char c) {
    return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '>' || c == '/';
}

// Match the existing named element, including nested Canvases. All of its
// handlers, bindings and names remain byte-for-byte intact when it moves.
static int element(const char *text, size_t length, const char *name, const char *tag,
                   Element *out) {
    const char *mark = text_find_unique(text, length, name);
    if (!mark) {
        return 0;
    }
    const char *start = mark;
    while (start > text && *start != '<') {
        --start;
    }
    size_t tag_length = text_length(tag);
    if (*start != '<' || (size_t)(text + length - start) <= tag_length + 1 ||
        !memory_equal(start + 1, tag, tag_length) || !boundary(start[1 + tag_length])) {
        return 0;
    }
    unsigned depth = 0;
    const char *p = start, *end = text + length;
    while (p < end) {
        if ((size_t)(end - p) >= 4 && memory_equal(p, "<!--", 4)) {
            const char *comment = text_find(p + 4, (size_t)(end - p - 4), "-->");
            if (!comment) {
                return 0;
            }
            p = comment + 3;
            continue;
        }
        if (*p != '<') {
            ++p;
            continue;
        }
        const char *close = text_find(p, (size_t)(end - p), ">");
        if (!close) {
            return 0;
        }
        int ending = p + 1 < end && p[1] == '/';
        const char *label = p + 1 + ending;
        if ((size_t)(close - label) >= tag_length && memory_equal(label, tag, tag_length) &&
            boundary(label[tag_length])) {
            if (ending) {
                if (!depth) {
                    return 0;
                }
                --depth;
            } else if (close == p || close[-1] != '/') {
                ++depth;
            }
            if (!depth) {
                out->begin = start;
                out->end = close + 1;
                return 1;
            }
        }
        p = close + 1;
    }
    return 0;
}

static int divider(const char *text, size_t length, Element *item) {
    const char *p = item->end, *end = text + length;
    while (p < end && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t')) {
        ++p;
    }
    static const char prefix[] =
        "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_Divider}\"";
    if ((size_t)(end - p) < sizeof(prefix) - 1 || !memory_equal(p, prefix, sizeof(prefix) - 1)) {
        return 0;
    }
    const char *close = text_find(p, (size_t)(end - p), "/>");
    if (!close) {
        return 0;
    }
    item->end = close + 2;
    return 1;
}

size_t pause_menu_reorder(const char *text, size_t length, char *output, size_t capacity) {
    Element home, cars, home_scope, cars_scope;
    if (!pause_menu_is_xaml(text, length) || capacity <= length ||
        !element(text, length, "x:Name=\"PART_GROUP_Home\"", "Button", &home) ||
        !element(text, length, "x:Name=\"PART_GROUP_Autoshow\"", "Button", &cars) ||
        !divider(text, length, &home) || !divider(text, length, &cars) ||
        !element(text, length, "x:Name=\"PART_SCOPE_Home\"", "Canvas", &home_scope) ||
        !element(text, length, "x:Name=\"PART_SCOPE_Autoshow\"", "Canvas", &cars_scope) ||
        home.end > cars.begin || cars.end > home_scope.begin || home_scope.end > cars_scope.begin) {
        return 0;
    }
    // Native TileMenuNavigationManager discovers group/content children in
    // document order. Reorder both collections together, without changing IDs.
    char *out = output;
#define COPY(a, b)                                                                                 \
    do {                                                                                           \
        size_t n = (size_t)((b) - (a));                                                            \
        memory_copy(out, (a), n);                                                                  \
        out += n;                                                                                  \
    } while (0)
    COPY(text, home.end);
    COPY(cars.begin, cars.end);
    COPY(home.end, cars.begin);
    COPY(cars.end, home_scope.end);
    COPY(cars_scope.begin, cars_scope.end);
    COPY(home_scope.end, cars_scope.begin);
    COPY(cars_scope.end, text + length);
#undef COPY
    *out = 0;
    return (size_t)(out - output);
}
