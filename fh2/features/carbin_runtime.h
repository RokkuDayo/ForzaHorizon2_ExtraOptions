// Included once by media_overrides.c; keeps cache volumes on the native file path.
// Cache entries are immutable after publication. Disk files are temporary and
// deleted when their last handle closes, including on process termination.
typedef struct CarbinCache {
    struct CarbinCache *next;
    unsigned revision, epoch;
    volatile LONG ready;
    HANDLE keeper;
    wchar_t physical[MAX_PATH];
    char relative[];
} CarbinCache;

static CarbinCache *volatile carbin_entries;
static SRWLOCK carbin_lock = SRWLOCK_INIT;
static volatile LONG carbin_thread;
static unsigned carbin_epoch = 1, carbin_count;

static wchar_t *carbin_cached_path(void *volume, const BorrowedGameString *path) {
    if (volume != carbin_volume ||
        GetCurrentThreadId() == (DWORD)InterlockedCompareExchange(&carbin_thread, 0, 0)) {
        return NULL;
    }
    CarbinCache *entry =
        InterlockedCompareExchangePointer((void *volatile *)&carbin_entries, NULL, NULL);
    for (; entry; entry = entry->next) {
        if (entry->epoch != carbin_epoch || entry->revision != addons_media_revision() ||
            !InterlockedCompareExchange(&entry->ready, 0, 0) ||
            !same_path(string_text(path), path->length, entry->relative)) {
            continue;
        }
        size_t bytes = ((size_t)lstrlenW(entry->physical) + 1) * sizeof(wchar_t);
        wchar_t *result = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (result) {
            memory_copy(result, entry->physical, bytes);
        }
        return result;
    }
    return NULL;
}

static void carbin_log(const wchar_t *path, const char *message) {
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
    char *text = n ? HeapAlloc(GetProcessHeap(), 0, (size_t)n) : NULL;
    if (text) {
        WideCharToMultiByte(CP_UTF8, 0, path, -1, text, n, NULL, NULL);
    }
    log_append("MODS CARBIN: ");
    log_append(text ? text : "[path]");
    log_append(" | ");
    log_append(message);
    log_append("\r\n");
    if (text) {
        HeapFree(GetProcessHeap(), 0, text);
    }
}

static int read_patch_file(const wchar_t *path, JsonDocument *doc) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER bytes = {0};
    char *data = NULL;
    DWORD read = 0;
    if (GetFileSizeEx(file, &bytes) && bytes.QuadPart > 0 && bytes.QuadPart <= 16 * 1024 * 1024) {
        data = HeapAlloc(GetProcessHeap(), 0, (size_t)bytes.QuadPart + 1);
        if (data && (!ReadFile(file, data, (DWORD)bytes.QuadPart, &read, NULL) ||
                     read != (DWORD)bytes.QuadPart)) {
            HeapFree(GetProcessHeap(), 0, data);
            data = NULL;
        }
    }
    CloseHandle(file);
    if (!data) {
        return 0;
    }
    data[read] = 0;
    return addon_json_parse(doc, data, read);
}

static unsigned char *carbin_read_base(void *fs, const BorrowedGameString *path, size_t *size,
                                       int *owner, int mapped_owner_only) {
    void *volume = NULL;
    BorrowedGameString relative = {.capacity = 15};
    unsigned char *data = NULL;
    if (original_resolve(fs, &volume, &relative, path, 0, 0, 0, NULL, 0) && volume &&
        volume != priority_volumes[0] && volume != priority_volumes[1]) {
        *owner = volume_addon(volume);
        if (*owner < 0) {
            *owner = addon_archives_volume_addon(volume);
        }
        if (!mapped_owner_only || (*owner >= 0 && addon_ids_changed(*owner))) {
            void *file = NULL;
            ((OpenNativeFile)(*(void ***)volume)[0x138 / 8])(volume, &file, &relative, 0x8000001b,
                                                             1, 0, NULL);
            if (file) {
                void **methods = *(void ***)file;
                typedef unsigned (*Size)(void *);
                typedef unsigned (*Read)(void *, void *, unsigned);
                unsigned bytes = ((Size)methods[0xd8 / 8])(file);
                if (bytes && bytes <= 32 * 1024 * 1024) {
                    data = HeapAlloc(GetProcessHeap(), 0, bytes);
                }
                if (data && ((Read)methods[0x90 / 8])(file, data, bytes) != bytes) {
                    HeapFree(GetProcessHeap(), 0, data);
                    data = NULL;
                }
                if (data) {
                    *size = bytes;
                }
                ((Reference)methods[0x18 / 8])(file);
            }
        }
    } else if (volume == priority_volumes[0] || volume == priority_volumes[1]) {
        *owner = -2;
    }
    if (volume) {
        ((Reference)(*(void ***)volume)[0x18 / 8])(volume);
    }
    if (relative.capacity >= 16) {
        ((Reference)(image + 0x31c7b0))((void *)relative.data);
    }
    return data;
}

