#include "fh3.h"
#include "fh3_internal.h"
#include "fh3_text.h"
#include "fh3_plugin.h"
#include "shared/sha256.h"
#include "shared/support.h"
#include <xmmintrin.h>
#include "shared/adapters/azimuth_markers.h"
#include "shared/logging.h"

/* All FH3 features participate in the same verified startup transaction. */
typedef struct {
    unsigned rva, size;
    unsigned char original[12], replacement[12];
    DWORD protection;
} Site;

/* Native std::string layout; capacity >= 16 selects the external pointer. */
typedef struct {
    const char *data;
    uintptr_t reserved;
    size_t length, capacity;
} GameString;
typedef unsigned char (*InitializeFiles)(void *);
typedef void *(*Allocate)(size_t);
typedef void *(*ConstructVolume)(void *, const GameString *, unsigned char);
typedef void (*Reference)(void *);
typedef unsigned char (*Mount)(void *, const GameString *, void **, unsigned, unsigned char,
                               unsigned char, const GameString *);
typedef void (*Sort)(void *);

static unsigned char *image;
static wchar_t ini_path[PATH_CAPACITY];
static unsigned char file_buffer[65536];
#ifdef FH2_TWEAKS_TEST
static int fail_protection = -1;
#endif

static void open_log(const wchar_t *directory) {
    static wchar_t path[PATH_CAPACITY];
    if (!path_join(path, directory, L"FH3_ExtraOptions.log")) return;
    HANDLE log = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    log_set_named(directory, L"FH3_ExtraOptions.log");
    log_append("Forza Horizon 3 Extra Options " MOD_VERSION "\r\n");
}

static int rejected(const char *reason) {
    status_set(reason);
    log_append(reason);
    log_append("\r\n");
    return 0;
}

static int option(const wchar_t *section, const wchar_t *key, int fallback) {
    wchar_t value[16];
    GetPrivateProfileStringW(section, key, fallback ? L"1" : L"0", value, ARRAY_COUNT(value),
                             ini_path);
    if (value[0] == L'0' && !value[1]) return 0;
    if (value[0] == L'1' && !value[1]) return 1;
    log_append(fallback ? "CONFIG: invalid boolean; expected 0 or 1, using default 1.\r\n"
                       : "CONFIG: invalid boolean; expected 0 or 1, using default 0.\r\n");
    return fallback;
}

static int verify_file(const wchar_t *path) {
    static const unsigned char expected[32] = {
        0xe6,0xa3,0x62,0xa2,0xfd,0xe1,0x18,0x57,0x7a,0xb3,0x01,0x8c,0x8e,0x04,0x2e,0x62,
        0xa3,0xbb,0x42,0xf8,0x3a,0x63,0xac,0xe4,0x11,0x45,0xc0,0x86,0x45,0x27,0xa6,0x61};
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    int valid = GetFileSizeEx(file, &size) && size.QuadPart == FH3_FILE_SIZE;
    Sha256Context hash;
    sha256_initialize(&hash);
    DWORD count;
    uint64_t total = 0;
    while (valid && total < FH3_FILE_SIZE) {
        valid = ReadFile(file, file_buffer, sizeof(file_buffer), &count, NULL) && count;
        if (valid) {
            sha256_update(&hash, file_buffer, count);
            total += count;
        }
    }
    CloseHandle(file);
    unsigned char digest[32];
    sha256_finalize(&hash, digest);
    return valid && total == FH3_FILE_SIZE && memory_equal(digest, expected, sizeof(expected));
}

static int readable_image(unsigned char *base) {
    uintptr_t cursor = (uintptr_t)base, end = cursor + FH3_IMAGE_SIZE;
    if (!base || end < cursor) return 0;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region;
        if (VirtualQuery((void *)cursor, &region, sizeof(region)) != sizeof(region) ||
            region.State != MEM_COMMIT || region.AllocationBase != base ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        DWORD protection = region.Protect & 0xff;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return 0;
        uintptr_t next = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (next <= cursor) return 0;
        cursor = next;
    }
    return 1;
}

static GameString borrowed(const char *text) {
    return (GameString){text, 0, text_length(text), 31};
}

static int mount_folder(void *filesystem, const char *folder, unsigned priority) {
    void *storage = ((Allocate)(image + 0x1edbd00))(0x88);
    if (!storage) return 0;
    GameString source = borrowed(folder), destination = borrowed("game:\\media\\");
    void *volume = ((ConstructVolume)(image + 0x1c40610))(storage, &source, 1);
    if (!volume) return 0;
    void **methods = *(void ***)volume;
    ((Reference)methods[0x10 / 8])(volume);
    /* The native mount retains its own reference. No manifest: files newly
       added to these folders are visible too. */
    int mounted = ((Mount)(*(void ***)filesystem)[0x1e8 / 8])(filesystem, &destination, &volume,
                                                             priority, 0, 0, NULL);
    ((Reference)methods[0x18 / 8])(volume);
    return mounted;
}

/* Replaces the single native call to the file-system initializer, which mounts
   game:\ with hash.manifest filtering and sorts the volumes. */
static int media_overrides;

/* Services for fh3_mods.c (fh3_internal.h). */
unsigned char *fh3_image(void) { return image; }
const wchar_t *fh3_ini_path(void) { return ini_path; }
int fh3_mount(void *filesystem, const char *folder, unsigned priority) {
    return mount_folder(filesystem, folder, priority);
}

static unsigned char initialize_files(void *filesystem) {
    unsigned char result = ((InitializeFiles)(image + 0x18d3770))(filesystem);
    if (!result) {
        log_append("FILES: native initialization failed; override mounts skipped.\r\n");
        return result;
    }
    int override = !media_overrides || mount_folder(filesystem, "game:\\mediaoverride\\", 0x7ffffffe);
    int pc = !media_overrides || mount_folder(filesystem, "game:\\mediapc\\", 0x7ffffffd - 0x100);
    fh3_mods_mount(filesystem);
    ((Sort)(*(void ***)filesystem)[0x1f8 / 8])(filesystem);
    log_append(override && pc ? "FILES: native mounts ready: mediaoverride > mods > mediapc > original "
                               "media.\r\n"
                             : "FILES: an override mount failed; native asset fallback retained.\r\n");
    fh3_mods_load_plugins();
    return result;
}

/* Forzathon unlocks. The game reads media\stripped\gamedbRC.slt into memory
   and opens it read-write with sqlite3_open("memorydb:\1"), so it is told
   apart from other databases by its HorizonEditionCars table. Forzathon
   prizes (the Horizon Editions and others) and other special cars are hidden
   from the Autoshow by Data_Car.NotAvailableInAutoshow, and the Forzathon
   horns are neither initial horns nor perk unlocks (31-33 unlock otherwise).
   Horizon Editions stay out of the Autoshow unless asked for.
   Wheelspin prizes (TombolaPrizes) award a random car of CarAwardIds, split
   on commas into a list of any length; the two 1,000,000 CR prizes hold 17
   of the 33 Horizon Editions, and are given all of them
   (HorizonEditionCars also lists 2691, which has no car).
   The connection is copied into memory and the copy edited, so the file on
   disk never changes. SQLite is FH2's build, 0x7410 further on. */
enum {
    SQLITE_OPEN_THUNK = 0x1a564d1,
    SQLITE_OPEN = 0x84ad0,
    SQLITE_CLOSE = 0x82330,
    SQLITE_PREPARE = 0x44b20,
    SQLITE_STEP = 0x262f0,
    SQLITE_FINALIZE = 0x21f50,
    SQLITE_BACKUP_INIT = 0x15b50,
    SQLITE_BACKUP_STEP = 0x16040,
    SQLITE_BACKUP_FINISH = 0x1fc20,
    SQLITE_ROW = 100,
    SQLITE_DONE = 101,
};
typedef int (*SqliteOpen)(const char *, void **);
typedef int (*SqliteCall)(void *);
typedef int (*SqlitePrepare)(void *, const char *, int, void **, const char **);
typedef void *(*SqliteBackupInit)(void *, const char *, void *, const char *);
typedef int (*SqliteBackupStep)(void *, int);

static int forzathon_cars, horizon_editions_autoshow, forzathon_horns, forzathon_wheelspins;

static int sql_run(void *database, const char *query) {
    void *statement = NULL;
    int ok = !((SqlitePrepare)(image + SQLITE_PREPARE))(database, query, -1, &statement, NULL) &&
             statement && ((SqliteCall)(image + SQLITE_STEP))(statement) == SQLITE_DONE;
    if (statement) ((SqliteCall)(image + SQLITE_FINALIZE))(statement);
    return ok;
}

static int game_database(void *database) {
    void *statement = NULL;
    int found = !((SqlitePrepare)(image + SQLITE_PREPARE))(
                    database, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='HorizonEditionCars'",
                    -1, &statement, NULL) &&
                statement && ((SqliteCall)(image + SQLITE_STEP))(statement) == SQLITE_ROW;
    if (statement) ((SqliteCall)(image + SQLITE_FINALIZE))(statement);
    return found;
}

static int open_database(const char *filename, void **database) {
    int result = ((SqliteOpen)(image + SQLITE_OPEN))(filename, database);
    if (result || !database || !*database || !game_database(*database)) return result;
    void *copy = NULL;
    if (((SqliteOpen)(image + SQLITE_OPEN))(":memory:", &copy) || !copy) {
        if (copy) ((SqliteCall)(image + SQLITE_CLOSE))(copy);
        log_append("UNLOCKS: no in-memory database; Forzathon cars and horns stay locked.\r\n");
        return result;
    }
    void *backup = ((SqliteBackupInit)(image + SQLITE_BACKUP_INIT))(copy, "main", *database, "main");
    int copied = backup && ((SqliteBackupStep)(image + SQLITE_BACKUP_STEP))(backup, -1) == SQLITE_DONE;
    if (backup && ((SqliteCall)(image + SQLITE_BACKUP_FINISH))(backup)) copied = 0;
    int cars = copied && forzathon_cars &&
               sql_run(copy, "UPDATE Data_Car SET NotAvailableInAutoshow=0 WHERE NotAvailableInAutoshow<>0 "
                             "AND Id NOT IN (SELECT CarId FROM HorizonEditionCars)");
    int editions = copied && horizon_editions_autoshow &&
                   sql_run(copy, "UPDATE Data_Car SET NotAvailableInAutoshow=0 "
                                 "WHERE Id IN (SELECT CarId FROM HorizonEditionCars)");
    if (cars) log_append("UNLOCKS: every car hidden from the Autoshow can be bought (in memory).\r\n");
    if (editions) log_append("UNLOCKS: Horizon Edition cars can be bought in the Autoshow (in memory).\r\n");
    int horns = copied && forzathon_horns &&
                sql_run(copy, "UPDATE CarHorns SET IsInitial=1 "
                              "WHERE IsInitial=0 AND UnlockByPerk=-1 AND Id NOT IN (31,32,33)");
    int spins = copied && forzathon_wheelspins &&
                sql_run(copy, "UPDATE TombolaPrizes SET CarAwardIds="
                              "(SELECT group_concat(h.CarId) FROM HorizonEditionCars h JOIN Data_Car c ON c.Id=h.CarId) "
                              "WHERE PrizeId IN (SELECT p.PrizeId FROM TombolaPrizes p JOIN HorizonEditionCars h "
                              "ON ','||p.CarAwardIds||',' LIKE '%,'||h.CarId||',%')");
    if (horns) log_append("UNLOCKS: Forzathon horns unlocked (in memory).\r\n");
    if (spins) log_append("UNLOCKS: every Horizon Edition car can be won in Wheelspins (in memory).\r\n");
    if ((cars || editions || horns || spins) && !((SqliteCall)(image + SQLITE_CLOSE))(*database)) {
        *database = copy;
    } else {
        ((SqliteCall)(image + SQLITE_CLOSE))(copy);
        log_append("UNLOCKS: database edit failed; cars, horns and Wheelspins unchanged.\r\n");
    }
    return result;
}

/* Autodrive. The scripted SetCarAIDriving state hands a car to the AI through
   0x18b7040(car, enabled); car+0x7398 is 1 while a human drives and 0 under
   AI. The local player's cars are listed at 0x1724f70(manager, 0)+0x298. */
typedef void (*SatNavUpdate)(void *, float);
typedef void *(*PlayerContext)(void *, unsigned);
typedef SHORT(WINAPI *KeyState)(int);
typedef HWND(WINAPI *ForegroundWindow)(void);
typedef DWORD(WINAPI *WindowProcess)(HWND, LPDWORD);

static KeyState key_state;
static ForegroundWindow foreground_window;
static WindowProcess window_process;

static int window_focused(void) {
    if (!key_state) {
        HMODULE user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!user32) return 0;
        key_state = (KeyState)(uintptr_t)GetProcAddress(user32, "GetAsyncKeyState");
        /* win32u's syscall stub, so a mod that makes user32's answer always
           the game (NoPause) doesn't turn keys typed elsewhere into input. */
        HMODULE win32u = GetModuleHandleW(L"win32u.dll");
        foreground_window =
            win32u ? (ForegroundWindow)(uintptr_t)GetProcAddress(win32u, "NtUserGetForegroundWindow")
                   : NULL;
        if (!foreground_window)
            foreground_window = (ForegroundWindow)(uintptr_t)GetProcAddress(user32, "GetForegroundWindow");
        window_process = (WindowProcess)(uintptr_t)GetProcAddress(user32, "GetWindowThreadProcessId");
        if (!key_state || !foreground_window || !window_process) {
            key_state = NULL;
            return 0;
        }
    }
    DWORD process = 0;
    HWND window = foreground_window();
    return window && (window_process(window, &process), process == GetCurrentProcessId());
}

static int focused_key(int key) {
    return window_focused() && (key_state(key) & 0x8000) != 0;
}

static int driver_mode(void *car) {
    void *holder, *driver;
    int mode;
    if (!memory_read_pointer((unsigned char *)car + 0x7378, &holder) ||
        !memory_read_pointer(holder, &driver) ||
        !memory_read((unsigned char *)car + 0x7398, &mode, sizeof(mode)))
        return -1;
    return mode;
}

static void *player_car(void) {
    void *manager, *context, *list;
    void *cars[2];
    if (!memory_read_pointer(image + 0x4e5bb48, &manager)) return NULL;
    context = ((PlayerContext)(image + 0x1724f70))(manager, 0);
    if (!context || !memory_read_pointer((unsigned char *)context + 0x298, &list) ||
        !memory_read(list, cars, sizeof(cars)) || !cars[0] || cars[1] < cars[0] ||
        (uintptr_t)cars[1] - (uintptr_t)cars[0] > 64 * sizeof(void *))
        return NULL;
    for (void **entry = cars[0]; entry < (void **)cars[1]; ++entry) {
        void *car;
        if (memory_read_pointer(entry, &car) && driver_mode(car) == 1) return car;
    }
    return NULL;
}

/* Pads are read straight from XInput. */
typedef struct {
    DWORD packet;
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT left_x, left_y, right_x, right_y;
} PadState;
typedef DWORD(WINAPI *GetPadState)(DWORD, PadState *);
enum { PAD_LEFT_THUMB = 0x40, PAD_RIGHT_THUMB = 0x80 };
static GetPadState get_pad_state;

