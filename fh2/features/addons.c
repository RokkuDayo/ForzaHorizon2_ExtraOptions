#include "addons.h"
#include "plugins.h"
#include "addon_database.h"
#include "driver_options.h"
#include "addon_strings.h"
#include "addon_wheels.h"
#include "engine_swaps.h"
#include "transmission_swaps.h"
#include "logging.h"
#include "sha256.h"
#include "support.h"

enum { MAX_ADDONS = 128, MAX_LOAD_ORDER = 65536, MAX_PLAN_BYTES = 64 * 1024 * 1024 };

typedef struct {
    const char *folder, *id;
    wchar_t *directory, *media;
    JsonDocument manifest, database, strings;
    JsonValue *dependencies;
    int valid;
    volatile LONG active;
} Addon;

static wchar_t mods_directory[PATH_CAPACITY], content_directory[PATH_CAPACITY];
static Addon addons[MAX_ADDONS];
static unsigned count, plan_bytes;
static int plan_disabled, loader_requested;
static char *load_order;
static INIT_ONCE discovery = INIT_ONCE_STATIC_INIT;
static SRWLOCK database_lock = SRWLOCK_INIT;
static volatile LONG database_thread;
static int captured_database;
static unsigned char *image;
static char vanilla_hash[65];
static int hash_ready;
static volatile LONG media_revision = 1;

static void report(const Addon *addon, const char *message) {
    log_append("MODS: ");
    if (addon) {
        log_append(addon->id ? addon->id : addon->folder);
        log_append(": ");
    }
    log_append(message);
    log_append("\r\n");
}

static void clear_plan(void) {
    addon_ids_reset();
    for (unsigned i = 0; i < count; ++i) {
        addon_json_free(&addons[i].manifest);
        addon_json_free(&addons[i].database);
        addon_json_free(&addons[i].strings);
        if (addons[i].directory) {
            HeapFree(GetProcessHeap(), 0, addons[i].directory);
        }
        if (addons[i].media) {
            HeapFree(GetProcessHeap(), 0, addons[i].media);
        }
        static const Addon empty;
        memory_copy(&addons[i], &empty, sizeof(empty));
    }
    if (load_order) {
        HeapFree(GetProcessHeap(), 0, load_order);
    }
    load_order = NULL;
    count = plan_bytes = 0;
    plan_disabled = loader_requested = 0;
    InterlockedIncrement(&media_revision);
    hash_ready = 0;
    vanilla_hash[0] = 0;
    discovery = (INIT_ONCE)INIT_ONCE_STATIC_INIT;
}

static int parent_directory(wchar_t *output, const wchar_t *file) {
    size_t size = 0, last = 0;
    while (file[size] && size < PATH_CAPACITY - 1) {
        output[size] = file[size];
        if (file[size] == L'\\' || file[size] == L'/') {
            last = size + 1;
        }
        ++size;
    }
    output[last] = 0;
    return last && !file[size];
}

static void check_load_order(void) {
    DWORD attributes =
        mods_directory[0] ? GetFileAttributesW(mods_directory) : INVALID_FILE_ATTRIBUTES;
    loader_requested =
        attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void addons_configure(const wchar_t *game_executable) {
    clear_plan();
    mods_directory[0] = content_directory[0] = 0;
    if (!parent_directory(content_directory, game_executable)) {
        return;
    }
    wchar_t *base = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (!base) {
        return;
    }
#ifdef FH2_AZIMUTH
    DWORD size = GetModuleFileNameW(NULL, base, PATH_CAPACITY);
    if (!size || size >= PATH_CAPACITY || !parent_directory(base, base)) {
        base[0] = 0;
    }
#else
    lstrcpyW(base, content_directory);
#endif
    if (base[0]) {
        path_join(mods_directory, base, L"mods");
    }
    HeapFree(GetProcessHeap(), 0, base);
    check_load_order();
}

int addons_configured(void) {
    return loader_requested;
}

const wchar_t *addons_content_directory(void) {
    return content_directory;
}

static char *read_file(const wchar_t *path, unsigned maximum, size_t *length) {
    *length = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    LARGE_INTEGER size;
    char *data = NULL;
    DWORD read = 0;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= maximum &&
        (uint64_t)size.QuadPart <= MAX_PLAN_BYTES - plan_bytes) {
        data = HeapAlloc(GetProcessHeap(), 0, (size_t)size.QuadPart + 1);
        if (data && ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL) &&
            read == (DWORD)size.QuadPart) {
            data[read] = 0;
            *length = read;
            plan_bytes += read;
        } else {
            if (data) {
                HeapFree(GetProcessHeap(), 0, data);
            }
            data = NULL;
        }
    }
    CloseHandle(file);
    return data;
}

