#include "fh3.h"
#include "fh3_internal.h"
#include "shared/support.h"
#include "shared/logging.h"
#include "fh3_plugin.h"

/* Mod loader: <Azimuth folder>\mods\Forza Horizon 3\<ModName>\{media,plugins}, next to
   azimuth.exe and FH3_ExtraOptions.ini as with FH2. Media folders are
   mounted with the native loose-file volume (like mediaoverride), plugins are
   loaded on the game thread after the file system is up, never under the
   loader lock. */
enum { MAX_MODS = 64, MOD_MEDIA_PRIORITY = 0x7fffff00 };

typedef struct Plugin {
    struct Plugin *next;
    FH3PluginContext context;
    FH3PluginUpdateFn update;
    wchar_t *path, *folder;
} Plugin;

static wchar_t mods_root[PATH_CAPACITY];
static wchar_t mod_names[MAX_MODS][MAX_PATH];
/* The mod's folder in the media mirror. The game's archive open fails on a
   game: path with a space in it (the zip manifest loader then crashes), so
   the mirror uses the mod name with anything but letters, digits, . _ -
   turned into _, made unique with ~<n> on a clash. */
static wchar_t mirror_names[MAX_MODS][MAX_PATH];
static unsigned char media_ready[MAX_MODS];
static unsigned mod_count;
static Plugin *plugins;
static int plugins_loaded;
/* Path scratch (PATH_CAPACITY characters each); the loader runs on one
   thread at a time: startup, then the game thread's file-system init. */
static wchar_t scratch_folder[PATH_CAPACITY], scratch_pattern[PATH_CAPACITY],
    scratch_base[PATH_CAPACITY], scratch_path[PATH_CAPACITY], scratch_mod[PATH_CAPACITY];

static void log_wide(const char *prefix, const wchar_t *text, const char *suffix) {
    char line[MAX_PATH * 3];
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, line, sizeof(line), NULL, NULL);
    log_append(prefix);
    log_append(n > 0 ? line : "?");
    log_append(suffix);
}

static void name_mirrors(void) {
    for (unsigned i = 0; i < mod_count; ++i) {
        wchar_t *name = mirror_names[i];
        unsigned n = 0;
        for (const wchar_t *c = mod_names[i]; *c && n < MAX_PATH - 8; ++c, ++n) {
            wchar_t ch = *c;
            int plain = (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z') ||
                        (ch >= L'a' && ch <= L'z') || ch == L'.' || ch == L'_' || ch == L'-';
            name[n] = plain ? ch : L'_';
        }
        name[n] = 0;
        for (unsigned j = 0; j < i; ++j) {
            if (lstrcmpiW(mirror_names[j], name)) continue;
            name[n] = L'~';
            unsigned value = i, digits = 0;
            do name[n + 1 + digits++] = (wchar_t)(L'0' + value % 10), value /= 10;
            while (value);
            for (unsigned a = 0, b = digits - 1; a < b; ++a, --b) {
                wchar_t t = name[n + 1 + a];
                name[n + 1 + a] = name[n + 1 + b];
                name[n + 1 + b] = t;
            }
            name[n + 1 + digits] = 0;
            break;
        }
    }
}

static void *heap_copy(const wchar_t *text) {
    size_t bytes = ((size_t)lstrlenW(text) + 1) * sizeof(wchar_t);
    wchar_t *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy) memory_copy(copy, text, bytes);
    return copy;
}

/* Azimuth's native loose volume opens game: paths under Mount. Keep a
   private mirror for FH3 until its external file callbacks are verified. */
static int has_directory(const wchar_t *mod, const wchar_t *child, wchar_t *out);
static void hook_manifest(void);
static void generate_manifest(const wchar_t *executable);
static int manifest_ready;
static int mapping_ready;
static int mapping_read_original(const wchar_t *executable);
static void add_engine_mapping(const char *car, const char *donor, int explicit_donor);
static void write_engine_mapping(void);
#define MIRROR_FOLDER L"__eo_fh3_media__"
#define MIRROR_NOTE L".extra-options-fh3-mirror"
static const char mirror_identity[] = "Extra Options FH3 media mirror v1";
enum { MIRROR_PATH = 1024 };
static wchar_t mirror_root[MIRROR_PATH];

static int join_short(wchar_t *out, const wchar_t *folder, const wchar_t *name) {
    int a = lstrlenW(folder), b = lstrlenW(name);
    if (a + b + 2 > MIRROR_PATH) return 0;
    memory_copy(out, folder, (size_t)a * sizeof(wchar_t));
    out[a] = L'\\';
    memory_copy(out + a + 1, name, ((size_t)b + 1) * sizeof(wchar_t));
    return 1;
}

static void *path_buffer(void) {
    return HeapAlloc(GetProcessHeap(), 0, MIRROR_PATH * sizeof(wchar_t));
}

static void free_buffer(void *buffer) {
    if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
}

/* Delete a file or a whole folder (it is ours: inside the marked mirror). */
static int remove_tree(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return attributes & FILE_ATTRIBUTE_DIRECTORY ? RemoveDirectoryW(path) : DeleteFileW(path);
    }
    wchar_t *pattern = path_buffer(), *child = path_buffer();
    if (pattern && child && join_short(pattern, path, L"*")) {
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(pattern, &data);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (data.cFileName[0] == L'.' &&
                    (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2])))
                    continue;
                if (join_short(child, path, data.cFileName)) remove_tree(child);
            } while (FindNextFileW(search, &data));
            FindClose(search);
        }
    }
    free_buffer(pattern);
    free_buffer(child);
    return RemoveDirectoryW(path);
}

static int same_file(const wchar_t *a, const wchar_t *b) {
    WIN32_FILE_ATTRIBUTE_DATA x, y;
    return GetFileAttributesExW(a, GetFileExInfoStandard, &x) &&
           GetFileAttributesExW(b, GetFileExInfoStandard, &y) &&
           x.nFileSizeLow == y.nFileSizeLow && x.nFileSizeHigh == y.nFileSizeHigh &&
           x.ftLastWriteTime.dwLowDateTime == y.ftLastWriteTime.dwLowDateTime &&
           x.ftLastWriteTime.dwHighDateTime == y.ftLastWriteTime.dwHighDateTime;
}

static int real_directory(DWORD attributes) {
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static int ensure_directory(const wchar_t *path) {
    return CreateDirectoryW(path, NULL) || real_directory(GetFileAttributesW(path));
}

/* Return failure for partial mirrors so stale files never mount. */
static int mirror_tree(const wchar_t *source, const wchar_t *destination) {
    if (!real_directory(GetFileAttributesW(source))) return 0;
    DWORD attributes = GetFileAttributesW(destination);
    if (attributes != INVALID_FILE_ATTRIBUTES && !real_directory(attributes) &&
        !remove_tree(destination)) return 0;
    if (!ensure_directory(destination)) return 0;
    wchar_t *pattern = path_buffer(), *from = path_buffer(), *to = path_buffer();
    int ready = 0;
    if (!pattern || !from || !to || !join_short(pattern, destination, L"*")) goto done;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    ready = search != INVALID_HANDLE_VALUE || GetLastError() == ERROR_FILE_NOT_FOUND;
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!lstrcmpW(data.cFileName, L".") || !lstrcmpW(data.cFileName, L"..")) continue;
            if (!join_short(from, source, data.cFileName) ||
                !join_short(to, destination, data.cFileName)) {
                ready = 0;
                break;
            }
            DWORD original = GetFileAttributesW(from);
            if ((original == INVALID_FILE_ATTRIBUTES || (original & FILE_ATTRIBUTE_REPARSE_POINT)) &&
                !remove_tree(to)) {
                ready = 0;
                break;
            }
        } while (FindNextFileW(search, &data));
        ready = ready && GetLastError() == ERROR_NO_MORE_FILES;
        FindClose(search);
    }
    if (!ready || !join_short(pattern, source, L"*")) {
        ready = 0;
        goto done;
    }
    search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) {
        ready = GetLastError() == ERROR_FILE_NOT_FOUND;
        goto done;
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ||
            !lstrcmpW(data.cFileName, L".") || !lstrcmpW(data.cFileName, L"..")) continue;
        if (!join_short(from, source, data.cFileName) || !join_short(to, destination, data.cFileName)) {
            ready = 0;
            break;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ready = mirror_tree(from, to);
        } else {
            DWORD existing = GetFileAttributesW(to);
            if (existing != INVALID_FILE_ATTRIBUTES &&
                !(existing & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                same_file(from, to)) continue;
            ready = remove_tree(to) && (CreateHardLinkW(to, from, NULL) || CopyFileW(from, to, TRUE));
        }
    } while (ready && FindNextFileW(search, &data));
    ready = ready && GetLastError() == ERROR_NO_MORE_FILES;
    FindClose(search);
