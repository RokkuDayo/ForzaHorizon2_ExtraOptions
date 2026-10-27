#include "azimuth_photos.h"
#include "azimuth_saves.h"
#include "sha256.h"
#include "support.h"

/* Photos as JPG. A photo is an Azimuth save container
   player-N\Photo_NNNN_<yyyyMMddHHmmss, UTC>\revisions\<16 hex digits>\ with
   data\image (a JFIF JPEG), data\thumb, data\header, data\metadata and a
   "commit" file written when the revision is complete:

       Azimuth complete save revision v2
       blobs 4
       <sha256 hex> <size> header
       <sha256 hex> <size> image
       ...

   Only revisions with a commit are read, and the image is copied only when its
   size and SHA-256 match the commit and it starts with a JPEG marker. The
   thread only lists folders between photos; it keeps what it exported in
   memory (container, revisions-folder time) and leaves a destination that
   already exists with the same size alone. */

#define PHOTO_INTERVAL_MS 3000u
#define PHOTO_FIRST_DELAY_MS 5000u
#define PHOTO_MAX_BYTES (256u << 20)
#define COMMIT_MAX 16384u
#define PHOTO_TRIES 5u
#define PHOTO_ERROR_LINES 20u
#define PHOTO_NAME_TRIES 10u

typedef struct {
    unsigned long long key;
    FILETIME revisions;
    unsigned char state, tries;
} PhotoEntry;

enum { PHOTO_PENDING, PHOTO_DONE, PHOTO_FAILED };
enum { RESULT_EXPORTED, RESULT_PRESENT, RESULT_WAIT, RESULT_ERROR };

static wchar_t saves_root[PATH_CAPACITY], photos_root[PATH_CAPACITY], photo_log[PATH_CAPACITY];
static wchar_t player_path[PATH_CAPACITY], container_path[PATH_CAPACITY];
static wchar_t work_path[PATH_CAPACITY], dest_path[PATH_CAPACITY], temp_path[PATH_CAPACITY];
static char commit_text[COMMIT_MAX + 1];
static PhotoEntry *entries;
static size_t entry_count, entry_capacity;
static unsigned error_lines;
static int photos_ready;
static unsigned long long found_count, exported_count, present_count;
static const char *failure;