static int load_pads(void) {
    if (!get_pad_state) {
        HMODULE xinput = LoadLibraryExW(L"xinput1_4.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        get_pad_state = xinput ? (GetPadState)(uintptr_t)GetProcAddress(xinput, "XInputGetState")
                               : NULL;
    }
    return get_pad_state != NULL;
}

typedef struct {
    union {
        char text[16];
        char *pointer;
    };
    size_t size, capacity;
} MsvcString;

static int game_language_code(char code[4]) {
    unsigned char *tables;
    void *vtable;
    MsvcString language;
    if (!memory_read_pointer(image + 0x4e7dda8, (void **)&tables) ||
        !memory_read_pointer(tables, &vtable) || vtable != image + 0x467d858 ||
        !memory_read(tables + 0x30, &language, sizeof(language)) ||
        language.capacity != 15 || language.size < 2 || language.size > 3 ||
        language.text[language.size] != 0) return 0;
    memory_copy(code, language.text, language.size + 1);
    return 1;
}
typedef void **(*CreateUIString)(void **);

/* Treasure Map. The native purchase completion (0x1532e00) calls 0x151bf80,
   which marks the map owned for the current world (0x228 mainland, 0x229 and
   0x22a the expansions; read at [0x4e6cc60]+8) and sets PlayerState's
   TreasureMapPurchased. It takes no arguments and does nothing without a
   PlayerState, so it runs once per world after that world has been loaded
   for a few seconds. */
static int unlock_treasure_map;

/* Photo mode limits are tunables from media/Camera/CameraSettings.ini
   ("PhotoModeCam" keys), each bound to a value in .data by 0x1a5baa0 when the
   file is parsed. They are overwritten after parsing, from the per-frame
   update. */
static int unrestricted_photo, photo_no_collision;
/* Unused tail of .data's last page (the section ends at 0x507992c): the ground
   probe's private CollisionFixIterations. */
#define PHOTO_PROBE_ITERATIONS 0x5079a00u
#define PHOTO_ITERATIONS 0x42ee8f0u

/* Camera FOVs (degrees) are tunables from the same CameraSettings.ini. The two
   chase cameras share a struct whose FOV is at +0x1c (FollowLowCam 0x463a090,
   FollowHighCam 0x463a0d0); the others are plain globals. 0 keeps the file's
   value. */
static struct {
    const wchar_t *key;
    unsigned rva;
    float value;
} camera_fovs[] = {
    {L"ChaseNearFOV", 0x463a090 + 0x1c, 0}, {L"ChaseFarFOV", 0x463a0d0 + 0x1c, 0},
    {L"HoodFOV", 0x4639f1c, 0},             {L"BumperFOV", 0x4639efc, 0},
    {L"CockpitFOV", 0x4639f58, 0},
};
static int camera_fov_used;

/* "65" or "72.5"; anything else (or out of 10..150) yields 0. */
static float parse_degrees(const wchar_t *text) {
    float value = 0.0f, scale = 0.0f;
    for (; *text; ++text) {
        if (*text >= L'0' && *text <= L'9') {
            if (scale) {
                value += (float)(*text - L'0') * scale;
                scale *= 0.1f;
            } else {
                value = value * 10.0f + (float)(*text - L'0');
            }
        } else if (*text == L'.' && !scale) {
            scale = 0.1f;
        } else if (*text != L' ') {
            return 0.0f;
        }
    }
    return value >= 10.0f && value <= 150.0f ? value : 0.0f;
}

static void camera_fov_tick(void) {
    for (unsigned i = 0; i < ARRAY_COUNT(camera_fovs); ++i)
        if (camera_fovs[i].value) *(volatile float *)(image + camera_fovs[i].rva) = camera_fovs[i].value;
}

static void photo_mode_tick(void) {
    static const struct { unsigned rva; float value; } limits[] = {
        {0x42ee900, 100000.0f}, /* BoundaryCylinderRadius (file: 100) */
        {0x42ee904, 100000.0f}, /* BoundaryCeilingHeight (30) */
        {0x42ee908, 100000.0f}, /* BoundaryFloorDepth (30) */
        {0x42ee78c, 10000.0f},  /* MaxHeight (2) */
        {0x42ee768, 0.0f},      /* CarBoundingBoxHorizontalExpansion (0.5) */
        {0x42ee76c, 0.0f},      /* CarBoundingBoxVerticalExpansion (0.5) */
    };
    for (unsigned i = 0; i < ARRAY_COUNT(limits); ++i)
        *(volatile float *)(image + limits[i].rva) = limits[i].value;
    if (photo_no_collision) {
        /* Zero CollisionFixIterations for every camera collision query, but
           let the ground probe (0x444b40, whose read is redirected) keep the
           file's value so it still measures the altitude. */
        volatile int *shared = (volatile int *)(image + PHOTO_ITERATIONS);
        volatile int *probe = (volatile int *)(image + PHOTO_PROBE_ITERATIONS);
        if (*shared) *probe = *shared;
        if (!*probe) *probe = 5;
        *shared = 0;
    }
}
static unsigned treasure_world, treasure_done;
static ULONGLONG treasure_since;

static void treasure_map_tick(void) {
    void *worlds;
    unsigned world = 0;
    if (!memory_read_pointer(image + 0x4e6cc60, &worlds) ||
        !memory_read((unsigned char *)worlds + 8, &world, sizeof(world)) || world < 0x228 ||
        world > 0x22a)
        return;
    ULONGLONG now = GetTickCount64();
    if (world != treasure_world) {
        treasure_world = world;
        treasure_since = now;
        return;
    }
    unsigned bit = 1u << (world - 0x228);
    if ((treasure_done & bit) || now - treasure_since < 5000) return;
    treasure_done |= bit;
    ((void (*)(void))(image + 0x151bf80))();
    log_append("TREASURE MAP: unlocked for the current world.\r\n");
}
/* Horn Accelerator, the HornUnlock add-on (product
   3bd05167-3977-4bc9-9870-5ad74e6eb21a). Store ownership asks the wrapper
   0xfa6350(manager, product), which passes manager+8 on to 0xfa6360; the
   wrapper is replaced so that product reports owned and every other product
   keeps the native answer. Extras tiles are refreshed by 0x1544530(record)
   (three calls): after the native refresh a HornUnlock record is marked
   purchased and seen (+0x54, +0x55) and its purchase badge (+0x58) cleared,
   with the native property change (0x125ae40 with 0x1548900's property). */
enum { HORN_OWNED = 0xfa6350, HORN_OWNED_INNER = 0xfa6360, EXTRAS_REFRESH = 0x1544530,
       EXTRAS_REFRESH_THUNK = 0x1544a32, EXTRAS_BADGE_PROPERTY = 0x1548900,
       PROPERTY_CHANGED = 0x125ae40 };
static volatile LONG horn_reported;

static unsigned char horn_owned(void *manager, void *product) {
    static const wchar_t id[] = L"3bd05167-3977-4bc9-9870-5ad74e6eb21a";
    struct {
        const wchar_t *text;
        uintptr_t reserved;
        size_t length, capacity;
    } name;
    wchar_t text[36];
    int same = product && memory_read(product, &name, sizeof(name)) && name.length == 36 &&
               name.capacity >= 36 && memory_read(name.text, text, sizeof(text));
    for (unsigned i = 0; same && i < 36; ++i)
        same = (text[i] >= L'A' && text[i] <= L'Z' ? text[i] + 32 : text[i]) == id[i];
    if (same) {
        if (!InterlockedExchange(&horn_reported, 1))
            log_append("HORN ACCELERATOR: owned (in memory).\r\n");
        return 1;
    }
    void *inner;
    if (!manager || !memory_read((unsigned char *)manager + 8, &inner, sizeof(inner))) return 0;
    return ((unsigned char (*)(void *, void *))(image + HORN_OWNED_INNER))(inner, product);
}

static void extras_refresh(void *record) {
    ((void (*)(void *))(image + EXTRAS_REFRESH))(record);
    unsigned char fields[0x5c], text[10];
    void **table, *getter;
    MsvcString value;
    if (!record || !memory_read(record, fields, sizeof(fields)) ||
        !memory_read_pointer(record, (void **)&table) || !memory_read_pointer(table + 0x58 / 8, &getter))
        return;
    const MsvcString *type =
        ((const MsvcString *(*)(void *, const char *))getter)(record, "AddOnType");
    if (!type || !memory_read(type, &value, sizeof(value)) || value.size != 10 ||
        !memory_read(value.capacity >= 16 ? value.pointer : type->text, text, sizeof(text)) ||
        !memory_equal(text, "HornUnlock", sizeof(text)))
        return;
    unsigned char *bytes = record;
    bytes[0x54] = 1;
    bytes[0x55] = 1;
    int badge;
    memory_copy(&badge, bytes + 0x58, sizeof(badge));
    if (badge) {
        badge = 0;
        memory_copy(bytes + 0x58, &badge, sizeof(badge));
        ((void (*)(void *, void *))(image + PROPERTY_CHANGED))(
            record, ((void *(*)(void))(image + EXTRAS_BADGE_PROPERTY))());
    }
}

void *fh3_player_car(void) { return player_car(); }

static void hud_set_hidden(int mode);
static void hud_set_prompt(const wchar_t *before, const char *button, const wchar_t *after);
static void hud_set_anna_status(const wchar_t *label, const wchar_t *value);
static void hud_set_anna_ready(int ready);
void fh3_set_anna_ready(int ready) { hud_set_anna_ready(ready); }
void fh3_set_hud_hidden(int mode) { hud_set_hidden(mode); }
void fh3_set_anna_status(const wchar_t *label, const wchar_t *value) { hud_set_anna_status(label, value); }
void fh3_set_hud_prompt(const wchar_t *before, const char *button, const wchar_t *after) {
    hud_set_prompt(before, button, after);
}

static void garage_tick(void);

static void satnav_update(void *self, float dt) {
    ((SatNavUpdate)(image + 0x8f7d10))(self, dt);
    if (unlock_treasure_map) treasure_map_tick();
    if (unrestricted_photo) photo_mode_tick();
    camera_fov_tick();
    garage_tick();
    fh3_mods_frame(dt);
}

/* Minimap follows the orbit camera. MapViewControllerTrackingMiniMap's update
   0x120c6c0(controller, position, direction, speed, zoom) has one caller
   (0x120b3d2); the direction (also kept at controller+0xd0) turns the map. In
   FH3 the look-around camera is driven by the right stick and springs back on
   release, so the camera's angle around the car is the stick's angle: the
   direction is rotated by a smoothed copy of the stick vector. */
typedef void (*MinimapUpdate)(void *, const float *, const float *, float, float);
static int minimap_follows_camera;
static int in_game_menu;
static float look_x, look_y = 1.0f; /* smoothed (sin, cos) of the look angle */
static float look_sin, look_cos = 1.0f; /* rotation applied to the minimap direction */
typedef float (*Atan2)(float, float);

/* The ANNA HUD's "N" marker (NorthAngle, 0xab2930) is atan2f(dx, dz) of the
   tracked heading via 0x1802d9a, independent of the controller direction.
   Rotating that heading by (sin, cos) lowers its angle by atan2f(sin, cos). */
static float north_angle(float y, float x) {
    Atan2 atan2_native = (Atan2)(image + 0x1802d9a);
    float angle = atan2_native(y, x);
    if (look_sin > 0.001f || look_sin < -0.001f || look_cos < 0.999f)
        angle -= atan2_native(look_sin, look_cos);
    return angle;
}

static float square_root(float value) {
    return value > 0.0f ? _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(value))) : 0.0f;
}

static void minimap_update(void *controller, const float *position, const float *direction,
                           float speed, float zoom) {
    /* The pause-menu World Map controller uses zero zoom. Only the HUD
       controller should follow the camera or change the smoothed heading. */
    if (zoom == 0.0f) {
        ((MinimapUpdate)(image + 0x120c6c0))(controller, position, direction, speed, zoom);
        return;
    }
    float target_x = 0.0f, target_y = 1.0f, strongest = 0.0f;
    for (DWORD pad = 0; minimap_follows_camera && load_pads() && pad < 4; ++pad) {
        PadState state;
        if (get_pad_state(pad, &state) != ERROR_SUCCESS) continue;
        float x = state.right_x / 32767.0f, y = state.right_y / 32767.0f;
        float length = square_root(x * x + y * y);
        if (length > 0.35f && length > strongest) {
            strongest = length;
            target_x = x / length;
            target_y = y / length;
        }
    }
    look_x += (target_x - look_x) * 0.2f;
    look_y += (target_y - look_y) * 0.2f;
    float length = square_root(look_x * look_x + look_y * look_y);
    if (length < 0.001f) {
        look_x = 0.0f;
        look_y = 1.0f;
        length = 1.0f;
    }
    float s = -look_x / length, c = look_y / length;
    look_sin = s;
    look_cos = c;
    if (s > 0.001f || s < -0.001f || c < 0.999f) {
        _Alignas(16) float rotated[4];
        const float *base = direction;
        if (!(direction[0] || direction[2]) &&
            memory_read((unsigned char *)controller + 0xd0, rotated, 16))
            base = rotated;
        float x = base[0], z = base[2];
        rotated[0] = x * c - z * s;
        rotated[1] = base[1];
        rotated[2] = z * c + x * s;
        rotated[3] = base[3];
        direction = rotated;
        ((MinimapUpdate)(image + 0x120c6c0))(controller, position, direction, speed, zoom);
        return;
    }
    ((MinimapUpdate)(image + 0x120c6c0))(controller, position, direction, speed, zoom);
}

/* Drift camera: while the car slides, the chase cameras swing toward the
   direction of travel instead of staying locked behind the body.
   - Chase near/far share the heading update 0x19bd250(camera, dt, reset).
     It takes the body heading as atan2f(-forward.basis_z, -forward.basis_x)
     at 0x19bd314 (camera in rbx), then springs the yaw at +0x1e0 (speed
     +0x1e4) with the settings at [camera+0x1a0]: +0x20 stiffness, +0x24
     damping, +0x28 yaw-rate lead, +0x2c speed limit.
   - The camera target (+8) is the ForzaCar; its velocity copy is +0x75e0. */
enum {
    CHASE_NEAR_VTABLE = 0x463b250,
    CHASE_FAR_VTABLE = 0x463b518,
    FORZA_CAR_VTABLE = 0x42e09a0,
    CAR_VELOCITY = 0x75e0,
    CAMERA_SETTINGS = 0x1a0,
    CAMERA_BASIS_X = 0x2b0,
    CAMERA_BASIS_Z = 0x2d0,
    HEADING_UPDATE = 0x19bd250,
    HEADING_ATAN2 = 0x19bd314,
    HEADING_THUNK = 0x19bd241, /* mov r8, rbx; mov rax, imm64; jmp rax */
    CHASE_UPDATE_CALL = 0x19c41a0,
    CHASE_THUNK = 0x19bab21,
};
typedef void (*HeadingUpdate)(void *, float, unsigned char);
static unsigned code_sites(Site *sites, unsigned rva, const unsigned char *code, unsigned length);
static unsigned redirect_calls(Site *sites, unsigned thunk, unsigned target,
                               const unsigned *calls, unsigned call_count, void *callback);

static int drift_camera = 1;
static float drift_sensitivity = 0.7f, drift_speed = 2.2f, drift_range = 1.675f;
/* Percent settings, FH2 chase defaults: sensitivity 35, speed 70, range 80. */
enum { DRIFT_SENSITIVITY, DRIFT_SPEED, DRIFT_RANGE, DRIFT_TUNING_COUNT };
static struct {
    const wchar_t *key;
    int percent;
} drift_tuning[DRIFT_TUNING_COUNT] = {
    {L"DriftSensitivity", 35}, {L"DriftSpeed", 70}, {L"DriftRange", 80}};

/* Livery render options; read at startup, so menu changes apply after a restart. */
static int livery_4k = 1, livery_msaa4 = 1, livery_supersampling;

static void drift_apply_tuning(void) {
    int sensitivity = drift_tuning[DRIFT_SENSITIVITY].percent;
    int speed = drift_tuning[DRIFT_SPEED].percent, range = drift_tuning[DRIFT_RANGE].percent;
    drift_sensitivity = sensitivity / 50.0f;
    drift_speed = speed <= 50 ? 0.25f + speed * 0.015f : 1.0f + (speed - 50) * 0.06f;
    drift_range = range * 2.09439510f / 100.0f;
}
static void *drift_camera_seen;
static float drift_slide;

static float smoothstep(float edge0, float edge1, float value) {
    if (value <= edge0) return 0.0f;
    if (value >= edge1) return 1.0f;
    float t = (value - edge0) / (edge1 - edge0);
    return t * t * (3.0f - 2.0f * t);
}

static float wrap_angle(float angle) {
    if (angle > 3.14159265f) angle -= 6.28318531f;
    else if (angle < -3.14159265f) angle += 6.28318531f;
    return angle;
}

/* Horizontal travel direction in the camera's basis, as (x, z) pointing the
   same way as the -forward vector the native heading uses. Returns the
   slide weight: 0 when gripping, 1 when well into a slide. */
static float drift_weight(void *camera, float body_x, float body_z, float *travel_x,
                          float *travel_z) {
    void *vtable, *car, *car_vtable;
    _Alignas(16) float velocity[4], basis_x[4], basis_z[4];
    if (!drift_camera || !memory_read_pointer(camera, &vtable) ||
        (vtable != image + CHASE_NEAR_VTABLE && vtable != image + CHASE_FAR_VTABLE) ||
        !memory_read_pointer((unsigned char *)camera + 8, &car) || !car ||
        !memory_read_pointer(car, &car_vtable) || car_vtable != image + FORZA_CAR_VTABLE ||
        !memory_read((unsigned char *)car + CAR_VELOCITY, velocity, 16) ||
        !memory_read((unsigned char *)camera + CAMERA_BASIS_X, basis_x, 16) ||
        !memory_read((unsigned char *)camera + CAMERA_BASIS_Z, basis_z, 16))
        return 0.0f;
    for (int i = 0; i < 3; ++i)
        if (!float_is_finite(velocity[i]) || !float_is_finite(basis_x[i]) ||
            !float_is_finite(basis_z[i]))
            return 0.0f;
    float x = -(velocity[0] * basis_x[0] + velocity[1] * basis_x[1] + velocity[2] * basis_x[2]);
    float z = -(velocity[0] * basis_z[0] + velocity[1] * basis_z[1] + velocity[2] * basis_z[2]);
    float speed_squared = x * x + z * z, body = body_x * body_x + body_z * body_z;
    if (!(speed_squared > 9.0f && speed_squared < 1e6f && body > 0.25f && body < 4.0f))
        return 0.0f;
    float speed = square_root(speed_squared), body_length = square_root(body);
    x /= speed;
    z /= speed;
    float bx = body_x / body_length, bz = body_z / body_length;
    float alignment = x * bx + z * bz, cross = x * bz - z * bx;
    if (cross < 0) cross = -cross;
    if (alignment <= -0.5f) return 0.0f; /* reversing */
    /* Slip angle 6..18 degrees (sin), fading in from 3 to 8 m/s. */
    float weight = smoothstep(0.105f, 0.31f, alignment < 0 ? 1.0f : cross) *
                   smoothstep(3.0f, 8.0f, speed);
    if (alignment < 0) weight *= (alignment + 0.5f) * 2.0f;
    *travel_x = x;
    *travel_z = z;
    return weight;
}

static float drift_heading(float body_z, float body_x, void *camera) {
    Atan2 atan2_native = (Atan2)(image + 0x1802d9a);
    float body_heading = atan2_native(body_z, body_x);
    float travel_x, travel_z;
    float weight = drift_weight(camera, body_x, body_z, &travel_x, &travel_z);
    if (!weight || !float_is_finite(body_heading)) return body_heading;
    float length = square_root(body_x * body_x + body_z * body_z);
    float bx = body_x / length, bz = body_z / length;
    float heading = atan2_native(bz + (travel_z - bz) * weight, bx + (travel_x - bx) * weight);
    float offset = wrap_angle(heading - body_heading) * drift_sensitivity;
    if (offset > drift_range) offset = drift_range;
    else if (offset < -drift_range) offset = -drift_range;
    return body_heading + offset;
}

/* Loosen the yaw spring while sliding so the camera can swing out, and ease
   back to the native spring as the slide ends. */