done:
    free_buffer(pattern);
    free_buffer(from);
    free_buffer(to);
    return ready;
}

static int prepare_mirror(const wchar_t *executable) {
    int folder = lstrlenW(executable);
    while (folder > 0 && executable[folder - 1] != L'\\' && executable[folder - 1] != L'/') --folder;
    if (folder <= 0 || (size_t)folder + ARRAY_COUNT(MIRROR_FOLDER) > MIRROR_PATH) return 0;
    memory_copy(mirror_root, executable, (size_t)folder * sizeof(wchar_t));
    memory_copy(mirror_root + folder, MIRROR_FOLDER, sizeof(MIRROR_FOLDER));
    wchar_t *note = path_buffer();
    int ready = 0;
    if (!note || !join_short(note, mirror_root, MIRROR_NOTE)) goto done;
    if (CreateDirectoryW(mirror_root, NULL)) {
        HANDLE file = CreateFileW(note, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            ready = WriteFile(file, mirror_identity, sizeof(mirror_identity), &written, NULL) &&
                    written == sizeof(mirror_identity);
            CloseHandle(file);
            if (!ready) DeleteFileW(note);
        }
        if (!ready) RemoveDirectoryW(mirror_root);
    } else if (real_directory(GetFileAttributesW(mirror_root))) {
        HANDLE file = CreateFileW(note, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            char data[sizeof(mirror_identity) + 1];
            DWORD read;
            ready = ReadFile(file, data, sizeof(data), &read, NULL) &&
                    read == sizeof(mirror_identity) && memory_equal(data, mirror_identity, read);
            CloseHandle(file);
        }
    }
done:
    free_buffer(note);
    return ready;
}

static void mirror_media(const wchar_t *executable) {
    int prepared = 0;
    wchar_t *destination = path_buffer(), *media = path_buffer();
    if (!destination || !media) goto done;
    for (unsigned i = 0; i < mod_count; ++i) {
        if (!has_directory(mod_names[i], L"media", scratch_folder)) continue;
        if (!prepared) {
            if (!prepare_mirror(executable)) {
                log_append("MODS: could not open an owned FH3 media mirror. Plugins can still load.\r\n");
                break;
            }
            prepared = 1;
        }
        media_ready[i] = join_short(destination, mirror_root, mirror_names[i]) &&
            ensure_directory(destination) && join_short(media, destination, L"media") &&
            mirror_tree(scratch_folder, media);
        if (!media_ready[i]) log_wide("MODS: ", mod_names[i], " media mirror failed.\r\n");
    }
    /* Folders of mods that are gone, renamed or disabled: only hard links and
       copies of their media; the generated manifest folder is rewritten. */
    if (prepared && join_short(media, mirror_root, L"*")) {
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(media, &data);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!real_directory(data.dwFileAttributes) || data.cFileName[0] == L'.' ||
                    !lstrcmpiW(data.cFileName, L"_eo_manifest"))
                    continue;
                unsigned i = 0;
                while (i < mod_count && lstrcmpiW(data.cFileName, mirror_names[i])) ++i;
                if (i < mod_count || !join_short(destination, mirror_root, data.cFileName)) continue;
                if (remove_tree(destination)) log_wide("MODS: stale media mirror removed: ", data.cFileName, "\r\n");
            } while (FindNextFileW(search, &data));
            FindClose(search);
        }
    }
done:
    free_buffer(destination);
    free_buffer(media);
}

/* loadorder.txt as in FH2 and FM5: mods\Forza Horizon 3\loadorder.txt is
   created on the first run and lists one folder name per line (UTF-8, 64 KiB
   at most). Packages load in file order; later entries win when mounting. New
   folders are appended alphabetically, listed folders that no longer exist
   are removed, and a line starting with # is a disabled package (kept as is). */
enum { MAX_ORDER_SIZE = 65536 };
static char order_data[MAX_ORDER_SIZE + 1];
static wchar_t order_name[MAX_PATH];

/* Next line of the file: [*start, *end) is its text without surrounding
   blanks, *comment says it began with # (then *start follows the # and its
   blanks). *cursor moves past the line break. 0 at the end of the data. */
static int order_line(const char *data, DWORD length, DWORD *cursor, DWORD *start, DWORD *end,
                      int *comment) {
    if (*cursor >= length) return 0;
    DWORD at = *cursor, first = at;
    while (at < length && data[at] != '\r' && data[at] != '\n') ++at;
    DWORD last = at;
    if (at < length && data[at++] == '\r' && at < length && data[at] == '\n') ++at;
    *cursor = at;
    while (first < last && (data[first] == ' ' || data[first] == '\t')) ++first;
    *comment = first < last && data[first] == '#';
    if (*comment) {
        ++first;
        while (first < last && (data[first] == ' ' || data[first] == '\t')) ++first;
    }
    while (last > first && (data[last - 1] == ' ' || data[last - 1] == '\t')) --last;
    *start = first;
    *end = last;
    return 1;
}

/* A plain folder name: no path separators or drive letters, not . or .. */
static int order_valid_name(const char *name, DWORD length) {
    if (!length || (length <= 2 && name[0] == '.' && (length == 1 || name[1] == '.'))) return 0;
    for (DWORD i = 0; i < length; ++i)
        if (name[i] == '\\' || name[i] == '/' || name[i] == ':' || (unsigned char)name[i] < 0x20)
            return 0;
    return 1;
}

/* The UTF-8 line text as a wide folder name in order_name; 0 if it isn't one. */
static int order_wide(const char *name, DWORD length) {
    if (!order_valid_name(name, length)) return 0;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, (int)length, order_name,
                                    MAX_PATH - 1);
    if (count <= 0) return 0;
    order_name[count] = 0;
    return lstrlenW(order_name) == count;
}

/* Is the folder named on any line, enabled or commented out? */
static int order_listed(const char *data, DWORD length, const wchar_t *folder) {
    DWORD cursor = length >= 3 && memory_equal(data, "\xef\xbb\xbf", 3) ? 3 : 0, start, end;
    int comment;
    while (order_line(data, length, &cursor, &start, &end, &comment))
        if (order_wide(data + start, end - start) && !lstrcmpiW(order_name, folder)) return 1;
    return 0;
}

/* Write the whole file through a temporary neighbour, so a crash leaves the
   old list rather than a truncated one. */
