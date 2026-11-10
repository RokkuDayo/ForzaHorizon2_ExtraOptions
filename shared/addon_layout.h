/* Private to addons.c. Configuration only resolves paths; filesystem changes
   happen after the game executable has passed startup validation. */
static wchar_t layout_root[PATH_CAPACITY], layout_error_path[PATH_CAPACITY];
static const char *layout_error;
static DWORD layout_error_code;
static unsigned layout_moved;
static int layout_pending, layout_ready, layout_reported, layout_deferred;

static void layout_reset(void) {
    layout_root[0] = layout_error_path[0] = 0;
    layout_error = NULL; layout_error_code = 0; layout_moved = 0;
    layout_pending = layout_ready = layout_reported = layout_deferred = 0;
}

static int layout_fail(const char *message, const wchar_t *path, DWORD error) {
    layout_deferred = 0;
    layout_error = message; layout_error_code = error;
    lstrcpyW(layout_error_path, path);
    return 0;
}

static int layout_move_deferred(const wchar_t *path, DWORD error) {
    if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
        return 0;
    if (!layout_deferred) {
        layout_fail("FH2 migration deferred: Windows refused to move this path", path, error);
        layout_deferred = 1;
    }
    return 1;
}

static int layout_legacy_order(void) { return layout_deferred && !fm5_mode; }

static int layout_directory(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (CreateDirectoryW(path, NULL)) return 1;
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) return 0;
        attributes = GetFileAttributesW(path);
    }
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return 1;
    SetLastError(ERROR_DIRECTORY);
    return 0;
}

static int layout_reserved(const wchar_t *name) {
    return !lstrcmpiW(name, L".") || !lstrcmpiW(name, L"..") ||
        !lstrcmpiW(name, L"Forza Horizon 2") || !lstrcmpiW(name, L"Forza Motorsport 5") ||
        !lstrcmpiW(name, L"Forza Horizon 3") || !lstrcmpiW(name, L"Cache");
}

static int layout_check_destination(const wchar_t *path, int *already_migrated) {
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (already_migrated && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            *already_migrated = 1;
            return 1;
        }
        return layout_fail("migration destination already exists; no files overwritten", path, ERROR_ALREADY_EXISTS);
    }
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 1;
    return layout_fail("cannot inspect migration destination", path, error);
}

typedef struct {
    wchar_t horizon[PATH_CAPACITY], motorsport[PATH_CAPACITY], cache[PATH_CAPACITY];
    wchar_t order[PATH_CAPACITY], target_order[PATH_CAPACITY], pattern[PATH_CAPACITY];
    wchar_t source[PATH_CAPACITY], target[PATH_CAPACITY];
} LayoutPaths;