static int exists(const wchar_t *path, int directory) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !!(attributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
}

static int relative_path(const char *text, size_t size, int single) {
    if (!size || text_length(text) != size || text[0] == '/' || text[0] == '\\') {
        return 0;
    }
    size_t component = 0;
    for (size_t i = 0; i <= size; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (i == size || ch == '/' || ch == '\\') {
            if ((single && i != size) || i == component ||
                (i - component == 1 && text[component] == '.') ||
                (i - component == 2 && text[component] == '.' && text[component + 1] == '.') ||
                text[i - 1] == '.' || text[i - 1] == ' ') {
                return 0;
            }
            component = i + 1;
        } else if (ch < 32 || ch == ':' || ch == '<' || ch == '>' || ch == '*' || ch == '?' ||
                   ch == '"' || ch == '|') {
            return 0;
        }
    }
    return 1;
}

static wchar_t *join_utf8(const wchar_t *base, const char *relative, size_t length, int single) {
    if (!relative_path(relative, length, single) || length > 32767) {
        return NULL;
    }
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative, (int)length, NULL, 0);
    size_t base_length = 0;
    while (base[base_length]) {
        ++base_length;
    }
    if (!size || base_length + (unsigned)size + 2 >= PATH_CAPACITY) {
        return NULL;
    }
    wchar_t *result = HeapAlloc(GetProcessHeap(), 0, (base_length + size + 2) * sizeof(wchar_t));
    if (!result) {
        return NULL;
    }
    memory_copy(result, base, base_length * sizeof(wchar_t));
    if (base_length && base[base_length - 1] != L'\\' && base[base_length - 1] != L'/') {
        result[base_length++] = L'\\';
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, relative, (int)length,
                             result + base_length, size)) {
        HeapFree(GetProcessHeap(), 0, result);
        return NULL;
    }
    result[base_length + size] = 0;
    return result;
}

static int read_document(const wchar_t *path, JsonDocument *document) {
    size_t length;
    char *data = read_file(path, 16 * 1024 * 1024, &length);
    return data && addon_json_parse(document, data, length) && document->root->kind == JSON_OBJECT;
}

static int valid_id(const JsonValue *value) {
    if (!value || value->kind != JSON_STRING || !value->length || value->length > 128) {
        return 0;
    }
    for (size_t i = 0; i < value->length; ++i) {
        char ch = value->text[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') ||
            (ch == '-' && (!i || i == value->length - 1 || value->text[i - 1] == '-'))) {
            return 0;
        }
    }
    return 1;
}