static int order_write(const wchar_t *path, const char *data, DWORD length) {
    static const wchar_t digits[] = L"0123456789abcdef";
    wchar_t *name = scratch_mod, *temporary = scratch_path;
    lstrcpyW(name, L"loadorder.extra-options-");
    int at = lstrlenW(name);
    unsigned long long unique = ((unsigned long long)GetCurrentProcessId() << 32) ^ GetTickCount64();
    for (int digit = 16; digit; --digit) name[at++] = digits[(unique >> ((digit - 1) * 4)) & 15];
    lstrcpyW(name + at, L".tmp");
    if (!path_join(temporary, mods_root, name)) return 0;
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                              NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int valid = WriteFile(file, data, length, &written, NULL) && written == length &&
                FlushFileBuffers(file);
    valid = CloseHandle(file) && valid;
    if (valid)
        valid = MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!valid) DeleteFileW(temporary);
    return valid;
}

/* Read (creating it if needed), prune and extend loadorder.txt. The result
   is in order_data; *length is its size. 0 when the file can't be used. */
static int refresh_load_order(const wchar_t *path, DWORD *length) {
    *length = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    DWORD bytes = 0;
    int valid = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
                size.QuadPart <= MAX_ORDER_SIZE;
    if (valid && size.QuadPart) {
        valid = ReadFile(file, order_data, (DWORD)size.QuadPart, &bytes, NULL) &&
                bytes == (DWORD)size.QuadPart &&
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, order_data, (int)bytes, NULL, 0) > 0;
        for (DWORD i = 0; valid && i < bytes; ++i)
            if (!order_data[i]) valid = 0;
    }
    CloseHandle(file);
    if (!valid) return 0;
    char *data = order_data;
    /* Drop enabled entries whose folders are gone; keep comments and blanks. */
    unsigned removed = 0;
    DWORD read = bytes >= 3 && memory_equal(data, "\xef\xbb\xbf", 3) ? 3 : 0, written = read;
    while (read < bytes) {
        DWORD line = read, start, end;
        int comment;
        order_line(data, bytes, &read, &start, &end, &comment);
        int missing = 0;
        if (!comment && start < end) {
            if (!order_wide(data + start, end - start)) return 0;
            if (!path_join(scratch_mod, mods_root, order_name)) return 0;
            DWORD attributes = GetFileAttributesW(scratch_mod);
            missing = attributes == INVALID_FILE_ATTRIBUTES
                          ? GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND
                          : !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        }
        if (missing) {
            ++removed;
        } else {
            if (written != line) memory_copy(data + written, data + line, read - line);
            written += read - line;
        }
    }
    bytes = written;
    /* Append every scanned folder that isn't on the list yet. */
    unsigned added = 0;
    for (unsigned i = 0; i < mod_count; ++i) {
        if (order_listed(data, bytes, mod_names[i])) continue;
        char name[MAX_PATH * 3];
        int size = WideCharToMultiByte(CP_UTF8, 0, mod_names[i], -1, name, sizeof(name), NULL, NULL);
        if (size <= 1) return 0;
        DWORD newline = bytes && data[bytes - 1] != '\n' && data[bytes - 1] != '\r' ? 2 : 0;
        if (bytes + newline + (DWORD)size + 1 > MAX_ORDER_SIZE) return 0;
        if (newline) {
            data[bytes++] = '\r';
            data[bytes++] = '\n';
        }
        memory_copy(data + bytes, name, (size_t)size - 1);
        bytes += (DWORD)size - 1;
        data[bytes++] = '\r';
        data[bytes++] = '\n';
        ++added;
    }
    data[bytes] = 0;
    if (removed || added) {
        if (!order_write(path, data, bytes)) return 0;
        if (removed) log_append("MODS: missing mod folders removed from loadorder.txt.\r\n");
        if (added) log_append("MODS: new mod folders appended to loadorder.txt.\r\n");
    }
    *length = bytes;
    return 1;
}

/* Reorder mod_names to loadorder.txt: enabled entries in file order, nothing
   else. Returns 0 when the list can't be trusted (duplicate entry). */
static int apply_load_order(DWORD length) {
    const char *data = order_data;
    DWORD cursor = length >= 3 && memory_equal(data, "\xef\xbb\xbf", 3) ? 3 : 0, start, end;
    int comment;
    unsigned slot = 0;
    while (order_line(data, length, &cursor, &start, &end, &comment)) {
        if (start == end || !order_wide(data + start, end - start)) continue;
        unsigned i = 0;
        while (i < mod_count && lstrcmpiW(order_name, mod_names[i])) ++i;
        if (i == mod_count) continue; /* disabled in the INI, or just removed */
        if (comment) {
            if (i >= slot) log_wide("MODS: ", mod_names[i], " disabled in loadorder.txt.\r\n");
            continue;
        }
        if (i < slot) {
            log_append("MODS: duplicate folder in loadorder.txt; package loading skipped.\r\n");
            return 0;
        }
        lstrcpyW(order_name, mod_names[i]);
        for (unsigned j = i; j > slot; --j) lstrcpyW(mod_names[j], mod_names[j - 1]);
        lstrcpyW(mod_names[slot++], order_name);
    }
    mod_count = slot;
    return 1;
}

void fh3_mods_configure(const wchar_t *directory, const wchar_t *executable) {
    wchar_t *folder = scratch_folder, *pattern = scratch_pattern;
    if (!path_join(folder, directory, L"mods") || !ensure_directory(folder) ||
        !path_join(mods_root, folder, L"Forza Horizon 3") || !ensure_directory(mods_root) ||
        !path_join(pattern, mods_root, L"*")) {
        log_append("MODS: could not open mods\\Forza Horizon 3.\r\n");
        return;
    }
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!real_directory(data.dwFileAttributes) || data.cFileName[0] == L'.') continue;
        if (!GetPrivateProfileIntW(L"Mods", data.cFileName, 1, fh3_ini_path())) {
            log_wide("MODS: ", data.cFileName, " disabled in FH3_ExtraOptions.ini.\r\n");
            continue;
        }
        if (mod_count == MAX_MODS) {
            log_append("MODS: more than 64 mods; package loading skipped.\r\n");
            mod_count = 0;
            break;
        }
        unsigned slot = mod_count++;
        while (slot && lstrcmpiW(mod_names[slot - 1], data.cFileName) > 0) {
            lstrcpyW(mod_names[slot], mod_names[slot - 1]);
            --slot;
        }
        lstrcpyW(mod_names[slot], data.cFileName);
    } while (FindNextFileW(search, &data));
    FindClose(search);
    if (!mod_count) return;
    DWORD order_length;
    if (!path_join(pattern, mods_root, L"loadorder.txt") ||
        !refresh_load_order(pattern, &order_length)) {
        log_append("MODS: cannot read/update UTF-8 loadorder.txt (64 KiB maximum); package loading "
                   "skipped.\r\n");
        mod_count = 0;
        return;
    }
    if (!apply_load_order(order_length) || !mod_count) {
        mod_count = 0;
        return;
    }
    name_mirrors();
    if (executable) mirror_media(executable);
    if (executable) generate_manifest(executable);
    fh3_addons_configure(mods_root, (const wchar_t (*)[MAX_PATH])mod_names, mod_count);
    if (executable && mirror_root[0] && mapping_read_original(executable)) {
        fh3_addons_engine_audio(add_engine_mapping);
        write_engine_mapping();
    }
    hook_manifest();
}

static int has_directory(const wchar_t *mod, const wchar_t *child, wchar_t *out) {
    wchar_t *base = scratch_base;
    return path_join(base, mods_root, mod) && real_directory(GetFileAttributesW(base)) &&
           path_join(out, base, child) && real_directory(GetFileAttributesW(out));
}

