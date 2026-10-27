#include "azimuth_saves.h"
#include "azimuth_host.h"
#include "support.h"

/* Saves in Documents (<Documents>\My Games\Forza Horizon 2\Saves\1A96545B).
   Azimuth refuses a saves folder that is a link ("local connected storage:
   Runtime storage path is a link"), so the folder is not linked: Azimuth's own
   KERNEL32 imports (its import address table only; its code is not touched or
   inspected) are wrapped and any path that names the game-folder saves folder
   or a file below it is rewritten to the Documents folder, a real directory.
   Paths handed back to Azimuth (GetFinalPathNameByHandleW,
   GetFileInformationByHandleEx name classes) are mapped back, so Azimuth keeps
   seeing the folder it asked for. */

#define TITLE_ID L"1A96545B"
#define ROOT_CAPACITY 2048u
#define COPY_CHUNK (1u << 20)

typedef HANDLE(WINAPI *CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef BOOL(WINAPI *CreateDirectoryWFn)(LPCWSTR, LPSECURITY_ATTRIBUTES);
typedef BOOL(WINAPI *PathOnlyFn)(LPCWSTR);
typedef DWORD(WINAPI *GetFileAttributesWFn)(LPCWSTR);
typedef BOOL(WINAPI *GetFileAttributesExWFn)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef HANDLE(WINAPI *FindFirstFileWFn)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef HANDLE(WINAPI *FindFirstFileExWFn)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS,
                                           LPVOID, DWORD);
typedef BOOL(WINAPI *MoveFileExWFn)(LPCWSTR, LPCWSTR, DWORD);
typedef BOOL(WINAPI *CopyFileWFn)(LPCWSTR, LPCWSTR, BOOL);
typedef BOOL(WINAPI *GetDiskFreeSpaceExWFn)(LPCWSTR, PULARGE_INTEGER, PULARGE_INTEGER,
                                            PULARGE_INTEGER);
typedef DWORD(WINAPI *GetFinalPathFn)(HANDLE, LPWSTR, DWORD, DWORD);
typedef BOOL(WINAPI *HandleInfoFn)(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);
typedef DWORD(WINAPI *GetProfileStringFn)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR);
typedef BOOL(WINAPI *WriteProfileStringFn)(LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR);

enum {
    API_CREATE_FILE,
    API_CREATE_DIRECTORY,
    API_REMOVE_DIRECTORY,
    API_DELETE_FILE,
    API_MOVE_FILE,
    API_COPY_FILE,
    API_GET_ATTRIBUTES,
    API_GET_ATTRIBUTES_EX,
    API_FIND_FIRST,
    API_FIND_FIRST_EX,
    API_FINAL_PATH,
    API_SET_INFO,
    API_GET_INFO,
    API_DISK_FREE,
    API_GET_PROFILE,
    API_WRITE_PROFILE,
    API_COUNT
};

static void *originals[API_COUNT];
static volatile LONG first_use[API_COUNT];
static volatile LONG active, first_redirect, first_reverse;
static wchar_t game_root[ROOT_CAPACITY], docs_root[ROOT_CAPACITY];
static wchar_t documents_base[ROOT_CAPACITY];
static size_t game_length, docs_length;
static const wchar_t *game_leaf;

/* ---- log: kept in memory until the mod's log is (re)started ---- */

static SRWLOCK log_lock = SRWLOCK_INIT;
static wchar_t log_path[ROOT_CAPACITY];
static char pending[16384];
static size_t pending_length;
static int log_ready, pending_dropped;

static size_t wide_length(const wchar_t *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

static void write_log_file(const char *text, size_t length) {
    if (!log_path[0] || !length) return;
    HANDLE file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, text, (DWORD)length, &written, NULL);
    CloseHandle(file);
}

static void saves_log(const char *text) {
    DWORD error = GetLastError();
    size_t length = text_length(text);
    AcquireSRWLockExclusive(&log_lock);
    if (log_ready) {
        write_log_file(text, length);
    } else if (pending_length + length < sizeof(pending)) {
        memory_copy(pending + pending_length, text, length);
        pending_length += length;
    } else {
        pending_dropped = 1;
    }
    ReleaseSRWLockExclusive(&log_lock);
    SetLastError(error);
}

typedef struct {
    char text[1536];
    size_t length;
} Line;

static void line_text(Line *line, const char *text) {
    while (*text && line->length + 1 < sizeof(line->text)) line->text[line->length++] = *text++;
    line->text[line->length] = 0;
}

static void line_path(Line *line, const wchar_t *path) {
    if (!path) {
        line_text(line, "(null)");
        return;
    }
    int room = (int)(sizeof(line->text) - line->length - 1);
    int written = room > 0 ? WideCharToMultiByte(CP_UTF8, 0, path, (int)wide_length(path),
                                                 line->text + line->length, room, NULL, NULL)
                           : 0;
    if (written > 0) line->length += (size_t)written;
    line->text[line->length] = 0;
}