static int dependencies_ready(const Addon *addon, int active) {
    if (!addon->dependencies) {
        return 1;
    }
    if (addon->dependencies->kind != JSON_ARRAY) {
        return 0;
    }
    for (JsonValue *dependency = addon->dependencies->child; dependency;
         dependency = dependency->next) {
        int found = 0;
        for (const Addon *prior = addons; prior < addon; ++prior) {
            if (prior->valid && addon_json_is(dependency, prior->id) &&
                (!active || InterlockedCompareExchange((volatile LONG *)&prior->active, 0, 0))) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

static int listed_folder(const char *order, size_t length, const char *folder, size_t size) {
    size_t at = length >= 3 && memory_equal(order, "\xef\xbb\xbf", 3) ? 3 : 0;
    while (at < length) {
        size_t start = at;
        while (at < length && order[at] != '\r' && order[at] != '\n') {
            ++at;
        }
        size_t end = at;
        while (start < end && (order[start] == ' ' || order[start] == '\t')) {
            ++start;
        }
        if (start < end && order[start] == '#') {
            ++start;
            while (start < end && (order[start] == ' ' || order[start] == '\t')) {
                ++start;
            }
        }
        while (end > start && (order[end - 1] == ' ' || order[end - 1] == '\t')) {
            --end;
        }
        wchar_t listed[256], candidate[256];
        int a = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, order + start,
                                    (int)(end - start), listed, 255);
        int b =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, folder, (int)size, candidate, 255);
        if (a && b) {
            listed[a] = candidate[b] = 0;
            if (!lstrcmpiW(listed, candidate)) {
                return 1;
            }
        }
        if (at < length) {
            ++at;
        }
    }
    return 0;
}

static int prune_load_order(char *data, DWORD *length, unsigned *removed) {
    DWORD read = *length >= 3 && memory_equal(data, "\xef\xbb\xbf", 3) ? 3 : 0;
    DWORD written = read;
    while (read < *length) {
        DWORD line = read;
        while (read < *length && data[read] != '\r' && data[read] != '\n') {
            ++read;
        }
        DWORD start = line, end = read;
        if (read < *length && data[read++] == '\r' && read < *length && data[read] == '\n') {
            ++read;
        }
        while (start < end && (data[start] == ' ' || data[start] == '\t')) {
            ++start;
        }
        while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t')) {
            --end;
        }
        int missing = 0;
        if (start < end && data[start] != '#') {
            char saved = data[end];
            data[end] = 0;
            int valid_name = relative_path(data + start, end - start, 1);
            wchar_t *folder =
                valid_name ? join_utf8(mods_directory, data + start, end - start, 1) : NULL;
            data[end] = saved;
            if (valid_name && !folder) {
                return 0;
            }
            if (folder) {
                DWORD attributes = GetFileAttributesW(folder);
                DWORD error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
                missing = attributes == INVALID_FILE_ATTRIBUTES
                              ? error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                              : !(attributes & FILE_ATTRIBUTE_DIRECTORY);
                HeapFree(GetProcessHeap(), 0, folder);
            }
        }
        if (missing) {
            ++*removed;
        } else {
            memory_copy(data + written, data + line, read - line);
            written += read - line;
        }
    }
    data[written] = 0;
    *length = written;
    return 1;
}

static int write_load_order(const wchar_t *path, const char *data, DWORD length) {
    wchar_t name[64] = L"loadorder.extra-options-";
    unsigned at = ARRAY_COUNT(L"loadorder.extra-options-") - 1;
    uint64_t unique = ((uint64_t)GetCurrentProcessId() << 32) ^ GetTickCount64();
    for (unsigned digit = 16; digit; --digit) {
        name[at++] = L"0123456789abcdef"[(unique >> ((digit - 1) * 4)) & 15];
    }
    lstrcpyW(name + at, L".tmp");
    wchar_t *temporary = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (!temporary) {
        return 0;
    }
    HANDLE file = path_join(temporary, mods_directory, name)
                      ? CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, NULL)
                      : INVALID_HANDLE_VALUE;
    int valid = file != INVALID_HANDLE_VALUE;
    if (valid) {
        DWORD written = 0;
        valid = WriteFile(file, data, length, &written, NULL) && written == length &&
                FlushFileBuffers(file);
        valid &= CloseHandle(file) != 0;
        if (valid) {
            valid = MoveFileExW(temporary, path,
                                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        }
        if (!valid) {
            DeleteFileW(temporary);
        }
    }
    HeapFree(GetProcessHeap(), 0, temporary);
    return valid;
}

