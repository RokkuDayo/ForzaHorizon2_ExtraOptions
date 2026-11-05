#include "extra_options.h"
#include "fh3.h"
#include "fh3_internal.h"
#include "support.h"
#include "fh3_plugin.h"

/* Mod loader: <Azimuth folder>\mods\<ModName>\{media,plugins}, next to
   azimuth.exe and FH3_ExtraOptions.ini as with FH2. Media folders are
   mounted with the native loose-file volume (like mediaoverride), plugins are
   loaded on the game thread after the file system is up, never under the
   loader lock. */
enum { MAX_MODS = 64, MOD_MEDIA_PRIORITY = 0x7ffffffc };

typedef struct Plugin {
    struct Plugin *next;
    FH3PluginContext context;
    FH3PluginUpdateFn update;
    wchar_t *path, *folder;
} Plugin;

static int mods_enabled;
static wchar_t mods_root[PATH_CAPACITY];
static wchar_t mod_names[MAX_MODS][MAX_PATH];
static int media_reachable; /* game:\mods mirrors each mod's media */
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
    fh3_log(prefix);
    fh3_log(n > 0 ? line : "?");
    fh3_log(suffix);
}

static void *heap_copy(const wchar_t *text) {
    size_t bytes = ((size_t)lstrlenW(text) + 1) * sizeof(wchar_t);
    wchar_t *copy = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (copy) memory_copy(copy, text, bytes);
    return copy;
}

/* Scan the mods folder once: enabled subfolders, sorted by name. The folder
   is the one holding FH3_ExtraOptions.ini (the Azimuth folder). */
/* Azimuth only lets the game open real folders under game:\ (the Mount
   folder): no "..", no junctions or symbolic links. So each enabled mod's
   media\ is mirrored to <Mount>\mods\<ModName>\media\ on every start: real
   folders holding hard links to the mod's files (copies if a hard link
   can't be made), refreshed when a file's size or time changes, and with
   anything no longer in a mod removed. The mirror is marked by MIRROR_NOTE
   and only ever touched when that file is there (or it is new). */
static int has_directory(const wchar_t *mod, const wchar_t *child, wchar_t *out);
#define MIRROR_NOTE L"Extra Options mirror - edit the mods next to azimuth.exe.txt"
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
static void remove_tree(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return;
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path);
        else DeleteFileW(path);
        return;
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
    RemoveDirectoryW(path);
}

static int same_file(const wchar_t *a, const wchar_t *b) {
    WIN32_FILE_ATTRIBUTE_DATA x, y;
    return GetFileAttributesExW(a, GetFileExInfoStandard, &x) &&
           GetFileAttributesExW(b, GetFileExInfoStandard, &y) &&
           x.nFileSizeLow == y.nFileSizeLow && x.nFileSizeHigh == y.nFileSizeHigh &&
           x.ftLastWriteTime.dwLowDateTime == y.ftLastWriteTime.dwLowDateTime &&
           x.ftLastWriteTime.dwHighDateTime == y.ftLastWriteTime.dwHighDateTime;
}

/* Make destination match source; returns the number of files placed. */
static unsigned mirror_tree(const wchar_t *source, const wchar_t *destination) {
    unsigned placed = 0;
    DWORD attributes = GetFileAttributesW(destination);
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        remove_tree(destination);
    CreateDirectoryW(destination, NULL);
    wchar_t *pattern = path_buffer(), *from = path_buffer(), *to = path_buffer();
    if (!pattern || !from || !to) goto done;
    /* Remove what the mod no longer has. */
    if (join_short(pattern, destination, L"*")) {
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(pattern, &data);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (data.cFileName[0] == L'.' &&
                    (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2])))
                    continue;
                if (join_short(from, source, data.cFileName) &&
                    GetFileAttributesW(from) == INVALID_FILE_ATTRIBUTES &&
                    join_short(to, destination, data.cFileName))
                    remove_tree(to);
            } while (FindNextFileW(search, &data));
            FindClose(search);
        }
    }
    if (!join_short(pattern, source, L"*")) goto done;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) goto done;
    do {
        if (data.cFileName[0] == L'.' &&
            (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2])))
            continue;
        if (!join_short(from, source, data.cFileName) || !join_short(to, destination, data.cFileName))
            continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            placed += mirror_tree(from, to);
            continue;
        }
        DWORD existing = GetFileAttributesW(to);
        if (existing != INVALID_FILE_ATTRIBUTES && !(existing & FILE_ATTRIBUTE_DIRECTORY) &&
            same_file(from, to)) {
            ++placed;
            continue;
        }
        remove_tree(to);
        if (CreateHardLinkW(to, from, NULL) || CopyFileW(from, to, FALSE)) ++placed;
        else log_wide("MODS: couldn't place ", to, " for the game.\r\n");
    } while (FindNextFileW(search, &data));
    FindClose(search);