static void line_number(Line *line, unsigned long long value) {
    char digits[24];
    char *cursor = digits + sizeof(digits) - 1;
    *cursor = 0;
    do {
        *--cursor = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    line_text(line, cursor);
}

static void line_time(Line *line, FILETIME time) {
    /* Days since 1601 to a civil UTC date and time (no extra imports). */
    unsigned long long ticks = ((unsigned long long)time.dwHighDateTime << 32) | time.dwLowDateTime;
    unsigned long long seconds = ticks / 10000000ull;
    long long days = (long long)(seconds / 86400ull) - 134774; /* to 1970-01-01 */
    unsigned rest = (unsigned)(seconds % 86400ull);
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long year = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1, month = mp < 10 ? mp + 3 : mp - 9;
    year += month <= 2;
    char text[24] = "0000-00-00 00:00:00 UTC";
    unsigned values[] = {(unsigned)year, month, day, rest / 3600, rest / 60 % 60, rest % 60};
    unsigned offsets[] = {0, 5, 8, 11, 14, 17}, widths[] = {4, 2, 2, 2, 2, 2};
    for (unsigned i = 0; i < 6; ++i) {
        unsigned value = values[i];
        for (unsigned d = widths[i]; d; --d) {
            text[offsets[i] + d - 1] = (char)('0' + value % 10);
            value /= 10;
        }
    }
    line_text(line, text);
}

static void line_end(Line *line) {
    line_text(line, "\r\n");
    saves_log(line->text);
}

void saves_redirect_log_ready(void) {
    AcquireSRWLockExclusive(&log_lock);
    log_ready = 1;
    write_log_file(pending, pending_length);
    if (pending_dropped) {
        static const char dropped[] = "SAVES: some early save-redirection log lines were dropped.\r\n";
        write_log_file(dropped, sizeof(dropped) - 1);
    }
    pending_length = 0;
    pending_dropped = 0;
    ReleaseSRWLockExclusive(&log_lock);
}

/* ---- paths ---- */

static wchar_t fold(wchar_t c) {
    return c >= L'a' && c <= L'z' ? (wchar_t)(c - 32) : c == L'/' ? L'\\' : c;
}

/* text[0..length) starts with root (case-insensitive, / = \) at a component boundary. */
static int root_prefix(const wchar_t *text, size_t length, const wchar_t *root, size_t root_length) {
    if (length < root_length) return 0;
    for (size_t i = 0; i < root_length; ++i) {
        if (fold(text[i]) != fold(root[i])) return 0;
    }
    return length == root_length || text[root_length] == L'\\' || text[root_length] == L'/';
}

static int contains_folded(const wchar_t *text, const wchar_t *needle) {
    size_t needle_length = wide_length(needle);
    for (; *text; ++text) {
        size_t i = 0;
        while (i < needle_length && text[i] && fold(text[i]) == fold(needle[i])) ++i;
        if (i == needle_length) return 1;
    }
    return 0;
}

static int starts_folded(const wchar_t *text, const wchar_t *prefix) {
    for (; *prefix; ++text, ++prefix) {
        if (fold(*text) != fold(*prefix)) return 0;
    }
    return 1;
}

static wchar_t *heap_text(size_t characters) {
    return (wchar_t *)HeapAlloc(GetProcessHeap(), 0, characters * sizeof(wchar_t));
}

static void heap_release(void *memory) {
    if (memory) {
        DWORD error = GetLastError();
        HeapFree(GetProcessHeap(), 0, memory);
        SetLastError(error);
    }
}

static const char *const api_names[API_COUNT] = {
    "CreateFileW",          "CreateDirectoryW",          "RemoveDirectoryW",
    "DeleteFileW",          "MoveFileExW",               "CopyFileW",
    "GetFileAttributesW",   "GetFileAttributesExW",      "FindFirstFileW",
    "FindFirstFileExW",     "GetFinalPathNameByHandleW", "SetFileInformationByHandle",
    "GetFileInformationByHandleEx", "GetDiskFreeSpaceExW", "GetPrivateProfileStringW",
    "WritePrivateProfileStringW"};

static void note_redirect(unsigned api, const wchar_t *from, const wchar_t *to, int reverse) {
    volatile LONG *first = reverse ? &first_reverse : &first_redirect;
    int api_first = api < API_COUNT && !InterlockedExchange(&first_use[api], 1);
    int overall_first = !InterlockedExchange(first, 1);
    if (!api_first && !overall_first) return;
    Line line;
    line.length = 0;
    line.text[0] = 0;
    line_text(&line, overall_first ? (reverse ? "SAVES: first path mapped back for Azimuth ("
                                              : "SAVES: first redirected path (")
                                   : "SAVES: first redirect through ");
    line_text(&line, api < API_COUNT ? api_names[api] : "mod");
    line_text(&line, overall_first ? "): " : ": ");
    line_path(&line, from);
    line_text(&line, " -> ");
    line_path(&line, to);
    line_end(&line);
}

/* A heap copy of path rewritten to the Documents folder, or NULL when the path is
   not the game-folder saves folder or below it. Keeps the \\?\ or \??\ prefix. */
static wchar_t *map_forward(const wchar_t *path, unsigned api) {
    if (!active || !path || !game_leaf || !contains_folded(path, game_leaf)) return NULL;
    DWORD error = GetLastError();
    size_t prefix = 0;
    if (path[0] == L'\\' && (path[1] == L'\\' || path[1] == L'?') && path[2] == L'?' &&
        path[3] == L'\\') {
        prefix = 4;
        if (starts_folded(path + 4, L"UNC\\")) return NULL;
    } else if ((path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/')) {
        return NULL; /* UNC share or \\.\ device */
    }
    wchar_t *full = NULL;
    const wchar_t *normal = path + prefix;
    if (!prefix) {
        full = heap_text(PATH_CAPACITY);
        DWORD count = full ? GetFullPathNameW(path, PATH_CAPACITY, full, NULL) : 0;
        if (!count || count >= PATH_CAPACITY) {
            heap_release(full);
            SetLastError(error);
            return NULL;
        }
        normal = full;
    }
    size_t normal_length = wide_length(normal);
    wchar_t *mapped = NULL;
    if (root_prefix(normal, normal_length, game_root, game_length)) {
        const wchar_t *tail = normal + game_length;
        size_t tail_length = normal_length - game_length;
        int long_form = !prefix && docs_length + tail_length >= 248;
        mapped = heap_text(4 + docs_length + tail_length + 1);
        if (mapped) {
            size_t at = 0;
            if (prefix) {
                for (; at < 4; ++at) mapped[at] = path[at];
            } else if (long_form) {
                mapped[at++] = L'\\', mapped[at++] = L'\\', mapped[at++] = L'?', mapped[at++] = L'\\';
            }
            for (size_t i = 0; i < docs_length; ++i) mapped[at++] = docs_root[i];
            for (size_t i = 0; i < tail_length; ++i) mapped[at++] = tail[i] == L'/' ? L'\\' : tail[i];
            mapped[at] = 0;
        }
    }
    heap_release(full);
    if (mapped) note_redirect(api, path, mapped, 0);
    SetLastError(error);
    return mapped;
}

/* Maps a Documents path that Windows returns back to the game-folder path.
   Handles DOS (\\?\C:\..., C:\...), volume-relative (\Users\...), volume GUID
   and \Device\ forms. Returns the new length, 0 when unchanged or too long. */
static size_t map_reverse(const wchar_t *text, size_t length, wchar_t *output, size_t capacity) {
    if (!active || !length) return 0;
    size_t q = 0, at;
    if (length >= 4 && text[0] == L'\\' && (text[1] == L'\\' || text[1] == L'?') && text[2] == L'?' &&
        text[3] == L'\\') {
        q = 4;
    }
    const wchar_t *match, *replacement;
    size_t match_length, replacement_length;
    int same_drive = fold(game_root[0]) == fold(docs_root[0]) && game_root[1] == L':' &&
                     docs_root[1] == L':';
    if (length >= q + 2 && text[q + 1] == L':') {
        at = q;
        match = docs_root, match_length = docs_length;
        replacement = game_root, replacement_length = game_length;
    } else if (same_drive) {
        match = docs_root + 2, match_length = docs_length - 2;
        replacement = game_root + 2, replacement_length = game_length - 2;
        if (!q && text[0] == L'\\' && starts_folded(text, L"\\Device\\")) {
            at = 8;
            while (at < length && text[at] != L'\\') ++at;
        } else if (!q && text[0] == L'\\') {
            at = 0;
        } else if (q && starts_folded(text + 4, L"Volume{")) {
            at = 4;
            while (at < length && text[at] != L'}') ++at;
            if (at < length) ++at;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
    if (!root_prefix(text + at, length - at, match, match_length)) return 0;
    size_t tail = length - at - match_length, total = at + replacement_length + tail;
    if (total + 1 > capacity) return 0;
    for (size_t i = 0; i < at; ++i) output[i] = text[i];
    for (size_t i = 0; i < replacement_length; ++i) output[at + i] = replacement[i];
    for (size_t i = 0; i < tail; ++i) output[at + replacement_length + i] = text[at + match_length + i];
    output[total] = 0;
    return total;
}

int saves_redirect_path(wchar_t *path) {
    wchar_t *mapped = path ? map_forward(path, API_COUNT) : NULL;
    if (!mapped) return 0;
    size_t length = wide_length(mapped);
    int fits = length < PATH_CAPACITY;
    if (fits) memory_copy(path, mapped, (length + 1) * sizeof(wchar_t));
    heap_release(mapped);
    return fits;
}

/* ---- hooks ---- */

#define ORIGINAL(api, type) ((type)originals[api])
#define PATH_OR(mapped, path) ((mapped) ? (const wchar_t *)(mapped) : (path))

static HANDLE WINAPI hook_create_file(LPCWSTR path, DWORD access, DWORD share,
                                      LPSECURITY_ATTRIBUTES security, DWORD disposition,
                                      DWORD flags, HANDLE template_file) {
    wchar_t *mapped = map_forward(path, API_CREATE_FILE);
    HANDLE result = ORIGINAL(API_CREATE_FILE, CreateFileWFn)(PATH_OR(mapped, path), access, share,
                                                            security, disposition, flags,
                                                            template_file);
    heap_release(mapped);
    return result;
}

static BOOL WINAPI hook_create_directory(LPCWSTR path, LPSECURITY_ATTRIBUTES security) {
    wchar_t *mapped = map_forward(path, API_CREATE_DIRECTORY);
    BOOL result = ORIGINAL(API_CREATE_DIRECTORY, CreateDirectoryWFn)(PATH_OR(mapped, path), security);
    heap_release(mapped);
    return result;
}

static BOOL WINAPI hook_remove_directory(LPCWSTR path) {
    wchar_t *mapped = map_forward(path, API_REMOVE_DIRECTORY);
    BOOL result = ORIGINAL(API_REMOVE_DIRECTORY, PathOnlyFn)(PATH_OR(mapped, path));
    heap_release(mapped);
    return result;
}

static BOOL WINAPI hook_delete_file(LPCWSTR path) {
    wchar_t *mapped = map_forward(path, API_DELETE_FILE);
    BOOL result = ORIGINAL(API_DELETE_FILE, PathOnlyFn)(PATH_OR(mapped, path));
    heap_release(mapped);
    return result;
}

static int different_drives(void) {
    return fold(game_root[0]) != fold(docs_root[0]);
}

static BOOL WINAPI hook_move_file(LPCWSTR from, LPCWSTR to, DWORD flags) {
    wchar_t *mapped_from = map_forward(from, API_MOVE_FILE);
    wchar_t *mapped_to = map_forward(to, API_MOVE_FILE);
    /* One side moved to another volume: let Windows copy across it. */
    if ((!mapped_from != !mapped_to) && to && different_drives()) flags |= MOVEFILE_COPY_ALLOWED;
    BOOL result = ORIGINAL(API_MOVE_FILE, MoveFileExWFn)(PATH_OR(mapped_from, from),
                                                        PATH_OR(mapped_to, to), flags);
    heap_release(mapped_from);
    heap_release(mapped_to);
    return result;
}

static BOOL WINAPI hook_copy_file(LPCWSTR from, LPCWSTR to, BOOL fail_if_exists) {
    wchar_t *mapped_from = map_forward(from, API_COPY_FILE);
    wchar_t *mapped_to = map_forward(to, API_COPY_FILE);
    BOOL result = ORIGINAL(API_COPY_FILE, CopyFileWFn)(PATH_OR(mapped_from, from),
                                                      PATH_OR(mapped_to, to), fail_if_exists);
    heap_release(mapped_from);
    heap_release(mapped_to);
    return result;
}

static DWORD WINAPI hook_get_attributes(LPCWSTR path) {
    wchar_t *mapped = map_forward(path, API_GET_ATTRIBUTES);
    DWORD result = ORIGINAL(API_GET_ATTRIBUTES, GetFileAttributesWFn)(PATH_OR(mapped, path));
    heap_release(mapped);
    return result;
}

static BOOL WINAPI hook_get_attributes_ex(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID data) {
    wchar_t *mapped = map_forward(path, API_GET_ATTRIBUTES_EX);
    BOOL result =
        ORIGINAL(API_GET_ATTRIBUTES_EX, GetFileAttributesExWFn)(PATH_OR(mapped, path), level, data);
    heap_release(mapped);
    return result;
}

static HANDLE WINAPI hook_find_first(LPCWSTR path, LPWIN32_FIND_DATAW data) {
    wchar_t *mapped = map_forward(path, API_FIND_FIRST);
    HANDLE result = ORIGINAL(API_FIND_FIRST, FindFirstFileWFn)(PATH_OR(mapped, path), data);
    heap_release(mapped);
    return result;
}

static HANDLE WINAPI hook_find_first_ex(LPCWSTR path, FINDEX_INFO_LEVELS level, LPVOID data,
                                        FINDEX_SEARCH_OPS search, LPVOID filter, DWORD flags) {
    wchar_t *mapped = map_forward(path, API_FIND_FIRST_EX);
    HANDLE result = ORIGINAL(API_FIND_FIRST_EX, FindFirstFileExWFn)(PATH_OR(mapped, path), level,
                                                                   data, search, filter, flags);
    heap_release(mapped);
    return result;
}

static BOOL WINAPI hook_disk_free(LPCWSTR path, PULARGE_INTEGER available, PULARGE_INTEGER total,
                                  PULARGE_INTEGER free_bytes) {
    wchar_t *mapped = map_forward(path, API_DISK_FREE);
    BOOL result = ORIGINAL(API_DISK_FREE, GetDiskFreeSpaceExWFn)(PATH_OR(mapped, path), available,
                                                                total, free_bytes);
    heap_release(mapped);
    return result;
}

static DWORD WINAPI hook_get_profile(LPCWSTR section, LPCWSTR key, LPCWSTR fallback, LPWSTR output,
                                     DWORD size, LPCWSTR file) {
    wchar_t *mapped = map_forward(file, API_GET_PROFILE);
    DWORD result = ORIGINAL(API_GET_PROFILE, GetProfileStringFn)(section, key, fallback, output,
                                                                size, PATH_OR(mapped, file));
    heap_release(mapped);
    return result;
}

static BOOL WINAPI hook_write_profile(LPCWSTR section, LPCWSTR key, LPCWSTR value, LPCWSTR file) {
    wchar_t *mapped = map_forward(file, API_WRITE_PROFILE);
    BOOL result = ORIGINAL(API_WRITE_PROFILE, WriteProfileStringFn)(section, key, value,
                                                                   PATH_OR(mapped, file));
    heap_release(mapped);
    return result;
}

/* The final path of a handle opened in the Documents folder reads as the
   game-folder path, so Azimuth's own comparisons still match. */
static DWORD WINAPI hook_final_path(HANDLE file, LPWSTR output, DWORD capacity, DWORD flags) {
    GetFinalPathFn original = ORIGINAL(API_FINAL_PATH, GetFinalPathFn);
    if (!active) return original(file, output, capacity, flags);
    wchar_t local[1024];
    wchar_t *buffer = local, *heap = NULL;
    DWORD buffer_capacity = ARRAY_COUNT(local);
    DWORD result = original(file, buffer, buffer_capacity, flags);
    if (result >= buffer_capacity && result < PATH_CAPACITY) {
        heap = heap_text(result + 1);
        if (!heap) return original(file, output, capacity, flags);
        buffer = heap, buffer_capacity = result + 1;
        result = original(file, buffer, buffer_capacity, flags);
    }
    if (!result || result >= buffer_capacity) {
        /* Failed (or the name grew between calls): Windows' own answer. */
        heap_release(heap);
        return original(file, output, capacity, flags);
    }
    DWORD error = GetLastError();
    size_t mapped_capacity = result + game_length + 8;
    wchar_t *mapped = heap_text(mapped_capacity);
    size_t mapped_length = mapped ? map_reverse(buffer, result, mapped, mapped_capacity) : 0;
    const wchar_t *answer_text = buffer;
    size_t answer_length = result;
    if (mapped_length) {
        note_redirect(API_FINAL_PATH, buffer, mapped, 1);
        answer_text = mapped, answer_length = mapped_length;
    }
    DWORD answer;
    if (answer_length < capacity && output) {
        memory_copy(output, answer_text, (answer_length + 1) * sizeof(wchar_t));
        answer = (DWORD)answer_length;
    } else {
        answer = (DWORD)answer_length + 1; /* required size, as Windows reports it */
    }
    heap_release(mapped);
    heap_release(heap);
    SetLastError(error);
    return answer;
}

static BOOL WINAPI hook_get_info(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID data,
                                 DWORD size) {
    BOOL result = ORIGINAL(API_GET_INFO, HandleInfoFn)(file, kind, data, size);
    /* FileNameInfo (2) and FileNormalizedNameInfo (24): volume-relative names. */
    if (!result || !active || ((unsigned)kind != 2 && (unsigned)kind != 24) || !data ||
        size < offsetof(FILE_NAME_INFO, FileName)) {
        return result;
    }
    DWORD error = GetLastError();
    FILE_NAME_INFO *info = (FILE_NAME_INFO *)data;
    size_t room = (size - offsetof(FILE_NAME_INFO, FileName)) / sizeof(wchar_t);
    size_t length = info->FileNameLength / sizeof(wchar_t);
    if (length <= room && length < PATH_CAPACITY) {
        wchar_t *mapped = heap_text(length + game_length + 8);
        size_t mapped_length = mapped ? map_reverse(info->FileName, length, mapped,
                                                    length + game_length + 8)
                                      : 0;
        if (mapped_length && mapped_length <= room) {
            note_redirect(API_GET_INFO, NULL, mapped, 1);
            memory_copy(info->FileName, mapped, mapped_length * sizeof(wchar_t));
            if (mapped_length < room) info->FileName[mapped_length] = 0;
            info->FileNameLength = (DWORD)(mapped_length * sizeof(wchar_t));
        }
        heap_release(mapped);
    }
    SetLastError(error);
    return result;
}

/* FileRenameInfo (3) and FileRenameInfoEx (22) carry the target path. */
static BOOL WINAPI hook_set_info(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, LPVOID data,
                                 DWORD size) {
    HandleInfoFn original = ORIGINAL(API_SET_INFO, HandleInfoFn);
    const size_t header = offsetof(FILE_RENAME_INFO, FileName);
    if (!active || ((unsigned)kind != 3 && (unsigned)kind != 22) || !data || size < header) {
        return original(file, kind, data, size);
    }
    FILE_RENAME_INFO *info = (FILE_RENAME_INFO *)data;
    size_t length = info->FileNameLength / sizeof(wchar_t);
    if (info->RootDirectory || header + info->FileNameLength > size || length >= PATH_CAPACITY) {
        return original(file, kind, data, size);
    }
    wchar_t *name = heap_text(length + 1);
    if (!name) return original(file, kind, data, size);
    memory_copy(name, info->FileName, length * sizeof(wchar_t));
    name[length] = 0;
    wchar_t *mapped = map_forward(name, API_SET_INFO);
    heap_release(name);
    if (!mapped) return original(file, kind, data, size);
    size_t mapped_length = wide_length(mapped);
    DWORD new_size = (DWORD)(header + (mapped_length + 1) * sizeof(wchar_t));
    unsigned char *copy = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, new_size);
    BOOL result;
    if (!copy) {
        heap_release(mapped);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    memory_copy(copy, data, header);
    FILE_RENAME_INFO *renamed = (FILE_RENAME_INFO *)copy;
    renamed->FileNameLength = (DWORD)(mapped_length * sizeof(wchar_t));
    memory_copy(renamed->FileName, mapped, (mapped_length + 1) * sizeof(wchar_t));
    result = original(file, kind, copy, new_size);
    heap_release(copy);
    heap_release(mapped);
    return result;
}

typedef struct {
    unsigned api;
    void *hook;
    int required;
} HookEntry;

static const HookEntry hooks[] = {
    {API_CREATE_FILE, (void *)(uintptr_t)hook_create_file, 1},
    {API_CREATE_DIRECTORY, (void *)(uintptr_t)hook_create_directory, 1},
    {API_REMOVE_DIRECTORY, (void *)(uintptr_t)hook_remove_directory, 1},
    {API_DELETE_FILE, (void *)(uintptr_t)hook_delete_file, 1},
    {API_MOVE_FILE, (void *)(uintptr_t)hook_move_file, 1},
    {API_COPY_FILE, (void *)(uintptr_t)hook_copy_file, 0},
    {API_GET_ATTRIBUTES, (void *)(uintptr_t)hook_get_attributes, 1},
    {API_GET_ATTRIBUTES_EX, (void *)(uintptr_t)hook_get_attributes_ex, 1},
    {API_FIND_FIRST, (void *)(uintptr_t)hook_find_first, 1},
    {API_FIND_FIRST_EX, (void *)(uintptr_t)hook_find_first_ex, 1},
    {API_FINAL_PATH, (void *)(uintptr_t)hook_final_path, 1},
    {API_SET_INFO, (void *)(uintptr_t)hook_set_info, 1},
    {API_GET_INFO, (void *)(uintptr_t)hook_get_info, 0},
    {API_DISK_FREE, (void *)(uintptr_t)hook_disk_free, 0},
    {API_GET_PROFILE, (void *)(uintptr_t)hook_get_profile, 0},
    {API_WRITE_PROFILE, (void *)(uintptr_t)hook_write_profile, 0},
};

/* The KERNEL32 functions as this DLL's own imports resolve them: an
   unpatched Azimuth slot holds the same address. */
static void *expected_function(unsigned api) {
    switch (api) {
    case API_CREATE_FILE: return (void *)(uintptr_t)CreateFileW;
    case API_CREATE_DIRECTORY: return (void *)(uintptr_t)CreateDirectoryW;
    case API_REMOVE_DIRECTORY: return (void *)(uintptr_t)RemoveDirectoryW;
    case API_DELETE_FILE: return (void *)(uintptr_t)DeleteFileW;
    case API_MOVE_FILE: return (void *)(uintptr_t)MoveFileExW;
    case API_COPY_FILE: return (void *)(uintptr_t)CopyFileW;
    case API_GET_ATTRIBUTES: return (void *)(uintptr_t)GetFileAttributesW;
    case API_GET_ATTRIBUTES_EX: return (void *)(uintptr_t)GetFileAttributesExW;
    case API_FIND_FIRST: return (void *)(uintptr_t)FindFirstFileW;
    case API_FIND_FIRST_EX: return (void *)(uintptr_t)FindFirstFileExW;
    case API_FINAL_PATH: return (void *)(uintptr_t)GetFinalPathNameByHandleW;
    case API_SET_INFO: return (void *)(uintptr_t)SetFileInformationByHandle;
    case API_GET_INFO: return (void *)(uintptr_t)GetFileInformationByHandleEx;
    case API_DISK_FREE: return (void *)(uintptr_t)GetDiskFreeSpaceExW;
    case API_GET_PROFILE: return (void *)(uintptr_t)GetPrivateProfileStringW;
    case API_WRITE_PROFILE: return (void *)(uintptr_t)WritePrivateProfileStringW;
    default: return NULL;
    }
}

/* Patches Azimuth's KERNEL32 import slots, all or none of the required ones. */
static int patch_imports(unsigned char *host) {
    void **slots[ARRAY_COUNT(hooks)];
    for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) slots[i] = NULL;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)host;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(host + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress || imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) return 0;
    const IMAGE_IMPORT_DESCRIPTOR *descriptor =
        (const IMAGE_IMPORT_DESCRIPTOR *)(host + imports.VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        if (!descriptor->OriginalFirstThunk ||
            !text_equal_ignore_case((const char *)(host + descriptor->Name), "KERNEL32.dll")) {
            continue;
        }
        const IMAGE_THUNK_DATA64 *names =
            (const IMAGE_THUNK_DATA64 *)(host + descriptor->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *entries = (IMAGE_THUNK_DATA64 *)(host + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++entries) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const char *name =
                (const char *)((const IMAGE_IMPORT_BY_NAME *)(host + names->u1.AddressOfData))->Name;
            for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) {
                if (!slots[i] && text_equal_ignore_case(name, api_names[hooks[i].api])) {
                    slots[i] = (void **)&entries->u1.Function;
                }
            }
        }
    }
    int usable = 1;
    for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) {
        Line line;
        line.length = 0;
        line.text[0] = 0;
        if (!slots[i]) {
            line_text(&line, "SAVES: Azimuth does not import KERNEL32!");
        } else if (*slots[i] != expected_function(hooks[i].api)) {
            line_text(&line, "SAVES: another module owns Azimuth's KERNEL32!");
            slots[i] = NULL;
        } else {
            continue;
        }
        line_text(&line, api_names[hooks[i].api]);
        line_text(&line, hooks[i].required ? " (required); save redirection is off.\r\n"
                                           : "; not hooked.\r\n");
        saves_log(line.text);
        if (hooks[i].required) usable = 0;
    }
    if (!usable) return 0;
    for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) {
        originals[hooks[i].api] = expected_function(hooks[i].api);
    }
    for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) {
        DWORD protection, ignored;
        if (!slots[i]) continue;
        if (!VirtualProtect(slots[i], sizeof(void *), PAGE_READWRITE, &protection)) {
            /* Undo the ones already written so redirection is all or nothing. */
            for (unsigned j = 0; j < i; ++j) {
                if (slots[j] && VirtualProtect(slots[j], sizeof(void *), PAGE_READWRITE, &protection)) {
                    InterlockedExchangePointer(slots[j], expected_function(hooks[j].api));
                    VirtualProtect(slots[j], sizeof(void *), protection, &ignored);
                }
            }
            saves_log("SAVES: Azimuth's import table could not be written; save redirection is off.\r\n");
            return 0;
        }
        InterlockedExchangePointer(slots[i], hooks[i].hook);
        VirtualProtect(slots[i], sizeof(void *), protection, &ignored);
        Line line;
        line.length = 0;
        line.text[0] = 0;
        line_text(&line, "SAVES: hooked Azimuth import KERNEL32!");
        line_text(&line, api_names[hooks[i].api]);
        line_end(&line);
    }
    return 1;
}