static char *refresh_load_order(const wchar_t *path, size_t *length) {
    *length = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    LARGE_INTEGER size;
    char *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_LOAD_ORDER + 1);
    DWORD bytes = 0;
    int valid =
        data && GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= MAX_LOAD_ORDER;
    if (valid && size.QuadPart) {
        valid = ReadFile(file, data, (DWORD)size.QuadPart, &bytes, NULL) &&
                bytes == size.QuadPart && text_length(data) == bytes &&
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, (int)bytes, NULL, 0);
    }
    unsigned removed = 0, added = 0;
    if (valid) {
        valid = prune_load_order(data, &bytes, &removed);
    }
    wchar_t *search = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (search && !path_join(search, mods_directory, L"*")) {
        HeapFree(GetProcessHeap(), 0, search);
        search = NULL;
    }
    valid = valid && search;
    wchar_t **folders =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_ADDONS * sizeof(wchar_t *));
    valid = valid && folders;
    unsigned found = 0;
    WIN32_FIND_DATAW entry;
    HANDLE find =
        valid && search && folders ? FindFirstFileW(search, &entry) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                entry.cFileName[0] == L'.') {
                continue;
            }
            if (found == MAX_ADDONS) {
                valid = 0;
                break;
            }
            size_t chars = (size_t)lstrlenW(entry.cFileName) + 1;
            wchar_t *name = HeapAlloc(GetProcessHeap(), 0, chars * sizeof(wchar_t));
            if (!name) {
                valid = 0;
                break;
            }
            memory_copy(name, entry.cFileName, chars * sizeof(wchar_t));
            unsigned insert = found++;
            while (insert && lstrcmpiW(folders[insert - 1], name) > 0) {
                folders[insert] = folders[insert - 1];
                --insert;
            }
            folders[insert] = name;
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    char name[1024];
    for (unsigned i = 0; valid && i < found; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, folders[i], -1, name,
                                       sizeof(name), NULL, NULL);
        if (!size || !relative_path(name, (size_t)size - 1, 1)) {
            valid = 0;
            break;
        }
        if (listed_folder(data, bytes, name, (size_t)size - 1)) {
            continue;
        }
        unsigned newline = bytes && data[bytes - 1] != '\n' && data[bytes - 1] != '\r' ? 2 : 0;
        if (bytes + newline + (unsigned)size + 1 > MAX_LOAD_ORDER) {
            valid = 0;
            break;
        }
        if (newline) {
            data[bytes++] = '\r';
            data[bytes++] = '\n';
        }
        memory_copy(data + bytes, name, (size_t)size - 1);
        bytes += (unsigned)size - 1;
        data[bytes++] = '\r';
        data[bytes++] = '\n';
        data[bytes] = 0;
        ++added;
    }
    CloseHandle(file);
    if (valid && (removed || added)) {
        valid = write_load_order(path, data, bytes);
        if (valid && removed) {
            report(NULL, "missing mod folders removed from loadorder.txt");
        }
        if (valid && added) {
            report(NULL, "new mod folders appended to loadorder.txt");
        }
    }
    if (search) {
        HeapFree(GetProcessHeap(), 0, search);
    }
    if (folders) {
        for (unsigned i = 0; i < found; ++i) {
            HeapFree(GetProcessHeap(), 0, folders[i]);
        }
        HeapFree(GetProcessHeap(), 0, folders);
    }
    if (!valid) {
        if (data) {
            HeapFree(GetProcessHeap(), 0, data);
        }
        return NULL;
    }
    *length = bytes;
    return data;
}

