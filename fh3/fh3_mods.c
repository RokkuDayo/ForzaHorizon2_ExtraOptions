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

static void *heap_copy(const wchar_t *text) {
    size_t bytes = ((size_t)lstrlenW(text) + 1) * sizeof(wchar_t);
    wchar_t *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy) memory_copy(copy, text, bytes);
    return copy;
}

/* Azimuth's native loose volume opens game: paths under Mount. Keep a
   private mirror for FH3 until its external file callbacks are verified. */
static int has_directory(const wchar_t *mod, const wchar_t *child, wchar_t *out);
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
        media_ready[i] = join_short(destination, mirror_root, mod_names[i]) &&
            ensure_directory(destination) && join_short(media, destination, L"media") &&
            mirror_tree(scratch_folder, media);
        if (!media_ready[i]) log_wide("MODS: ", mod_names[i], " media mirror failed.\r\n");
    }
done:
    free_buffer(destination);
    free_buffer(media);
}

/* Listed folders go first, in UTF-8 file order. New folders follow in
   alphabetical order. Higher indices take priority when mounting. */
static void apply_load_order(const wchar_t *path) {
    enum { MAX_ORDER_SIZE = 65536 };
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size;
    char *data = HeapAlloc(GetProcessHeap(), 0, MAX_ORDER_SIZE);
    DWORD bytes;
    int valid = data && GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
        size.QuadPart <= MAX_ORDER_SIZE && ReadFile(file, data, (DWORD)size.QuadPart, &bytes, NULL) &&
        bytes == size.QuadPart && (!bytes || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                                                 data, bytes, NULL, 0));
    CloseHandle(file);
    if (valid) {
        unsigned cursor = bytes >= 3 && memory_equal(data, "\xef\xbb\xbf", 3) ? 3 : 0, slot = 0;
        while (cursor < bytes && slot < mod_count) {
            unsigned start = cursor;
            while (cursor < bytes && data[cursor] != '\n') ++cursor;
            unsigned end = cursor;
            if (cursor < bytes) ++cursor;
            while (start < end && (data[start] == ' ' || data[start] == '\t')) ++start;
            while (start < end && (data[end-1] == '\r' || data[end-1] == ' ' || data[end-1] == '\t')) --end;
            if (start == end || data[start] == '#' || data[start] == ';') continue;
            wchar_t name[MAX_PATH];
            int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data + start, end - start,
                                           name, ARRAY_COUNT(name) - 1);
            if (!count) continue;
            name[count] = 0;
            if (lstrlenW(name) != count) continue;
            for (unsigned i = slot; i < mod_count; ++i) {
                if (lstrcmpiW(name, mod_names[i])) continue;
                lstrcpyW(name, mod_names[i]);
                for (unsigned j = i; j > slot; --j)
                    lstrcpyW(mod_names[j], mod_names[j - 1]);
                lstrcpyW(mod_names[slot++], name);
                break;
            }
        }
    } else {
        log_append("MODS: could not read UTF-8 loadorder.txt; using alphabetical order.\r\n");
    }
    if (data) HeapFree(GetProcessHeap(), 0, data);
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
    if (path_join(pattern, mods_root, L"loadorder.txt")) apply_load_order(pattern);
    if (executable) mirror_media(executable);
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
        int n = WideCharToMultiByte(CP_UTF8, 0, mod_names[i], -1, source + prefix,
                                    (int)sizeof(source) - prefix - 8, NULL, NULL);
        if (n <= 0) continue;
        memory_copy(source + prefix + n - 1, "\\media\\", 8);
        int mounted = fh3_mount(filesystem, source, MOD_MEDIA_PRIORITY + i);
        log_wide("MODS: ", mod_names[i], mounted ? " media mounted.\r\n" : " media mount failed.\r\n");
    }
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