void fh3_mods_mount(void *filesystem) {
    for (unsigned i = 0; i < mod_count; ++i) {
        if (!media_ready[i]) continue;
        char source[MAX_PATH * 3];
        const char head[] = "game:\\__eo_fh3_media__\\";
        int prefix = sizeof(head) - 1;
        memory_copy(source, head, (size_t)prefix);
        int n = WideCharToMultiByte(CP_UTF8, 0, mirror_names[i], -1, source + prefix,
                                    (int)sizeof(source) - prefix - 8, NULL, NULL);
        if (n <= 0) continue;
        memory_copy(source + prefix + n - 1, "\\media\\", 8);
        int mounted = fh3_mount(filesystem, source, MOD_MEDIA_PRIORITY + i);
        log_wide("MODS: ", mod_names[i], mounted ? " media mounted.\r\n" : " media mount failed.\r\n");
    }
    if (manifest_ready || mapping_ready) {
        int mounted = fh3_mount(filesystem, "game:\\__eo_fh3_media__\\_eo_manifest\\media\\", MOD_MEDIA_PRIORITY + mod_count);
        log_append(mounted ? "MODS: generated zip manifest / engine audio mapping mounted above the mods.\r\n"
                           : "MODS: generated zip manifest / engine audio mapping mount failed.\r\n");
    }
}

/* --- Zip manifest --------------------------------------------------------
   FH3 mounts only the car zips named in media\zipmanifest.xml, each with the
   location of its central directory. Extra Options writes a copy of the game's
   manifest with every mod's zips added (media\cars\**\*.zip in the mirrors;
   entries of zips a mod overrides, such as the library tyres, replaced) into
   the mirror's _eo_manifest\media\, which is mounted above all mods. */
#define MANIFEST_MIRROR L"_eo_manifest"
static int manifest_ready;
static int mapping_ready;
static char *manifest_text;
static size_t manifest_length, manifest_capacity;
static unsigned manifest_added, manifest_replaced;

static char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static int manifest_reserve(size_t extra) {
    if (manifest_length + extra <= manifest_capacity) return 1;
    size_t capacity = manifest_capacity * 2 + extra + 65536;
    char *grown = HeapAlloc(GetProcessHeap(), 0, capacity);
    if (!grown) return 0;
    if (manifest_text) {
        memory_copy(grown, manifest_text, manifest_length);
        HeapFree(GetProcessHeap(), 0, manifest_text);
    }
    manifest_text = grown;
    manifest_capacity = capacity;
    return 1;
}

static int manifest_read_original(const wchar_t *executable) {
    int folder = lstrlenW(executable);
    while (folder > 0 && executable[folder - 1] != L'\\' && executable[folder - 1] != L'/') --folder;
    static const wchar_t name[] = L"media\\zipmanifest.xml";
    wchar_t *path = path_buffer();
    int ready = 0;
    if (!path || folder <= 0 || (size_t)folder + ARRAY_COUNT(name) > MIRROR_PATH) goto done;
    memory_copy(path, executable, (size_t)folder * sizeof(wchar_t));
    memory_copy(path + folder, name, sizeof(name));
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    LARGE_INTEGER size;
    DWORD read;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 16 * 1024 * 1024 &&
        manifest_reserve((size_t)size.QuadPart + 1) &&
        ReadFile(file, manifest_text, (DWORD)size.QuadPart, &read, NULL) && read == size.QuadPart) {
        manifest_length = read;
        ready = 1;
    }
    CloseHandle(file);
done:
    free_buffer(path);
    return ready;
}

/* The zip's end-of-central-directory record: entry count, directory size and offset. */
static int zip_directory(const wchar_t *path, unsigned *entries, unsigned *size, unsigned *start) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    static unsigned char tail[65536 + 22];
    LARGE_INTEGER length, position;
    DWORD read = 0;
    int found = 0;
    if (GetFileSizeEx(file, &length) && length.QuadPart >= 22) {
        DWORD want = length.QuadPart < (LONGLONG)sizeof(tail) ? (DWORD)length.QuadPart : (DWORD)sizeof(tail);
        position.QuadPart = length.QuadPart - want;
        if (SetFilePointerEx(file, position, NULL, FILE_BEGIN) && ReadFile(file, tail, want, &read, NULL) && read == want) {
            for (int i = (int)read - 22; i >= 0 && !found; --i) {
                if (tail[i] != 'P' || tail[i + 1] != 'K' || tail[i + 2] != 5 || tail[i + 3] != 6) continue;
                *entries = tail[i + 10] | (unsigned)tail[i + 11] << 8;
                *size = tail[i + 12] | (unsigned)tail[i + 13] << 8 | (unsigned)tail[i + 14] << 16 | (unsigned)tail[i + 15] << 24;
                *start = tail[i + 16] | (unsigned)tail[i + 17] << 8 | (unsigned)tail[i + 18] << 16 | (unsigned)tail[i + 19] << 24;
                found = 1;
            }
        }
    }
    CloseHandle(file);
    return found;
}

static unsigned append_text(char *out, unsigned at, unsigned capacity, const char *text) {
    while (*text && at + 1 < capacity) out[at++] = *text++;
    return at;
}

static unsigned append_number(char *out, unsigned at, unsigned capacity, unsigned value) {
    char reverse[12];
    unsigned digits = 0;
    do reverse[digits++] = (char)('0' + value % 10), value /= 10;
    while (value);
    while (digits && at + 1 < capacity) out[at++] = reverse[--digits];
    return at;
}

/* Replaces the entry naming game_path (lower-case) or adds one before </ZipFiles>. */
static void manifest_add(const char *game_path, unsigned entries, unsigned size, unsigned start) {
    char line[1024];
    unsigned at = 0;
    at = append_text(line, at, sizeof(line), "  <Zip version=\"1\" path=\"");
    at = append_text(line, at, sizeof(line), game_path);
    at = append_text(line, at, sizeof(line), "\" priority=\"80\" dirstart=\"");
    at = append_number(line, at, sizeof(line), start);
    at = append_text(line, at, sizeof(line), "\" dirsize=\"");
    at = append_number(line, at, sizeof(line), size);
    at = append_text(line, at, sizeof(line), "\" direntries=\"");
    at = append_number(line, at, sizeof(line), entries);
    at = append_text(line, at, sizeof(line), "\" />\r\n");
    line[at] = 0;
    size_t path_length = 0;
    while (game_path[path_length]) ++path_length;
    /* An existing entry: path="<game_path>" (case-insensitive), inside a <Zip ... /> element. */
    size_t from = 0, to = 0;
    for (size_t i = 0; i + path_length + 7 < manifest_length && !to; ++i) {
        if (!memory_equal(manifest_text + i, "path=\"", 6)) continue;
        size_t p = i + 6, k = 0;
        while (k < path_length && p + k < manifest_length && ascii_lower(manifest_text[p + k]) == game_path[k]) ++k;
        if (k != path_length || p + k >= manifest_length || manifest_text[p + k] != '"') continue;
        size_t begin = i;
        while (begin > 0 && !(manifest_text[begin] == '<' && begin + 4 <= manifest_length && memory_equal(manifest_text + begin, "<Zip", 4))) --begin;
        while (begin > 0 && (manifest_text[begin - 1] == ' ' || manifest_text[begin - 1] == '\t')) --begin;
        size_t end = p + k;
        while (end + 1 < manifest_length && !(manifest_text[end] == '/' && manifest_text[end + 1] == '>')) ++end;
        end += 2;
        while (end < manifest_length && (manifest_text[end] == '\r' || manifest_text[end] == '\n')) ++end;
        from = begin, to = end;
    }
    if (!to) {
        /* Before the last </ZipFiles>. */
        size_t close = manifest_length;
        for (size_t i = manifest_length; i >= 11; --i)
            if (memory_equal(manifest_text + i - 11, "</ZipFiles>", 11)) { close = i - 11; break; }
        if (close == manifest_length) return;
        from = to = close;
        ++manifest_added;
    } else {
        ++manifest_replaced;
    }
    size_t old = to - from;
    if (at > old && !manifest_reserve(at - old)) return;
    size_t rest = manifest_length - to;
    if (at != old) {
        char *source = manifest_text + to, *destination = manifest_text + from + at;
        if (destination < source) for (size_t i = 0; i < rest; ++i) destination[i] = source[i];
        else for (size_t i = rest; i > 0; --i) destination[i - 1] = source[i - 1];
    }
    memory_copy(manifest_text + from, line, at);
    manifest_length = manifest_length - old + at;
}

