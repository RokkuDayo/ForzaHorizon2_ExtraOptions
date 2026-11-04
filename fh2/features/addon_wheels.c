#include "addon_wheels.h"
#include "addon_database.h"
#include "logging.h"
#include "support.h"

typedef struct AddonWheel {
    struct AddonWheel *next;
    char name[96], donor[96];
    char *models[4], *textures[4];
    unsigned state;
} AddonWheel;
typedef struct {
    const char *data;
    uintptr_t reserved;
    size_t length, capacity;
} WheelString;
static unsigned char *image;
static AddonWheel *wheels;
static unsigned count;
static SRWLOCK lock = SRWLOCK_INIT;

static char lower(char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }
static int equal(const char *a, size_t length, const char *b) {
    for (size_t i = 0; i < length; ++i)
        if (!b[i] || lower(a[i]) != lower(b[i]))
            return 0;
    return !b[length];
}
static AddonWheel *find(const char *name, size_t length) {
    for (AddonWheel *wheel = wheels; wheel; wheel = wheel->next)
        if (equal(name, length, wheel->name))
            return wheel;
    return NULL;
}
static int copy_name(char *out, const char *text) {
    size_t length = text ? text_length(text) : 0;
    if (!length || length >= 96)
        return 0;
    for (size_t i = 0; i < length; ++i)
        if (!((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z') ||
              (text[i] >= '0' && text[i] <= '9') || text[i] == '_'))
            return 0;
    memory_copy(out, text, length + 1);
    return 1;
}

void addon_wheels_register_database(void *database) {
    const AddonSqlite *api = addon_database_api();
    static const char query[] =
        "SELECT W.MediaName,C.MediaName FROM List_Wheels W JOIN Data_Car C ON C.StockWheelID=W.ID "
        "WHERE W.IsStock=1 ORDER BY W.ID,CASE WHEN C.MediaName COLLATE NOCASE=W.MediaName THEN 0 "
        "ELSE 1 END,C.ID";
    void *statement = NULL;
    if (api->prepare(database, query, -1, &statement, NULL))
        return;
    AcquireSRWLockExclusive(&lock);
    while (api->step(statement) == 100 && count < 512) {
        const char *name = api->column_text(statement, 0), *donor = api->column_text(statement, 1);
        if (!name || find(name, text_length(name)))
            continue;
        // The authored vanilla routes include deliberate AO corrections.
        extern int stock_wheel_has_authored_route(const char *, size_t);
        if (stock_wheel_has_authored_route(name, text_length(name)))
            continue;
        AddonWheel *wheel = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(AddonWheel));
        if (!wheel)
            break;
        if (!copy_name(wheel->name, name) || !copy_name(wheel->donor, donor)) {
            HeapFree(GetProcessHeap(), 0, wheel);
            continue;
        }
        wheel->next = wheels;
        wheels = wheel;
        ++count;
    }
    ReleaseSRWLockExclusive(&lock);
    api->finalize(statement);
}

const char *addon_wheels_donor(const char *name, size_t length) {
    AcquireSRWLockShared(&lock);
    AddonWheel *wheel = find(name, length);
    ReleaseSRWLockShared(&lock);
    return wheel ? wheel->donor : NULL;
}

static WheelString borrowed(const char *text) {
    return (WheelString){text, 0, text_length(text), 511};
}
static char *copy_path(const unsigned char *text, size_t size) {
    if (!size || size >= ADDON_WHEEL_PATH_CAPACITY)
        return NULL;
    char *result = HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (result) {
        memory_copy(result, text, size);
        result[size] = 0;
    }
    return result;
}
static unsigned read32(const unsigned char *p) {
    return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}
static int prefix(const unsigned char *data, size_t size, const char *text) {
    size_t length = text_length(text);
    if (length > size)
        return 0;
    for (size_t i = 0; i < length; ++i)
        if (lower((char)data[i]) != lower(text[i]))
            return 0;
    return 1;
}
static size_t path_end(const unsigned char *data, size_t size, size_t start,
                       const char *extension) {
    for (size_t at = start; at < size && at - start < ADDON_WHEEL_PATH_CAPACITY; ++at) {
        if (data[at] <= 32)
            return 0;
        if (prefix(data + at, size - at, extension))
            return at + text_length(extension);
    }
    return 0;
}