static int prepare_addon(Addon *addon) {
    wchar_t *path = join_utf8(addon->directory, "mod.json", 8, 0);
    if (path && !exists(path, 0)) {
        HeapFree(GetProcessHeap(), 0, path);
        path = join_utf8(addon->directory, "addon.json", 10, 0);
    }
    if (!path) {
        return 0;
    }
    int manifest_exists = exists(path, 0);
    int valid = !manifest_exists || read_document(path, &addon->manifest);
    HeapFree(GetProcessHeap(), 0, path);
    if (!valid) {
        report(addon, "invalid mod manifest; addon disabled");
        return 0;
    }
    JsonValue *manifest = addon->manifest.root;
    if (manifest) {
        unsigned version;
        JsonValue *id = addon_json_get(manifest, "id");
        if (!addon_json_uint(addon_json_get(manifest, "format_version"), &version) ||
            version != 1 || !valid_id(id)) {
            report(addon, "unsupported manifest format or invalid ID; addon disabled");
            return 0;
        }
        addon->id = id->text;
        addon->dependencies = addon_json_get(manifest, "dependencies");
    }
    JsonValue *database = addon_json_get(manifest, "database"),
              *media = addon_json_get(manifest, "media");
    if (database && database->kind != JSON_NULL && database->kind != JSON_STRING) {
        return 0;
    }
    if (media && media->kind != JSON_NULL && media->kind != JSON_STRING) {
        return 0;
    }
    if (!database || database->kind == JSON_STRING) {
        const char *filename = database ? database->text : "database.json";
        path = join_utf8(addon->directory, filename, database ? database->length : 13, 0);
        if (!path) {
            return 0;
        }
        int present = exists(path, 0);
        valid = present ? read_document(path, &addon->database) : !database;
        HeapFree(GetProcessHeap(), 0, path);
        if (!valid) {
            report(addon, "missing/invalid database JSON; addon disabled");
            return 0;
        }
        // The containing folder/manifest owns the database. Legacy addon_id
        // metadata must not reject a file or make two folders duplicate mods.
    }
    JsonValue *strings = addon_json_get(manifest, "strings");
    if (strings && strings->kind != JSON_NULL && strings->kind != JSON_STRING) {
        return 0;
    }
    if (!strings || strings->kind == JSON_STRING) {
        path = join_utf8(addon->directory, strings ? strings->text : "strings.json",
                         strings ? strings->length : 12, 0);
        if (!path) {
            return 0;
        }
        int present = exists(path, 0);
        valid = present ? read_document(path, &addon->strings) : !strings;
        HeapFree(GetProcessHeap(), 0, path);
        if (!valid || (present && !addon_strings_validate(addon->strings.root))) {
            report(addon, "missing/invalid strings JSON; mod disabled");
            return 0;
        }
    }
    if (!addon->id) {
        addon->id = addon->folder;
    }
    for (Addon *prior = addons; prior < addon; ++prior) {
        if (prior->id && text_equal_ignore_case(prior->id, addon->id)) {
            report(addon, "duplicate addon ID; addon disabled");
            return 0;
        }
    }
    if (!media || media->kind == JSON_STRING) {
        addon->media = join_utf8(addon->directory, media ? media->text : "media",
                                 media ? media->length : 5, 0);
        if (!addon->media) {
            return 0;
        }
        if (!exists(addon->media, 1)) {
            HeapFree(GetProcessHeap(), 0, addon->media);
            addon->media = NULL;
        }
    }
    if (!dependencies_ready(addon, 0)) {
        report(addon, "dependency missing, disabled or later in load order; addon disabled");
        return 0;
    }
    addon->valid = 1;
    if (!addon->database.root && dependencies_ready(addon, 1)) {
        InterlockedExchange(&addon->active, 1);
    }
    report(addon, addon->database.root ? "database queued; media awaits successful merge"
                                       : "media addon ready");
    return 1;
}