/* Every *.zip under folder (recursively); prefix is its game path so far, lower-case, ending in '\'. */
static void manifest_scan(const wchar_t *folder, const char *prefix, int depth) {
    if (depth > 8) return;
    wchar_t *pattern = path_buffer(), *child = path_buffer();
    if (!pattern || !child || !join_short(pattern, folder, L"*")) goto done;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) goto done;
    do {
        if (data.cFileName[0] == L'.') continue;
        char path[MAX_PATH * 3];
        unsigned at = append_text(path, 0, sizeof(path), prefix);
        int n = WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, path + at, (int)sizeof(path) - (int)at - 4, NULL, NULL);
        if (n <= 0 || !join_short(child, folder, data.cFileName)) continue;
        for (char *c = path + at; *c; ++c) *c = ascii_lower(*c);
        at += (unsigned)n - 1;
        if (real_directory(data.dwFileAttributes)) {
            path[at] = '\\', path[at + 1] = 0;
            manifest_scan(child, path, depth + 1);
            continue;
        }
        if (at < 4 || !memory_equal(path + at - 4, ".zip", 4)) continue;
        unsigned entries, size, start;
        if (zip_directory(child, &entries, &size, &start)) manifest_add(path, entries, size, start);
    } while (FindNextFileW(search, &data));
    FindClose(search);
done:
    free_buffer(pattern);
    free_buffer(child);
}

/* mod.json declares "fh3_cars": true for car zips in FH3's format (fh3cars writes it). Zips of other mods
   (FH2/FM5 packs as shipped) are left out: the game hangs on FH2-format models. */
static int declares_fh3_cars(const wchar_t *mod) {
    wchar_t *path = path_buffer();
    int declared = 0;
    if (!path || !path_join(path, mods_root, mod) || !join_short(path, path, L"mod.json")) goto done;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    static char data[65536];
    DWORD read = 0;
    if (ReadFile(file, data, sizeof(data) - 1, &read, NULL)) {
        static const char key[] = "\"fh3_cars\"";
        for (DWORD i = 0; i + sizeof(key) < read && !declared; ++i) {
            if (!memory_equal(data + i, key, sizeof(key) - 1)) continue;
            DWORD at = i + sizeof(key) - 1;
            while (at < read && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n' || data[at] == ':')) ++at;
            declared = at + 4 <= read && memory_equal(data + at, "true", 4);
        }
    }
    CloseHandle(file);
done:
    free_buffer(path);
    return declared;
}

static void generate_manifest(const wchar_t *executable) {
    if (!mirror_root[0] || !manifest_read_original(executable)) {
        log_append("MODS: the game's zipmanifest.xml could not be read; mod car zips need their own manifest.\r\n");
        return;
    }
    wchar_t *folder = path_buffer(), *cars = path_buffer();
    if (!folder || !cars) goto done;
    for (unsigned i = 0; i < mod_count; ++i) {
        if (!media_ready[i] || !join_short(folder, mirror_root, mirror_names[i]) || !join_short(cars, folder, L"media") ||
            !join_short(folder, cars, L"cars") || !real_directory(GetFileAttributesW(folder)))
            continue;
        if (!declares_fh3_cars(mod_names[i])) {
            if (join_short(cars, folder, L"*.zip") && GetFileAttributesW(folder) != INVALID_FILE_ATTRIBUTES) {
                WIN32_FIND_DATAW probe;
                HANDLE search = FindFirstFileW(cars, &probe);
                if (search != INVALID_HANDLE_VALUE) {
                    FindClose(search);
                    log_wide("MODS: ", mod_names[i], " has car zips but its mod.json lacks \"fh3_cars\": true; "
                             "they are not FH3 cars until converted (fh3cars) and stay unlisted.\r\n");
                }
            }
            continue;
        }
        manifest_scan(folder, "game:\\media\\cars\\", 0);
    }
    if (!manifest_added && !manifest_replaced) goto done;
    if (!join_short(folder, mirror_root, MANIFEST_MIRROR) || !ensure_directory(folder) || !join_short(cars, folder, L"media") ||
        !ensure_directory(cars) || !join_short(folder, cars, L"zipmanifest.xml"))
        goto done;
    HANDLE file = CreateFileW(folder, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        manifest_ready = WriteFile(file, manifest_text, (DWORD)manifest_length, &written, NULL) && written == manifest_length;
        CloseHandle(file);
    }
    {
        char line[128];
        unsigned at = append_text(line, 0, sizeof(line), manifest_ready ? "MODS: zip manifest written: " : "MODS: zip manifest could not be written: ");
        at = append_number(line, at, sizeof(line), manifest_added);
        at = append_text(line, at, sizeof(line), " mod zips added, ");
        at = append_number(line, at, sizeof(line), manifest_replaced);
        at = append_text(line, at, sizeof(line), " replaced.\r\n");
        line[at] = 0;
        log_append(line);
    }
done:
    free_buffer(folder);
    free_buffer(cars);
    if (manifest_text) { HeapFree(GetProcessHeap(), 0, manifest_text); manifest_text = NULL; }
    manifest_length = manifest_capacity = 0;
}

/* --- Engine audio ----------------------------------------------------------
   FH3 plays a car's engine through media\audio\cars\CarEngineEventMapping.xml:
   <Engine mediaName="<car>" engineName="<FH3 engine>" /> names the PG_<engine>.bank
   to play; a car that isn't listed gets "fallback" (NUL_Car_00), which is silence.
   FH2/FM5 banks belong to another FMOD project and can't be used, so an add-on car
   borrows an FH3 engine named in its mod's mod.json ("fh3_engine_audio":
   {"<car>": "<FH3 car or engine>", "*": "<default>"}). The game's mapping plus one
   line per add-on car is written to _eo_manifest\media\audio\cars\, which is
   mounted above the mods. */
static int mapping_ready;
static char *mapping_text;
static size_t mapping_length, mapping_capacity;
static unsigned mapping_added;
static wchar_t mapping_mount[MIRROR_PATH]; /* the game's Mount folder */

static int mapping_reserve(size_t extra) {
    if (mapping_length + extra <= mapping_capacity) return 1;
    size_t capacity = mapping_capacity * 2 + extra + 16384;
    char *grown = HeapAlloc(GetProcessHeap(), 0, capacity);
    if (!grown) return 0;
    if (mapping_text) {
        memory_copy(grown, mapping_text, mapping_length);
        HeapFree(GetProcessHeap(), 0, mapping_text);
    }
    mapping_text = grown;
    mapping_capacity = capacity;
    return 1;
}

static int mapping_read_original(const wchar_t *executable) {
    int folder = lstrlenW(executable);
    while (folder > 0 && executable[folder - 1] != L'\\' && executable[folder - 1] != L'/') --folder;
    static const wchar_t name[] = L"media\\audio\\cars\\CarEngineEventMapping.xml";
    wchar_t *path = path_buffer();
    int ready = 0;
    if (!path || folder <= 1 || (size_t)folder + ARRAY_COUNT(name) > MIRROR_PATH) goto done;
    memory_copy(mapping_mount, executable, (size_t)(folder - 1) * sizeof(wchar_t));
    mapping_mount[folder - 1] = 0;
    memory_copy(path, executable, (size_t)folder * sizeof(wchar_t));
    memory_copy(path + folder, name, sizeof(name));
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    LARGE_INTEGER size;
    DWORD read;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 4 * 1024 * 1024 &&
        mapping_reserve((size_t)size.QuadPart + 1) &&
        ReadFile(file, mapping_text, (DWORD)size.QuadPart, &read, NULL) && read == size.QuadPart) {
        mapping_length = read;
        mapping_text[read] = 0;
        ready = 1;
    }
    CloseHandle(file);
done:
    free_buffer(path);
    return ready;
}