done:
    free_buffer(pattern);
    free_buffer(from);
    free_buffer(to);
    return placed;
}

/* The mirror root, <Mount>\mods; 0 if it holds something that isn't ours. */
static int prepare_mirror(const wchar_t *executable) {
    int folder = lstrlenW(executable);
    while (folder > 0 && executable[folder - 1] != L'\\' && executable[folder - 1] != L'/') --folder;
    if (folder <= 0 || folder + 6 >= MIRROR_PATH) return 0;
    memory_copy(mirror_root, executable, (size_t)folder * sizeof(wchar_t));
    memory_copy(mirror_root + folder, L"mods", 5 * sizeof(wchar_t));
    wchar_t *note = path_buffer();
    int ready = 0;
    if (!note || !join_short(note, mirror_root, MIRROR_NOTE)) goto done;
    DWORD attributes = GetFileAttributesW(mirror_root);
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        RemoveDirectoryW(mirror_root), attributes = INVALID_FILE_ATTRIBUTES; /* an old link */
    if (attributes != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(note) == INVALID_FILE_ATTRIBUTES &&
        !RemoveDirectoryW(mirror_root)) { /* only succeeds when empty */
        log_wide("MODS: ", mirror_root, " holds files Extra Options didn't put there; move those "
                                        "mods next to azimuth.exe. Mod media won't mount.\r\n");
        goto done;
    }
    CreateDirectoryW(mirror_root, NULL);
    HANDLE marker = CreateFileW(note, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (marker == INVALID_HANDLE_VALUE) goto done;
    static const char text[] =
        "Extra Options copies each mod's media folder here for the game, because Azimuth\r\n"
        "only lets it read the game's own folder. Edit the mods in the mods folder next to\r\n"
        "azimuth.exe; this folder is rebuilt every time the game starts.\r\n";
    DWORD written;
    WriteFile(marker, text, sizeof(text) - 1, &written, NULL);
    CloseHandle(marker);
    ready = 1;
done:
    free_buffer(note);
    return ready;
}

static void mirror_media(const wchar_t *executable) {
    if (!prepare_mirror(executable)) return;
    wchar_t *source = path_buffer(), *destination = path_buffer(), *pattern = path_buffer();
    if (!source || !destination || !pattern) goto done;
    /* Drop mirrored mods that are gone or disabled. */
    if (join_short(pattern, mirror_root, L"*")) {
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(pattern, &data);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == L'.')
                    continue;
                int kept = 0;
                for (unsigned i = 0; i < mod_count && !kept; ++i)
                    kept = !lstrcmpiW(mod_names[i], data.cFileName) &&
                           has_directory(mod_names[i], L"media", scratch_folder);
                if (!kept && join_short(destination, mirror_root, data.cFileName)) remove_tree(destination);
            } while (FindNextFileW(search, &data));
            FindClose(search);
        }
    }
    for (unsigned i = 0; i < mod_count; ++i) {
        if (!has_directory(mod_names[i], L"media", scratch_folder)) continue;
        if (lstrlenW(scratch_folder) >= MIRROR_PATH) continue;
        lstrcpyW(source, scratch_folder);
        if (!join_short(destination, mirror_root, mod_names[i])) continue;
        CreateDirectoryW(destination, NULL);
        if (!join_short(pattern, destination, L"media")) continue;
        unsigned files = mirror_tree(source, pattern);
        (void)files;
    }
    media_reachable = 1;