static void drift_chase_update(void *camera, float dt, unsigned char reset) {
    HeadingUpdate native = (HeadingUpdate)(image + HEADING_UPDATE);
    float body[2] = {0.0f, 0.0f}, travel_x, travel_z;
    _Alignas(16) float forward[4];
    void *car, **settings_slot = (void **)((unsigned char *)camera + CAMERA_SETTINGS);
    float slide = 0.0f;
    if (drift_camera && memory_read_pointer((unsigned char *)camera + 8, &car) && car) {
        /* Body heading in world axes is enough to grade the slide here. */
        void *dynamics;
        if (memory_read_pointer((unsigned char *)car + 0x71c8, &dynamics) && dynamics &&
            memory_read((unsigned char *)dynamics - 0x420, forward, 16)) {
            _Alignas(16) float basis_x[4], basis_z[4];
            if (memory_read((unsigned char *)camera + CAMERA_BASIS_X, basis_x, 16) &&
                memory_read((unsigned char *)camera + CAMERA_BASIS_Z, basis_z, 16)) {
                body[0] = -(forward[0] * basis_x[0] + forward[1] * basis_x[1] + forward[2] * basis_x[2]);
                body[1] = -(forward[0] * basis_z[0] + forward[1] * basis_z[1] + forward[2] * basis_z[2]);
                slide = drift_weight(camera, body[0], body[1], &travel_x, &travel_z);
            }
        }
    }
    if (drift_camera_seen != camera || reset) {
        drift_camera_seen = camera;
        drift_slide = slide;
    }
    if (slide < drift_slide && dt > 0.0f && dt < 0.25f) {
        float decay = dt * 4.0f / (1.0f + dt * 4.0f);
        slide = drift_slide + (slide - drift_slide) * decay;
        if (slide < 0.0001f) slide = 0.0f;
    }
    drift_slide = slide;
    float *settings;
    _Alignas(16) float tuned[16];
    if (!slide || !float_is_finite(dt) || !memory_read_pointer(settings_slot, (void **)&settings) ||
        !settings || !memory_read(settings, tuned, sizeof(tuned)) ||
        !float_is_finite(tuned[8]) || !float_is_finite(tuned[9]) || !float_is_finite(tuned[10])) {
        native(camera, dt, reset);
        return;
    }
    float stiffness = 0.55f * drift_speed, damping = 0.5f * square_root(drift_speed);
    tuned[8] *= 1.0f + (stiffness - 1.0f) * slide;
    tuned[9] *= 1.0f + (damping - 1.0f) * slide;
    tuned[10] *= 1.0f - slide;
    *settings_slot = tuned;
    native(camera, dt, reset);
    *settings_slot = settings;
}

static unsigned drift_prepare(Site *sites) {
    static const unsigned char thunk_code[15] = {0x49,0x89,0xd8, 0x48,0xb8,0,0,0,0,0,0,0,0, 0xff,0xe0};
    unsigned char code[15];
    memory_copy(code, thunk_code, sizeof(code));
    uintptr_t callback = (uintptr_t)drift_heading;
    memory_copy(code + 5, &callback, sizeof(callback));
    unsigned count = code_sites(sites, HEADING_THUNK, code, sizeof(code));
    Site call = {HEADING_ATAN2, 5, {0xe8}, {0xe8}, 0};
    int32_t native = (int32_t)(0x1802d9a - HEADING_ATAN2 - 5),
            redirected = (int32_t)(HEADING_THUNK - HEADING_ATAN2 - 5);
    memory_copy(call.original + 1, &native, sizeof(native));
    memory_copy(call.replacement + 1, &redirected, sizeof(redirected));
    sites[count++] = call;
    static const unsigned updates[] = {CHASE_UPDATE_CALL};
    count += redirect_calls(sites + count, CHASE_THUNK, HEADING_UPDATE, updates, 1,
                            drift_chase_update);
    return count;
}

/* In-game settings: Extra Options rows on the native HUD options page
   (ForzaUI.A_HudOptions, data context HudOptionsDataSource).
   - XAML: the page loader 0x21db660 reads the file through 0x21dd6c0 into a
     buffer object at loader+0x38 (data +0x28, size +0x30, made by
     0x208f320); the call is wrapped and the buffer replaced with an extended
     copy before the parser sees it.
   - Bindings: the type properties are linked once by 0x14fad70 through
     0x1fe9210(type 0x4e247a0, &{property, next}). Ours copy a native int
     (CameraAngle, 0x14fbfd0) or float (HUDSafeFrameHorizontal, 0x14fdd00)
     property and replace name, getter and setter. Getters are
     (out, property, object) and box with 0x5592b0 (int) / 0x495110 (float);
     setters are (property, object, boxed) with the value at boxed+0x28. */
typedef struct {
    void **vtable;
    unsigned char header[0x20];
    void *getter, *setter, *unused;
    const char *name;
    void *type, *owner;
    unsigned char flags[8];
} UiProperty;
_Static_assert(sizeof(UiProperty) == 0x60, "native property descriptor");
typedef struct UiLink {
    UiProperty *property;
    struct UiLink *next;
} UiLink;
typedef void **(*UiBox)(void **, const void *);
typedef void (*UiLoad)(void *);
typedef void **(*UiMakeBuffer)(void **, unsigned);
typedef UiProperty *(*UiBuilder)(void);

static void ui_release(unsigned char *object) {
    if (object && --*(volatile int *)(object + 0x10) == 0)
        ((void (*)(void *))(image + 0x31eb20))(object);
}

enum {
    UI_FOV_FIRST,
    UI_FOV_LAST = UI_FOV_FIRST + 4,
    UI_MINIMAP,
    UI_DRIFT,
    UI_DRIFT_FIRST,
    UI_DRIFT_LAST = UI_DRIFT_FIRST + DRIFT_TUNING_COUNT - 1,
    UI_FPS,
    UI_FPS_GPU,
    UI_FPS_MS,
    UI_FPS_BOX,
    UI_LIVERY_4K, /* video options page from here on */
    UI_LIVERY_MSAA,
    UI_LIVERY_SUPERSAMPLING,
    UI_HUD_HIDDEN, /* A_Hud page from here on */
    UI_LETTERBOX,
    UI_PROMPT_BEFORE,
    UI_PROMPT_BUTTON,
    UI_PROMPT_AFTER,
    UI_ANNA_LABEL, /* HUDAnnaControl (through its Page): the tag above ANNA, her ring lit */
    UI_ANNA_VALUE,
    UI_ANNA_READY, /* her button shows the D-pad where the game has her unavailable (races) */
    UI_MAKE_GRID,  /* PopupMenu: the manufacturer picker's grid layout */
    UI_FRAME_STATS, /* every page (base type 0x4e214e0): the FPS readout */
    UI_FPS_BOX_PAGE, /* every page: whether the readout draws its box */
    UI_COUNT
};
static const char *const ui_names[UI_COUNT] = {
    "EOChaseNearFOV", "EOChaseFarFOV", "EOHoodFOV", "EOBumperFOV", "EOCockpitFOV",
    "EOMinimapFollowsCamera", "EODriftCamera", "EODriftSensitivity", "EODriftSpeed",
    "EODriftRange", "EOShowFPS", "EOShowGPUUsage", "EOShowFrameTime", "EOFPSBackground",
    "EOLivery4K", "EOLiveryMSAA", "EOLiverySupersampling",
    "EOHudHidden", "EOLetterbox", "EOPromptBefore", "EOPromptButton", "EOPromptAfter", "EOAnnaLabel", "EOAnnaValue", "EOAnnaReady", "EOMakeGrid", "EOFrameStats", "EOFPSBox"};
static UiProperty ui_properties[UI_COUNT];
static UiLink ui_links[UI_COUNT];
static int ui_registered;

/* The HUD (ForzaUI.A_Hud, its own data context) binds LayoutRoot's
   Visibility to EOHudHidden, its letterbox bars to EOLetterbox and a plugin's
   prompt to EOPromptBefore/Button/After. Their getters record the page so a
   change can raise PropertyChanged the way the native page setters do
   (0x56d4f0). */
static int hud_hidden, hud_letterbox;
static wchar_t prompt_before[96], prompt_after[96], anna_label[48], anna_value[32];
static char prompt_button[16];
static void *hud_page, *hud_page_vtable;
static int anna_ready;

static int ui_index(const UiProperty *property) {
    return property >= ui_properties && property < ui_properties + UI_COUNT
               ? (int)(property - ui_properties)
               : -1;
}

/* Same as the native setters (0x1503130): the object hook, then 0x125ac00,
   which raises the PropertyChanged event that refreshes bindings such as
   the slider value text. */
static void ui_notify(void *object, UiProperty *property) {
    void **vtable;
    if (!object || !memory_read_pointer(object, (void **)&vtable)) return;
    ((void (*)(void *, void *))vtable[0x40 / 8])(object, property);
    ((void (*)(void *, void *))(image + 0x125ac00))(object, property);
}

static void ui_save(const wchar_t *section, const wchar_t *key, int value) {
    wchar_t text[16], digits[12];
    unsigned length = 0, count = 0;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    do {
        digits[count++] = (wchar_t)(L'0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) text[length++] = L'-';
    while (count) text[length++] = digits[--count];
    text[length] = 0;
    WritePrivateProfileStringW(section, key, text, ini_path);
}

static int picker_owns(void *popup);
static void **frame_stats_text(void **output, void *object);
static void *make_ui_string(const wchar_t *text, unsigned length);
static void frame_stats_configure(int which, int on);
static int frame_stats_enabled, frame_stats_gpu, frame_stats_ms, frame_stats_box = 1;

static void **ui_get(void **output, UiProperty *property, void *object) {
    (void)object;
    int index = ui_index(property);
    *output = NULL;
    if (index >= UI_FOV_FIRST && index <= UI_FOV_LAST) {
        unsigned i = (unsigned)(index - UI_FOV_FIRST);
        float value = camera_fovs[i].value;
        if (!value) value = *(volatile float *)(image + camera_fovs[i].rva);
        return ((UiBox)(image + 0x495110))(output, &value);
    }
    if (index == UI_MINIMAP || index == UI_DRIFT ||
        (index >= UI_FPS && index <= UI_FPS_BOX)) {
        int value = (index == UI_MINIMAP   ? minimap_follows_camera
                     : index == UI_DRIFT   ? drift_camera
                     : index == UI_FPS     ? frame_stats_enabled
                     : index == UI_FPS_GPU ? frame_stats_gpu
                     : index == UI_FPS_MS  ? frame_stats_ms
                                           : frame_stats_box) != 0;
        return ((UiBox)(image + 0x5592b0))(output, &value);
    }
    if (index == UI_FRAME_STATS) return frame_stats_text(output, object);
    if (index == UI_LETTERBOX || index == UI_ANNA_READY) {
        unsigned char value = (index == UI_LETTERBOX ? hud_letterbox : anna_ready) != 0;
        if (object && memory_read_pointer(object, &hud_page_vtable)) hud_page = object;
        return ((UiBox)(image + 0x5592d0))(output, &value);
    }
    if (index >= UI_PROMPT_BEFORE && index <= UI_ANNA_VALUE) {
        wchar_t button[16];
        const wchar_t *text = index == UI_PROMPT_BEFORE ? prompt_before
                              : index == UI_PROMPT_AFTER ? prompt_after
                              : index == UI_ANNA_LABEL   ? anna_label
                              : index == UI_ANNA_VALUE   ? anna_value
                                                         : button;
        if (index >= UI_ANNA_LABEL && object && memory_read_pointer(object, &hud_page_vtable)) hud_page = object;
        unsigned length = 0;
        for (; index == UI_PROMPT_BUTTON && prompt_button[length] && length + 1 < 16; ++length)
            button[length] = (wchar_t)prompt_button[length];
        if (index == UI_PROMPT_BUTTON) button[length] = 0;
        length = 0;
        while (text[length]) ++length;
        *output = make_ui_string(text, length);
        return output;
    }
    if (index == UI_FPS_BOX_PAGE) {
        int value = frame_stats_box != 0;
        return ((UiBox)(image + 0x5592b0))(output, &value);
    }
    if (index == UI_MAKE_GRID) {
        unsigned char value = (unsigned char)picker_owns(object);
        return ((UiBox)(image + 0x5592d0))(output, &value);
    }
    if (index == UI_HUD_HIDDEN) {
        unsigned char value = hud_hidden != 0;
        if (object && memory_read_pointer(object, &hud_page_vtable)) hud_page = object;
        return ((UiBox)(image + 0x5592d0))(output, &value);
    }
    if (index >= UI_LIVERY_4K) {
        int value = index == UI_LIVERY_4K     ? livery_4k
                    : index == UI_LIVERY_MSAA ? livery_msaa4
                                              : livery_supersampling;
        value = value != 0;
        return ((UiBox)(image + 0x5592b0))(output, &value);
    }
    if (index >= UI_DRIFT_FIRST && index <= UI_DRIFT_LAST) {
        float value = (float)drift_tuning[index - UI_DRIFT_FIRST].percent;
        return ((UiBox)(image + 0x495110))(output, &value);
    }
    return output;
}

static void ui_set(UiProperty *property, void *object, void *boxed) {
    int index = ui_index(property);
    if (index < 0 || index >= UI_HUD_HIDDEN || !boxed) return;
    if (index >= UI_FOV_FIRST && index <= UI_FOV_LAST) {
        float value;
        if (!memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value)) ||
            !float_is_finite(value) || value < 10.0f || value > 150.0f)
            return;
        unsigned i = (unsigned)(index - UI_FOV_FIRST);
        int degrees = (int)(value + 0.5f);
        camera_fovs[i].value = (float)degrees;
        ui_save(L"Camera", camera_fovs[i].key, degrees);
    } else if (index == UI_MINIMAP) {
        int value;
        if (!memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value))) return;
        minimap_follows_camera = value != 0;
        ui_save(L"HUD", L"MinimapFollowsCamera", minimap_follows_camera);
    } else if (index == UI_DRIFT) {
        int value;
        if (!memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value))) return;
        drift_camera = value != 0;
        ui_save(L"Camera", L"DriftCamera", drift_camera);
    } else if (index >= UI_FPS && index <= UI_FPS_BOX) {
        int value;
        if (!memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value))) return;
        frame_stats_configure(index - UI_FPS, value != 0);
    } else if (index >= UI_DRIFT_FIRST && index <= UI_DRIFT_LAST) {
        float value;
        if (!memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value)) ||
            !float_is_finite(value) || value < 0.0f || value > 100.0f)
            return;
        unsigned i = (unsigned)(index - UI_DRIFT_FIRST);
        drift_tuning[i].percent = (int)(value + 0.5f);
        drift_apply_tuning();
        ui_save(L"Camera", drift_tuning[i].key, drift_tuning[i].percent);
    } else if (index >= UI_LIVERY_4K) {
        int value;
        if (!memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value))) return;
        value = value != 0;
        if (index == UI_LIVERY_4K) {
            livery_4k = value;
            ui_save(L"Livery", L"Resolution4K", value);
        } else if (index == UI_LIVERY_MSAA) {
            livery_msaa4 = value;
            ui_save(L"Livery", L"MSAA", value ? 4 : 2);
        } else {
            livery_supersampling = value;
            ui_save(L"Livery", L"Supersampling", value);
        }
    }
    ui_notify(object, property);
}

/* FPS readout (FPS, GPU usage, frame time), e.g. "72 GPU:71%  13.9 ms".
   The UI updates every layer's current scene once a frame through
   0x1f0e440(ui), from its single call at 0x1f0e3ec; the wrapper times those
   frames and, twice a second, rebuilds the text and raises PropertyChanged
   for EOFrameStats on the pages bound to it. GPU usage is the summed 3D-engine
   utilization ("\GPU Engine(*engtype_3D)\Utilization Percentage"), sampled
   by a background thread through PDH. */
enum { UI_LAYERS_UPDATE = 0x1f0e440, UI_LAYERS_CALL = 0x1f0e3ec, UI_LAYERS_THUNK = 0x1f01cf1 };
static volatile LONG gpu_percent = -1, gpu_sampling;
static wchar_t frame_stats[40];
static unsigned frame_stats_length;
static LARGE_INTEGER stats_frequency, stats_window;
static unsigned stats_frames;

static void *make_ui_string(const wchar_t *text, unsigned length);

/* Bound pages are the getter's callers; each is held with a reference and
   notified on every refresh. A page that hasn't read the text again by the
   next refresh is no longer showing it, so it is dropped. */
typedef struct {
    unsigned char *page;
    int seen;
} StatsSource;
static StatsSource stats_sources[8];

static void **frame_stats_text(void **output, void *object) {
    unsigned char *page = object;
    unsigned free_slot = ARRAYSIZE(stats_sources);
    for (unsigned i = 0; page && i < ARRAYSIZE(stats_sources); ++i) {
        if (stats_sources[i].page == page) {
            stats_sources[i].seen = 1;
            page = NULL;
        } else if (!stats_sources[i].page && free_slot == ARRAYSIZE(stats_sources)) {
            free_slot = i;
        }
    }
    if (page && free_slot < ARRAYSIZE(stats_sources)) {
        ++*(volatile int *)(page + 0x10);
        stats_sources[free_slot] = (StatsSource){page, 1};
    }
    *output = make_ui_string(frame_stats, frame_stats_length);
    return output;
}

static unsigned put_number(wchar_t *at, unsigned value) {
    wchar_t digits[10];
    unsigned count = 0;
    do digits[count++] = (wchar_t)(L'0' + value % 10), value /= 10;
    while (value && count < 10);
    for (unsigned i = 0; i < count; ++i) at[i] = digits[count - 1 - i];
    return count;
}

static unsigned put_text(wchar_t *at, const wchar_t *text) {
    unsigned count = 0;
    while (text[count]) at[count] = text[count], ++count;
    return count;
}