static size_t wide_length(const wchar_t *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

/* buffer[0..length) + text; the new length, 0 when it does not fit. */
static size_t append(wchar_t *buffer, size_t length, const wchar_t *text) {
    size_t extra = wide_length(text);
    if (!length || length + extra + 1 >= PATH_CAPACITY) return 0;
    memory_copy(buffer + length, text, (extra + 1) * sizeof(wchar_t));
    return length + extra;
}

static size_t set_path(wchar_t *buffer, const wchar_t *text) {
    size_t length = wide_length(text);
    if (!length || length >= PATH_CAPACITY) return 0;
    memory_copy(buffer, text, (length + 1) * sizeof(wchar_t));
    return length;
}

/* ---- log ---- */

typedef struct {
    char text[2048];
    size_t length;
} PhotoLine;

static void put(PhotoLine *line, const char *text) {
    while (*text && line->length + 3 < sizeof(line->text)) line->text[line->length++] = *text++;
    line->text[line->length] = 0;
}

static void put_path(PhotoLine *line, const wchar_t *path) {
    int room = (int)(sizeof(line->text) - line->length - 3);
    int written = room > 0 && path ? WideCharToMultiByte(CP_UTF8, 0, path, (int)wide_length(path),
                                                         line->text + line->length, room, NULL,
                                                         NULL)
                                   : 0;
    if (written > 0) line->length += (size_t)written;
    line->text[line->length] = 0;
}

static void put_number(PhotoLine *line, unsigned long long value) {
    char digits[24];
    char *cursor = digits + sizeof(digits) - 1;
    *cursor = 0;
    do {
        *--cursor = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    put(line, cursor);
}

static void emit(PhotoLine *line) {
    line->text[line->length++] = '\r';
    line->text[line->length++] = '\n';
    line->text[line->length] = 0;
    if (!photo_log[0]) return;
    HANDLE file = CreateFileW(photo_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, line->text, (DWORD)line->length, &written, NULL);
    CloseHandle(file);
}

static __attribute__((noinline)) void log_line(const char *text, const wchar_t *path, const char *after) {
    PhotoLine line;
    line.length = 0;
    put(&line, text);
    if (path) put_path(&line, path);
    put(&line, after);
    emit(&line);
}

/* Errors: at most PHOTO_ERROR_LINES lines per session. 1 = this one may be logged. */
static int error_allowed(void) {
    if (error_lines > PHOTO_ERROR_LINES) return 0;
    if (++error_lines > PHOTO_ERROR_LINES) {
        log_line("PHOTOS: further errors are not logged this session.", NULL, "");
        return 0;
    }
    return 1;
}

/* ---- remembered containers ---- */

static wchar_t fold(wchar_t c) {
    return c >= L'a' && c <= L'z' ? (wchar_t)(c - 32) : c;
}

static unsigned long long hash_name(unsigned long long hash, const wchar_t *text) {
    for (; *text; ++text) {
        hash ^= (unsigned long long)fold(*text);
        hash *= 1099511628211ull;
    }
    return hash;
}

static PhotoEntry *find_entry(unsigned long long key) {
    for (size_t i = 0; i < entry_count; ++i) {
        if (entries[i].key == key) return &entries[i];
    }
    if (entry_count == entry_capacity) {
        size_t capacity = entry_capacity ? entry_capacity * 2 : 256;
        PhotoEntry *grown =
            (PhotoEntry *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(PhotoEntry));
        if (!grown) return NULL;
        if (entries) {
            memory_copy(grown, entries, entry_count * sizeof(PhotoEntry));
            HeapFree(GetProcessHeap(), 0, entries);
        }
        entries = grown;
        entry_capacity = capacity;
    }
    PhotoEntry *entry = &entries[entry_count++];
    entry->key = key;
    entry->revisions.dwLowDateTime = entry->revisions.dwHighDateTime = 0;
    entry->state = PHOTO_PENDING;
    entry->tries = 0;
    return entry;
}

/* ---- one container ---- */

static int dot_entry(const WIN32_FIND_DATAW *data) {
    return data->cFileName[0] == L'.' &&
           (!data->cFileName[1] || (data->cFileName[1] == L'.' && !data->cFileName[2]));
}

static int plain_folder(const WIN32_FIND_DATAW *data) {
    return (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(data->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && !dot_entry(data);
}

static int hex_value(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                          : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The newest revision folder with a commit file; its name goes to best. */
static __attribute__((noinline)) int newest_committed(size_t container_length, wchar_t best[20]) {
    WIN32_FIND_DATAW data;
    unsigned long long best_value = 0;
    int found = 0;
    size_t revisions = append(work_path, set_path(work_path, container_path), L"\\revisions\\*");
    HANDLE find = revisions ? FindFirstFileW(work_path, &data) : INVALID_HANDLE_VALUE;
    (void)container_length;
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!plain_folder(&data)) continue;
        unsigned long long value = 0;
        size_t digits = 0;
        int valid = 1;
        for (; data.cFileName[digits]; ++digits) {
            wchar_t c = data.cFileName[digits];
            int nibble = c < 128 ? hex_value((char)c) : -1;
            if (nibble < 0 || digits >= 16) {
                valid = 0;
                break;
            }
            value = value << 4 | (unsigned)nibble;
        }
        if (!valid || !digits || (found && value <= best_value)) continue;
        size_t length = append(work_path, set_path(work_path, container_path), L"\\revisions\\");
        length = append(work_path, length, data.cFileName);
        length = append(work_path, length, L"\\commit");
        DWORD attributes = length ? GetFileAttributesW(work_path) : INVALID_FILE_ATTRIBUTES;
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        memory_copy(best, data.cFileName, (digits + 1) * sizeof(wchar_t));
        best_value = value;
        found = 1;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

/* The image line of a commit: 64 hex digits, a size, "image". */
static int commit_image(const char *text, unsigned char hash[32], unsigned long long *size) {
    static const char header[] = "Azimuth complete save revision";
    for (size_t i = 0; i < sizeof(header) - 1; ++i) {
        if (text[i] != header[i]) return 0;
    }
    for (const char *line = text; *line;) {
        const char *end = line;
        while (*end && *end != '\n') ++end;
        size_t length = (size_t)(end - line);
        if (length > 0 && line[length - 1] == '\r') --length;
        if (length >= 64 + 1 + 1 + 1 + 5 && line[64] == ' ' && length - 6 > 65 &&
            line[length - 6] == ' ' && line[length - 5] == 'i' && line[length - 4] == 'm' &&
            line[length - 3] == 'a' && line[length - 2] == 'g' && line[length - 1] == 'e') {
            int valid = 1;
            for (unsigned i = 0; i < 32 && valid; ++i) {
                int high = hex_value(line[2 * i]), low = hex_value(line[2 * i + 1]);
                valid = high >= 0 && low >= 0;
                hash[i] = (unsigned char)(high << 4 | low);
            }
            unsigned long long value = 0;
            for (size_t i = 65; i < length - 6 && valid; ++i) {
                valid = line[i] >= '0' && line[i] <= '9' && value < (1ull << 40);
                value = value * 10 + (unsigned)(line[i] - '0');
            }
            if (valid) {
                *size = value;
                return 1;
            }
        }
        line = *end ? end + 1 : end;
    }
    return 0;
}

static int read_commit(void) {
    HANDLE file = CreateFileW(work_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD read = 0;
    int ok = ReadFile(file, commit_text, COMMIT_MAX, &read, NULL);
    CloseHandle(file);
    commit_text[ok ? read : 0] = 0;
    return ok && read;
}

static void put_digits(wchar_t *at, unsigned value, unsigned width) {
    for (unsigned d = width; d; --d) {
        at[d - 1] = (wchar_t)(L'0' + value % 10);
        value /= 10;
    }
}

/* "FH2 yyyy-MM-dd HH-mm-ss" in local time from the container name
   Photo_NNNN_yyyyMMddHHmmss (UTC); else from the image's write time.
   number receives NNNN. */
static __attribute__((noinline)) void photo_name(const wchar_t *container, FILETIME written, wchar_t name[32],
                       wchar_t number[12]) {
    SYSTEMTIME utc = {0, 0, 0, 0, 0, 0, 0, 0}, local;
    int parsed = 0;
    number[0] = 0;
    const wchar_t *cursor = container + 6; /* after "Photo_" */
    size_t digits = 0;
    while (digits < 10 && cursor[digits] >= L'0' && cursor[digits] <= L'9') {
        number[digits] = cursor[digits];
        ++digits;
    }
    number[digits] = 0;
    if (digits && cursor[digits] == L'_') {
        const wchar_t *stamp = cursor + digits + 1;
        unsigned values[14];
        unsigned i = 0;
        for (; i < 14 && stamp[i] >= L'0' && stamp[i] <= L'9'; ++i) values[i] = stamp[i] - L'0';
        if (i == 14 && !stamp[14]) {
            utc.wYear = (WORD)(values[0] * 1000 + values[1] * 100 + values[2] * 10 + values[3]);
            utc.wMonth = (WORD)(values[4] * 10 + values[5]);
            utc.wDay = (WORD)(values[6] * 10 + values[7]);
            utc.wHour = (WORD)(values[8] * 10 + values[9]);
            utc.wMinute = (WORD)(values[10] * 10 + values[11]);
            utc.wSecond = (WORD)(values[12] * 10 + values[13]);
            utc.wMilliseconds = 0;
            utc.wDayOfWeek = 0;
            parsed = utc.wYear >= 2000 && utc.wMonth >= 1 && utc.wMonth <= 12 && utc.wDay >= 1 &&
                     utc.wDay <= 31 && utc.wHour < 24 && utc.wMinute < 60 && utc.wSecond < 61;
        }
    }
    if (!parsed) parsed = FileTimeToSystemTime(&written, &utc);
    if (!parsed || !SystemTimeToTzSpecificLocalTime(NULL, &utc, &local)) local = utc;
    static const wchar_t pattern[] = L"FH2 0000-00-00 00-00-00";
    memory_copy(name, pattern, sizeof(pattern));
    put_digits(name + 4, local.wYear, 4);
    put_digits(name + 9, local.wMonth, 2);
    put_digits(name + 12, local.wDay, 2);
    put_digits(name + 15, local.wHour, 2);
    put_digits(name + 18, local.wMinute, 2);
    put_digits(name + 21, local.wSecond, 2);
}

static int ensure_photos_folder(void) {
    if (photos_ready) return 1;
    /* Create every missing folder of the path, from the top. */
    size_t length = set_path(work_path, photos_root);
    for (size_t i = 3; length && i <= length; ++i) {
        if (i < length && work_path[i] != L'\\') continue;
        wchar_t saved = work_path[i];
        work_path[i] = 0;
        if (!CreateDirectoryW(work_path, NULL)) {
            DWORD attributes = GetFileAttributesW(work_path);
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                work_path[i] = saved;
                return 0;
            }
        }
        work_path[i] = saved;
    }
    photos_ready = length != 0;
    return photos_ready;
}

/* Writes the image as dest_path through a temporary file; never replaces. */
static __attribute__((noinline)) int write_photo(const unsigned char *image, DWORD size, FILETIME written, DWORD *error) {
    size_t length = append(temp_path, set_path(temp_path, dest_path), L".fh2-export.tmp");
    *error = ERROR_BUFFER_OVERFLOW;
    if (!length) return 0;
    HANDLE file = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return 0;
    }
    DWORD done = 0;
    int ok = WriteFile(file, image, size, &done, NULL) && done == size && FlushFileBuffers(file);
    if (ok) SetFileTime(file, &written, NULL, &written);
    *error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (ok && !MoveFileExW(temp_path, dest_path, MOVEFILE_WRITE_THROUGH)) {
        *error = GetLastError();
        ok = 0;
    }
    if (!ok) DeleteFileW(temp_path);
    return ok;
}

static __attribute__((noinline)) int export_container(const wchar_t *player, const wchar_t *container) {
    wchar_t revision[20], name[32], number[12], suffix[24];
    unsigned char expected[32], digest[32];
    unsigned long long size = 0;
    size_t container_length = wide_length(container_path);
    if (!newest_committed(container_length, revision)) return RESULT_WAIT;
    size_t base = append(work_path, set_path(work_path, container_path), L"\\revisions\\");
    base = append(work_path, base, revision);
    if (!append(work_path, base, L"\\commit") || !read_commit()) {
        failure = "its commit file cannot be read";
        return RESULT_ERROR;
    }
    if (!commit_image(commit_text, expected, &size)) {
        failure = "its commit lists no image";
        return RESULT_ERROR;
    }
    if (!size || size > PHOTO_MAX_BYTES) {
        failure = "the image size is out of range";
        return RESULT_ERROR;
    }
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!append(work_path, base, L"\\data\\image") ||
        !GetFileAttributesExW(work_path, GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        info.nFileSizeHigh || info.nFileSizeLow != size) {
        failure = "data\\image is missing or its size differs from the commit";
        return RESULT_ERROR;
    }
    unsigned char *image =
        (unsigned char *)VirtualAlloc(NULL, (SIZE_T)size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!image) {
        failure = "out of memory";
        return RESULT_ERROR;
    }
    int result = RESULT_ERROR;
    HANDLE file = CreateFileW(work_path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    DWORD read = 0;
    int ok = file != INVALID_HANDLE_VALUE && ReadFile(file, image, (DWORD)size, &read, NULL) &&
             read == size;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    Sha256Context context;
    if (!ok) {
        failure = "data\\image cannot be read";
        goto done;
    }
    if (image[0] != 0xFF || image[1] != 0xD8 || image[2] != 0xFF) {
        failure = "data\\image is not a JPEG";
        goto done;
    }
    sha256_initialize(&context);
    sha256_update(&context, image, (size_t)size);
    sha256_finalize(&context, digest);
    if (!memory_equal(digest, expected, 32)) {
        failure = "data\\image does not match the SHA-256 in its commit";
        goto done;
    }
    if (!ensure_photos_folder()) {
        failure = "the Photos folder cannot be created";
        goto done;
    }
    photo_name(container, info.ftLastWriteTime, name, number);
    for (unsigned attempt = 0; attempt < PHOTO_NAME_TRIES; ++attempt) {
        /* "", " (NNNN)", " (NNNN-2)", ... */
        size_t at = 0;
        if (attempt) {
            suffix[at++] = L' ';
            suffix[at++] = L'(';
            const wchar_t *digits = number[0] ? number : L"0";
            while (*digits && at < 14) suffix[at++] = *digits++;
            if (attempt > 1) {
                suffix[at++] = L'-';
                suffix[at++] = (wchar_t)(L'0' + attempt);
            }
            suffix[at++] = L')';
        }
        suffix[at] = 0;
        size_t length = append(dest_path, set_path(dest_path, photos_root), L"\\");
        length = append(dest_path, length, name);
        length = append(dest_path, length, suffix);
        length = append(dest_path, length, L".jpg");
        if (!length) {
            failure = "the destination path is too long";
            goto done;
        }
        WIN32_FILE_ATTRIBUTE_DATA existing;
        if (GetFileAttributesExW(dest_path, GetFileExInfoStandard, &existing)) {
            if (!(existing.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !existing.nFileSizeHigh && existing.nFileSizeLow == size) {
                result = RESULT_PRESENT; /* exported before (same size): left alone */
                goto done;
            }
            continue; /* a different file: never overwritten */
        }
        DWORD error;
        if (write_photo(image, (DWORD)size, info.ftLastWriteTime, &error)) {
            PhotoLine line;
            line.length = 0;
            put(&line, "PHOTOS: exported ");
            put_path(&line, player);
            put(&line, "\\");
            put_path(&line, container);
            put(&line, "\\revisions\\");
            put_path(&line, revision);
            put(&line, " -> ");
            put_path(&line, dest_path);
            put(&line, " (");
            put_number(&line, size);
            put(&line, " bytes)");
            emit(&line);
            result = RESULT_EXPORTED;
            goto done;
        }
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) {
            failure = "the JPG cannot be written";
            goto done;
        }
    }
    failure = "every file name for it is taken by a different file";
done:
    VirtualFree(image, 0, MEM_RELEASE);
    return result;
}

static __attribute__((noinline)) int check_container(const wchar_t *player, const wchar_t *container) {
    WIN32_FIND_DATAW data;
    size_t length = append(work_path, set_path(work_path, container_path), L"\\revisions");
    HANDLE find = length ? FindFirstFileW(work_path, &data) : INVALID_HANDLE_VALUE;
    if (find == INVALID_HANDLE_VALUE) return 0;
    FindClose(find);
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;
    }
    unsigned long long key = hash_name(hash_name(14695981039346656037ull, player), container);
    PhotoEntry *entry = find_entry(key);
    if (!entry) return 0;
    int changed = entry->revisions.dwLowDateTime != data.ftLastWriteTime.dwLowDateTime ||
                  entry->revisions.dwHighDateTime != data.ftLastWriteTime.dwHighDateTime;
    if (!changed && entry->state != PHOTO_PENDING) return 0;
    if (changed) {
        entry->revisions = data.ftLastWriteTime;
        entry->state = PHOTO_PENDING;
        entry->tries = 0;
    }
    failure = NULL;
    int result = export_container(player, container);
    if (result == RESULT_WAIT) return 0;
    if (result == RESULT_ERROR) {
        if (++entry->tries < PHOTO_TRIES) return 0;
        entry->state = PHOTO_FAILED;
        PhotoLine line;
        line.length = 0;
        put(&line, "PHOTOS: ERROR: ");
        put_path(&line, container_path);
        put(&line, " not exported: ");
        put(&line, failure ? failure : "unknown error");
        put(&line, " (checked ");
        put_number(&line, PHOTO_TRIES);
        put(&line, " times; tried again when the photo changes).");
        if (error_allowed()) emit(&line);
        return 0;
    }
    entry->state = PHOTO_DONE;
    ++found_count;
    if (result == RESULT_PRESENT) {
        ++present_count;
        return 0;
    }
    ++exported_count;
    return 1;
}

/* One pass over <saves>\player-*\Photo_*. The number of JPGs written. */
static int scan_once(void) {
    WIN32_FIND_DATAW players, photos;
    int exported = 0;
    size_t root = set_path(player_path, saves_root);
    HANDLE find = append(player_path, root, L"\\player-*") ? FindFirstFileW(player_path, &players)
                                                          : INVALID_HANDLE_VALUE;
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!plain_folder(&players)) continue;
        size_t player = append(player_path, root, L"\\");
        player = append(player_path, player, players.cFileName);
        size_t pattern = append(container_path, set_path(container_path, player_path), L"\\Photo_*");
        HANDLE inner = pattern ? FindFirstFileW(container_path, &photos) : INVALID_HANDLE_VALUE;
        if (inner == INVALID_HANDLE_VALUE) continue;
        do {
            if (!plain_folder(&photos)) continue;
            size_t length = append(container_path, set_path(container_path, player_path), L"\\");
            if (!append(container_path, length, photos.cFileName)) continue;
            exported += check_container(players.cFileName, photos.cFileName);
        } while (FindNextFileW(inner, &photos));
        FindClose(inner);
    } while (FindNextFileW(find, &players));
    FindClose(find);
    return exported;
}

/* Temporary files of an export cut short (the game closed mid-write). */
static __attribute__((noinline)) void remove_stale_temporaries(void) {
    WIN32_FIND_DATAW data;
    size_t root = set_path(work_path, photos_root);
    HANDLE find = append(work_path, root, L"\\*.jpg.fh2-export.tmp") ? FindFirstFileW(work_path, &data)
                                                                   : INVALID_HANDLE_VALUE;
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        size_t length = append(temp_path, set_path(temp_path, photos_root), L"\\");
        if (append(temp_path, length, data.cFileName)) DeleteFileW(temp_path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static __attribute__((noinline)) void log_summary(void) {
    PhotoLine line;
    line.length = 0;
    put(&line, "PHOTOS: first scan: ");
    put_number(&line, found_count);
    put(&line, " photos, ");
    put_number(&line, exported_count);
    put(&line, " exported now, ");
    put_number(&line, present_count);
    put(&line, " already in the Photos folder.");
    emit(&line);
}

static int set_photos_folder(void) {
    int fallback;
    if (!saves_documents_folder(photos_root, PATH_CAPACITY - 64, &fallback) ||
        !append(photos_root, wide_length(photos_root), L"\\My Games\\Forza Horizon 2\\Photos")) {
        photos_root[0] = 0;
        return 0;
    }
    return 1;
}

static DWORD WINAPI photo_thread(LPVOID unused) {
    (void)unused;
    HANDLE self = GetCurrentThread();
    if (!SetThreadPriority(self, THREAD_MODE_BACKGROUND_BEGIN)) {
        SetThreadPriority(self, THREAD_PRIORITY_LOWEST);
    }
    Sleep(PHOTO_FIRST_DELAY_MS);
    if (!set_photos_folder()) {
        log_line("PHOTOS: ERROR: the Documents folder cannot be found; photos are not exported.",
                 NULL, "");
        return 0;
    }
    log_line("PHOTOS: JPG folder: ", photos_root, "");
    remove_stale_temporaries();
    scan_once();
    log_summary();
    for (;;) {
        Sleep(PHOTO_INTERVAL_MS);
        scan_once();
    }
}

static int photo_option(const wchar_t *ini) {
    return GetPrivateProfileIntW(L"Photos", L"ExportJpg", 0, ini) != 0;
}

void photo_export_start(const wchar_t *settings_directory) {
    static volatile LONG started;
    static wchar_t ini[PATH_CAPACITY];
    if (InterlockedExchange(&started, 1)) return;
    if (!path_join(photo_log, settings_directory, MOD_LOG_NAME)) photo_log[0] = 0;
    if (!path_join(ini, settings_directory, MOD_INI_NAME)) return;
    if (!photo_option(ini)) {
        log_line("PHOTOS: [Photos] ExportJpg=0: photos are not exported as JPG.", NULL, "");
        return;
    }
    size_t length = 0;
    DWORD count = GetModuleFileNameW(NULL, saves_root, PATH_CAPACITY);
    if (count && count < PATH_CAPACITY) {
        while (count && saves_root[count - 1] != L'\\' && saves_root[count - 1] != L'/') --count;
        saves_root[count] = 0;
        length = append(saves_root, count, L"data\\titles\\1A96545B\\saves");
    }
    if (!length) {
        log_line("PHOTOS: ERROR: the saves folder cannot be resolved; photos are not exported.",
                 NULL, "");
        return;
    }
    /* The mod's own reads are not redirected by the Azimuth hooks: map them. */
    int redirected = saves_redirect_path(saves_root);
    log_line(redirected ? "PHOTOS: [Photos] ExportJpg=1 (on): photos are exported as JPG from "
                          "the redirected saves folder "
                        : "PHOTOS: [Photos] ExportJpg=1 (on): photos are exported as JPG from ",
             saves_root, " (checked every 3 s).");
#ifdef FH2_TWEAKS_TEST
    /* Offline tests never scan real saves or write to the real Documents. */
    log_line("PHOTOS: test build: the export thread is not started.", NULL, "");
    return;
#endif
    HANDLE thread = CreateThread(NULL, 0, photo_thread, NULL, 0, NULL);
    if (!thread) {
        log_line("PHOTOS: ERROR: the export thread cannot be started.", NULL, "");
        return;
    }
    CloseHandle(thread);
}

#ifdef FH2_TWEAKS_TEST
/* Offline tests: no thread, explicit folders. */
__declspec(dllexport) int Fh2TestPhotosConfigure(const wchar_t *saves, const wchar_t *photos,
                                                 const wchar_t *log_file) {
    if (!set_path(saves_root, saves) || !set_path(photos_root, photos)) return 0;
    if (!log_file || !set_path(photo_log, log_file)) photo_log[0] = 0;
    entry_count = 0;
    error_lines = 0;
    photos_ready = 0;
    found_count = exported_count = present_count = 0;
    remove_stale_temporaries();
    return 1;
}

__declspec(dllexport) int Fh2TestPhotosScan(void) {
    return scan_once();
}

__declspec(dllexport) void Fh2TestPhotosSummary(void) {
    log_summary();
}

__declspec(dllexport) int Fh2TestPhotosOption(const wchar_t *ini) {
    return photo_option(ini);
}
#endif