static BOOL CALLBACK discover(PINIT_ONCE once, void *parameter, void **context) {
    (void)once;
    (void)parameter;
    (void)context;
    if (!mods_directory[0]) {
        return TRUE;
    }
    wchar_t *path = join_utf8(mods_directory, "loadorder.txt", 13, 0);
    if (!path) {
        return TRUE;
    }
    size_t length;
    load_order = refresh_load_order(path, &length);
    HeapFree(GetProcessHeap(), 0, path);
    if (!load_order) {
        report(NULL, "cannot read/update UTF-8 loadorder.txt (64 KiB maximum); "
                     "loader disabled");
        return TRUE;
    }
    char *cursor = load_order;
    if (length >= 3 && memory_equal(cursor, "\xef\xbb\xbf", 3)) {
        cursor += 3;
    }
    while (*cursor) {
        char *start = cursor;
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            ++cursor;
        }
        if (*cursor) {
            *cursor++ = 0;
        }
        while (*start == ' ' || *start == '\t') {
            ++start;
        }
        size_t size = text_length(start);
        while (size && (start[size - 1] == ' ' || start[size - 1] == '\t')) {
            start[--size] = 0;
        }
        if (!size || *start == '#') {
            continue;
        }
        if (count == MAX_ADDONS || !relative_path(start, size, 1)) {
            report(NULL, "invalid/too many load-order entries; loader disabled");
            goto invalid;
        }
        Addon *addon = &addons[count++];
        addon->folder = start;
        addon->directory = join_utf8(mods_directory, start, size, 1);
        if (!addon->directory || !exists(addon->directory, 1)) {
            report(addon, "folder missing; addon disabled");
            continue;
        }
        for (Addon *prior = addons; prior < addon; ++prior) {
            if (prior->directory && !lstrcmpiW(prior->directory, addon->directory)) {
                report(NULL, "duplicate folder in loadorder.txt; loader disabled");
                goto invalid;
            }
        }
        if (!prepare_addon(addon)) {
            addon->valid = 0;
            InterlockedExchange(&addon->active, 0);
            report(addon, "addon rejected; its media will not be used");
        }
    }
    return TRUE;
invalid:
    plan_disabled = 1;
    for (unsigned i = 0; i < count; ++i) {
        addons[i].valid = 0;
        InterlockedExchange(&addons[i].active, 0);
    }
    return TRUE;
}

unsigned addons_media_revision(void) {
    return (unsigned)InterlockedCompareExchange(&media_revision, 0, 0);
}

unsigned addons_count(void) {
    InitOnceExecuteOnce(&discovery, discover, NULL, NULL);
    return plan_disabled ? 0 : count;
}

const wchar_t *addons_directory(unsigned index) {
    return index < addons_count() && addons[index].valid ? addons[index].directory : NULL;
}

const wchar_t *addons_media_folder(unsigned index) {
    return index < addons_count() && addons[index].valid ? addons[index].media : NULL;
}

int addons_media_active(unsigned index) {
    return index < count && InterlockedCompareExchange(&addons[index].active, 0, 0) != 0;
}

const JsonValue *addons_strings_document(unsigned index) {
    return index < addons_count() && addons[index].valid ? addons[index].strings.root : NULL;
}


static void hash_vanilla(void) {
    if (hash_ready) {
        return;
    }
    hash_ready = 1;
    static const char database_path[] = "media/stripped/gamedbRC.slt";
    wchar_t *path = join_utf8(content_directory, database_path, sizeof(database_path) - 1, 0);
    if (!path) {
        return;
    }
    HANDLE file =
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    HeapFree(GetProcessHeap(), 0, path);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    Sha256Context hash;
    sha256_initialize(&hash);
    unsigned char *buffer = HeapAlloc(GetProcessHeap(), 0, 65536), digest[32];
    DWORD bytes;
    int valid = buffer != NULL;
    while (valid) {
        valid = ReadFile(file, buffer, 65536, &bytes, NULL) != 0;
        if (!valid || !bytes) {
            break;
        }
        sha256_update(&hash, buffer, bytes);
    }
    CloseHandle(file);
    if (buffer) {
        HeapFree(GetProcessHeap(), 0, buffer);
    }
    if (!valid) {
        return;
    }
    sha256_finalize(&hash, digest);
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        vanilla_hash[i * 2] = hex[digest[i] >> 4];
        vanilla_hash[i * 2 + 1] = hex[digest[i] & 15];
    }
    vanilla_hash[64] = 0;
    report(NULL, "vanilla database SHA-256:");
    report(NULL, vanilla_hash);
}