typedef LONG(WINAPI *PdhOpenQuery)(const wchar_t *, DWORD_PTR, HANDLE *);
typedef LONG(WINAPI *PdhAddCounter)(HANDLE, const wchar_t *, DWORD_PTR, HANDLE *);
typedef LONG(WINAPI *PdhCollect)(HANDLE);
typedef LONG(WINAPI *PdhCounterArray)(HANDLE, DWORD, DWORD *, DWORD *, void *);
typedef struct {
    wchar_t *name;
    DWORD status;
    double value;
} PdhItem;

static DWORD WINAPI gpu_sampler(void *unused) {
    (void)unused;
    HMODULE pdh = LoadLibraryExW(L"pdh.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    PdhOpenQuery open = pdh ? (PdhOpenQuery)(void *)GetProcAddress(pdh, "PdhOpenQueryW") : NULL;
    PdhAddCounter add = pdh ? (PdhAddCounter)(void *)GetProcAddress(pdh, "PdhAddEnglishCounterW") : NULL;
    PdhCollect collect = pdh ? (PdhCollect)(void *)GetProcAddress(pdh, "PdhCollectQueryData") : NULL;
    PdhCounterArray values =
        pdh ? (PdhCounterArray)(void *)GetProcAddress(pdh, "PdhGetFormattedCounterArrayW") : NULL;
    HANDLE query = NULL, counter = NULL;
    if (!open || !add || !collect || !values || open(NULL, 0, &query) != 0 ||
        add(query, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &counter) != 0) {
        log_append("FPS: GPU usage counters unavailable.\r\n");
        return 0;
    }
    collect(query);
    HANDLE heap = GetProcessHeap();
    PdhItem *items = NULL;
    DWORD capacity = 0;
    for (;;) {
        Sleep(1000);
        if (collect(query) != 0) continue;
        DWORD size = capacity, count = 0;
        LONG status = values(counter, 0x8200 /* PDH_FMT_DOUBLE | NOCAP100 */, &size, &count, items);
        if (status == (LONG)0x800007D2 /* PDH_MORE_DATA */) {
            if (items) HeapFree(heap, 0, items);
            items = HeapAlloc(heap, 0, size);
            capacity = items ? size : 0;
            size = capacity;
            status = items ? values(counter, 0x8200, &size, &count, items) : -1;
        }
        if (status != 0) continue;
        double total = 0;
        for (DWORD i = 0; i < count; ++i)
            if (items[i].status == 0) total += items[i].value;
        if (total > 100) total = 100;
        InterlockedExchange(&gpu_percent, (LONG)(total + 0.5));
    }
}

static void gpu_sampler_start(void) {
    if (InterlockedExchange(&gpu_sampling, 1)) return;
    HANDLE thread = CreateThread(NULL, 0, gpu_sampler, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

/* Settings > HUD and Gameplay rows: 0 readout, 1 GPU usage, 2 frame time,
   3 background box. */
static void frame_stats_configure(int which, int on) {
    static const wchar_t *const keys[] = {L"ShowFPS", L"ShowGPUUsage", L"ShowFrameTime",
                                          L"ShowFPSBackground"};
    int *flags[] = {&frame_stats_enabled, &frame_stats_gpu, &frame_stats_ms, &frame_stats_box};
    *flags[which] = on;
    ui_save(L"HUD", keys[which], on);
    for (unsigned i = 0; which == 3 && i < ARRAYSIZE(stats_sources); ++i)
        if (stats_sources[i].page)
            ((void (*)(void *, void *))(image + 0x56d4f0))(stats_sources[i].page,
                                                           &ui_properties[UI_FPS_BOX_PAGE]);
    if (frame_stats_enabled && frame_stats_gpu) gpu_sampler_start();
    stats_frames = 0;
    stats_window.QuadPart = 0; /* the text is rebuilt on the next half second */
    if (!frame_stats_enabled) frame_stats_length = 0;
}

static void frame_stats_notify(void) {
    for (unsigned i = 0; i < ARRAYSIZE(stats_sources); ++i) {
        StatsSource *source = &stats_sources[i];
        if (!source->page) continue;
        if (!source->seen) {
            unsigned char *page = source->page;
            source->page = NULL;
            ui_release(page);
            continue;
        }
        source->seen = 0;
        ((void (*)(void *, void *))(image + 0x56d4f0))(source->page, &ui_properties[UI_FRAME_STATS]);
    }
}

static void ui_layers_update(void *ui) {
    ((void (*)(void *))(image + UI_LAYERS_UPDATE))(ui);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!stats_window.QuadPart) {
        stats_window = now;
        return;
    }
    ++stats_frames;
    LONGLONG elapsed = now.QuadPart - stats_window.QuadPart;
    if (elapsed < stats_frequency.QuadPart / 2) return;
    if (!frame_stats_enabled) {
        stats_frames = 0;
        stats_window = now;
        frame_stats_length = 0;
        if (ui_registered > 0) frame_stats_notify();
        return;
    }
    /* fps and tenths of a millisecond per frame, rounded. */
    unsigned fps = (unsigned)((stats_frames * stats_frequency.QuadPart + elapsed / 2) / elapsed);
    unsigned tenths = (unsigned)((elapsed * 10000 + stats_frames * stats_frequency.QuadPart / 2) /
                                 (stats_frames * stats_frequency.QuadPart));
    stats_frames = 0;
    stats_window = now;
    wchar_t *at = frame_stats;
    at += put_number(at, fps);
    LONG gpu = gpu_percent;
    if (frame_stats_gpu && gpu >= 0) {
        at += put_text(at, L" GPU:");
        at += put_number(at, (unsigned)gpu);
        *at++ = L'%';
    }
    if (frame_stats_ms) {
        at += put_text(at, L"  ");
        at += put_number(at, tenths / 10);
        *at++ = L'.';
        *at++ = (wchar_t)(L'0' + tenths % 10);
        at += put_text(at, L" ms");
    }
    if (!(frame_stats_gpu && gpu >= 0) && !frame_stats_ms) at += put_text(at, L" FPS");
    frame_stats_length = (unsigned)(at - frame_stats);
    if (ui_registered > 0) frame_stats_notify();
}

/* Pause menu tabs: offline, the pause menu (0x159bbd0, 0x159d120) disables
   the Social, Clubs, Rivals and Marketplace groups by setting their
   TileMenuNavigationManager.TileState to 1 through 0x16a4cf0(page, part
   name, state); its 21 calls naming those four parts go through our wrapper,
   which keeps them Enabled (0). */
enum { TILE_STATE_SET = 0x16a4cf0, TILE_STATE_THUNK = 0x1590091, TILE_DISABLED = 1 };
static const unsigned online_tab_names[] = {0x2f34b28, 0x2f34af8, 0x2f34b10, 0x2f34ae0};
static const unsigned online_tab_calls[] = {
    0x159bc19, 0x159c455, 0x159c467, 0x159c479, 0x159c4e7, 0x159c510, 0x159c588,
    0x159c85f, 0x159cacd, 0x159cadf, 0x159cb15, 0x159d3f7, 0x159d436, 0x159d460,
    0x159d49f, 0x159d519, 0x159d722, 0x159d767, 0x159d86d, 0x159da2e, 0x159dbd4};

static void tile_state_set(void *page, const char *part, int state) {
    for (unsigned i = 0; state == TILE_DISABLED && i < ARRAYSIZE(online_tab_names); ++i)
        if (part == (const char *)image + online_tab_names[i]) state = 0;
    ((void (*)(void *, const char *, int))(image + TILE_STATE_SET))(page, part, state);
}

/* Jump to Manufacturer (My Cars, the Autoshow and every other car list on
   ForzaUI.Scenes.Anthem.CarSelect): View (or Backspace) opens the native vertical
   popup menu listing the list's manufacturer tabs; picking one selects that
   tab. The current scene is [[UserInterface 0x4faf130]+0x50 Flow]+0xc8; an
   open popup is the scene's +0x218 child (its +0x210 parent). The tabs are
   Groups (object+0xa80), a collection of EnumDataItems: count at
   [coll+0x50]+0x30, items at [[[coll+0x50]+0x28]+0x28], value +0x50, name
   UIString +0x58. CurrentGroupIndex (+0xa88) is set through its reflected
   setter 0x141e0e0(property, object, boxed), which raises PropertyChanged.
   The popup is opened like the native Sort menu: 0x14abc10(page, &title,
   &body, items+0x38), style 0x1598950(popup, 6), and an accept callback
   0x1598860(popup, 2, &function, 1). The callback is an MSVC std::function
   (impl pointer at +0x38) whose call reads the highlighted item through
   0x1607210(popup). Input is read from each car list's per-frame update,
   vtable slot 0x580: the scene base update 0x14a43d0 (two classes go through
   the jump 0x143ebb0). */
enum { UI_ROOT = 0x4faf130, GROUP_SETTER = 0x141e0e0, GROUP_PROPERTY = 0x1424b10,
       ENUM_ITEM_VTABLE = 0x44a7b80, PAD_VIEW = 0x20, SCENE_UPDATE_SLOT = 0x580,
       SCENE_UPDATE = 0x14a43d0, SCENE_UPDATE_JUMP = 0x143ebb0 };
static const unsigned car_select_vtables[] = {
    0x4533578, 0x4533fe0, 0x4534918, 0x4535250, 0x44afa98, 0x44b03d0, 0x44b2890,
    0x44b1f48, 0x4538318, 0x453e868, 0x44b31d8, 0x4535b88, 0x44b3b20, 0x44b4468,
    0x45379d0, 0x44b4db0, 0x44b56e8, 0x44b6030, 0x453c358, 0x453b0e8, 0x453ba20,
    0x453cc90, 0x44b6978, 0x453d5d8, 0x453f1b0, 0x453fae8, 0x453df20, 0x4540430,
    0x45799a8 /* MarketplaceCarSelect: Marketplace > Car Packs > Buy Car */};
static int manufacturer_jump;
static int picker_button_down = 1, picker_opening;
static void *picker_page, *picker_popup, *picker_initial; /* initial: the current tab's item */
/* The tab bar can miss a PropertyChanged raised while the popup closes, so
   the jump is announced again on the next frame. */
static void *jump_object, *jump_vtable;
static int jump_index;

/* The popup binds EOMakeGrid while it is being built inside 0x14abc10. */
static int picker_owns(void *popup) {
    return popup && (picker_opening || popup == picker_popup);
}

static void *current_scene(void) {
    unsigned char *root, *flow;
    void *scene;
    if (!memory_read_pointer(image + UI_ROOT, (void **)&root) || !root ||
        !memory_read_pointer(root + 0x50, (void **)&flow) || !flow ||
        !memory_read_pointer(flow + 0xc8, &scene))
        return NULL;
    return scene;
}

static unsigned char **group_items(void *page, int *count) {
    unsigned char *collection, *list, *inner;
    unsigned char **items;
    *count = 0;
    if (!memory_read_pointer((unsigned char *)page + 0xa80, (void **)&collection) || !collection ||
        !memory_read_pointer(collection + 0x50, (void **)&list) || !list ||
        !memory_read(list + 0x30, count, sizeof(*count)) || *count <= 0 || *count > 4096 ||
        !memory_read_pointer(list + 0x28, (void **)&inner) || !inner ||
        !memory_read_pointer(inner + 0x28, (void **)&items) || !items) {
        *count = 0;
        return NULL;
    }
    return items;
}

/* A fresh UIString holding a copy of text (see anna_make_label). */
static void *make_ui_string(const wchar_t *text, unsigned length) {
    void *output = NULL;
    ((CreateUIString)(image + 0x1214480))(&output);
    unsigned char *label = output;
    if (!label) return NULL;
    wchar_t *copy = ((Allocate)(image + 0x1edbd00))((length + 1) * sizeof(wchar_t));
    if (copy) {
        memory_copy(copy, text, length * sizeof(wchar_t));
        copy[length] = 0;
        *(wchar_t **)(label + 0x28) = copy;
        *(unsigned *)(label + 0x30) = length;
    }
    return label;
}

/* An EnumDataItem built like 0x1111240: allocated with its reflected type,
   base-constructed by 0x2120730, value at +0x50 and its own label at +0x58. */
static void *make_enum_item(int value, const wchar_t *text, unsigned length) {
    void *type = ((void *(*)(void))(image + 0x127ba60))();
    unsigned char *item = ((void *(*)(size_t, void *))(image + 0x1f72ce0))(0x60, type);
    if (!item) return NULL;
    ((void (*)(void *))(image + 0x2120730))(item);
    *(void **)item = image + ENUM_ITEM_VTABLE;
    *(int *)(item + 0x50) = value;
    *(void **)(item + 0x58) = make_ui_string(text, length);
    return item;
}

static void select_group(void *page, int index) {
    void *property = ((void *(*)(void))(image + GROUP_PROPERTY))(), *box = NULL;
    if (!property) return;
    ((void **(*)(void **, const int *))(image + 0x5592b0))(&box, &index);
    if (!box) return;
    ((void (*)(void *, void *, void *))(image + GROUP_SETTER))(property, page, box);
    ui_release(box);
    if (memory_read_pointer(page, &jump_vtable)) {
        jump_index = index;
        jump_object = page;
    }
}

typedef struct PickFunction {
    void **vtable;
    void *page, *popup, *unused;
    unsigned char storage[0x18];
    struct PickFunction *impl;
} PickFunction;
_Static_assert(sizeof(PickFunction) == 0x40, "std::function layout");

static void *pick_copy(PickFunction *self, void *where) {
    if (where) memory_copy(where, self, 0x20);
    return where;
}

static void pick_accept(PickFunction *self) {
    if (!self->page || self->page != picker_page || self->popup != picker_popup) return;
    unsigned char *item = ((void *(*)(void *))(image + 0x1607210))(self->popup);
    void *vtable;
    int index, count;
    if (!item || !memory_read_pointer(item, &vtable) || vtable != image + ENUM_ITEM_VTABLE ||
        !memory_read(item + 0x50, &index, sizeof(index)))
        return;
    group_items(self->page, &count);
    if (index < 0 || index >= count) return;
    select_group(self->page, index);
    log_append("MENUS: jumped to the chosen manufacturer.\r\n");
}

static void *pick_type(PickFunction *self) {
    (void)self;
    return NULL;
}

static void pick_delete(PickFunction *self, unsigned char deallocate) {
    (void)self, (void)deallocate; /* always stored inline */
}

static void *pick_get(PickFunction *self) {
    return &self->page;
}

static void *pick_vtable[] = {(void *)pick_copy, (void *)pick_copy, (void *)pick_accept,
                              (void *)pick_type, (void *)pick_delete, (void *)pick_get};

static void open_manufacturer_picker(void *page) {
    int count, current = 0;
    unsigned char **items = group_items(page, &count);
    if (!items || count < 3) return;
    memory_read((unsigned char *)page + 0xa88, &current, sizeof(current));
    unsigned char *list = NULL;
    ((void **(*)(void **))(image + 0x110d5c0))((void **)&list);
    if (!list) return;
    int added = 0;
    for (int i = 0; i < count; ++i) {
        unsigned char *group, *name;
        const wchar_t *text;
        unsigned length;
        if (!memory_read_pointer(items + i, (void **)&group) || !group ||
            !memory_read_pointer(group + 0x58, (void **)&name) || !name ||
            !memory_read_pointer(name + 0x28, (void **)&text) || !text ||
            !memory_read(name + 0x30, &length, sizeof(length)) || length > 256)
            break;
        void *item = make_enum_item(i, text, length);
        if (!item) break;
        if (i == current) picker_initial = item;
        ((void (*)(void *, int, void *))(*(void ***)list)[0x40 / 8])(list, added++, item);
        ui_release(item);
    }
    if (added != count) {
        picker_initial = NULL;
        ui_release(list);
        return;
    }
    void *title = NULL, *body = NULL;
    picker_opening = 1;
    unsigned char *popup =
        ((void *(*)(void *, void **, void **, void *))(image + 0x14abc10))(page, &title, &body,
                                                                          list + 0x38);
    picker_opening = 0;
    ui_release(list);
    if (!popup) {
        picker_initial = NULL;
        return;
    }
    wchar_t heading[128];
    unsigned length = fh3_text_wide("Jump to Manufacturer", heading, ARRAY_COUNT(heading));
    void *label = make_ui_string(heading, length);
    if (label) {
        ui_release(*(unsigned char **)(popup + 0x470));
        *(void **)(popup + 0x470) = label;
        if (((unsigned char (*)(void *))(image + 0x2117810))(popup))
            ((void (*)(void *, void *))(image + 0x56d4f0))(
                popup, ((void *(*)(void))(image + 0x161f750))());
    }
    ((void (*)(void *, int))(image + 0x1598950))(popup, 6);
    PickFunction function = {pick_vtable, page, popup, NULL, {0}, NULL};
    function.impl = &function;
    ((void (*)(void *, int, PickFunction *, unsigned char))(image + 0x1598860))(popup, 2,
                                                                                &function, 1);
    picker_page = page;
    picker_popup = popup;
    if (ui_registered > 0)
        ((void (*)(void *, void *))(image + 0x56d4f0))(popup, &ui_properties[UI_MAKE_GRID]);
    log_append("MENUS: manufacturer picker opened.\r\n");
}

static int picker_button(void) {
    if (focused_key(VK_BACK)) return 1;
    for (DWORD pad = 0; load_pads() && pad < 4; ++pad) {
        PadState state;
        if (get_pad_state(pad, &state) == ERROR_SUCCESS && (state.buttons & PAD_VIEW)) return 1;
    }
    return 0;
}

static void car_select_update(void *page) {
    ((void (*)(void *))(image + SCENE_UPDATE))(page);
    if (page != current_scene()) return; /* a list under another scene */
    void *object = jump_object, *vtable;
    int index;
    jump_object = NULL;
    if (object == page && memory_read_pointer(object, &vtable) && vtable == jump_vtable &&
        memory_read((unsigned char *)object + 0xa88, &index, sizeof(index)) && index == jump_index) {
        void *property = ((void *(*)(void))(image + GROUP_PROPERTY))();
        if (property) ((void (*)(void *, void *))(image + 0x56d4f0))(object, property);
    }
    void *scene = page, *child = NULL, *list;
    if (!memory_read((unsigned char *)scene + 0x218, &child, sizeof(child))) return;
    int ready = !child;
    if (picker_page && (picker_page != scene || child != picker_popup))
        picker_page = picker_popup = picker_initial = NULL; /* closed or left */
    /* Highlight the current tab once the popup's list (+0x4b0) exists: its
       SelectedItem dependency property is the global at 0x4fdfed8. */
    if (picker_initial && picker_page &&
        memory_read_pointer((unsigned char *)picker_popup + 0x4b0, &list) && list) {
        void *property;
        if (memory_read_pointer(image + 0x4fdfed8, &property) && property)
            ((void (*)(void *, void *, void *))(image + 0x2145760))(list, property, picker_initial);
        picker_initial = NULL;
    }
    int down = picker_button();
    if (ready && down && !picker_button_down) open_manufacturer_picker(scene);
    picker_button_down = down;
}

/* Return to Garage: the pause menu's X (Help) prompt closes the pause menu
   and enters the nearest built festival site's hub (its garage, upgrades,
   paint...), as the game does after joining an online session.
   - The prompt is registered by 0x14a6950(page, 2, &label, &callback) from
     one call (0x1586af7); that call is wrapped to pass our label. The
     callback is a function object whose vtable (0x4599348) only the Help
     prompt uses; its invoke (slot 2, 0x15a9e20) is replaced.
   - The pause menu leaves through its "back" exit (0x1f046d0(page, &id));
     the festival is entered on the next frames (enter_nearest_festival).
   - Modules are looked up by type name, as the state flow's
     ModuleMethodCall does: 0x237ecf0(0x23819d0(), name), then
     0x9f7d40([0x4d85c00], &ref, type). */
enum { PROMPT_ADD = 0x14a6950, HELP_INVOKE = 0x15a9e20, HELP_INVOKE_SLOT = 0x4599348 + 2 * 8 };
typedef struct {
    void *object, *control;
} SharedRef;
static int return_to_garage;
static int garage_step; /* 1: waiting for the pause menu to close */
static ULONGLONG garage_at;

static void shared_release(SharedRef *ref) {
    unsigned char *control = ref->control;
    if (control && InterlockedDecrement((volatile LONG *)(control + 8)) == 0) {
        (*(void (***)(void *))control)[0](control);
        if (InterlockedDecrement((volatile LONG *)(control + 12)) == 0)
            (*(void (***)(void *))control)[1](control);
    }
}

static int find_module(const char *name, SharedRef *out) {
    void *registry = ((void *(*)(void))(image + 0x23819d0))(), *manager;
    void *type = registry ? ((void *(*)(void *, const char *))(image + 0x237ecf0))(registry, name)
                          : NULL;
    *out = (SharedRef){0};
    if (!type || !memory_read_pointer(image + 0x4d85c00, &manager)) return 0;
    ((void *(*)(void *, SharedRef *, void *))(image + 0x9f7d40))(manager, out, type);
    return out->object != NULL;
}

/* Enter the nearest built festival site the way the game does after joining
   online (pause.xml trigger_festival_site_activation): the
   CallComponentMethodOnNearestActiveFestivalEntity behavior (0xcd5d30) takes
   CareerNavigationModule's nearest festival entity 0x563430(module, &entity,
   2), names the component with 0x24336b0 then 0x242b850, and calls
   0x6b19e0(&result, &entity, &component, "StartFlow", &argument, 1) on its
   activate_pause_state_machine: festival_sites.root_from_pause, the festival
   hub (garage, upgrades, paint...) behind a loading screen. */
static int enter_nearest_festival(void) {
    SharedRef career, festival = {0};
    int started = 0;
    if (find_module("CareerNavigationModule", &career)) {
        ((void (*)(void *, SharedRef *, int))(image + 0x563430))(career.object, &festival, 2);
        if (!festival.object)
            ((void (*)(void *, SharedRef *, int))(image + 0x563430))(career.object, &festival, 3);
    }
    shared_release(&career);
    if (festival.object) {
        static const char component_name[] = "activate_pause_state_machine";
        MsvcString name = {{{0}}, sizeof(component_name) - 1, sizeof(component_name) - 1};
        name.pointer = (char *)component_name;
        MsvcString argument = {{{0}}, 0, 15};
        uintptr_t text[8] = {0}, component[8] = {0}, result[4] = {0};
        ((void *(*)(void *, const MsvcString *))(image + 0x24336b0))(text, &name);
        ((void *(*)(void *, void *))(image + 0x242b850))(component, text);
        ((void *(*)(void *, SharedRef *, void *, const char *, MsvcString *, unsigned char))(
            image + 0x6b19e0))(result, &festival, component, "StartFlow", &argument, 1);
        void **returned = (void **)result[1];
        if (returned) ((void (*)(void *))(*(void ***)returned)[3])(returned);
        ((void (*)(void *))(image + 0x243750))(component);
        ((void (*)(void *))(image + 0x243750))(text);
        started = 1;
    }
    shared_release(&festival);
    return started;
}

/* Per frame in the game: once the pause menu has gone, enter the festival. */
static void garage_tick(void) {
    if (!garage_step) return;
    ULONGLONG now = GetTickCount64();
    if (!garage_at) garage_at = now + 300;
    if (now < garage_at) return;
    garage_step = 0;
    log_append(enter_nearest_festival()
                  ? "GARAGE: entering the nearest festival site.\r\n"
                  : "GARAGE: no festival site found; Return to Garage unavailable here.\r\n");
}

static unsigned char return_to_garage_invoke(void *closure) {
    void *page;
    if (!memory_read_pointer((unsigned char *)closure + 8, &page)) return 1;
    garage_step = 1;
    garage_at = 0;
    static const char back[] = "back";
    MsvcString exit = {{{0}}, sizeof(back) - 1, 15};
    memory_copy(exit.text, back, sizeof(back));
    ((void (*)(void *, const MsvcString *))(image + 0x1f046d0))(page, &exit);
    return 1;
}

/* 0x14a6950 consumes the label reference it is given (it keeps its own and
   releases the caller's), so ours is handed over, not released here. The
   native label it replaces is released, as the call would have done. */
static void pause_help_prompt(void *page, int button, void **label, void *callback) {
    wchar_t garage[128];
    unsigned length = fh3_text_wide("Return to Garage", garage, ARRAY_COUNT(garage));
    void *text = make_ui_string(garage, length);
    if (text) {
        ui_release(*label);
        *label = NULL;
    }
    void *holder = text;
    ((void (*)(void *, int, void **, void *))(image + PROMPT_ADD))(page, button,
                                                                 text ? &holder : label, callback);
}

static int ui_register(void) {
    if (ui_registered) return ui_registered > 0;
    ((void (*)(void))(image + 0x14fad70))(); /* native properties first */
    ((void (*)(void))(image + 0x1712210))();
    ((void (*)(void))(image + 0x14fad20))(); /* A_Hud page */
    ((void (*)(void))(image + 0x161d180))(); /* PopupMenu */
    ((void (*)(void))(image + 0x14af4a0))(); /* base page type */
    UiProperty *integer = ((UiBuilder)(image + 0x14fbfd0))();
    UiProperty *real = ((UiBuilder)(image + 0x14fdd00))();
    UiProperty *video = ((UiBuilder)(image + 0x17136f0))(); /* VideoOptions Subtitles */
    UiProperty *hud = ((UiBuilder)(image + 0x14fec60))();   /* A_Hud IsSpeedoVisible */
    UiProperty *text = ((UiBuilder)(image + 0x161f750))();  /* PopupMenu Title (UIString) */
    if (!integer || !real || !video || !hud || !text) {
        ui_registered = -1;
        log_append("SETTINGS: native property templates unavailable; menu unchanged.\r\n");
        return 0;
    }
    for (unsigned i = 0; i < UI_COUNT; ++i) {
        UiProperty *property = &ui_properties[i];
        memory_copy(property,
                    i == UI_FRAME_STATS || (i >= UI_PROMPT_BEFORE && i <= UI_ANNA_VALUE) ? text
                    : i == UI_FPS_BOX_PAGE               ? integer
                    : i >= UI_HUD_HIDDEN                 ? hud
                    : i >= UI_LIVERY_4K                  ? video
                    : i == UI_MINIMAP || i == UI_DRIFT ||
                              (i >= UI_FPS && i <= UI_FPS_BOX)
                          ? integer
                                                       : real,
                    sizeof(*property));
        property->getter = (void *)ui_get;
        property->setter = (void *)ui_set;
        property->unused = NULL;
        property->name = ui_names[i];
        ui_links[i].property = property;
        ui_links[i].next = NULL;
        void *type = i == UI_FRAME_STATS || i == UI_FPS_BOX_PAGE
                         ? ((void *(*)(void *))(image + 0x14b3940))(NULL)
                     : i == UI_MAKE_GRID ? ((void *(*)(void *))(image + 0x1607070))(NULL)
                     : image + (i >= UI_HUD_HIDDEN && i <= UI_ANNA_READY ? 0x4e246c0
                                : i >= UI_LIVERY_4K ? 0x4e558f0
                                                    : 0x4e247a0);
        ((void (*)(void *, UiLink *))(image + 0x1fe9210))(type, &ui_links[i]);
    }
    ui_registered = 1;
    log_append("SETTINGS: Extra Options properties registered on the HUD and video options.\r\n");
    return 1;
}

static const char ui_resources[] =
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_ChaseNearFOV\" Heading=\"Chase Near FOV\" "
    "Description=\"Extra Options: field of view of the near chase camera.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_ChaseFarFOV\" Heading=\"Chase Far FOV\" "
    "Description=\"Extra Options: field of view of the far chase camera.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_HoodFOV\" Heading=\"Hood FOV\" "
    "Description=\"Extra Options: field of view of the hood camera.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_BumperFOV\" Heading=\"Bumper FOV\" "
    "Description=\"Extra Options: field of view of the bumper camera.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_CockpitFOV\" Heading=\"Cockpit FOV\" "
    "Description=\"Extra Options: field of view of the cockpit camera.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_Minimap\" Heading=\"Minimap Follows Camera\" "
    "Description=\"Extra Options: turn the minimap with the right-stick look camera.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_FPS\" Heading=\"Show FPS\" "
    "Description=\"Extra Options: frame rate in the top-right corner of the HUD and menus.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_FPSGPU\" Heading=\"Show GPU Usage\" "
    "Description=\"Extra Options: add the total GPU 3D load to the frame rate readout.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_FPSMS\" Heading=\"Show Frame Time\" "
    "Description=\"Extra Options: add the average frame time in milliseconds to the frame rate readout.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_FPSBox\" Heading=\"Show FPS Background\" "
    "Description=\"Extra Options: draw the frame rate readout on a dark box; off shows the text alone.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_Drift\" Heading=\"Drift Camera\" "
    "Description=\"Extra Options: chase cameras swing toward the direction of travel while the car slides.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_DriftSensitivity\" Heading=\"Drift Camera Sensitivity\" "
    "Description=\"Extra Options: how much of the slide angle the drift camera follows.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_DriftSpeed\" Heading=\"Drift Camera Speed\" "
    "Description=\"Extra Options: how quickly the drift camera swings into and out of a slide.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_DriftRange\" Heading=\"Drift Camera Range\" "
    "Description=\"Extra Options: the largest angle the drift camera swings away from the car.\" />"
    "\r\n<local:NumUnitConverter x:Key=\"EO_Converter_Degrees\" FractionDigits=\"0\" "
    "ShowUnit=\"False\" />"
    "\r\n<Style x:Key=\"EO_Style_FOV\" BasedOn=\"{StaticResource A_Style_Slider_Options}\" "
    "TargetType=\"Slider\"><Setter Property=\"SmallChange\" Value=\"1\" />"
    "<Setter Property=\"LargeChange\" Value=\"0\" /><Setter Property=\"Minimum\" Value=\"30\" />"
    "<Setter Property=\"Maximum\" Value=\"110\" /><Setter Property=\"TickFrequency\" Value=\"1\" />"
    "<Setter Property=\"IsSnapToTickEnabled\" Value=\"True\" /></Style>"
    "\r\n<Style x:Key=\"EO_Style_Percent\" BasedOn=\"{StaticResource A_Style_Slider_Options}\" "
    "TargetType=\"Slider\"><Setter Property=\"SmallChange\" Value=\"1\" />"
    "<Setter Property=\"LargeChange\" Value=\"0\" /><Setter Property=\"Minimum\" Value=\"0\" />"
    "<Setter Property=\"Maximum\" Value=\"100\" /><Setter Property=\"TickFrequency\" Value=\"1\" />"
    "<Setter Property=\"IsSnapToTickEnabled\" Value=\"True\" /></Style>\r\n";

static const char ui_controls[] =
    "<local:SpinnerControl Label=\"Minimap Follows Camera\" Tag=\"{StaticResource EO_Item_Minimap}\" "
    "SelectedIndex=\"{Binding EOMinimapFollowsCamera}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<local:SpinnerControl Label=\"Show FPS\" Tag=\"{StaticResource EO_Item_FPS}\" "
    "SelectedIndex=\"{Binding EOShowFPS}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<local:SpinnerControl Label=\"Show GPU Usage\" Tag=\"{StaticResource EO_Item_FPSGPU}\" "
    "SelectedIndex=\"{Binding EOShowGPUUsage}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<local:SpinnerControl Label=\"Show Frame Time\" Tag=\"{StaticResource EO_Item_FPSMS}\" "
    "SelectedIndex=\"{Binding EOShowFrameTime}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<local:SpinnerControl Label=\"Show FPS Background\" Tag=\"{StaticResource EO_Item_FPSBox}\" "
    "SelectedIndex=\"{Binding EOFPSBackground}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<local:SpinnerControl Label=\"Drift Camera\" Tag=\"{StaticResource EO_Item_Drift}\" "
    "SelectedIndex=\"{Binding EODriftCamera}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<Slider Value=\"{Binding EODriftSensitivity}\" Tag=\"{StaticResource EO_Item_DriftSensitivity}\" "
    "Style=\"{StaticResource EO_Style_Percent}\" local:BindingProxy.B=\"{Binding EODriftSensitivity, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n"
    "<Slider Value=\"{Binding EODriftSpeed}\" Tag=\"{StaticResource EO_Item_DriftSpeed}\" "
    "Style=\"{StaticResource EO_Style_Percent}\" local:BindingProxy.B=\"{Binding EODriftSpeed, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n"
    "<Slider Value=\"{Binding EODriftRange}\" Tag=\"{StaticResource EO_Item_DriftRange}\" "
    "Style=\"{StaticResource EO_Style_Percent}\" local:BindingProxy.B=\"{Binding EODriftRange, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n"
    "<Slider Value=\"{Binding EOChaseNearFOV}\" Tag=\"{StaticResource EO_Item_ChaseNearFOV}\" "
    "Style=\"{StaticResource EO_Style_FOV}\" local:BindingProxy.B=\"{Binding EOChaseNearFOV, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n"
    "<Slider Value=\"{Binding EOChaseFarFOV}\" Tag=\"{StaticResource EO_Item_ChaseFarFOV}\" "
    "Style=\"{StaticResource EO_Style_FOV}\" local:BindingProxy.B=\"{Binding EOChaseFarFOV, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n"
    "<Slider Value=\"{Binding EOHoodFOV}\" Tag=\"{StaticResource EO_Item_HoodFOV}\" "
    "Style=\"{StaticResource EO_Style_FOV}\" local:BindingProxy.B=\"{Binding EOHoodFOV, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n"
    "<Slider Value=\"{Binding EOBumperFOV}\" Tag=\"{StaticResource EO_Item_BumperFOV}\" "
    "Style=\"{StaticResource EO_Style_FOV}\" local:BindingProxy.B=\"{Binding EOBumperFOV, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n"
    "<Slider Value=\"{Binding EOCockpitFOV}\" Tag=\"{StaticResource EO_Item_CockpitFOV}\" "
    "Style=\"{StaticResource EO_Style_FOV}\" local:BindingProxy.B=\"{Binding EOCockpitFOV, Converter={StaticResource EO_Converter_Degrees}}\" />\r\n";

static const char video_resources[] =
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_Livery4K\" "
    "Description=\"Extra Options: render car liveries at 4096x4096 instead of 1024 or 2048. Applies after restarting the game.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_LiveryMSAA\" "
    "Description=\"Extra Options: smooth livery and vinyl edges with 4x MSAA instead of 2x. Applies after restarting the game.\" />"
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_LiverySupersampling\" "
    "Description=\"Extra Options: render liveries at twice the resolution before downsampling (heavier). Applies after restarting the game.\" />\r\n";

static const char video_controls[] =
    "<local:SpinnerControl Tag=\"{StaticResource EO_Item_Livery4K}\" Label=\"4K Liveries\" "
    "SelectedIndex=\"{Binding EOLivery4K}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<local:SpinnerControl Tag=\"{StaticResource EO_Item_LiveryMSAA}\" Label=\"Livery 4x MSAA\" "
    "SelectedIndex=\"{Binding EOLiveryMSAA}\" ItemsSource=\"{Binding OnOffOptions}\" "
    "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n"
    "<local:SpinnerControl Tag=\"{StaticResource EO_Item_LiverySupersampling}\" "
    "Label=\"Livery Supersampling\" SelectedIndex=\"{Binding EOLiverySupersampling}\" "
    "ItemsSource=\"{Binding OnOffOptions}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" />\r\n";

static const char video_resource_anchor[] =
    "<local:TileMenuDataItem x:Key=\"A_DataItem_Subtitles\" "
    "Description=\"{local:Loc MyProfile.IDS_SubtitlesDescription}\" />";
static const char video_control_anchor[] = "</local:CommonListBox>";

/* The FPS readout sits outside LayoutRoot, so it stays when the HUD hides;
   menus show it above the right end of the player bar (Shared_Bars). */
#define FPS_BOX_STYLE(source)                                                              \
    "<Border.Style><Style TargetType=\"Border\"><Setter Property=\"Background\" "              \
    "Value=\"#66000000\"/><Style.Triggers><DataTrigger Binding=\"{Binding EOFPSBox" source \
    "}\" Value=\"0\"><Setter Property=\"Background\" Value=\"Transparent\"/></DataTrigger>"    \
    "</Style.Triggers></Style></Border.Style>"
/* Letterbox and prompt, before LayoutRoot so they stay while the HUD is hidden: two black bars slide
   in from above and below (EOLetterbox), and a plugin's prompt sits at the bottom centre (EOPromptButton
   set: text, the button's picture, text). */
#define HUD_LETTERBOX_AND_PROMPT \
    "<Grid IsHitTestVisible=\"False\"><Grid.Resources><CircleEase x:Key=\"EO_LetterboxEase\" EasingMode=\"EaseOut\"/></Grid.Resources>\r\n" \
    "<Rectangle Fill=\"Black\" Height=\"330\" VerticalAlignment=\"Top\" Margin=\"-200,-200,-200,0\" IsHitTestVisible=\"False\">" \
    "<Rectangle.RenderTransform><TranslateTransform Y=\"-330\"/></Rectangle.RenderTransform>" \
    "<Rectangle.Style><Style TargetType=\"{x:Type Rectangle}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOLetterbox}\" Value=\"True\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/><DataTrigger.EnterActions><BeginStoryboard><Storyboard>" \
    "<DoubleAnimationUsingKeyFrames Storyboard.TargetProperty=\"(UIElement.RenderTransform).(TranslateTransform.Y)\">" \
    "<DiscreteDoubleKeyFrame KeyTime=\"0:0:0\" Value=\"-330\"/>" \
    "<EasingDoubleKeyFrame KeyTime=\"0:0:0.6\" Value=\"0\" EasingFunction=\"{StaticResource EO_LetterboxEase}\"/>" \
    "</DoubleAnimationUsingKeyFrames></Storyboard></BeginStoryboard></DataTrigger.EnterActions>" \
    "</DataTrigger></Style.Triggers></Style></Rectangle.Style></Rectangle>\r\n" \
    "<Rectangle Fill=\"Black\" Height=\"330\" VerticalAlignment=\"Bottom\" Margin=\"-200,0,-200,-200\" IsHitTestVisible=\"False\">" \
    "<Rectangle.RenderTransform><TranslateTransform Y=\"330\"/></Rectangle.RenderTransform>" \
    "<Rectangle.Style><Style TargetType=\"{x:Type Rectangle}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOLetterbox}\" Value=\"True\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/><DataTrigger.EnterActions><BeginStoryboard><Storyboard>" \
    "<DoubleAnimationUsingKeyFrames Storyboard.TargetProperty=\"(UIElement.RenderTransform).(TranslateTransform.Y)\">" \
    "<DiscreteDoubleKeyFrame KeyTime=\"0:0:0\" Value=\"330\"/>" \
    "<EasingDoubleKeyFrame KeyTime=\"0:0:0.6\" Value=\"0\" EasingFunction=\"{StaticResource EO_LetterboxEase}\"/>" \
    "</DoubleAnimationUsingKeyFrames></Storyboard></BeginStoryboard></DataTrigger.EnterActions>" \
    "</DataTrigger></Style.Triggers></Style></Rectangle.Style></Rectangle>\r\n" \
    "</Grid>\r\n" \
    "<Border HorizontalAlignment=\"Center\" VerticalAlignment=\"Bottom\" Margin=\"0,0,0,60\" Padding=\"14,4,14,4\" " \
    "CornerRadius=\"3\" Background=\"#B0000000\" IsHitTestVisible=\"False\" " \
    "Visibility=\"{Binding EOPromptButton, Converter={StaticResource A_StringVisConverter}}\">" \
    "<StackPanel Orientation=\"Horizontal\">" \
    "<local:FZTextBlock Text=\"{Binding EOPromptBefore}\" Style=\"{StaticResource A_Style_FZTextBlock_PlayerBarBold}\" " \
    "FontSize=\"26\" VerticalAlignment=\"Center\"/>" \
    "<Grid Margin=\"8,0,8,0\" VerticalAlignment=\"Center\">" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_LB}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"LB\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_RB}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"RB\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_LT}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"LT\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_RT}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"RT\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_A}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"A\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_B}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"B\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_X}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"X\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_Y}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"Y\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_View}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"View\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_Menu}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"Menu\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_L3}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"L3\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_R3}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"R3\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_DPadUp}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"DPadUp\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_DPadDown}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"DPadDown\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_DPadLeft}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"DPadLeft\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_DPadRight}\" Width=\"40\" Height=\"40\">" \
    "<local:FZMaterial.Style><Style TargetType=\"{x:Type local:FZMaterial}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/>" \
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOPromptButton}\" Value=\"DPadRight\">" \
    "<Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style>" \
    "</local:FZMaterial.Style></local:FZMaterial>" \
    "</Grid>" \
    "<local:FZTextBlock Text=\"{Binding EOPromptAfter}\" Style=\"{StaticResource A_Style_FZTextBlock_PlayerBarBold}\" " \
    "FontSize=\"26\" VerticalAlignment=\"Center\"/>" \
    "</StackPanel></Border>\r\n"