static int carbin_save(CarbinCache *entry, const unsigned char *data, size_t size) {
    typedef DWORD(WINAPI * TempPath)(DWORD, wchar_t *);
    typedef UINT(WINAPI * TempName)(const wchar_t *, const wchar_t *, UINT, wchar_t *);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    TempPath temp_path = (TempPath)(void *)GetProcAddress(kernel, "GetTempPathW");
    TempName temp_name = (TempName)(void *)GetProcAddress(kernel, "GetTempFileNameW");
    wchar_t folder[MAX_PATH];
    DWORD length = temp_path ? temp_path(MAX_PATH, folder) : 0;
    if (!length || length >= MAX_PATH || !temp_name ||
        !temp_name(folder, L"fh2", 0, entry->physical)) {
        return 0;
    }
    HANDLE file =
        CreateFileW(entry->physical, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(entry->physical);
        return 0;
    }
    DWORD wrote = 0;
    if (!WriteFile(file, data, (DWORD)size, &wrote, NULL) || wrote != size) {
        CloseHandle(file);
        return 0;
    }
    // Keep a delete-on-close handle alive; native file objects open independent
    // read handles and therefore retain their own seek/close/reopen semantics.
    entry->keeper = file;
    return 1;
}

static int carbin_sha_matches(const JsonValue *root, const unsigned char digest[32]) {
    const JsonValue *sha = addon_json_get(root, "base_sha256");
    if (!sha || sha->kind == JSON_NULL) {
        return 1;
    }
    if (sha->kind != JSON_STRING || sha->length != 64) {
        return -1;
    }
    int matches = 1;
    for (unsigned i = 0; i < 32; ++i) {
        unsigned value = 0;
        for (unsigned j = 0; j < 2; ++j) {
            char ch = sha->text[i * 2 + j];
            unsigned digit = ch >= '0' && ch <= '9'   ? (unsigned)(ch - '0')
                             : ch >= 'a' && ch <= 'f' ? (unsigned)(ch - 'a' + 10)
                             : ch >= 'A' && ch <= 'F' ? (unsigned)(ch - 'A' + 10)
                                                      : 99;
            if (digit > 15) {
                return -1;
            }
            value = value * 16 + digit;
        }
        if (value != digest[i]) {
            matches = 0;
        }
    }
    return matches;
}