static void merge_database(void **connection) {
    plugins_database_ready(NULL);
    unsigned total = addons_count();
    int needed = 0;
    for (unsigned i = 0; i < total; ++i) {
        needed |= addons[i].valid && addons[i].database.root;
    }
    if ((!needed && !engine_swaps_enabled() && !transmission_swaps_enabled()) || !*connection) {
        return;
    }
    const AddonSqlite *api = addon_database_api();
    void *copy = addon_database_clone(*connection);
    if (!copy) {
        report(NULL, "cannot create in-memory database; database addons disabled");
        return;
    }
    if (needed) {
        hash_vanilla();
    }
    unsigned applied = 0;
    JsonValue *documents[MAX_ADDONS];
    const char *labels[MAX_ADDONS];
    unsigned char excluded[MAX_ADDONS];
    for (unsigned i = 0; i < total; ++i) {
        documents[i] = addons[i].database.root;
        labels[i] = addons[i].id;
        excluded[i] = !addons[i].valid;
    }
    // A failed higher-priority transaction must not retain ID reservations.
    // Rebuild from the untouched source until the set of successful mods is stable.
    int stable = 0;
    for (unsigned pass = 0; pass <= total && !stable; ++pass) {
        for (unsigned i = 0; i < total; ++i) {
            InterlockedExchange(&addons[i].active, 0);
        }
        if (!addon_ids_plan(*connection, documents, labels, excluded, total)) {
            report(
                NULL,
                "ID planning failed; original database retained and database mod media disabled");
            api->close(copy);
            addon_ids_reset();
            return;
        }
        stable = 1;
        applied = 0;
        for (unsigned i = 0; i < total; ++i) {
            Addon *addon = &addons[i];
            InterlockedExchange(&addon->active, 0);
            if (!addon->valid || excluded[i] || addon_ids_blocked(i)) {
                if (addon_ids_blocked(i)) {
                    report(addon, "ID conflict or invalid ID policy; mod skipped (use "
                                  "policy.id_conflicts: nearest to opt in)");
                }
                continue;
            }
            if (!dependencies_ready(addon, 1)) {
                report(addon, "dependency did not load; addon skipped");
                continue;
            }
            if (addon->database.root) {
                char error[256];
                if (!addon_database_apply(copy, addon->database.root,
                                          vanilla_hash[0] ? vanilla_hash : NULL, addon->id,
                                          error)) {
                    report(addon, error);
                    report(addon, "transaction rolled back; media disabled");
                    excluded[i] = 1;
                    stable = 0;
                    continue;
                }
                ++applied;
                report(addon, "database transaction committed in memory");
            }
            InterlockedExchange(&addon->active, 1);
        }
        if (stable) {
            for (unsigned i = 0; i < total; ++i) {
                if (!excluded[i] && addons[i].valid && !addons[i].active) {
                    excluded[i] = 1;
                    stable = 0;
                }
            }
        }
        if (!stable) {
            report(NULL, "skipped/failed mods released ID reservations; rebuilding merge from "
                         "original database");
            api->close(copy);
            copy = addon_database_clone(*connection);
            if (!copy) {
                addon_ids_reset();
                for (unsigned i = 0; i < total; ++i) {
                    InterlockedExchange(&addons[i].active, 0);
                }
                return;
            }
        }
    }
    int engines = engine_swaps_expand(copy);
    int transmissions = transmission_swaps_expand(copy);
    if ((applied || engines || transmissions) && !api->close(*connection)) {
        *connection = copy;
        for (unsigned i = 0; i < total; ++i) {
            if (addons[i].active) {
                addon_ids_committed(i);
            }
        }
        report(NULL, "merged database ready; original SLT unchanged");
    } else {
        api->close(copy);
        if (applied || engines || transmissions) {
            for (unsigned i = 0; i < total; ++i) {
                InterlockedExchange(&addons[i].active, 0);
            }
            report(NULL, "original connection busy; database handoff refused and mod "
                         "media disabled");
        }
    }
    InterlockedIncrement(&media_revision);
    if(*connection==copy)plugins_database_ready(copy);
    if (applied && *connection == copy) {
        addon_wheels_register_database(*connection);
        addon_strings_refresh();
    }
}