static const char hud_controls[] =
    HUD_LETTERBOX_AND_PROMPT
    "<Border CornerRadius=\"3\" Padding=\"8,1,8,1\" "
    "HorizontalAlignment=\"Right\" VerticalAlignment=\"Top\" Margin=\"0,6,8,0\" "
    "IsHitTestVisible=\"False\" Visibility=\"{Binding EOFrameStats, "
    "Converter={StaticResource A_StringVisConverter}}\">"
    FPS_BOX_STYLE("")
    "<local:FZTextBlock Text=\"{Binding EOFrameStats}\" "
    "Style=\"{StaticResource A_Style_FZTextBlock_PlayerBarBold}\" FontSize=\"20\"/></Border>\r\n"
    "<Grid x:Name=\"LayoutRoot\" Visibility=\"{Binding EOHudHidden, "
    "Converter={StaticResource A_FZBooleanToVisibilityConverter_Inverse}}\" ";
static const char hud_resource_anchor[] = "x:Class=\"ForzaUI.A_Hud\"";
static const char hud_control_anchor[] = "<Grid x:Name=\"LayoutRoot\"";
static const char bar_controls[] =
    "<Canvas DockPanel.Dock=\"Right\" Width=\"0\" Height=\"0\" VerticalAlignment=\"Top\" "
    "IsHitTestVisible=\"False\"><Border Canvas.Right=\"-70\" Canvas.Top=\"-34\" "
    "CornerRadius=\"3\" Padding=\"8,1,8,1\" "
    "Visibility=\"{Binding EOFrameStats, RelativeSource={RelativeSource FindAncestor, "
    "AncestorType=Page}, Converter={StaticResource A_StringVisConverter}}\">"
    FPS_BOX_STYLE(", RelativeSource={RelativeSource FindAncestor, AncestorType=Page}")
    "<local:FZTextBlock Text=\"{Binding EOFrameStats, RelativeSource={RelativeSource "
    "FindAncestor, AncestorType=Page}}\" Style=\"{StaticResource "
    "A_Style_FZTextBlock_PlayerBarBold}\" FontSize=\"20\"/></Border></Canvas>\r\n";