static void carbin_prepare(void *filesystem, const BorrowedGameString *path) {
    const char *text = string_text(path);
    size_t length = path->length;
    const char prefix[] = "game:\\media\\";
    if (length < sizeof(prefix) + 7 || length > 1024 ||
        !same_path(text, sizeof(prefix) - 1, prefix) ||
        !same_path(text + length - 7, 7, ".carbin") ||
        !safe_relative(text + sizeof(prefix) - 1, length - (sizeof(prefix) - 1)) ||
        !addons_count() ||
        GetCurrentThreadId() == (DWORD)InterlockedCompareExchange(&carbin_thread, 0, 0)) {
        return;
    }
    void *root = NULL;
    if (!memory_read_pointer((unsigned char *)filesystem + 0x68, &root) || !root) {
        return;
    }
    text += sizeof(prefix) - 1;
    length -= sizeof(prefix) - 1;
    AcquireSRWLockExclusive(&carbin_lock);
    unsigned revision = addons_media_revision();
    CarbinCache *entries =
        InterlockedCompareExchangePointer((void *volatile *)&carbin_entries, NULL, NULL);
    for (CarbinCache *p = entries; p; p = p->next) {
        if (p->revision == revision && p->epoch == carbin_epoch &&
            same_path(text, length, p->relative)) {
            ReleaseSRWLockExclusive(&carbin_lock);
            return;
        }
    }
    if (carbin_count >= 1024) {
        ReleaseSRWLockExclusive(&carbin_lock);
        return;
    }
    CarbinCache *entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry) + length + 1);
    if (!entry) {
        ReleaseSRWLockExclusive(&carbin_lock);
        return;
    }
    entry->revision = revision;
    entry->epoch = carbin_epoch;
    memory_copy(entry->relative, text, length);
    entry->next = entries;
    InterlockedExchangePointer((void *volatile *)&carbin_entries, entry);
    ++carbin_count;
    InterlockedExchange(&carbin_thread, (LONG)GetCurrentThreadId());
    char name[1030];
    memory_copy(name, text, length);
    memory_copy(name + length, ".json", 6);
    BorrowedGameString patch_name = borrowed_string(name);
    unsigned mods = addons_count();
    int any = 0;
    wchar_t *paths[128];
    for (unsigned i = 0; i < mods && i < ARRAY_COUNT(paths); ++i) {
        paths[i] = addon_file_path(i, &patch_name);
        any |= paths[i] != NULL;
    }
    size_t bytes = 0;
    int owner = -1, applied = 0;
    unsigned char *data = any || addon_ids_changed(-1)
                              ? carbin_read_base(filesystem, path, &bytes, &owner, !any)
                              : NULL;
    unsigned char digest[32] = {0};
    unsigned final_ordinal = 0, source_ordinal = 0;
    if (data) {
        Sha256Context sha;
        sha256_initialize(&sha);
        sha256_update(&sha, data, bytes);
        sha256_finalize(&sha, digest);
        if (bytes >= 23) {
            memory_copy(&source_ordinal, data + 19, 4);
        }
        final_ordinal = source_ordinal;
        if (owner >= 0 && addon_ids_changed(owner)) {
            final_ordinal = addon_ids_lookup((unsigned)owner, "Data_Car", "Id", source_ordinal);
            if (!carbin_remap_ids(data, bytes, addon_ids_carbin_value,
                                  (void *)(uintptr_t)(unsigned)owner)) {
                HeapFree(GetProcessHeap(), 0, data);
                data = NULL;
            } else {
                applied = 1;
            }
        }
    }
    for (unsigned i = 0; i < mods && i < ARRAY_COUNT(paths); ++i) {
        if (paths[i]) {
            if (owner == -2) {
                carbin_log(paths[i], "skipped: mediapc/mediaoverride takes priority");
            } else if ((int)i < owner) {
                carbin_log(paths[i], "skipped: later mod supplies a complete carbin");
            } else if (!data) {
                carbin_log(paths[i], "skipped: base carbin could not be read");
            } else {
                JsonDocument doc = {0};
                const char *error = NULL;
                unsigned char *merged = NULL;
                size_t merged_size = 0;
                void *temporary = NULL;
                if (!read_patch_file(paths[i], &doc)) {
                    error = doc.error ? doc.error : "could not read patch (16 MiB maximum)";
                } else {
                    int match = carbin_sha_matches(doc.root, digest);
                    if (match < 0) {
                        error = "invalid base_sha256";
                    } else {
                        if (!match) {
                            carbin_log(paths[i],
                                       "WARNING: base SHA-256 differs; attempting record merge");
                        }
                        if (!addon_ids_carbin_json(i, doc.root, &temporary)) {
                            error = "ID reference remapping failed";
                        } else if (carbin_patch_apply(data, bytes, doc.root, &merged, &merged_size,
                                                      &error)) {
                            HeapFree(GetProcessHeap(), 0, data);
                            data = merged;
                            bytes = merged_size;
                            ++applied;
                            unsigned remapped =
                                addon_ids_lookup(i, "Data_Car", "Id", source_ordinal);
                            if (remapped != source_ordinal) {
                                final_ordinal = remapped;
                            }
                            carbin_log(paths[i], "record patch applied");
                        }
                    }
                }
                if (error) {
                    carbin_log(paths[i], error);
                }
                addon_json_free(&doc);
                addon_ids_free_temporary(temporary);
            }
            HeapFree(GetProcessHeap(), 0, paths[i]);
        }
    }
    if (data && applied && bytes >= 23) {
        memory_copy(data + 19, &final_ordinal, 4);
    }
    if (applied && carbin_save(entry, data, bytes)) {
        if (!carbin_volume &&
            mount_folder(filesystem, "game:\\__fh2_carbin_cache__\\", 0x7ffffffc, -2)) {
            ((SortVolumes)(*(void ***)filesystem)[0x1f8 / 8])(filesystem);
        }
        if (carbin_volume) {
            InterlockedExchange(&entry->ready, 1);
        } else {
            CloseHandle(entry->keeper);
            entry->keeper = NULL;
            log_append("MODS CARBIN: cache mount failed; using unmodified base.\r\n");
        }
    } else if (applied) {
        log_append("MODS CARBIN: temporary cache write failed; using unmodified base.\r\n");
    }
    if (data) {
        HeapFree(GetProcessHeap(), 0, data);
    }
    InterlockedExchange(&carbin_thread, 0);
    ReleaseSRWLockExclusive(&carbin_lock);
}

static void carbin_reset(void) {
    AcquireSRWLockExclusive(&carbin_lock);
    ++carbin_epoch;
    carbin_count = 0;
    priority_volumes[0] = priority_volumes[1] = NULL;
    // Readers may still hold native file objects. Their handles keep old files
    // alive after invalidation. Metadata remains safe for lock-free exists calls.
    CarbinCache *entry =
        InterlockedCompareExchangePointer((void *volatile *)&carbin_entries, NULL, NULL);
    for (; entry; entry = entry->next) {
        if (entry->keeper) {
            CloseHandle(entry->keeper);
            entry->keeper = NULL;
        }
    }
    ReleaseSRWLockExclusive(&carbin_lock);
}