typedef struct {
    char data[16];
    size_t length, capacity;
} GameString;

typedef void **(*OpenDatabase)(void **, const GameString *, unsigned char, unsigned char, void *);

static void **open_database(void **output, const GameString *path, unsigned char a, unsigned char b,
                            void *extra) {
    if (!addons_configured() && !engine_swaps_enabled() && !transmission_swaps_enabled()) {
        return ((OpenDatabase)(image + 0x108e900))(output, path, a, b, extra);
    }
    addons_count();
    AcquireSRWLockExclusive(&database_lock);
    captured_database = 0;
    InterlockedExchange(&database_thread, (LONG)GetCurrentThreadId());
    void **result = ((OpenDatabase)(image + 0x108e900))(output, path, a, b, extra);
    InterlockedExchange(&database_thread, 0);
    ReleaseSRWLockExclusive(&database_lock);
    return result;
}

static int open_sqlite(const char *filename, void **database) {
    int result = addon_database_api()->open(filename, database);
    if (!result &&
        InterlockedCompareExchange(&database_thread, 0, 0) == (LONG)GetCurrentThreadId() &&
        !captured_database) {
        captured_database = 1;
        merge_database(database);
        driver_models_database(*database);
    }
    return result;
}

static unsigned char original[4][12], replacement[4][12];
static const unsigned char *accepted[4][2];
PatchDefinition addons_hook_patches[4];

void addons_prepare_hooks(unsigned char *base) {
    image = base;
    addon_database_bind(base);
    const unsigned slots[] = {0x108e9d4, 0x108d524, 0xf65322, 0x109312e};
    const unsigned targets[] = {0x108e900, 0x7d6c0};
    const char *names[] = {"addon_database_open_thunk", "addon_sqlite_open_thunk",
                           "addon_database_open", "addon_sqlite_open"};
    const uintptr_t callbacks[] = {(uintptr_t)open_database, (uintptr_t)open_sqlite};
    for (unsigned i = 0; i < 4; ++i) {
        unsigned size = i < 2 ? 12 : 5;
        if (i < 2) {
            for (unsigned j = 0; j < 12; ++j) {
                original[i][j] = 0xcc;
            }
            const unsigned char jump[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
            memory_copy(replacement[i], jump, 12);
            memory_copy(replacement[i] + 2, &callbacks[i], 8);
        } else {
            original[i][0] = replacement[i][0] = 0xe8;
            int32_t old = (int32_t)(targets[i - 2] - slots[i] - 5),
                    next = (int32_t)(slots[i - 2] - slots[i] - 5);
            memory_copy(original[i] + 1, &old, 4);
            memory_copy(replacement[i] + 1, &next, 4);
        }
        accepted[i][0] = original[i];
        accepted[i][1] = replacement[i];
        addons_hook_patches[i] = (PatchDefinition){
            slots[i],       0,           size, GROUP_ADDONS, names[i], original[i], replacement[i],
            replacement[i], accepted[i], 2};
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestAddonLocations(const wchar_t *executable, wchar_t *mods,
                                                 wchar_t *content) {
    addons_configure(executable);
    lstrcpyW(mods, mods_directory);
    lstrcpyW(content, content_directory);
}

__declspec(dllexport) void Fh2TestAddonPaths(const wchar_t *mods, const wchar_t *content) {
    clear_plan();
    lstrcpyW(mods_directory, mods);
    lstrcpyW(content_directory, content);
    check_load_order();
}

__declspec(dllexport) unsigned Fh2TestAddonCount(void) {
    return addons_count();
}

__declspec(dllexport) int Fh2TestAddonActive(unsigned i) {
    return addons_media_active(i);
}

__declspec(dllexport) void Fh2TestAddonMerge(void **connection) {
    merge_database(connection);
}
#endif