done:
    free_buffer(source);
    free_buffer(destination);
    free_buffer(pattern);
}

void fh3_mods_configure(const wchar_t *executable) {
    mods_enabled = fh3_option(L"Mods", L"Enabled", 1);
    const wchar_t *ini = fh3_ini_path();
    if (!mods_enabled || !ini || !ini[0]) return;
    wchar_t *folder = scratch_folder, *pattern = scratch_pattern;
    int length = lstrlenW(ini);
    while (length > 0 && ini[length - 1] != L'\\' && ini[length - 1] != L'/') --length;
    if (length <= 0 || length >= PATH_CAPACITY) return;
    memory_copy(folder, ini, (size_t)length * sizeof(wchar_t));
    folder[length] = 0;
    if (!path_join(mods_root, folder, L"mods") || !path_join(pattern, mods_root, L"*")) return;
    if (GetFileAttributesW(mods_root) == INVALID_FILE_ATTRIBUTES) return;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == L'.') continue;
        int disabled = !GetPrivateProfileIntW(L"Mods", data.cFileName, 1, fh3_ini_path());
        if (disabled) {
            log_wide("MODS: ", data.cFileName, " disabled in FH3_ExtraOptions.ini.\r\n");
            continue;
        }
        if (mod_count == MAX_MODS) {
            fh3_log("MODS: more than 64 mods; the rest are ignored.\r\n");
            break;
        }
        unsigned slot = mod_count++;
        while (slot && lstrcmpiW(mod_names[slot - 1], data.cFileName) > 0) {
            memory_copy(mod_names[slot], mod_names[slot - 1], sizeof(mod_names[slot]));
            --slot;
        }
        lstrcpyW(mod_names[slot], data.cFileName);
    } while (FindNextFileW(search, &data));
    FindClose(search);
    if (!mod_count) return;
    if (executable) mirror_media(executable);
    fh3_log(media_reachable ? "MODS: mods folder scanned, media mirrored to game:\\mods; media and "
                              "plugins load after file-system initialization.\r\n"
                            : "MODS: mods folder scanned; mod media won't mount (see above).\r\n");
}

int fh3_mods_enabled(void) {
    return mods_enabled && mod_count;
}

static int has_directory(const wchar_t *mod, const wchar_t *child, wchar_t *out) {
    wchar_t *base = scratch_base;
    if (!path_join(base, mods_root, mod) || !path_join(out, base, child)) return 0;
    DWORD attributes = GetFileAttributesW(out);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* game:\mods holds the mirrored media (mirror_media), so each mounts as
   game:\mods\<ModName>\media\ over game:\media\. */
void fh3_mods_mount(void *filesystem) {
    for (unsigned i = 0; media_reachable && i < mod_count; ++i) {
        if (!has_directory(mod_names[i], L"media", scratch_folder)) continue;
        char source[MAX_PATH * 3];
        const char head[] = "game:\\mods\\";
        int prefix = sizeof(head) - 1;
        memory_copy(source, head, (size_t)prefix);
        int n = WideCharToMultiByte(CP_UTF8, 0, mod_names[i], -1, source + prefix,
                                    (int)sizeof(source) - prefix - 8, NULL, NULL);
        if (n <= 0) continue;
        memory_copy(source + prefix + n - 1, "\\media\\", 8);
        int mounted = fh3_mount(filesystem, source, MOD_MEDIA_PRIORITY - i);
        log_wide("MODS: ", mod_names[i], mounted ? " media mounted.\r\n" : " media mount failed.\r\n");
    }
}

/* --- Plugin services. --- */

static void plugin_log(const char *message) {
    fh3_log("PLUGIN: ");
    fh3_log(message ? message : "");
    fh3_log("\r\n");
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
    HMODULE module = LoadLibraryExW(path, NULL,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
        log_wide("MODS: ", path, " failed to load.\r\n");
        return;
    }
    Plugin *plugin = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*plugin));
    if (!plugin) return;
    plugin->path = heap_copy(path);
    plugin->folder = heap_copy(mod_directory);
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
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || count == 32) continue;
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