/* Is mediaName="<car>" already in the mapping (any case)? */
static int mapping_has(const char *car) {
    static const char key[] = "mediaName=\"";
    size_t klen = sizeof(key) - 1, clen = text_length(car);
    for (size_t i = 0; i + klen + clen + 1 <= mapping_length; ++i) {
        if (!memory_equal(mapping_text + i, key, klen)) continue;
        const char *at = mapping_text + i + klen;
        size_t k = 0;
        while (k < clen && ascii_lower(at[k]) == ascii_lower(car[k])) ++k;
        if (k == clen && at[k] == '"') return 1;
    }
    return 0;
}

/* Does some FH3 car already play engineName="<donor>"? */
static int mapping_uses(const char *donor) {
    static const char key[] = "engineName=\"";
    size_t klen = sizeof(key) - 1, dlen = text_length(donor);
    for (size_t i = 0; i + klen + dlen + 1 <= mapping_length; ++i) {
        if (!memory_equal(mapping_text + i, key, klen)) continue;
        const char *at = mapping_text + i + klen;
        size_t k = 0;
        while (k < dlen && ascii_lower(at[k]) == ascii_lower(donor[k])) ++k;
        if (k == dlen && at[k] == '"') return 1;
    }
    return 0;
}

static int donor_bank_exists(const char *donor) {
    wchar_t *path = path_buffer();
    static wchar_t relative[MAX_PATH];
    static const wchar_t head[] = L"media\\audio\\fmodopus\\xboxone\\PG_";
    int at = (int)ARRAY_COUNT(head) - 1, found = 0;
    memory_copy(relative, head, sizeof(head) - sizeof(wchar_t));
    int n = MultiByteToWideChar(CP_UTF8, 0, donor, -1, relative + at, MAX_PATH - at - 8);
    if (path && n > 0) {
        memory_copy(relative + at + n - 1, L".bank", 6 * sizeof(wchar_t));
        found = join_short(path, mapping_mount, relative) && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }
    free_buffer(path);
    return found;
}

static void add_engine_mapping(const char *car, const char *donor, int explicit_donor) {
    if (!mapping_text) return;
    if (!explicit_donor && mapping_has(car)) return; /* FH3 already plays something for it */
    if (!donor_bank_exists(donor) && !mapping_uses(donor)) {
        /* DLC engines live in their content packages, so this is a note, not a refusal. */
        log_append("MODS: engine audio donor ");
        log_append(donor);
        log_append(" has no PG_ bank in Mount and no FH3 car uses it; ");
        log_append(car);
        log_append(" may stay silent.\r\n");
    }
    static const char close[] = "</Engines>";
    size_t clen = sizeof(close) - 1, tail = mapping_length;
    while (tail >= clen && !memory_equal(mapping_text + tail - clen, close, clen)) --tail;
    if (tail < clen) return;
    size_t at = tail - clen;
    while (at > 0 && (mapping_text[at - 1] == ' ' || mapping_text[at - 1] == '\t')) --at;
    char line[640];
    unsigned n = append_text(line, 0, sizeof(line), "\t<Engine mediaName=\"");
    n = append_text(line, n, sizeof(line), car);
    n = append_text(line, n, sizeof(line), "\" engineName=\"");
    n = append_text(line, n, sizeof(line), donor);
    n = append_text(line, n, sizeof(line), "\" />\r\n");
    if (n + 1 >= sizeof(line) || !mapping_reserve(n + 1)) return;
    for (size_t k = mapping_length; k > at; --k) mapping_text[k - 1 + n] = mapping_text[k - 1];
    memory_copy(mapping_text + at, line, n);
    mapping_length += n;
    mapping_text[mapping_length] = 0;
    ++mapping_added;
    log_append("MODS: engine audio of ");
    log_append(car);
    log_append(" borrowed from ");
    log_append(donor);
    log_append(".\r\n");
}

static void write_engine_mapping(void) {
    wchar_t *folder = path_buffer(), *path = path_buffer();
    if (mapping_added && folder && path && join_short(folder, mirror_root, MANIFEST_MIRROR) && ensure_directory(folder) &&
        join_short(path, folder, L"media") && ensure_directory(path) && join_short(folder, path, L"audio") &&
        ensure_directory(folder) && join_short(path, folder, L"cars") && ensure_directory(path) &&
        join_short(folder, path, L"CarEngineEventMapping.xml")) {
        HANDLE file = CreateFileW(folder, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written;
            mapping_ready = WriteFile(file, mapping_text, (DWORD)mapping_length, &written, NULL) && written == mapping_length;
            CloseHandle(file);
        }
        log_append(mapping_ready ? "MODS: engine audio mapping written for the add-on cars.\r\n"
                                 : "MODS: engine audio mapping could not be written.\r\n");
    }
    free_buffer(folder);
    free_buffer(path);
    if (mapping_text) HeapFree(GetProcessHeap(), 0, mapping_text);
    mapping_text = NULL;
    mapping_length = mapping_capacity = 0;
}

/* Cars. FH3 mounts each media\cars\<name>.zip it knows (zipmanifest.xml) as
   game:\media\cars\<name>\ and draws NUL_Car_00 for a car whose folder isn't
   there; a mod volume at game:\media\ doesn't serve new files under it. A mod's
   unpacked car, media\cars\<name>\, is mounted at game:\media\cars\<name>\
   the same way, above the mods' media. */
void fh3_mods_mount_cars(void *filesystem) {
    unsigned mounted = 0, failed = 0;
    wchar_t *folder = path_buffer(), *pattern = path_buffer();
    if (!folder || !pattern) goto done;
    for (unsigned i = 0; i < mod_count; ++i) {
        if (!media_ready[i] || !join_short(folder, mirror_root, mirror_names[i]) ||
            !join_short(pattern, folder, L"media") || !join_short(folder, pattern, L"cars") ||
            !join_short(pattern, folder, L"*"))
            continue;
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(pattern, &data);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (!real_directory(data.dwFileAttributes) || data.cFileName[0] == L'.') continue;
            char source[MAX_PATH * 3], target[MAX_PATH * 3];
            static const char mirror[] = "game:\\__eo_fh3_media__\\", cars[] = "game:\\media\\cars\\";
            int at = sizeof(mirror) - 1;
            memory_copy(source, mirror, (size_t)at);
            int n = WideCharToMultiByte(CP_UTF8, 0, mirror_names[i], -1, source + at, (int)sizeof(source) - at - 16,
                                        NULL, NULL);
            if (n <= 0) continue;
            at += n - 1;
            memory_copy(source + at, "\\media\\cars\\", 13);
            at += 12;
            n = WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, source + at, (int)sizeof(source) - at - 2, NULL,
                                    NULL);
            if (n <= 0) continue;
            at += n - 1;
            source[at] = '\\', source[at + 1] = 0;
            int t = sizeof(cars) - 1;
            memory_copy(target, cars, (size_t)t);
            n = WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, target + t, (int)sizeof(target) - t - 2, NULL, NULL);
            if (n <= 0) continue;
            t += n - 1;
            target[t] = '\\', target[t + 1] = 0;
            if (fh3_mount_at(filesystem, source, target, MOD_MEDIA_PRIORITY + 0x80 + i)) {
                ++mounted;
            } else {
                ++failed;
                log_append("MODS: could not mount car folder ");
                log_append(source);
                log_append("\r\n");
            }
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    if (mounted) ((void (*)(void *))(*(void ***)filesystem)[0x1f8 / 8])(filesystem);
    if (mounted || failed) {
        char line[96], number[12];
        unsigned length = 0;
        const char *parts[4];
        parts[0] = "MODS: car folders mounted: ", parts[1] = NULL, parts[2] = "; failed: ", parts[3] = NULL;
        unsigned values[2];
        values[0] = mounted, values[1] = failed;
        for (unsigned k = 0, v = 0; k < 4; ++k) {
            const char *part = parts[k];
            if (!part) {
                unsigned value = values[v++], digits = 0;
                char reverse[12];
                do reverse[digits++] = (char)('0' + value % 10), value /= 10;
                while (value);
                for (unsigned d = 0; d < digits; ++d) number[d] = reverse[digits - 1 - d];
                number[digits] = 0;
                part = number;
            }
            for (const char *c = part; *c && length + 1 < sizeof(line); ++c) line[length++] = *c;
        }
        line[length] = 0;
        log_append(line);
        log_append("\r\n");
    }