static const char ui_resource_anchor[] =
    "<System:Double x:Key=\"A_Double_HUDSafeFrameHorizontalMax\">0.69</System:Double>";
static const char ui_control_anchor[] = "<Slider x:Name=\"HUDSafeFrameHorizontalSpinner\"";

/* The shared message box (PopupVerticalMenu) keeps its native text menu
   unless EOMakeGrid is true, which only the manufacturer picker sets: then the
   list becomes a scrolling four-column grid of name tiles. */
static const char make_resources[] =
    "\r\n<Style x:Key=\"EO_MakeTile\" TargetType=\"{x:Type ListBoxItem}\" "
    "BasedOn=\"{StaticResource A_Style_ListBoxItem_TextMenu}\">"
    "<Setter Property=\"Width\" Value=\"348\"/><Setter Property=\"Height\" Value=\"50\"/>"
    "<Setter Property=\"Margin\" Value=\"2\"/>"
    "<Setter Property=\"FocusVisualStyle\" Value=\"{x:Null}\"/>"
    "<Setter Property=\"local:FocusOnMouseOverBehaviour.Self\" Value=\"True\"/>"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"ListBoxItem\">"
    "<Border x:Name=\"Tile\" Background=\"#FFF5F5F5\" BorderBrush=\"Transparent\" "
    "BorderThickness=\"3\">"
    "<local:FZTextBlock x:Name=\"Label\" Text=\"{Binding Text, Converter={StaticResource "
    "A_StringToUpper}}\" Style=\"{StaticResource A_Style_FZTextBlock_OptionText}\" "
    "HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\"/>"
    "</Border><ControlTemplate.Triggers><Trigger Property=\"IsSelected\" Value=\"True\">"
    "<Setter TargetName=\"Tile\" Property=\"Background\" Value=\"#FF141414\"/>"
    "<Setter TargetName=\"Tile\" Property=\"BorderBrush\" Value=\"#FFFF4C14\"/>"
    "<Setter TargetName=\"Label\" Property=\"Color\" Value=\"#FFFFFFFF\"/>"
    "</Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>"
    "<Style x:Key=\"EO_MakeMenu\" TargetType=\"{x:Type local:CommonListBox}\" "
    "BasedOn=\"{StaticResource A_Style_ListBox_TextMenu_Default}\">"
    "<Setter Property=\"Width\" Value=\"670\"/>"
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOMakeGrid}\" Value=\"True\">"
    "<Setter Property=\"Width\" Value=\"1440\"/><Setter Property=\"MaxHeight\" Value=\"648\"/>"
    "<Setter Property=\"ItemContainerStyle\" Value=\"{StaticResource EO_MakeTile}\"/>"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"{x:Type ListBox}\">"
    "<local:FZScrollViewer Style=\"{StaticResource A_Style_ScrollViewer_TextMenu}\" "
    "Template=\"{StaticResource A_ControlTemplate_ScrollViewer_TextMenu}\" "
    "CanContentScroll=\"False\" HorizontalScrollBarVisibility=\"Disabled\" "
    "VerticalScrollBarVisibility=\"Auto\" Focusable=\"False\" KeepSelectionOnScreen=\"True\">"
    "<WrapPanel Orientation=\"Horizontal\" Width=\"1408\" HorizontalAlignment=\"Center\" "
    "IsItemsHost=\"True\" Focusable=\"False\"/>"
    "</local:FZScrollViewer></ControlTemplate></Setter.Value></Setter>"
    "</DataTrigger></Style.Triggers></Style>\r\n";
/* Car lists show the View prompt at the end of their footer. */
static const char make_prompt[] =
    "<local:ControllerButtonControl Button=\"Back\" Description=\"Manufacturer\"/>\r\n";
static const char make_menu[] =
    "<local:CommonListBox x:Name=\"Menu\" Style=\"{StaticResource EO_MakeMenu}\" "
    "ItemsSource=\"{Binding Collection}\"/>";

/* ANNA's HUD control (minimap, ring, list), its status (the button below the minimap) replaced:
 * - while a plugin sets a status (EOAnnaLabel) her ring glows as when she listens (her ShowFull glow,
 *   held) and a tag sits above her button: the label, then the value in her colour;
 * - while a plugin has her ready (EOAnnaReady: races, where the game has her unavailable) the button
 *   shows D-pad Down and ANNA instead of her crossed microphone.
 * Bound through the HUD page, her DataContext being hers. */
static const char anna_controls[] =
    "<Decorator IsHitTestVisible=\"False\"><Decorator.Style><Style TargetType=\"{x:Type Decorator}\">"
    "<Setter Property=\"Visibility\" Value=\"Collapsed\"/><Style.Triggers><MultiDataTrigger><MultiDataTrigger.Conditions>"
    "<Condition Binding=\"{Binding AnnaDisplayMode}\" Value=\"3\"/><Condition Binding=\"{Binding EOAnnaLabel, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}, Converter={StaticResource A_StringVisConverter}}\" Value=\"{x:Static Visibility.Visible}\"/>"
    "</MultiDataTrigger.Conditions><Setter Property=\"Visibility\" Value=\"Visible\"/></MultiDataTrigger>"
    "</Style.Triggers></Style></Decorator.Style>"
    "<local:FZMaterial Preset=\"{StaticResource A_FZMaterialPresetAdditive}\" Width=\"310\" Height=\"310\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Bottom\" Margin=\"-8,0,0,-15\"><local:FZMaterial.Effect>"
    "<local:TextureMaskEffect Texture=\"{StaticResource A_Brush_Outline}\" Mask=\"{StaticResource A_Brush_OutlineMaskGradient}\" Color=\"{StaticResource A_Color_Minimap_EdgeActive}\" FadeInStart=\"0\" FadeInEnd=\"0\" FadeOutStart=\"0.65\" FadeOutEnd=\"0.8\"/>"
    "</local:FZMaterial.Effect></local:FZMaterial></Decorator>\r\n"
    "<Decorator IsHitTestVisible=\"False\"><Decorator.Style><Style TargetType=\"{x:Type Decorator}\">"
    "<Setter Property=\"Visibility\" Value=\"Collapsed\"/><Style.Triggers><MultiDataTrigger><MultiDataTrigger.Conditions>"
    "<Condition Binding=\"{Binding AnnaDisplayMode}\" Value=\"1\"/><Condition Binding=\"{Binding EOAnnaLabel, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}, Converter={StaticResource A_StringVisConverter}}\" Value=\"{x:Static Visibility.Visible}\"/>"
    "</MultiDataTrigger.Conditions><Setter Property=\"Visibility\" Value=\"Visible\"/></MultiDataTrigger>"
    "</Style.Triggers></Style></Decorator.Style>"
    "<local:FZMaterial Preset=\"{StaticResource A_FZMaterialPresetAdditive}\" Width=\"179\" Height=\"54\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Bottom\" Margin=\"57,0,0,-10\"><local:FZMaterial.Effect>"
    "<local:TextureMaskEffect Texture=\"{StaticResource A_Brush_Outline_ANNAOnly}\" Mask=\"{StaticResource A_Brush_OutlineMaskGradient}\" Color=\"{StaticResource A_Color_Minimap_EdgeActive}\" FadeInStart=\"0\" FadeInEnd=\"0\" FadeOutStart=\"1\" FadeOutEnd=\"1\"/>"
    "</local:FZMaterial.Effect></local:FZMaterial></Decorator>\r\n"
    "<Grid Width=\"156\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Bottom\" Margin=\"67,0,0,44\" IsHitTestVisible=\"False\" Visibility=\"{Binding EOAnnaLabel, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}, Converter={StaticResource A_StringVisConverter}}\">"
    "<Border Background=\"#33000000\" CornerRadius=\"9\" Padding=\"16,7,16,7\" HorizontalAlignment=\"Center\">"
    "<StackPanel Orientation=\"Horizontal\">"
    "<local:FZTextBlock Text=\"{Binding EOAnnaLabel, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}}\" Style=\"{StaticResource A_Style_FZTextBlock_HudMedium_Tight}\" FontSize=\"22\" Margin=\"0,0,6,0\" VerticalAlignment=\"Center\"/>"
    "<local:FZTextBlock Text=\"{Binding EOAnnaValue, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}}\" Style=\"{StaticResource A_Style_FZTextBlock_HudMedium_Tight}\" FontSize=\"22\" Color=\"{StaticResource A_Color_Minimap_EdgeActive}\" VerticalAlignment=\"Center\"/>"
    "</StackPanel></Border></Grid>\r\n"
    "<Decorator x:Name=\"HUDAnnaControl_ANNAStatus\" Style=\"{StaticResource A_Style_FrameworkElement_HideIfMapOnly}\">"
    "<Border BorderBrush=\"{StaticResource A_Brush_ANNAButtonBorder}\" CornerRadius=\"9\" BorderThickness=\"3\" HorizontalAlignment=\"Left\" VerticalAlignment=\"Bottom\" Margin=\"67,0,0,0\" Width=\"156\">"
    "<Viewbox Width=\"142\" Height=\"30\" Stretch=\"Uniform\" StretchDirection=\"DownOnly\">"
    "<Grid HorizontalAlignment=\"Center\" Margin=\"-3,0,0,0\">"
    "<StackPanel Orientation=\"Horizontal\" Visibility=\"{Binding IsVoiceAvailable, Converter={StaticResource A_FZBooleanToVisibilityConverter}}\">"
    "<local:FZMaterial x:Name=\"ANNAMic\" Width=\"30\" Height=\"30\" Margin=\"0,-4,4,-3\" ImageSource=\"/ForzaUI;component/Textures/Anthem/HUD/ANNA_Mic_Icon_Available.png\" Color=\"White\" VerticalAlignment=\"Top\"/>"
    "<local:FZTextBlock local:TextBlockInlineMarkup.Markup=\"{local:Loc Anna.IDS_Label_Available}\" local:TextBlockInlineMarkup.ResourceBaseName=\"A_Style_ANNAStatusMarkup_\" Style=\"{StaticResource A_Style_FZTextBlock_HudMedium_Tight}\" VerticalAlignment=\"Center\"/>"
    "</StackPanel>"
    "<StackPanel Orientation=\"Horizontal\"><StackPanel.Style><Style TargetType=\"{x:Type FrameworkElement}\" BasedOn=\"{StaticResource A_Style_FrameworkElement_ANNAAvailable_DPad}\"><Style.Triggers><DataTrigger Binding=\"{Binding EOAnnaReady, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}}\" Value=\"True\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/></DataTrigger></Style.Triggers></Style></StackPanel.Style>"
    "<Viewbox Height=\"25\" MinWidth=\"25\" Margin=\"0,0,4,0\">"
    "<local:ControllerButtonControl CommandOverride=\"INPUTCMD_ANNA_ACTIVATE\" ContextOverride=\"INPUTCONTEXT_RACING\" SupportsControllerInput=\"True\" Visibility=\"Visible\" Margin=\"0\" UseAlternateVisualStyle=\"True\"/>"
    "</Viewbox><local:FZTextBlock local:TextBlockInlineMarkup.Markup=\"{local:Loc Anna.IDS_Label_ANNA}\" local:TextBlockInlineMarkup.ResourceBaseName=\"A_Style_ANNAStatusMarkup_\" Style=\"{StaticResource A_Style_FZTextBlock_HudMedium_Tight}\" VerticalAlignment=\"Center\"/></StackPanel>"
    "<StackPanel Orientation=\"Horizontal\"><StackPanel.Style><Style TargetType=\"{x:Type FrameworkElement}\" BasedOn=\"{StaticResource A_Style_FrameworkElement_ANNAUnavailable}\"><Style.Triggers><DataTrigger Binding=\"{Binding EOAnnaReady, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}}\" Value=\"True\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/></DataTrigger></Style.Triggers></Style></StackPanel.Style>"
    "<local:FZMaterial Width=\"30\" Height=\"30\" Margin=\"0,-4,4,-3\" ImageSource=\"/ForzaUI;component/Textures/Anthem/HUD/ANNA_Mic_Icon_Unavailable.png\" Color=\"White\" VerticalAlignment=\"Top\" Opacity=\"{StaticResource A_Double_ANNAStatusFadedOpacity}\"/>"
    "<local:FZTextBlock local:TextBlockInlineMarkup.Markup=\"{local:Loc Anna.IDS_Label_Unavailable}\" local:TextBlockInlineMarkup.ResourceBaseName=\"A_Style_ANNAStatusMarkup_\" Style=\"{StaticResource A_Style_FZTextBlock_HudMedium_Tight}\" VerticalAlignment=\"Center\"/>"
    "</StackPanel>"
    "<StackPanel Orientation=\"Horizontal\"><StackPanel.Style><Style TargetType=\"{x:Type FrameworkElement}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/><Style.Triggers><DataTrigger Binding=\"{Binding EOAnnaReady, RelativeSource={RelativeSource FindAncestor, AncestorType=Page}}\" Value=\"True\"><Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style></StackPanel.Style>"
    "<local:FZMaterial ImageSource=\"{StaticResource A_BitmapImage_ControllerButton_DPadDown}\" Width=\"25\" Height=\"25\" Margin=\"0,0,4,0\" Color=\"White\" VerticalAlignment=\"Center\"/><local:FZTextBlock local:TextBlockInlineMarkup.Markup=\"{local:Loc Anna.IDS_Label_ANNA}\" local:TextBlockInlineMarkup.ResourceBaseName=\"A_Style_ANNAStatusMarkup_\" Style=\"{StaticResource A_Style_FZTextBlock_HudMedium_Tight}\" VerticalAlignment=\"Center\"/></StackPanel>"
    "</Grid></Viewbox></Border></Decorator>\r\n";