static int layout_migrate(LayoutPaths *paths) {
    DWORD attributes = GetFileAttributesW(paths->order);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 1;
        return layout_fail("cannot inspect legacy loadorder.txt", paths->order, error);
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return layout_fail("legacy loadorder.txt is not a regular file", paths->order, ERROR_INVALID_DATA);
    if (!layout_check_destination(paths->target_order, NULL)) return 0;

    /* Preflight every destination before moving anything. Keep the root
       loadorder as the migration marker until every directory has moved, so a
       failed move can be retried next boot without changing mod priority. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(paths->pattern, &data);
        if (search == INVALID_HANDLE_VALUE)
            return layout_fail("cannot enumerate legacy mods", layout_root, GetLastError());
        int ok = 1;
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || layout_reserved(data.cFileName)) continue;
            if (!path_join(paths->source, layout_root, data.cFileName) ||
                !path_join(paths->target, paths->horizon, data.cFileName)) {
                ok = layout_fail("migration path is too long", layout_root, ERROR_FILENAME_EXCED_RANGE); break;
            }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ok = layout_fail("move linked legacy mod folders manually before retrying migration", paths->source, ERROR_INVALID_DATA); break;
            }
            int already_migrated = 0;
            if (!layout_check_destination(paths->target, &already_migrated)) { ok = 0; break; }
            if (already_migrated) continue;
            if (pass) {
                if (!MoveFileExW(paths->source, paths->target, MOVEFILE_WRITE_THROUGH)) {
                    DWORD error = GetLastError();
                    if (layout_move_deferred(paths->source, error)) continue;
                    ok = layout_fail("cannot move legacy mod folder; migration will retry next boot", paths->source, error); break;
                }
                ++layout_moved;
            }
        } while (FindNextFileW(search, &data));
        DWORD error = GetLastError();
        FindClose(search);
        if (!ok) return 0;
        if (error != ERROR_NO_MORE_FILES)
            return layout_fail("cannot finish enumerating legacy mods", layout_root, error);
    }
    if (layout_deferred) return 1; // Keep the original ordering until every folder is ready.
    if (!MoveFileExW(paths->order, paths->target_order, MOVEFILE_WRITE_THROUGH)) {
        DWORD error = GetLastError();
        if (layout_move_deferred(paths->order, error)) return 1;
        return layout_fail("cannot move legacy loadorder.txt; migration will retry next boot", paths->order, error);
    }
    return 1;
}

int addons_layout_prepare(void) {
    if (!layout_pending) return layout_ready;
    layout_pending = 0;
    loader_requested = 0;
    LayoutPaths *paths = HeapAlloc(GetProcessHeap(), 0, sizeof(*paths));
    int ok = 0;
    if (!paths) return layout_fail("cannot allocate migration paths", layout_root, ERROR_NOT_ENOUGH_MEMORY);
    if (!path_join(paths->horizon, layout_root, L"Forza Horizon 2") ||
        !path_join(paths->motorsport, layout_root, L"Forza Motorsport 5") ||
        !path_join(paths->cache, layout_root, L"Cache") ||
        !path_join(paths->order, layout_root, L"loadorder.txt") ||
        !path_join(paths->target_order, paths->horizon, L"loadorder.txt") ||
        !path_join(paths->pattern, layout_root, L"*")) {
        layout_fail("mods layout path is too long", layout_root, ERROR_FILENAME_EXCED_RANGE); goto done;
    }
    const wchar_t *directories[] = {layout_root, paths->horizon, paths->motorsport, paths->cache};
    for (unsigned i = 0; i < ARRAY_COUNT(directories); ++i) {
        if (!layout_directory(directories[i])) {
            layout_fail("cannot create/access mods directory (linked directories are unsupported)", directories[i], GetLastError()); goto done;
        }
    }
    /* The legacy root belongs to FH2, even if FM5 is the first game booted. */
    ok = layout_migrate(paths);
done:
    HeapFree(GetProcessHeap(), 0, paths);
    layout_ready = ok;
    loader_requested = ok;
    return ok;
}

void addons_layout_log(void) {
    if (layout_reported || (!layout_error && !layout_ready)) return;
    layout_reported = 1;
    if (layout_error) {
        report(NULL, layout_error);
        int length = WideCharToMultiByte(CP_UTF8, 0, layout_error_path, -1, NULL, 0, NULL, NULL);
        char *path = length ? HeapAlloc(GetProcessHeap(), 0, (size_t)length) : NULL;
        if (path) {
            WideCharToMultiByte(CP_UTF8, 0, layout_error_path, -1, path, length, NULL, NULL);
            report(NULL, path); HeapFree(GetProcessHeap(), 0, path);
        }
        if (layout_deferred) {
            log_append_value("MODS: migration deferred; Windows error=", (int)layout_error_code);
            report(NULL, "original FH2 loadorder.txt retained; only packages in the FH2 folder can load; retrying migration next boot");
        } else log_append_value("MODS: layout failure; loader disabled; Windows error=", (int)layout_error_code);
    } else {
        report(NULL, fm5_mode ? "using mods/Forza Motorsport 5; cache in mods/Cache" :
                              "using mods/Forza Horizon 2; cache in mods/Cache");
        if (layout_moved) log_append_value("MODS: migrated legacy FH2 folders and loadorder.txt; folders=", (int)layout_moved);
    }
}

static void layout_configure(const wchar_t *base) {
    if (path_join(layout_root, base, L"mods") &&
        path_join(mods_directory, layout_root, fm5_mode ? L"Forza Motorsport 5" : L"Forza Horizon 2"))
        layout_pending = 1;
}