done:
    free_buffer(folder);
    free_buffer(pattern);
}

static int write_code(unsigned char *target, const void *bytes, size_t size);
/* Car zips as archives. FH3 mounts the zips its zipmanifest.xml lists (read
   at 0x1899146, after the mod mounts) as game:\media\cars\<name>\; a mod's
   zipmanifest.xml can list more. Once the manifest is read, each mod car zip is
   mounted with the game's archive mount (slot 0x1d8). */
typedef unsigned char (*ZipFolderExists)(void *, const void *);
typedef void *(*ZipMount)(void *, void *, const void *, uint64_t, uint32_t, unsigned char, unsigned char,
                          unsigned char);
typedef struct {
    const char *data;
    uint64_t unused, size, capacity;
} ZipString;
typedef void (*ManifestLoad)(void *);
enum { MANIFEST_CALL = 0x1899146, MANIFEST_LOAD = 0x18dd6d0, MAX_CAR_ZIPS = 1024 };

/* Install chunks. Slot 0x120 gives a file's streaming-install chunk from
   hash.manifest; mod files have none (0), which FH3 can treat as not installed.
   Mod car zips report the chunk of an installed car zip. */
typedef int (*FileChunk)(void *, const void *);
static FileChunk chunk_original;
static int installed_chunk;
static int mod_car_zip(const void *path) {
    const ZipString *name = path;
    const char *text = name->capacity >= 16 ? name->data : (const char *)path;
    static const char head[] = "game:\\media\\cars\\";
    size_t length = text_length(text), prefix = sizeof(head) - 1;
    if (length <= prefix + 4) return 0;
    for (size_t i = 0; i < prefix; ++i) {
        char a = text[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a != head[i]) return 0;
    }
    return text_equal_ignore_case(text + length - 4, ".zip");
}
static int file_chunk(void *filesystem, const void *path) {
    int chunk = chunk_original(filesystem, path);
    return !chunk && installed_chunk && mod_car_zip(path) ? installed_chunk : chunk;
}
static void hook_chunks(void *filesystem) {
    void **slot = *(void ***)filesystem + 0x120 / 8;
    if (chunk_original) return;
    chunk_original = (FileChunk)*slot;
    static const char sample[] = "game:\\media\\cars\\FER_250GTO_64.zip";
    ZipString name = {sample, 0, sizeof(sample) - 1, 31};
    installed_chunk = chunk_original(filesystem, &name);
    void *hook = (void *)file_chunk;
    if (installed_chunk && write_code((unsigned char *)slot, &hook, sizeof(hook)))
        log_append("MODS: mod car zips use an installed content chunk.\r\n");
}