/* Copy the page with the two insertions; 0 if an anchor is missing or repeated. */
/* Resources go right after the resource anchor, controls right before the
   control anchor; both anchors must appear exactly once. */
typedef struct {
    const char *page, *name;
    const char *resources, *controls, *resource_anchor, *control_anchor;
    const char *control_end; /* if set, the controls replace anchor..end */
} UiPage;
static const UiPage ui_pages[] = {
    {"x:Class=\"ForzaUI.A_HudOptions\"", "HUD options page", ui_resources, ui_controls, ui_resource_anchor,
     ui_control_anchor, NULL},
    {"x:Class=\"ForzaUI.A_VideoOptions\"", "video options page", video_resources, video_controls,
     video_resource_anchor, video_control_anchor, NULL},
    {"x:Class=\"ForzaUI.A_Hud\"", "HUD scene", "", hud_controls, hud_resource_anchor,
     hud_control_anchor, "<Grid x:Name=\"LayoutRoot\""},
    {"x:Class=\"ForzaUI.Scenes.Anthem.A_MessageBox\"", "popup menu", make_resources, make_menu,
     "<ResourceDictionary>", "<local:CommonListBox x:Name=\"Menu\"", "/>"},
    {"x:Class=\"ForzaUI.Scenes.Anthem.CarSelect\"", "car list footer", "", make_prompt,
     "x:Class=\"ForzaUI.Scenes.Anthem.CarSelect\"", "</local:ControllerButtonPanel>", NULL},
    {"x:Key=\"A_ControlTemplate_PlayerBar\"", "player bar", "", bar_controls,
     "x:Key=\"A_ControlTemplate_PlayerBar\"",
     "<StackPanel Orientation=\"Horizontal\" DockPanel.Dock=\"Right\"", NULL},
    {"x:Class=\"ForzaUI.HUDAnnaControl\"", "ANNA control", "", anna_controls,
     "x:Class=\"ForzaUI.HUDAnnaControl\"", "<Decorator x:Name=\"HUDAnnaControl_ANNAStatus\"", "</Decorator>"}};

static void hud_changed(int property) {
    void *vtable;
    if (hud_page && memory_read_pointer(hud_page, &vtable) && vtable == hud_page_vtable)
        ((void (*)(void *, void *))(image + 0x56d4f0))(hud_page, &ui_properties[property]);
}

/* 0: the HUD as usual; 1: hidden; 2: hidden, with the letterbox's black bars. */
static void hud_set_hidden(int mode) {
    int hidden = mode != 0, letterbox = mode == 2;
    if (hud_hidden != hidden) {
        hud_hidden = hidden;
        hud_changed(UI_HUD_HIDDEN);
    }
    if (hud_letterbox != letterbox) {
        hud_letterbox = letterbox;
        hud_changed(UI_LETTERBOX);
    }
}

static void copy_wide(wchar_t *out, unsigned capacity, const wchar_t *text) {
    unsigned i = 0;
    for (; text && text[i] && i + 1 < capacity; ++i) out[i] = text[i];
    out[i] = 0;
}

/* A prompt at the bottom of the HUD: text, a controller button's picture, text. No button: none. */
static void hud_set_prompt(const wchar_t *before, const char *button, const wchar_t *after) {
    copy_wide(prompt_before, ARRAY_COUNT(prompt_before), before);
    copy_wide(prompt_after, ARRAY_COUNT(prompt_after), after);
    unsigned i = 0;
    for (; button && button[i] && i + 1 < sizeof(prompt_button); ++i) prompt_button[i] = button[i];
    prompt_button[i] = 0;
    hud_changed(UI_PROMPT_BEFORE);
    hud_changed(UI_PROMPT_BUTTON);
    hud_changed(UI_PROMPT_AFTER);
}

/* ANNA's status: her ring lit and "label value" above her button; NULL label: none. */
static void hud_set_anna_status(const wchar_t *label, const wchar_t *value) {
    copy_wide(anna_label, ARRAY_COUNT(anna_label), label);
    copy_wide(anna_value, ARRAY_COUNT(anna_value), label ? value : NULL);
    hud_changed(UI_ANNA_LABEL);
    hud_changed(UI_ANNA_VALUE);
}

/* ANNA answers D-pad Down where the game has her unavailable (a plugin's race menu). */
static void hud_set_anna_ready(int ready) {
    if (anna_ready == (ready != 0)) return;
    anna_ready = ready != 0;
    hud_changed(UI_ANNA_READY);
}

static unsigned ui_extend(const UiPage *page, const char *text, unsigned length, char *out,
                          unsigned capacity) {
    const char *resource = text_find_unique(text, length, page->resource_anchor);
    const char *control = text_find_unique(text, length, page->control_anchor);
    if (!resource || !control || control < resource) return 0;
    unsigned resources = (unsigned)text_length(page->resources);
    unsigned controls = (unsigned)text_length(page->controls);
    unsigned at_resource = (unsigned)(resource - text) + (unsigned)text_length(page->resource_anchor);
    unsigned at_control = (unsigned)(control - text), after_control = at_control;
    if (page->control_end) {
        const char *end = text_find(control, length - at_control, page->control_end);
        if (!end) return 0;
        after_control = (unsigned)(end - text) + (unsigned)text_length(page->control_end);
    }
    if (length + resources + controls > capacity) return 0;
    unsigned used = 0;
    memory_copy(out, text, at_resource);
    used += at_resource;
    memory_copy(out + used, page->resources, resources);
    used += resources;
    memory_copy(out + used, text + at_resource, at_control - at_resource);
    used += at_control - at_resource;
    memory_copy(out + used, page->controls, controls);
    used += controls;
    memory_copy(out + used, text + after_control, length - after_control);
    used += length - after_control;
    return used;
}


static void ui_load_resource(void *loader) {
    ((UiLoad)(image + 0x21dd6c0))(loader);
    unsigned char *buffer;
    const char *bytes;
    int size;
    if (!memory_read_pointer((unsigned char *)loader + 0x38, (void **)&buffer) ||
        !memory_read_pointer(buffer + 0x28, (void **)&bytes) ||
        !memory_read(buffer + 0x30, &size, sizeof(size)) || size <= 0 || size > 262144)
        return;
    const UiPage *page = NULL;
    /* The settings pages need InGameMenu; the HUD scene only the cinematic
       camera; the message box and car list footer only Jump to Manufacturer;
       the player bar always carries the (possibly empty) FPS readout. */
    for (unsigned i = in_game_menu ? 0 : 2; i < ARRAY_COUNT(ui_pages) && !page; ++i)
        if ((i < 3 || i >= 5 || manufacturer_jump) &&
            text_find(bytes, (size_t)size, ui_pages[i].page))
            page = &ui_pages[i];
    if (!page || !ui_register()) return;
    int language = fh3_text_language();
    UiPage localized = *page;
    char *resources = fh3_text_xaml(page->resources, language);
    char *controls = fh3_text_xaml(page->controls, language);
    if (resources) localized.resources = resources;
    if (controls) localized.controls = controls;
    unsigned capacity =
        (unsigned)(size + text_length(localized.resources) + text_length(localized.controls) + 1);
    void *replacement = NULL;
    ((UiMakeBuffer)(image + 0x208f320))(&replacement, capacity);
    char *data;
    unsigned length = replacement && memory_read_pointer((unsigned char *)replacement + 0x28, (void **)&data)
        ? ui_extend(&localized, bytes, (unsigned)size, data, capacity) : 0;
    if (resources) HeapFree(GetProcessHeap(), 0, resources);
    if (controls) HeapFree(GetProcessHeap(), 0, controls);
    if (!replacement) return;
    if (!length) {
        ui_release(replacement);
        log_append("SETTINGS: ");
        log_append(page->name);
        log_append(" layout did not match; original page kept.\r\n");
        return;
    }
    *(int *)((unsigned char *)replacement + 0x30) = (int)length;
    *(void **)((unsigned char *)loader + 0x38) = replacement;
    ui_release(buffer);
    log_append("SETTINGS: Extra Options extended the ");
    log_append(page->name);
    log_append(" in ");
    log_append(fh3_text_code(language));
    log_append(fh3_text_from_game() ? " (game language).\r\n" : " (startup language).\r\n");
}

/* Startup-only transaction: check every enabled site before making any changes,
   acquire all write permissions first, and restore original bytes on failure. */
static int apply(unsigned char *image, Site *sites, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        unsigned char actual[sizeof(sites[i].original)];
        if (!memory_read(image + sites[i].rva, actual, sites[i].size) ||
            !memory_equal(actual, sites[i].original, sites[i].size)) return 0;
    }
    unsigned protected = 0;
    for (; protected < count; ++protected) {
#ifdef FH2_TWEAKS_TEST
        if ((int)protected == fail_protection) break;
#endif
        Site *site = sites + protected;
        if (!VirtualProtect(image + site->rva, site->size, PAGE_EXECUTE_READWRITE,
                            &site->protection)) break;
    }
    if (protected != count) {
        while (protected) {
            Site *site = sites + --protected;
            DWORD unused;
            VirtualProtect(image + site->rva, site->size, site->protection, &unused);
        }
        return 0;
    }
    for (unsigned i = 0; i < count; ++i)
        memory_copy(image + sites[i].rva, sites[i].replacement, sites[i].size);
    int success = FlushInstructionCache(GetCurrentProcess(), image, FH3_IMAGE_SIZE) != 0;
    /* Reverse order matters when two sites share a page: the first saved
       protection is the one that restores the page's original protection. */
    for (unsigned i = count; i; --i) {
        Site *site = sites + i - 1;
        DWORD unused;
        if (!VirtualProtect(image + site->rva, site->size, site->protection, &unused)) success = 0;
    }
    if (!success) {
        for (unsigned i = 0; i < count; ++i) {
            Site *site = sites + i;
            DWORD unused;
            if (VirtualProtect(image + site->rva, site->size, PAGE_EXECUTE_READWRITE, &unused))
                memory_copy(image + site->rva, site->original, site->size);
        }
        FlushInstructionCache(GetCurrentProcess(), image, FH3_IMAGE_SIZE);
        for (unsigned i = count; i; --i) {
            Site *site = sites + i - 1;
            DWORD unused;
            VirtualProtect(image + site->rva, site->size, site->protection, &unused);
        }
    }
    return success;
}

/* Route direct calls to `target` through a 12-byte absolute-jump thunk placed
   in int3 padding at `thunk`. Each call site must still call `target`. */
