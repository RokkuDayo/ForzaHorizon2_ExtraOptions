#include "wheel_variants.h"
#include "features.h"
#include "support.h"

enum { MAX_SQL = 65536 };

typedef int (*Prepare)(void *, const char *, int, void **, const char **);
static unsigned char *image;
static SRWLOCK cache_lock = SRWLOCK_INIT;
static unsigned char thunk[12];
static const unsigned char padding[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                          0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted[] = {padding, thunk};
PatchDefinition wheel_variants_hook_patch = {
    0x108d414, 0,     12,    GROUP_RIMS, "stock_wheel_variant_query_thunk",
    padding,   thunk, thunk, accepted,   ARRAY_COUNT(accepted)};

static const char wheel_rows[] =
    "(/*FH2_WHEEL_VARIANTS*/SELECT * FROM List_Wheels UNION ALL "
    "SELECT " FF_WHEEL_ID_TEXT ", '" FF_WHEEL_MEDIA
    "', DisplayName, PartManufacturerID, Mass, Price, "
    "IsStock, Type, Lip, CenterCap, Logo, ActualColor, WheelColor, RGB, "
    "BrakeDrawOffset, IconPath "
    "FROM List_Wheels WHERE ID=91 AND EXISTS (SELECT 1 FROM Data_Car WHERE "
    "ID=" FF_CAR_ID_TEXT " AND MediaName='" FF_WHEEL_MEDIA "') AND NOT EXISTS "
    "(SELECT 1 FROM List_Wheels WHERE ID=" FF_WHEEL_ID_TEXT "))";

static int word_char(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_';
}

static int equals(const char *text, size_t length, const char *word) {
    if (length != text_length(word)) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        char ch = text[i];
        if (ch >= 'a' && ch <= 'z') {
            ch -= 'a' - 'A';
        }
        if (ch != word[i]) {
            return 0;
        }
    }
    return 1;
}

static int implicit_alias(const char *sql, size_t size, size_t after) {
    while (after < size &&
           (sql[after] == ' ' || sql[after] == '\t' || sql[after] == '\r' || sql[after] == '\n')) {
        ++after;
    }
    size_t end = after;
    while (end < size && word_char(sql[end])) {
        ++end;
    }
    if (end == after) {
        return 1;
    }
    static const char *const clauses[] = {"WHERE", "ON",    "JOIN",  "LEFT",  "RIGHT", "INNER",
                                          "CROSS", "ORDER", "GROUP", "LIMIT", "UNION", "USING"};
    for (unsigned i = 0; i < ARRAY_COUNT(clauses); ++i) {
        if (equals(sql + after, end - after, clauses[i])) {
            return 1;
        }
    }
    return 0;
}

static size_t expand_query(const char *sql, size_t size, char *out, size_t capacity, int indexed) {
    if (!sql || !out || !size || size >= MAX_SQL ||
        text_find(sql, size, "/*FH2_WHEEL_VARIANTS*/")) {
        return 0;
    }
    size_t first = 0;
    while (first < size &&
           (sql[first] == ' ' || sql[first] == '\n' || sql[first] == '\r' || sql[first] == '\t')) {
        ++first;
    }
    if (size - first < 6 || !equals(sql + first, 6, "SELECT") ||
        (size - first > 6 && word_char(sql[first + 6]))) {
        return 0;
    }
    size_t read = 0, written = 0;
    int source = 0, changed = 0;
    int stock_query = indexed && text_find(sql, size, " AS StockCar") != NULL;
    for (size_t i = first; i < size;) {
        char ch = sql[i];
        if (ch == '\'' || ch == '"' || ch == '`' || ch == '[') {
            char close = ch == '[' ? ']' : ch;
            ++i;
            while (i < size) {
                if (sql[i++] == close) {
                    if (close != ']' && i < size && sql[i] == close) {
                        ++i;
                    } else {
                        break;
                    }
                }
            }
            source = 0;
        } else if (i + 1 < size && ch == '-' && sql[i + 1] == '-') {
            while (i < size && sql[i] != '\n') {
                ++i;
            }
        } else if (i + 1 < size && ch == '/' && sql[i + 1] == '*') {
            i += 2;
            while (i + 1 < size && !(sql[i] == '*' && sql[i + 1] == '/')) {
                ++i;
            }
            i = i + 2 < size ? i + 2 : size;
        } else if (word_char(ch)) {
            size_t begin = i++;
            while (i < size && word_char(sql[i])) {
                ++i;
            }
            int wheels = equals(sql + begin, i - begin, "LIST_WHEELS");
            int cars = stock_query && equals(sql + begin, i - begin, "DATA_CAR");
            if (source && (wheels || cars)) {
                size_t unchanged = begin - read;
                const char *replacement = cars      ? "fh2_wheel_cache.Cars"
                                          : indexed ? "fh2_wheel_cache.Wheels"
                                                    : wheel_rows;
                const char *alias =
                    implicit_alias(sql, size, i) ? cars ? " AS Data_Car" : " AS List_Wheels" : "";
                size_t replacement_size = text_length(replacement);
                size_t added = unchanged + replacement_size + text_length(alias);
                if (added >= capacity - written) {
                    return 0;
                }
                memory_copy(out + written, sql + read, unchanged);
                written += unchanged;
                memory_copy(out + written, replacement, replacement_size);
                written += replacement_size;
                memory_copy(out + written, alias, text_length(alias));
                written += text_length(alias);
                read = i;
                changed = 1;
            }
            source =
                equals(sql + begin, i - begin, "FROM") || equals(sql + begin, i - begin, "JOIN");
        } else {
            if (ch == ';') {
                for (size_t rest = i + 1; rest < size; ++rest) {
                    if (sql[rest] != ' ' && sql[rest] != '\t' && sql[rest] != '\n' &&
                        sql[rest] != '\r') {
                        return 0;
                    }
                }
            }
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                source = 0;
            }
            ++i;
        }
    }
    if (!changed || size - read >= capacity - written) {
        return 0;
    }
    memory_copy(out + written, sql + read, size - read);
    written += size - read;
    out[written] = 0;
    return written;
}