static void mount_car_zips(void *filesystem) {
    static unsigned char handles[MAX_CAR_ZIPS][64];
    static unsigned used;
    unsigned mounted = 0, present = 0, failed = 0;
    wchar_t *folder = path_buffer(), *pattern = path_buffer();
    if (!folder || !pattern) goto done;
    void **methods = *(void ***)filesystem;
    for (unsigned i = 0; i < mod_count; ++i) {
        if (!media_ready[i] || !join_short(folder, mirror_root, mirror_names[i]) ||
            !join_short(pattern, folder, L"media") || !join_short(folder, pattern, L"cars") ||
            !join_short(pattern, folder, L"*.zip"))
            continue;
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(pattern, &data);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char path[MAX_PATH * 3];
            static const char head[] = "game:\\media\\cars\\";
            int prefix = sizeof(head) - 1, length = lstrlenW(data.cFileName);
            if (length <= 4) continue;
            memory_copy(path, head, (size_t)prefix);
            int n = WideCharToMultiByte(CP_UTF8, 0, data.cFileName, length - 4, path + prefix,
                                        (int)sizeof(path) - prefix - 2, NULL, NULL);
            if (n <= 0) continue;
            path[prefix + n] = '\\';
            path[prefix + n + 1] = 0;
            ZipString name = {path, 0, (uint64_t)(prefix + n + 1), 31};
            if (((ZipFolderExists)methods[0xe8 / 8])(filesystem, &name)) {
                ++present;
                continue;
            }
            if (used < MAX_CAR_ZIPS)
                ((ZipMount)methods[0x1d8 / 8])(filesystem, handles[used], &name, 0, 0, 0, 0, 1);
            if (used < MAX_CAR_ZIPS && *(uint64_t *)handles[used] != UINT64_MAX &&
                ((ZipFolderExists)methods[0xe8 / 8])(filesystem, &name)) {
                ++used, ++mounted;
            } else {
                ++failed;
                log_append("MODS: car zip not accepted by the game: ");
                log_append(path);
                log_append("\r\n");
            }
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    if (mounted) ((void (*)(void *))methods[0x1f8 / 8])(filesystem);
    if (mounted || failed || present) {
        char line[128], number[12];
        unsigned length = 0;
        const char *parts[6];
        parts[0] = "MODS: car zips mounted: ", parts[1] = NULL, parts[2] = "; already known: ", parts[3] = NULL,
        parts[4] = "; not accepted: ", parts[5] = NULL;
        unsigned values[3];
        values[0] = mounted, values[1] = present, values[2] = failed;
        for (unsigned k = 0, v = 0; k < 6; ++k) {
            const char *part = parts[k];
            if (!part) {
                unsigned value = values[v++], digits = 0;
                char reverse[12];
                do reverse[digits++] = (char)('0' + value % 10), value /= 10;
                while (value);
                for (unsigned d = 0; d < digits; ++d) number[d] = reverse[digits - 1 - d];
                number[digits] = 0;
                part = number;
            }
            for (const char *c = part; *c && length + 1 < sizeof(line); ++c) line[length++] = *c;
        }
        line[length] = 0;
        log_append(line);
        log_append("\r\n");
    }
done:
    free_buffer(folder);
    free_buffer(pattern);
}

static void manifest_loaded(void *filesystem) {
    hook_chunks(filesystem);
    ((ManifestLoad)(fh3_image() + MANIFEST_LOAD))(filesystem);
    log_append("MODS: the game's zip manifest is loaded; mounting mod car zips.\r\n");
    mount_car_zips(filesystem);
}

static void hook_manifest(void) {
    unsigned char *site = fh3_image() + MANIFEST_CALL;
    int32_t relative;
    memory_copy(&relative, site + 1, sizeof(relative));
    unsigned char *stub = NULL;
    if (site[0] != 0xe8 || site + 5 + relative != fh3_image() + MANIFEST_LOAD || !(stub = fh3_code_stub())) {
        log_append("MODS: the zip manifest loader is not the expected one; mod car zips stay unmounted.\r\n");
        return;
    }
    static unsigned char jump[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t callback = (uintptr_t)manifest_loaded;
    memory_copy(jump + 2, &callback, sizeof(callback));
    intptr_t distance = stub - (site + 5);
    static unsigned char call[5] = {0xe8};
    int32_t rel32 = (int32_t)distance;
    memory_copy(call + 1, &rel32, sizeof(rel32));
    if (distance > 0x7fffffff || distance < -0x7fffffffLL - 1 || !write_code(stub, jump, sizeof(jump)) ||
        !write_code(site, call, sizeof(call)))
        log_append("MODS: could not hook the zip manifest loader.\r\n");
}

/* --- Plugin services. --- */

static void plugin_log(const char *message) {
    log_append("PLUGIN: ");
    log_append(message ? message : "");
    log_append("\r\n");
}

static int write_code(unsigned char *target, const void *bytes, size_t size) {
    DWORD old;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memory_copy(target, bytes, size);
    DWORD unused;
    VirtualProtect(target, size, old, &unused);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    return 1;
}

static int in_image(uint32_t rva, uint32_t size) {
    return rva < FH3_IMAGE_SIZE && size <= FH3_IMAGE_SIZE - rva;
}

static int plugin_patch_bytes(uint32_t rva, const void *expected, const void *replacement,
                              uint32_t size) {
    unsigned char *target = fh3_image() + rva;
    if (!replacement || !size || !in_image(rva, size)) return 0;
    if (expected && !memory_equal(target, expected, size)) return 0;
    return write_code(target, replacement, size);
}

/* Absolute-jump stubs within rel32 reach of the image. */
static unsigned char *stub_pool;
static size_t stub_used, stub_size;

static unsigned char *allocate_stub(void) {
    if (!stub_pool) {
        uintptr_t base = (uintptr_t)fh3_image() & ~(uintptr_t)0xffff;
        for (uintptr_t step = 0x10000; step < 0x70000000 && !stub_pool; step += 0x10000) {
            if (base > step)
                stub_pool = VirtualAlloc((void *)(base - step), 0x10000, MEM_RESERVE | MEM_COMMIT,
                                         PAGE_EXECUTE_READWRITE);
            if (!stub_pool)
                stub_pool = VirtualAlloc((void *)(base + FH3_IMAGE_SIZE + step), 0x10000,
                                         MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        }
        if (!stub_pool) return NULL;
        stub_size = 0x10000;
    }
    if (stub_used + 16 > stub_size) return NULL;
    unsigned char *stub = stub_pool + stub_used;
    stub_used += 16;
    return stub;
}

unsigned char *fh3_code_stub(void) { return allocate_stub(); }
int fh3_write_code(unsigned char *target, const void *bytes, size_t size) {
    return write_code(target, bytes, size);
}

static void *plugin_hook_call(uint32_t rva, void *callback) {
    unsigned char *site = fh3_image() + rva;
    if (!callback || !in_image(rva, 5) || site[0] != 0xe8) return NULL;
    int32_t relative;
    memory_copy(&relative, site + 1, sizeof(relative));
    unsigned char *original = site + 5 + relative;
    unsigned char *stub = allocate_stub();
    if (!stub) return NULL;
    unsigned char jump[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t address = (uintptr_t)callback;
    memory_copy(jump + 2, &address, sizeof(address));
    memory_copy(stub, jump, sizeof(jump));
    intptr_t distance = stub - (site + 5);
    if (distance > 0x7fffffff || distance < -0x7fffffffLL - 1) return NULL;
    unsigned char call[5] = {0xe8};
    int32_t rel32 = (int32_t)distance;
    memory_copy(call + 1, &rel32, sizeof(rel32));
    return write_code(site, call, sizeof(call)) ? original : NULL;
}

static void *plugin_hook_pointer(uint32_t rva, void *callback) {
    void *previous;
    if (!callback || !in_image(rva, 8) || (rva & 7)) return NULL;
    memory_copy(&previous, fh3_image() + rva, sizeof(previous));
    return write_code(fh3_image() + rva, &callback, sizeof(callback)) ? previous : NULL;
}

static void load_plugin(const wchar_t *mod, const wchar_t *folder, const wchar_t *file) {
    wchar_t *path = scratch_path, *mod_directory = scratch_mod;
    if (!path_join(path, folder, file) || !path_join(mod_directory, mods_root, mod)) return;
    Plugin *plugin = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*plugin));
    if (!plugin) return;
    plugin->path = heap_copy(path);
    plugin->folder = heap_copy(mod_directory);
    if (!plugin->path || !plugin->folder) goto failed;
    HMODULE module = LoadLibraryExW(path, NULL,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        log_wide("MODS: ", path, " failed to load.\r\n");
        goto failed;
    }
    FH3PluginContext *context = &plugin->context;
    context->size = sizeof(*context);
    context->api_version = FH3_PLUGIN_API_VERSION;
    context->game_image = fh3_image();
    context->game_image_size = FH3_IMAGE_SIZE;
    context->plugin_module = module;
    context->mod_directory = plugin->folder;
    context->plugin_path = plugin->path;
    context->extra_options_version = MOD_VERSION;
    context->log_utf8 = plugin_log;
    context->patch_bytes = plugin_patch_bytes;
    context->hook_call = plugin_hook_call;
    context->hook_pointer = plugin_hook_pointer;
    context->player_car = fh3_player_car;
    context->set_hud_hidden = fh3_set_hud_hidden;
    context->set_hud_prompt = fh3_set_hud_prompt;
    context->set_anna_status = fh3_set_anna_status;
    context->set_anna_ready = fh3_set_anna_ready;
    FH3PluginInitializeFn initialize =
        (FH3PluginInitializeFn)(uintptr_t)GetProcAddress(module, "FH3PluginInitialize");
    BOOL ready = initialize ? initialize(context) : TRUE;
    log_wide("MODS: ", path, ready ? " loaded.\r\n" : " initializer returned FALSE; kept loaded.\r\n");
    if (ready) plugin->update = (FH3PluginUpdateFn)(uintptr_t)GetProcAddress(module, "FH3PluginUpdate");
    plugin->next = plugins;
    plugins = plugin;
    return;
failed:
    free_buffer(plugin->path);
    free_buffer(plugin->folder);
    free_buffer(plugin);
}

void fh3_mods_load_plugins(void) {
    if (plugins_loaded) return;
    plugins_loaded = 1;
    for (unsigned i = 0; i < mod_count; ++i) {
        wchar_t *folder = scratch_folder, *pattern = scratch_pattern;
        if (!has_directory(mod_names[i], L"plugins", folder) || !path_join(pattern, folder, L"*.dll"))
            continue;
        static wchar_t files[32][MAX_PATH];
        unsigned count = 0;
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(pattern, &data);
        if (search == INVALID_HANDLE_VALUE) continue;
        do {
            if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || count == 32) continue;
            unsigned slot = count++;
            while (slot && lstrcmpiW(files[slot - 1], data.cFileName) > 0) {
                memory_copy(files[slot], files[slot - 1], sizeof(files[slot]));
                --slot;
            }
            lstrcpyW(files[slot], data.cFileName);
        } while (FindNextFileW(search, &data));
        FindClose(search);
        for (unsigned f = 0; f < count; ++f) load_plugin(mod_names[i], folder, files[f]);
    }
}

void fh3_mods_frame(float dt) {
    if (!plugins) return;
    FH3PluginFrame frame = {sizeof(frame), dt, fh3_player_car()};
    for (Plugin *plugin = plugins; plugin; plugin = plugin->next)
        if (plugin->update) plugin->update(&frame);
}