static unsigned redirect_calls(Site *sites, unsigned thunk, unsigned target,
                               const unsigned *calls, unsigned call_count, void *callback) {
    unsigned count = 0;
    Site jump = {thunk, 12, {0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
                 {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}, 0};
    uintptr_t address = (uintptr_t)callback;
    memory_copy(jump.replacement + 2, &address, sizeof(address));
    sites[count++] = jump;
    for (unsigned i = 0; i < call_count; ++i) {
        Site call = {calls[i], 5, {0xe8}, {0xe8}, 0};
        int32_t native = (int32_t)(target - calls[i] - 5), redirected = (int32_t)(thunk - calls[i] - 5);
        memory_copy(call.original + 1, &native, sizeof(native));
        memory_copy(call.replacement + 1, &redirected, sizeof(redirected));
        sites[count++] = call;
    }
    return count;
}

/* Write `length` bytes of new code into int3 padding at `rva`, split into
   12-byte sites so each chunk is still verified as untouched padding. */
static unsigned code_sites(Site *sites, unsigned rva, const unsigned char *code, unsigned length) {
    unsigned count = 0;
    for (unsigned offset = 0; offset < length; offset += 12) {
        Site site = {rva + offset, length - offset < 12 ? length - offset : 12, {0}, {0}, 0};
        for (unsigned i = 0; i < site.size; ++i) site.original[i] = 0xcc;
        memory_copy(site.replacement, code + offset, site.size);
        sites[count++] = site;
    }
    return count;
}

static Site call_site(unsigned rva, unsigned size, const unsigned char *original, unsigned thunk) {
    Site site = {rva, size, {0}, {0xe8}, 0};
    memory_copy(site.original, original, size);
    int32_t delta = (int32_t)(thunk - rva - 5);
    memory_copy(site.replacement + 1, &delta, sizeof(delta));
    for (unsigned i = 5; i < size; ++i) site.replacement[i] = 0x90;
    return site;
}

/* Livery renders: CCarLivery creates "LivRenderTexture" at 1024 or 2048 by
   quality, draws vinyls into an MSAA target twice the layer rect, then
   resolves into the texture. The projection scale is texture size * 0.5, so
   a larger texture with a larger target keeps vinyl placement unchanged. */
enum {
    LIVERY_SHIFT_THUNK = 0x1c890a1, /* shl r12d,n; shl r13d,n; ret; float scale */
    LIVERY_SCALE = 0x1c890aa,
    LIVERY_X_THUNK = 0x1c872e1, /* vcvtsi2ss xmm0,eax; vmulss xmm3,xmm0,[scale]; ret */
    LIVERY_Y_THUNK = 0x1c87371, /* vcvtsi2ss xmm1,esi; vmulss xmm2,xmm1,[scale]; ret */
};

static unsigned livery_prepare(Site *sites, int resolution_4k, int samples, int supersampling) {
    unsigned count = 0;
    if (resolution_4k) {
        sites[count++] = (Site){0x1c89860, 5, {0xba,0x00,0x08,0x00,0x00}, {0xba,0x00,0x10,0x00,0x00}, 0};
        sites[count++] = (Site){0x1c89898, 5, {0xba,0x00,0x04,0x00,0x00}, {0xba,0x00,0x10,0x00,0x00}, 0};
    }
    if (samples == 4)
        sites[count++] = (Site){0x1c89113, 6, {0x8b,0x05,0x17,0x42,0x9f,0x02},
                                {0xb8,0x04,0x00,0x00,0x00,0x90}, 0};
    if (resolution_4k || supersampling) {
        unsigned char shift = resolution_4k && supersampling ? 3 : 2;
        float scale = shift == 3 ? 0.125f : 0.25f;
        unsigned char shifter[13] = {0x41,0xc1,0xe4,shift, 0x41,0xc1,0xe5,shift, 0xc3};
        memory_copy(shifter + 9, &scale, sizeof(scale));
        count += code_sites(sites + count, LIVERY_SHIFT_THUNK, shifter, sizeof(shifter));
        unsigned char x[13] = {0xc5,0xfa,0x2a,0xc0, 0xc5,0xfa,0x59,0x1d,0,0,0,0, 0xc3};
        unsigned char y[13] = {0xc5,0xf2,0x2a,0xce, 0xc5,0xf2,0x59,0x15,0,0,0,0, 0xc3};
        int32_t x_scale = (int32_t)(LIVERY_SCALE - (LIVERY_X_THUNK + 12));
        int32_t y_scale = (int32_t)(LIVERY_SCALE - (LIVERY_Y_THUNK + 12));
        memory_copy(x + 8, &x_scale, sizeof(x_scale));
        memory_copy(y + 8, &y_scale, sizeof(y_scale));
        count += code_sites(sites + count, LIVERY_X_THUNK, x, sizeof(x));
        count += code_sites(sites + count, LIVERY_Y_THUNK, y, sizeof(y));
        static const unsigned char doubling[] = {0x45,0x03,0xe4,0x45,0x03,0xed};
        static const unsigned char x_size[] = {0xc5,0xfa,0x2a,0xc0,0xc5,0xfa,0x59,0xdc};
        static const unsigned char y_size[] = {0xc5,0xf2,0x2a,0xce,0xc5,0xf2,0x59,0xd4};
        sites[count++] = call_site(0x1c891a7, sizeof(doubling), doubling, LIVERY_SHIFT_THUNK);
        sites[count++] = call_site(0x1c8ff5d, sizeof(x_size), x_size, LIVERY_X_THUNK);
        sites[count++] = call_site(0x1c8ff69, sizeof(y_size), y_size, LIVERY_Y_THUNK);
    }
    return count;
}

static unsigned horn_prepare(Site *sites) {
    Site wrapper = {HORN_OWNED, 12, {0x48,0x8b,0x49,0x08,0xe9,0x07,0x00,0x00,0x00,0xcc,0xcc,0xcc},
                    {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}, 0};
    uintptr_t callback = (uintptr_t)horn_owned;
    memory_copy(wrapper.replacement + 2, &callback, sizeof(callback));
    sites[0] = wrapper;
    static const unsigned refreshes[] = {0x150f78c, 0x1516539, 0x15345c0};
    return 1 + redirect_calls(sites + 1, EXTRAS_REFRESH_THUNK, EXTRAS_REFRESH, refreshes,
                              ARRAY_COUNT(refreshes), extras_refresh);
}

int fh3_initialize(unsigned title_id, unsigned char *game, const wchar_t *executable,
                   const wchar_t *directory) {
    if (title_id != FH3_TITLE_ID) return 0;
    if (!directory || !directory[0] || !path_join(ini_path, directory, L"FH3_ExtraOptions.ini"))
        return rejected("REFUSED: cannot resolve the FH3 settings path. No FH3 patches applied.");
    open_log(directory);
    log_append("TITLE: FH3 (4CE1DFAB) 1.0.125.0; independent title profile.\r\n");
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!memory_read(game, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 64 || dos.e_lfanew > 4096 ||
        !memory_read(game + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != FH3_TIMESTAMP ||
        nt.OptionalHeader.SizeOfImage != FH3_IMAGE_SIZE || !readable_image(game) ||
        !executable || !verify_file(executable))
        return rejected("REFUSED: unsupported or modified FH3 executable; only the unmodified "
                        "1.0.125.0 build is supported. No FH3 patches applied.");
    if (!option(L"Global", L"Enabled", 1)) {
        status_set("OK: FH3 Extra Options disabled in FH3_ExtraOptions.ini.");
        log_append(status_get());
        log_append("\r\n");
        return 1;
    }
    image = game;
    fh3_text_init(directory, game_language_code);
    int load_mods = option(L"Mods", L"Enabled", 1);
    static Site sites[160]; /* static: too large for the stack without __chkstk */
    unsigned count = 0;
    if (option(L"Frontend", L"SkipIntro", 1)) {
        /* The logo parade is the 6 s legal screen followed by the 21 s
           T10/Microsoft Studios video. Leave the video entry uncounted so the
           parade ends after the legal screen through its normal completion
           path. (Taking the developer no-parade branch instead hangs boot on
           the legal screen.) */
        sites[count++] = (Site){0x1729355, 2, {0xff,0xc6}, {0x66,0x90}, 0};
        log_append("INTRO: T10/Microsoft Studios startup video skipped; legal screen retained.\r\n");
    }
    if (option(L"Content", L"OfflineVIP", 1)) {
        /* VIP license refresh: treat the membership license check as owned.
           The caller then stores Main/IsVip=true and sets gamer-flag bit 0
           while preserving the other flag bits. */
        sites[count++] = (Site){0x347ac3, 3, {0x0f,0xb6,0xf8}, {0x40,0xb7,0x01}, 0};
        /* Every native Main/IsVip read goes through the same one-byte getter;
           replace those calls with true. Profile load, UI badge, reward check. */
        sites[count++] = (Site){0x3c5b74, 5, {0xe8,0x37,0x3c,0xcf,0x00}, {0xb8,1,0,0,0}, 0};
        sites[count++] = (Site){0x9826c8, 5, {0xe8,0xe3,0x70,0x73,0x00}, {0xb8,1,0,0,0}, 0};
        sites[count++] = (Site){0x992770, 5, {0xe8,0x3b,0x70,0x72,0x00}, {0xb8,1,0,0,0}, 0};
        log_append("VIP: VIP license refresh and Main/IsVip reads forced on.\r\n");
    }
    unlock_treasure_map = option(L"Content", L"UnlockTreasureMap", 0);
    return_to_garage = option(L"Frontend", L"ReturnToGarage", 1);
    for (unsigned i = 0; i < ARRAY_COUNT(camera_fovs); ++i) {
        wchar_t value[32];
        GetPrivateProfileStringW(L"Camera", camera_fovs[i].key, L"0", value, ARRAY_COUNT(value),
                                 ini_path);
        camera_fovs[i].value = parse_degrees(value);
        camera_fov_used |= camera_fovs[i].value != 0.0f;
    }
    if (camera_fov_used) log_append("CAMERA: custom field of view applied.\r\n");
    minimap_follows_camera = option(L"HUD", L"MinimapFollowsCamera", 1);
    in_game_menu = option(L"Settings", L"InGameMenu", 1);
    if (minimap_follows_camera || in_game_menu) {
        /* Installed whenever the setting can change at runtime; the flag gates it
           (with the flag off the stick is ignored and the rotation eases back). */
        static const unsigned updates[] = {0x120b3d2};
        count += redirect_calls(sites + count, 0x1002d43, 0x120c6c0, updates, 1, minimap_update);
        static const unsigned north[] = {0xab2e54};
        count += redirect_calls(sites + count, 0x1002fc3, 0x1802d9a, north, 1, north_angle);
        if (minimap_follows_camera)
            log_append("HUD: minimap turns with the right-stick look camera.\r\n");
    }
    drift_camera = option(L"Camera", L"DriftCamera", 1);
    if (drift_camera || in_game_menu) {
        for (unsigned i = 0; i < DRIFT_TUNING_COUNT; ++i) {
            int percent = (int)GetPrivateProfileIntW(L"Camera", drift_tuning[i].key,
                                                     drift_tuning[i].percent, ini_path);
            drift_tuning[i].percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
        }
        drift_apply_tuning();
        count += drift_prepare(sites + count);
        if (drift_camera) log_append("CAMERA: drift camera follows the slide on chase cameras.\r\n");
    }
    if (in_game_menu || load_mods) { /* plugins may hide the HUD */
        static const unsigned loads[] = {0x21db70c};
        count += redirect_calls(sites + count, 0x1004542, 0x21dd6c0, loads, 1, ui_load_resource);
        log_append("SETTINGS: settings rows and HUD scene extensions scheduled.\r\n");
    }
    unrestricted_photo = option(L"PhotoMode", L"Unrestricted", 1);
    photo_no_collision = unrestricted_photo && option(L"PhotoMode", L"NoCollision", 0);
    if (photo_no_collision) {
        /* Ground probe: mov eax, [CollisionFixIterations] -> private copy. */
        Site probe = {0x444be2, 6, {0x8b,0x05,0x08,0x9d,0xea,0x03}, {0x8b,0x05}, 0};
        int32_t displacement = (int32_t)(PHOTO_PROBE_ITERATIONS - (0x444be2 + 6));
        memory_copy(probe.replacement + 2, &displacement, sizeof(displacement));
        sites[count++] = probe;
        /* Ground probe: keep the altitude (+0x368), drop the lift (+0x354). */
        sites[count++] = (Site){0x444f49, 8, {0xc5,0xfa,0x11,0xaf,0x54,0x03,0x00,0x00},
                                {0x0f,0x1f,0x84,0x00,0x00,0x00,0x00,0x00}, 0};
    }
    if (unrestricted_photo)
        log_append(photo_no_collision
                      ? "PHOTO MODE: boundary and height limits removed; collision off.\r\n"
                      : "PHOTO MODE: boundary and height limits removed; collision kept.\r\n");
    if (unlock_treasure_map || unrestricted_photo || camera_fov_used || in_game_menu ||
        return_to_garage || load_mods) {
        /* SatNavComponent vtable slot 2 is its per-frame update. */
        Site slot = {0x4378bd0, 8, {0}, {0}, 0};
        uintptr_t original = (uintptr_t)(image + 0x8f7d10), callback = (uintptr_t)satnav_update;
        memory_copy(slot.original, &original, sizeof(original));
        memory_copy(slot.replacement, &callback, sizeof(callback));
        sites[count++] = slot;
    }
    if (option(L"Content", L"UnlockHornAccelerator", 1)) {
        count += horn_prepare(sites + count);
        log_append("HORN ACCELERATOR: add-on reported owned and shown as purchased in Extras.\r\n");
    }
    if (unlock_treasure_map)
        log_append("TREASURE MAP: unlocks once per world after it has loaded.\r\n");
    livery_4k = option(L"Livery", L"Resolution4K", 1);
    livery_msaa4 = GetPrivateProfileIntW(L"Livery", L"MSAA", 4, ini_path) == 4;
    livery_supersampling = option(L"Livery", L"Supersampling", 0);
    int livery_samples = livery_msaa4 ? 4 : 2;
    count += livery_prepare(sites + count, livery_4k, livery_samples, livery_supersampling);
    if (livery_4k || livery_samples == 4 || livery_supersampling) {
        log_append(livery_4k ? "LIVERY: 4096x4096 livery textures" : "LIVERY: native livery size");
        log_append(livery_samples == 4 ? ", 4x MSAA" : ", native 2x MSAA");
        log_append(livery_supersampling ? ", supersampled.\r\n" : ".\r\n");
    }
    /* Always installed so Settings can switch the readout on and off. */
    frame_stats_enabled = option(L"HUD", L"ShowFPS", 0);
    frame_stats_gpu = option(L"HUD", L"ShowGPUUsage", 1);
    frame_stats_ms = option(L"HUD", L"ShowFrameTime", 1);
    frame_stats_box = option(L"HUD", L"ShowFPSBackground", 1);
    QueryPerformanceFrequency(&stats_frequency);
    static const unsigned layers[] = {UI_LAYERS_CALL};
    count += redirect_calls(sites + count, UI_LAYERS_THUNK, UI_LAYERS_UPDATE, layers, 1,
                            ui_layers_update);
    if (frame_stats_enabled && frame_stats_gpu) gpu_sampler_start();
    log_append(frame_stats_enabled ? "FPS: frame rate readout in the top-right corner of the HUD and menus.\r\n"
                                  : "FPS: frame rate readout off (Settings > HUD and Gameplay).\r\n");
    if (option(L"Frontend", L"OnlineTabs", 1)) {
        count += redirect_calls(sites + count, TILE_STATE_THUNK, TILE_STATE_SET, online_tab_calls,
                                ARRAYSIZE(online_tab_calls), tile_state_set);
        log_append("MENUS: Social, Clubs, Rivals and Marketplace pause tabs stay enabled offline.\r\n");
    }
    if (return_to_garage) {
        static const unsigned prompts[] = {0x1586af7};
        count += redirect_calls(sites + count, 0x14a7522, PROMPT_ADD, prompts, 1, pause_help_prompt);
        static const struct { unsigned slot, native; void *callback; } slots[] = {
            {HELP_INVOKE_SLOT, HELP_INVOKE, (void *)return_to_garage_invoke}};
        for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
            Site slot = {slots[i].slot, 8, {0}, {0}, 0};
            uintptr_t native = (uintptr_t)(image + slots[i].native),
                      callback = (uintptr_t)slots[i].callback;
            memory_copy(slot.original, &native, sizeof(native));
            memory_copy(slot.replacement, &callback, sizeof(callback));
            sites[count++] = slot;
        }
        log_append("MENUS: the pause menu's X button enters the nearest festival site (Return to Garage).\r\n");
    }
    manufacturer_jump = option(L"Frontend", L"JumpToManufacturer", option(L"Frontend", L"ManufacturerJump", 1));
    for (unsigned i = 0; manufacturer_jump && i < ARRAYSIZE(car_select_vtables); ++i) {
        Site slot = {car_select_vtables[i] + SCENE_UPDATE_SLOT, 8, {0}, {0}, 0};
        uintptr_t native;
        if (!memory_read(image + slot.rva, &native, sizeof(native)) ||
            (native != (uintptr_t)(image + SCENE_UPDATE) &&
             native != (uintptr_t)(image + SCENE_UPDATE_JUMP)))
            native = (uintptr_t)(image + SCENE_UPDATE); /* verification then refuses */
        uintptr_t wrapper = (uintptr_t)car_select_update;
        memory_copy(slot.original, &native, sizeof(native));
        memory_copy(slot.replacement, &wrapper, sizeof(wrapper));
        sites[count++] = slot;
    }
    if (manufacturer_jump)
        log_append("MENUS: View or Backspace on a car list opens Jump to Manufacturer.\r\n");
    media_overrides = option(L"Files", L"EnableMediaOverrides", option(L"Files", L"MediaOverrides", 1));
    if (media_overrides || load_mods) {
        /* Redirect the only call to the file-system initializer through a
           thunk in int3 padding between two functions. */
        Site thunk = {0x188c6f2, 12, {0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
                      {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}, 0};
        uintptr_t callback = (uintptr_t)initialize_files;
        memory_copy(thunk.replacement + 2, &callback, sizeof(callback));
        sites[count++] = thunk;
        Site call = {0x1899035, 5, {0xe8,0x36,0xa7,0x03,0x00}, {0xe8,0,0,0,0}, 0};
        int32_t delta = (int32_t)(0x188c6f2 - 0x1899035 - 5);
        memory_copy(call.replacement + 1, &delta, sizeof(delta));
        sites[count++] = call;
        log_append("FILES: mediaoverride and mediapc mounts scheduled after native file-system "
                  "initialization.\r\n");
    }
    forzathon_cars = option(L"Content", L"UnlockAllCars", 1);
    forzathon_horns = option(L"Content", L"UnlockForzathonHorns", 1);
    horizon_editions_autoshow = option(L"Content", L"UnlockForzathonHorizonEditionCarsInAutoshow", 0);
    forzathon_wheelspins = option(L"Content", L"HorizonEditionWheelspins", 1);
    if (forzathon_cars || horizon_editions_autoshow || forzathon_horns || forzathon_wheelspins) {
        static const unsigned opens[] = {0x1a50293, 0x1a562f8};
        count += redirect_calls(sites + count, SQLITE_OPEN_THUNK, SQLITE_OPEN, opens, 2, open_database);
    }
    if (option(L"Compatibility", L"DisableParentalLocks", 1)) {
        /* As in FH2: the profile's two Xbox privilege refreshes ask the
           console (CheckPrivilegeAsync) for multiplayer sessions (0xfe,
           result at +0x5c) and user-created content (0xf7, +0x60); 6 blocks,
           4 allows. Both now just store 4: mov dword [rcx+field], 4; ret. */
        static const unsigned char prologue[10] = {0x48,0x89,0x4c,0x24,0x08,0x57,0x48,0x83,0xec,0x30};
        Site multiplayer = {0x1cb9e80, 10, {0}, {0xc7,0x41,0x5c,0x04,0x00,0x00,0x00,0xc3,0x90,0x90}, 0};
        Site content = {0x1cba0e0, 10, {0}, {0xc7,0x41,0x60,0x04,0x00,0x00,0x00,0xc3,0x90,0x90}, 0};
        memory_copy(multiplayer.original, prologue, sizeof(prologue));
        memory_copy(content.original, prologue, sizeof(prologue));
        sites[count++] = multiplayer;
        sites[count++] = content;
        log_append("PARENTAL: multiplayer and user-content privileges reported allowed.\r\n");
    }
    /* No DLC option: car packs are rebuilt from store licenses on every boot,
       and Azimuth already reports them as owned. */
    if (!apply(game, sites, count))
        return rejected("REFUSED: FH3 patch bytes or page protections changed; no FH3 patches "
                        "applied.");
    if (load_mods) fh3_mods_configure(directory, executable);
    if (option(L"Compatibility", L"GenerateMissingSaveCommits", 1))
        azimuth_repair_title_markers((unsigned char *)GetModuleHandleW(NULL), FH3_TITLE_ID,
                                     log_append);
    status_set("OK: FH3 Extra Options initialized (4CE1DFAB); FH2 and FM5 features inactive.");
    log_append(status_get());
    log_append("\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh3TestTextBind(unsigned char *game, const wchar_t *directory) {
    image = game;
    fh3_text_init(directory, game_language_code);
}
__declspec(dllexport) void *Fh3TestMinimapBind(unsigned char *game, GetPadState pad) {
    image = game;
    get_pad_state = pad;
    minimap_follows_camera = 1;
    look_x = look_sin = 0.0f;
    look_y = look_cos = 1.0f;
    return (void *)minimap_update;
}
__declspec(dllexport) int Fh3TestInitialize(unsigned title, void *game, const wchar_t *path,
                                            const wchar_t *directory) {
    return fh3_initialize(title, game, path, directory);
}
__declspec(dllexport) void Fh3TestProtectionFailure(int index) { fail_protection = index; }
#endif