static int run_sql(void *database, const char *sql) {
    void *statement = NULL;
    int result = ((Prepare)(image + 0x3d710))(database, sql, -1, &statement, NULL);
    if (result || !statement) {
        return 0;
    }
    result = ((int (*)(void *))(image + 0x1eee0))(statement);
    while (result == 100) {
        result = ((int (*)(void *))(image + 0x1eee0))(statement);
    }
    int finalized = ((int (*)(void *))(image + 0x1ab40))(statement);
    return result == 101 && !finalized;
}

static int build_wheel_cache(void *database) {
    if (run_sql(database, "SELECT ID FROM fh2_wheel_cache.Wheels LIMIT 0")) {
        return 1;
    }
    if (!run_sql(database, "ATTACH DATABASE ':memory:' AS fh2_wheel_cache")) {
        return 0;
    }
    char create[2048];
    const char prefix[] = "CREATE TABLE fh2_wheel_cache.Wheels AS ";
    memory_copy(create, prefix, sizeof(prefix) - 1);
    memory_copy(create + sizeof(prefix) - 1, wheel_rows + 1, sizeof(wheel_rows) - 3);
    create[sizeof(prefix) + sizeof(wheel_rows) - 4] = 0;
    if (!run_sql(database, "SAVEPOINT fh2_wheel_build")) {
        run_sql(database, "DETACH DATABASE fh2_wheel_cache");
        return 0;
    }
    int complete =
        run_sql(database, create) &&
        run_sql(database, "CREATE UNIQUE INDEX fh2_wheel_cache.Wheel_ID ON Wheels(ID)") &&
        run_sql(database, "CREATE INDEX fh2_wheel_cache.Wheel_Media ON "
                          "Wheels(MediaName COLLATE NOCASE)") &&
        run_sql(database, "CREATE TABLE fh2_wheel_cache.Cars AS SELECT * FROM Data_Car") &&
        run_sql(database, "CREATE UNIQUE INDEX fh2_wheel_cache.Car_ID ON Cars(ID)") &&
        run_sql(database, "CREATE INDEX fh2_wheel_cache.Car_Wheel ON "
                          "Cars(StockWheelID,MediaName COLLATE NOCASE,ID)");
    if (!complete) {
        run_sql(database, "ROLLBACK TO fh2_wheel_build");
    }
    int released = run_sql(database, "RELEASE fh2_wheel_build");
    if (!complete || !released) {
        run_sql(database, "DETACH DATABASE fh2_wheel_cache");
    }
    return complete && released;
}

static int indexed_wheels(void *database) {
    AcquireSRWLockExclusive(&cache_lock);
    int ready = build_wheel_cache(database);
    ReleaseSRWLockExclusive(&cache_lock);
    return ready;
}

static int prepare_wheel_query(void *database, const char *sql, int bytes, void **statement,
                               const char **tail) {
    Prepare original = (Prepare)(image + 0x3d710);
    size_t size = 0;
    if (sql) {
        while (size < MAX_SQL && (bytes < 0 || size < (unsigned)bytes) && sql[size]) {
            ++size;
        }
    }
    if (!size || size >= MAX_SQL || !text_find(sql, size, "List_Wheels") ||
        text_find(sql, size, "AND WheelCategories.IsStock = 0 JOIN UpgradeTypes") ||
        text_find(sql, size, "/*FH2_WHEEL_VARIANTS*/")) {
        return original(database, sql, bytes, statement, tail);
    }
    char *expanded = HeapAlloc(GetProcessHeap(), 0, MAX_SQL);
    int indexed = expanded && indexed_wheels(database);
    size_t count = expanded ? expand_query(sql, size, expanded, MAX_SQL, indexed) : 0;
    if (!count) {
        if (expanded) {
            HeapFree(GetProcessHeap(), 0, expanded);
        }
        return original(database, sql, bytes, statement, tail);
    }
    const char *end = NULL;
    int result = original(database, expanded, (int)count, statement, &end);
    if (tail) {
        *tail = sql + size;
    }
    HeapFree(GetProcessHeap(), 0, expanded);
    return result;
}

void wheel_variants_prepare_hook(unsigned char *game_image) {
    image = game_image;
    const unsigned char jump[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t callback = (uintptr_t)prepare_wheel_query;
    memory_copy(thunk, jump, sizeof(jump));
    memory_copy(thunk + 2, &callback, 8);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) size_t Fh2TestWheelVariants(const char *sql, size_t size, char *output,
                                                  size_t capacity) {
    return expand_query(sql, size, output, capacity, 0);
}
#endif