static int parse_routes(AddonWheel *wheel, const unsigned char *data, size_t size) {
    const char root[] = "game:\\media\\cars\\";
    for (size_t at = 4; at + sizeof(root) < size; ++at) {
        if (!prefix(data + at, size - at, root))
            continue;
        size_t end = path_end(data, size, at, ".modelbin");
        if (!end || read32(data + at - 4) != end - at)
            continue;
        size_t basename = at;
        for (size_t i = at; i < end; ++i)
            if (data[i] == '\\' || data[i] == '/')
                basename = i + 1;
        int is_wheel = 0;
        for (size_t i = basename; i < end; ++i)
            if (prefix(data + i, end - i, "wheel")) {
                is_wheel = 1;
                break;
            }
        if (!is_wheel)
            continue;
        size_t limit = end;
        while (limit + sizeof(root) < size) {
            if (prefix(data + limit, size - limit, root) &&
                path_end(data, size, limit, ".modelbin"))
                break;
            ++limit;
        }
        if (limit + sizeof(root) >= size)
            limit = size;
        int corner = -1;
        static const char *locators[] = {"LF", "RF", "RR", "LR"};
        for (size_t i = end; i + 5 <= limit && corner < 0; ++i) {
            size_t length = prefix(data + i, limit - i, "spindle") ? 7
                            : prefix(data + i, limit - i, "hub")   ? 3
                                                                   : 0;
            if (length && i + length + 2 <= limit)
                for (unsigned k = 0; k < 4; ++k)
                    if (lower((char)data[i + length]) == lower(locators[k][0]) &&
                        lower((char)data[i + length + 1]) == lower(locators[k][1]))
                        corner = (int)k;
        }
        if (corner < 0 || wheel->models[corner])
            continue;
        wheel->models[corner] = copy_path(data + at, end - at);
        for (size_t i = end; i + sizeof(root) < limit; ++i)
            if (prefix(data + i, limit - i, root)) {
                size_t stop = path_end(data, limit, i, ".swatchbin");
                if (stop) {
                    wheel->textures[corner] = copy_path(data + i, stop - i);
                    break;
                }
            }
    }
    for (unsigned i = 0; i < 4; ++i)
        if (!wheel->models[i])
            return 0;
    return 1;
}

static int read_routes(AddonWheel *wheel) {
    void *filesystem;
    if (!image || !memory_read_pointer(image + 0x36614f0, &filesystem))
        return 0;
    char path[256];
    size_t length = 0;
    const char *parts[] = {"game:\\media\\cars\\", wheel->donor, "\\", wheel->donor, ".carbin"};
    for (unsigned i = 0; i < ARRAY_COUNT(parts); ++i) {
        size_t chars = text_length(parts[i]);
        if (length + chars >= sizeof(path))
            return 0;
        memory_copy(path + length, parts[i], chars);
        length += chars;
    }
    path[length] = 0;
    WheelString filename = borrowed(path);
    typedef void **(*Open)(void *, void **, const WheelString *, unsigned, unsigned char,
                           unsigned char, void *);
    void *file = NULL;
    ((Open)(*(void ***)filesystem)[0x138 / 8])(filesystem, &file, &filename, 0x8000001b, 1, 0,
                                               NULL);
    if (!file)
        return 0;
    void **methods = *(void ***)file;
    typedef unsigned (*Size)(void *);
    typedef unsigned (*Read)(void *, void *, unsigned);
    typedef void (*Release)(void *);
    unsigned bytes = ((Size)methods[0xd8 / 8])(file);
    unsigned char *data =
        bytes && bytes <= 32 * 1024 * 1024 ? HeapAlloc(GetProcessHeap(), 0, bytes) : NULL;
    int result = data && ((Read)methods[0x90 / 8])(file, data, bytes) == bytes &&
                 parse_routes(wheel, data, bytes);
    ((Release)methods[0x18 / 8])(file);
    if (data)
        HeapFree(GetProcessHeap(), 0, data);
    return result;
}

int addon_wheels_paths(const char *name, size_t length, const char **models,
                       const char **textures) {
    AcquireSRWLockExclusive(&lock);
    AddonWheel *wheel = find(name, length);
    if (wheel && !wheel->state) {
        wheel->state = read_routes(wheel) ? 1 : 2;
        log_append(wheel->state == 1 ? "MODS: stock rim paths read from donor car.\r\n"
                                     : "MODS: donor stock rim paths unavailable.\r\n");
    }
    int result = wheel && wheel->state == 1;
    if (result)
        for (unsigned i = 0; i < 4; ++i) {
            models[i] = wheel->models[i];
            textures[i] = wheel->textures[i] ? wheel->textures[i] : "";
        }
    ReleaseSRWLockExclusive(&lock);
    return result;
}

void addon_wheels_bind(unsigned char *base) { image = base; }

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestAddonWheelsRegister(void *database) {
    addon_wheels_register_database(database);
}
__declspec(dllexport) const char *Fh2TestAddonWheelDonor(const char *name) {
    return addon_wheels_donor(name, text_length(name));
}
__declspec(dllexport) const char *Fh2TestAddonWheelPath(const char *name, unsigned kind,
                                                        unsigned corner) {
    const char *models[4], *textures[4];
    return kind <= 1 && corner < 4 && addon_wheels_paths(name, text_length(name), models, textures)
               ? (kind ? textures[corner] : models[corner])
               : NULL;
}
#endif