/* ---- folder scan, copy and verification (DllMain, one thread) ---- */

typedef struct {
    unsigned long long files, bytes, links, errors;
    FILETIME newest;
} TreeStats;

static wchar_t source_path[PATH_CAPACITY], target_path[PATH_CAPACITY];

static size_t push_name(wchar_t *path, size_t length, const wchar_t *name) {
    size_t name_length = wide_length(name);
    if (length + 1 + name_length + 2 >= PATH_CAPACITY) return 0;
    if (length && path[length - 1] != L'\\') path[length++] = L'\\';
    memory_copy(path + length, name, (name_length + 1) * sizeof(wchar_t));
    return length + name_length;
}

static int dot_entry(const WIN32_FIND_DATAW *data) {
    return data->cFileName[0] == L'.' &&
           (!data->cFileName[1] || (data->cFileName[1] == L'.' && !data->cFileName[2]));
}

static int later(FILETIME a, FILETIME b) {
    return a.dwHighDateTime != b.dwHighDateTime ? a.dwHighDateTime > b.dwHighDateTime
                                                : a.dwLowDateTime > b.dwLowDateTime;
}

/* Counts files below source_path[0..length); reparse points are counted, not entered. */
static void scan_tree(size_t length, TreeStats *stats) {
    WIN32_FIND_DATAW data;
    size_t pattern = push_name(source_path, length, L"*");
    HANDLE find = pattern ? FindFirstFileW(source_path, &data) : INVALID_HANDLE_VALUE;
    source_path[length] = 0;
    if (find == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) ++stats->errors;
        return;
    }
    do {
        if (dot_entry(&data)) continue;
        size_t child = push_name(source_path, length, data.cFileName);
        if (!child) {
            ++stats->errors;
        } else if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            ++stats->links;
        } else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_tree(child, stats);
        } else {
            ++stats->files;
            stats->bytes += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            if (later(data.ftLastWriteTime, stats->newest)) stats->newest = data.ftLastWriteTime;
        }
        source_path[length] = 0;
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static int stats_of(const wchar_t *root, TreeStats *stats) {
    size_t length = wide_length(root);
    *stats = (TreeStats){0, 0, 0, 0, {0, 0}};
    if (length >= PATH_CAPACITY) return 0;
    memory_copy(source_path, root, (length + 1) * sizeof(wchar_t));
    DWORD attributes = GetFileAttributesW(root);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 0;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        stats->links = 1;
        return 1;
    }
    scan_tree(length, stats);
    return 1;
}

static unsigned char *copy_buffer;

static int copy_one(const wchar_t *from, const wchar_t *to, const WIN32_FIND_DATAW *data) {
    HANDLE input = CreateFileW(from, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (input == INVALID_HANDLE_VALUE) return 0;
    HANDLE output = CreateFileW(to, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        CloseHandle(input);
        return 0;
    }
    int ok = 1;
    for (;;) {
        DWORD read = 0, written = 0;
        if (!ReadFile(input, copy_buffer, COPY_CHUNK, &read, NULL)) {
            ok = 0;
            break;
        }
        if (!read) break;
        if (!WriteFile(output, copy_buffer, read, &written, NULL) || written != read) {
            ok = 0;
            break;
        }
    }
    ok = ok && FlushFileBuffers(output) &&
         SetFileTime(output, &data->ftCreationTime, &data->ftLastAccessTime, &data->ftLastWriteTime);
    CloseHandle(output);
    CloseHandle(input);
    return ok;
}

/* Copies source_path[0..from) into target_path[0..to); never overwrites. */
static void copy_tree(size_t from, size_t to, TreeStats *copied) {
    WIN32_FIND_DATAW data;
    size_t pattern = push_name(source_path, from, L"*");
    HANDLE find = pattern ? FindFirstFileW(source_path, &data) : INVALID_HANDLE_VALUE;
    source_path[from] = 0;
    if (find == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) ++copied->errors;
        return;
    }
    do {
        if (dot_entry(&data)) continue;
        size_t source = push_name(source_path, from, data.cFileName);
        size_t target = push_name(target_path, to, data.cFileName);
        if (!source || !target || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            ++copied->errors; /* links are not followed or copied */
        } else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (CreateDirectoryW(target_path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
                copy_tree(source, target, copied);
            } else {
                ++copied->errors;
            }
        } else if (copy_one(source_path, target_path, &data)) {
            ++copied->files;
            copied->bytes += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        } else {
            ++copied->errors;
        }
        source_path[from] = 0;
        target_path[to] = 0;
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

/* Every file below source_path has a same-size twin below target_path. */
static void verify_tree(size_t from, size_t to, TreeStats *verified) {
    WIN32_FIND_DATAW data;
    size_t pattern = push_name(source_path, from, L"*");
    HANDLE find = pattern ? FindFirstFileW(source_path, &data) : INVALID_HANDLE_VALUE;
    source_path[from] = 0;
    if (find == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) ++verified->errors;
        return;
    }
    do {
        if (dot_entry(&data)) continue;
        size_t source = push_name(source_path, from, data.cFileName);
        size_t target = push_name(target_path, to, data.cFileName);
        WIN32_FILE_ATTRIBUTE_DATA twin;
        if (!source || !target || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            ++verified->errors;
        } else if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            verify_tree(source, target, verified);
        } else if (GetFileAttributesExW(target_path, GetFileExInfoStandard, &twin) &&
                   !(twin.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                   twin.nFileSizeHigh == data.nFileSizeHigh &&
                   twin.nFileSizeLow == data.nFileSizeLow) {
            ++verified->files;
            verified->bytes += ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        } else {
            ++verified->errors;
        }
        source_path[from] = 0;
        target_path[to] = 0;
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

/* Removes target_path and the directories below it only when no file or link
   is anywhere inside (RemoveDirectoryW never removes a non-empty folder). */
static int remove_empty_tree(size_t length) {
    WIN32_FIND_DATAW data;
    size_t pattern = push_name(target_path, length, L"*");
    HANDLE find = pattern ? FindFirstFileW(target_path, &data) : INVALID_HANDLE_VALUE;
    target_path[length] = 0;
    int empty = 1;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (dot_entry(&data)) continue;
            size_t child = push_name(target_path, length, data.cFileName);
            if (!child || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !remove_empty_tree(child)) {
                empty = 0;
            }
            target_path[length] = 0;
        } while (empty && FindNextFileW(find, &data));
        FindClose(find);
    }
    return empty && RemoveDirectoryW(target_path);
}

static void log_stats(Line *line, const TreeStats *stats) {
    line_number(line, stats->files);
    line_text(line, " files, ");
    line_number(line, stats->bytes);
    line_text(line, " bytes");
}

static void log_folder(const char *text, const wchar_t *path, const char *after) {
    Line line;
    line.length = 0;
    line.text[0] = 0;
    line_text(&line, text);
    line_path(&line, path);
    line_text(&line, after);
    line_end(&line);
}

static int ensure_directory(const wchar_t *path) {
    if (CreateDirectoryW(path, NULL)) return 1;
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

/* An intermediate folder (Documents\My Games, ...): any directory will do. */
static int ensure_any_directory(const wchar_t *path) {
    if (CreateDirectoryW(path, NULL)) return 1;
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static const TreeStats no_files = {0, 0, 0, 0, {0, 0}};

/* Copies source_root (never changed) into a fresh staging folder beside
   docs_root, verifies count and sizes, then renames the staging folder into
   place. 1 = docs_root now holds the verified copy. */
static int migrate_from(const wchar_t *source_root, const TreeStats *source, const char *label) {
    Line line;
    line.length = 0;
    line.text[0] = 0;
    size_t base = wide_length(docs_root);
    static const wchar_t suffix[] = L".migrating-";
    unsigned long long stamp = GetTickCount64();
    memory_copy(target_path, docs_root, base * sizeof(wchar_t));
    memory_copy(target_path + base, suffix, sizeof(suffix));
    size_t staging = base + ARRAY_COUNT(suffix) - 1;
    for (int shift = 60; shift >= 0; shift -= 4) {
        target_path[staging++] = L"0123456789abcdef"[(stamp >> shift) & 15];
    }
    target_path[staging] = 0;
    static wchar_t staging_path[ROOT_CAPACITY];
    memory_copy(staging_path, target_path, (staging + 1) * sizeof(wchar_t));
    if (!CreateDirectoryW(staging_path, NULL)) {
        log_folder("SAVES: cannot create ", staging_path, "; nothing copied.");
        return 0;
    }
    copy_buffer = (unsigned char *)VirtualAlloc(NULL, COPY_CHUNK, MEM_COMMIT | MEM_RESERVE,
                                                PAGE_READWRITE);
    TreeStats copied = no_files, verified = no_files, staged;
    size_t source_chars = wide_length(source_root);
    memory_copy(source_path, source_root, (source_chars + 1) * sizeof(wchar_t));
    if (copy_buffer) copy_tree(source_chars, staging, &copied);
    if (copy_buffer) VirtualFree(copy_buffer, 0, MEM_RELEASE);
    copy_buffer = NULL;
    memory_copy(source_path, source_root, (source_chars + 1) * sizeof(wchar_t));
    memory_copy(target_path, staging_path, (staging + 1) * sizeof(wchar_t));
    verify_tree(source_chars, staging, &verified);
    stats_of(staging_path, &staged);
    int ok = copied.files == source->files && !copied.errors && !verified.errors &&
             verified.files == source->files && verified.bytes == source->bytes &&
             staged.files == source->files && staged.bytes == source->bytes && !staged.links;
    if (!ok) {
        line_text(&line, "SAVES: migration FAILED from ");
        line_text(&line, label);
        line_path(&line, source_root);
        line_text(&line, ": source ");
        log_stats(&line, source);
        line_text(&line, ", copied ");
        log_stats(&line, &copied);
        line_text(&line, ", verified ");
        log_stats(&line, &verified);
        line_text(&line, " (");
        line_number(&line, copied.errors + verified.errors);
        line_text(&line, " errors). The partial copy stays in ");
        line_path(&line, staging_path);
        line_text(&line, " and the source is untouched.");
        line_end(&line);
        return 0;
    }
    memory_copy(target_path, docs_root, (base + 1) * sizeof(wchar_t));
    if (GetFileAttributesW(docs_root) != INVALID_FILE_ATTRIBUTES && !remove_empty_tree(base)) {
        log_folder("SAVES: the verified copy cannot replace the empty folder ", docs_root, ".");
        return 0;
    }
    if (!MoveFileExW(staging_path, docs_root, 0)) {
        log_folder("SAVES: the verified copy cannot be renamed to ", docs_root, ".");
        return 0;
    }
    line_text(&line, "SAVES: migrated ");
    log_stats(&line, source);
    line_text(&line, " from ");
    line_text(&line, label);
    line_path(&line, source_root);
    line_text(&line, " to ");
    line_path(&line, docs_root);
    line_text(&line, "; count and sizes verified; the source is kept as it was.");
    line_end(&line);
    return 1;
}

/* 1 = the Documents folder holds the saves (migrated, already there, or new).
   Source when it has no files: the game-folder saves. */
static int prepare_folder(void) {
    TreeStats game, docs;
    int game_exists = stats_of(game_root, &game);
    int docs_exists = stats_of(docs_root, &docs);
    Line line;
    line.length = 0;
    line.text[0] = 0;
    if (docs_exists && docs.links) {
        log_folder("SAVES: ", docs_root,
                   " is (or contains) a link; Azimuth refuses links, so save redirection is off.");
        return 0;
    }
    if (docs.errors) {
        log_folder("SAVES: ", docs_root, " cannot be read; save redirection is off.");
        return 0;
    }
    if (docs_exists && docs.files) {
        line_text(&line, "SAVES: using the Documents saves (");
        log_stats(&line, &docs);
        line_text(&line, ", last written ");
        line_time(&line, docs.newest);
        line_text(&line, ")");
        if (game_exists && (game.files || game.links)) {
            line_text(&line, "; the game-folder copy (");
            log_stats(&line, &game);
            line_text(&line, ", last written ");
            line_time(&line, game.newest);
            line_text(&line, ") is ignored and left untouched");
        }
        line_text(&line, ".");
        line_end(&line);
        if (game_exists && game.files && later(game.newest, docs.newest)) {
            log_folder("SAVES: WARNING: the game-folder saves were written after the Documents "
                       "ones. The game now uses the older Documents saves. To use the game-folder "
                       "saves instead, close the game, move ",
                       docs_root,
                       " aside (it is never deleted by the mod) and start the game: they are "
                       "copied again.");
        }
        return 1;
    }
    if (!game_exists || !game.files) {
        if (game_exists && game.links) {
            log_folder("SAVES: the game-folder saves folder ", game_root,
                       " is or contains a link; nothing copied.");
        }
        if (!ensure_directory(docs_root)) {
            log_folder("SAVES: cannot create ", docs_root, "; save redirection is off.");
            return 0;
        }
        log_folder("SAVES: no saves in the game folder or Documents yet; "
                   "new saves go to ",
                   docs_root, ".");
        return 1;
    }
    if (game.links || game.errors) {
        log_folder("SAVES: the game-folder saves in ", game_root,
                   " contain links or unreadable folders; nothing copied and save redirection "
                   "is off.");
        return 0;
    }
    saves_log("SAVES: migration source: the game-folder saves.\r\n");
    if (!migrate_from(game_root, &game, "the game folder ")) {
        saves_log("SAVES: the game uses the game-folder saves this session.\r\n");
        return 0;
    }
    return 1;
}

static int set_roots(const wchar_t *game, const wchar_t *documents) {
    DWORD count = GetFullPathNameW(game, ROOT_CAPACITY, game_root, NULL);
    if (!count || count >= ROOT_CAPACITY - 1) return 0;
    count = GetFullPathNameW(documents, ROOT_CAPACITY, docs_root, NULL);
    if (!count || count >= ROOT_CAPACITY - 1) return 0;
    game_length = wide_length(game_root);
    docs_length = wide_length(docs_root);
    while (game_length > 3 && game_root[game_length - 1] == L'\\') game_root[--game_length] = 0;
    while (docs_length > 3 && docs_root[docs_length - 1] == L'\\') {
        docs_root[--docs_length] = 0;
    }
    game_leaf = game_root + game_length;
    while (game_leaf > game_root && game_leaf[-1] != L'\\') --game_leaf;
    /* Neither folder may contain the other. */
    return game_length >= 4 && docs_length >= 4 && *game_leaf &&
           !root_prefix(docs_root, docs_length, game_root, game_length) &&
           !root_prefix(game_root, game_length, docs_root, docs_length);
}

static int directory_of(wchar_t *path, size_t capacity, HMODULE module) {
    DWORD count = GetModuleFileNameW(module, path, (DWORD)capacity);
    if (!count || count >= capacity) return 0;
    while (count && path[count - 1] != L'\\' && path[count - 1] != L'/') --count;
    path[count] = 0;
    return count != 0;
}

/* ---- the Documents known folder ---- */

typedef HRESULT(WINAPI *KnownFolderPathFn)(const GUID *, DWORD, HANDLE, PWSTR *);
typedef void(WINAPI *TaskMemFreeFn)(LPVOID);

/* FOLDERID_Documents {FDD39AD0-238F-46AF-ADB4-6C85480369C7} */
static const GUID documents_id = {0xFDD39AD0, 0x238F, 0x46AF,
                                  {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};

static HMODULE system_module(const wchar_t *name, int *loaded) {
    HMODULE module = GetModuleHandleW(name);
    *loaded = 0;
    if (!module) {
        module = LoadLibraryExW(name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        *loaded = module != NULL;
    }
    return module;
}

int saves_documents_folder(wchar_t *output, size_t capacity, int *fallback) {
    DWORD error = GetLastError();
    int found = 0, shell_loaded = 0, ole_loaded = 0;
    *fallback = 0;
    output[0] = 0;
    HMODULE shell = system_module(L"shell32.dll", &shell_loaded);
    KnownFolderPathFn known =
        shell ? (KnownFolderPathFn)(uintptr_t)GetProcAddress(shell, "SHGetKnownFolderPath") : NULL;
    PWSTR path = NULL;
    if (known && known(&documents_id, 0, NULL, &path) >= 0 && path) {
        size_t length = wide_length(path);
        if (length >= 3 && length < capacity) {
            memory_copy(output, path, (length + 1) * sizeof(wchar_t));
            found = 1;
        }
        HMODULE ole = system_module(L"ole32.dll", &ole_loaded);
        TaskMemFreeFn release =
            ole ? (TaskMemFreeFn)(uintptr_t)GetProcAddress(ole, "CoTaskMemFree") : NULL;
        if (release) release(path); /* else a few bytes stay allocated */
        if (ole_loaded) FreeLibrary(ole);
    }
    if (shell_loaded) FreeLibrary(shell);
    if (!found) {
        static const wchar_t documents[] = L"\\Documents";
        DWORD count = GetEnvironmentVariableW(L"USERPROFILE", output, (DWORD)capacity);
        found = count >= 3 && count + ARRAY_COUNT(documents) < capacity;
        if (found) {
            if (output[count - 1] == L'\\') --count;
            memory_copy(output + count, documents, sizeof(documents));
            *fallback = 1;
        } else {
            output[0] = 0;
        }
    }
    SetLastError(error);
    return found;
}

/* [Saves] InDocuments (default 0). */
static int saves_option(const wchar_t *ini, const char **key) {
    *key = "InDocuments";
    return GetPrivateProfileIntW(L"Saves", L"InDocuments", 0, ini) != 0;
}

static void log_option(const char *key, int enabled, const char *text, const wchar_t *path,
                       const char *after) {
    Line line;
    line.length = 0;
    line.text[0] = 0;
    line_text(&line, "SAVES: [Saves] ");
    line_text(&line, key);
    line_text(&line, enabled ? "=1" : "=0");
    line_text(&line, text);
    if (path) line_path(&line, path);
    line_text(&line, after);
    line_end(&line);
}

void saves_redirect_attach(const wchar_t *mod_directory) {
    static wchar_t exe[ROOT_CAPACITY], launcher[ROOT_CAPACITY], ini[ROOT_CAPACITY];
    static wchar_t value[64];
    if (!path_join(source_path, mod_directory, MOD_LOG_NAME) ||
        wide_length(source_path) >= ROOT_CAPACITY) {
        return;
    }
    memory_copy(log_path, source_path, (wide_length(source_path) + 1) * sizeof(wchar_t));
    DWORD count = GetModuleFileNameW(NULL, exe, ROOT_CAPACITY);
    if (!count || count >= ROOT_CAPACITY) return;
    const wchar_t *name = exe;
    for (const wchar_t *p = exe; *p; ++p) {
        if (*p == L'\\' || *p == L'/') name = p + 1;
    }
    if (lstrcmpiW(name, L"azimuth.exe")) return;
    /* Azimuth 1.0.x keeps the saves in data\titles\1A96545B\saves. */
    AzimuthBuild build = azimuth_host_build((unsigned char *)GetModuleHandleW(NULL));
    if (build != AZIMUTH_101 && build != AZIMUTH_102) {
        saves_log("SAVES: host is not an inspected Azimuth build; saves stay where Azimuth puts "
                  "them.\r\n");
        return;
    }
    if (!directory_of(launcher, ROOT_CAPACITY, NULL) ||
        !path_join(source_path, launcher, L"data\\azimuth.ini") ||
        wide_length(source_path) >= ROOT_CAPACITY) {
        return;
    }
    memory_copy(ini, source_path, (wide_length(source_path) + 1) * sizeof(wchar_t));
    GetPrivateProfileStringW(L"launcher", L"game", L"1a96545b", value, ARRAY_COUNT(value), ini);
    if (lstrcmpiW(value, L"1a96545b")) return;
    if (!path_join(source_path, mod_directory, MOD_INI_NAME)) return;
    const char *key;
    int enabled = saves_option(source_path, &key);
    if (!path_join(target_path, launcher, L"data\\titles\\" TITLE_ID L"\\saves")) return;
    if (!enabled) {
        log_option(key, 0, ": saves stay in ", target_path, "; no file hooks installed.");
        return;
    }
    int fallback;
    if (!saves_documents_folder(documents_base, ROOT_CAPACITY - 96, &fallback)) {
        log_option(key, 1, " but the Documents folder cannot be found; save redirection is off.",
                   NULL, "");
        return;
    }
    log_folder(fallback ? "SAVES: Documents folder (SHGetKnownFolderPath failed; %USERPROFILE% "
                          "fallback): "
                        : "SAVES: Documents folder: ",
               documents_base, "");
    if (!path_join(source_path, documents_base,
                   L"My Games\\Forza Horizon 2\\Saves\\" TITLE_ID) ||
        !set_roots(target_path, source_path)) {
        log_option(key, 1, " but the save folders are not usable; save redirection is off.", NULL,
                   "");
        return;
    }
    log_option(key, 1, " (on): Azimuth's saves folder ", game_root, " is redirected.");
    static const wchar_t *const parents[] = {L"My Games", L"My Games\\Forza Horizon 2",
                                             L"My Games\\Forza Horizon 2\\Saves"};
    for (unsigned i = 0; i < ARRAY_COUNT(parents); ++i) {
        if (!path_join(source_path, documents_base, parents[i]) ||
            !ensure_any_directory(source_path)) {
            log_folder("SAVES: cannot create ", source_path, "; save redirection is off.");
            return;
        }
    }
    if (!prepare_folder()) return;
    InterlockedExchange(&active, 1);
    if (!patch_imports((unsigned char *)GetModuleHandleW(NULL))) {
        InterlockedExchange(&active, 0);
        return;
    }
    log_folder("SAVES: folder used: ", docs_root,
               " (the game-folder saves are not read or written while this is on).");
}

#ifdef FH2_TWEAKS_TEST
/* Offline tests: set the roots and the log, optionally run the copy step. The hooks call KERNEL32
   directly. Returns 1 when redirection is on. */
__declspec(dllexport) int Fh2TestSavesConfigure(const wchar_t *game, const wchar_t *documents,
                                                const wchar_t *log_file,
                                                int prepare) {
    InterlockedExchange(&active, 0);
    for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) originals[hooks[i].api] = expected_function(hooks[i].api);
    for (unsigned i = 0; i < API_COUNT; ++i) first_use[i] = 0;
    first_redirect = first_reverse = 0;
    log_path[0] = 0;
    if (log_file && wide_length(log_file) < ROOT_CAPACITY) {
        memory_copy(log_path, log_file, (wide_length(log_file) + 1) * sizeof(wchar_t));
    }
    log_ready = 1;
    if (!game || !documents || !set_roots(game, documents)) return 0;
    if (prepare && !prepare_folder()) return 0;
    InterlockedExchange(&active, 1);
    return 1;
}

/* The folder redirection now uses. */
__declspec(dllexport) int Fh2TestSavesRoot(wchar_t *output, unsigned capacity) {
    if (docs_length >= capacity) return 0;
    memory_copy(output, docs_root, (docs_length + 1) * sizeof(wchar_t));
    return (int)docs_length;
}

/* [Saves] InDocuments as saves_redirect_attach reads it. */
__declspec(dllexport) int Fh2TestSavesOption(const wchar_t *ini) {
    const char *key;
    return saves_option(ini, &key);
}

__declspec(dllexport) int Fh2TestDocumentsFolder(wchar_t *output, unsigned capacity) {
    int fallback;
    if (!saves_documents_folder(output, capacity, &fallback)) return 0;
    return fallback ? 2 : 1;
}

__declspec(dllexport) void *Fh2TestSavesHook(const char *name) {
    for (unsigned i = 0; i < ARRAY_COUNT(hooks); ++i) {
        if (text_equal_ignore_case(name, api_names[hooks[i].api])) return hooks[i].hook;
    }
    return NULL;
}

__declspec(dllexport) int Fh2TestSavesMap(const wchar_t *path, wchar_t *output, unsigned capacity,
                                          int reverse) {
    if (reverse) {
        return (int)map_reverse(path, wide_length(path), output, capacity);
    }
    wchar_t *mapped = map_forward(path, API_COUNT);
    if (!mapped) return 0;
    size_t length = wide_length(mapped);
    int fits = length < capacity;
    if (fits) memory_copy(output, mapped, (length + 1) * sizeof(wchar_t));
    heap_release(mapped);
    return fits ? (int)length : 0;
}

__declspec(dllexport) int Fh2TestSavesRedirectPath(wchar_t *path) {
    return saves_redirect_path(path);
}
#endif
