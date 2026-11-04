#include "fh3.h"
#include "fh3_internal.h"
#include "sha256.h"
#include "support.h"
#include <xmmintrin.h>
#ifdef FH2_AZIMUTH
#include "adapters/azimuth_host.h"
#include "adapters/azimuth_markers.h"
#endif

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
static wchar_t ini_path[PATH_CAPACITY], log_path[PATH_CAPACITY];
static unsigned char file_buffer[65536];
#ifdef FH2_TWEAKS_TEST
static int fail_protection = -1;
#endif

static void write_log(const char *message) {
    if (!log_path[0]) return;
    HANDLE file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, message, (DWORD)text_length(message), &written, NULL);
    CloseHandle(file);
}

static int open_log(const wchar_t *directory) {
    if (!path_join(log_path, directory, MOD_LOG_NAME)) {
        log_path[0] = 0;
        return 0;
    }
    HANDLE log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    write_log(MOD_NAME " " MOD_VERSION "\r\n");
    return 1;
}

void fh3_log_status(const wchar_t *directory) {
    if (!open_log(directory)) return;
    write_log(status_get());
    write_log("\r\n");
}

static int rejected(const char *reason) {
    status_set(reason);
    write_log(reason);
    write_log("\r\n");
    return 0;
}

static int option(const wchar_t *section, const wchar_t *key, int fallback) {
    wchar_t value[16];
    GetPrivateProfileStringW(section, key, fallback ? L"1" : L"0", value, ARRAY_COUNT(value),
                             ini_path);
    if (value[0] == L'0' && !value[1]) return 0;
    if (value[0] == L'1' && !value[1]) return 1;
    write_log(fallback ? "CONFIG: invalid boolean; expected 0 or 1, using default 1.\r\n"
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
void fh3_log(const char *message) { write_log(message); }
int fh3_option(const wchar_t *section, const wchar_t *key, int fallback) {
    return option(section, key, fallback);
}
const wchar_t *fh3_ini_path(void) { return ini_path; }
int fh3_mount(void *filesystem, const char *folder, unsigned priority) {
    return mount_folder(filesystem, folder, priority);
}

static unsigned char initialize_files(void *filesystem) {
    unsigned char result = ((InitializeFiles)(image + 0x18d3770))(filesystem);
    if (!result) {
        write_log("FILES: native initialization failed; override mounts skipped.\r\n");
        return result;
    }
    int override = !media_overrides || mount_folder(filesystem, "game:\\mediaoverride\\", 0x7ffffffe);
    int pc = !media_overrides || mount_folder(filesystem, "game:\\mediapc\\", 0x7ffffffd - 0x100);
    fh3_mods_mount(filesystem); /* 0x7ffffffc downward, above mediapc */
    ((Sort)(*(void ***)filesystem)[0x1f8 / 8])(filesystem);
    write_log(override && pc ? "FILES: native mounts ready: mediaoverride > mods > mediapc > original "
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
    if (result || !*database || !game_database(*database)) return result;
    void *copy = NULL;
    if (((SqliteOpen)(image + SQLITE_OPEN))(":memory:", &copy) || !copy) {
        if (copy) ((SqliteCall)(image + SQLITE_CLOSE))(copy);
        write_log("UNLOCKS: no in-memory database; Forzathon cars and horns stay locked.\r\n");
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
    if (cars) write_log("UNLOCKS: every car hidden from the Autoshow can be bought (in memory).\r\n");
    if (editions) write_log("UNLOCKS: Horizon Edition cars can be bought in the Autoshow (in memory).\r\n");
    int horns = copied && forzathon_horns &&
                sql_run(copy, "UPDATE CarHorns SET IsInitial=1 "
                              "WHERE IsInitial=0 AND UnlockByPerk=-1 AND Id NOT IN (31,32,33)");
    int spins = copied && forzathon_wheelspins &&
                sql_run(copy, "UPDATE TombolaPrizes SET CarAwardIds="
                              "(SELECT group_concat(h.CarId) FROM HorizonEditionCars h JOIN Data_Car c ON c.Id=h.CarId) "
                              "WHERE PrizeId IN (SELECT p.PrizeId FROM TombolaPrizes p JOIN HorizonEditionCars h "
                              "ON ','||p.CarAwardIds||',' LIKE '%,'||h.CarId||',%')");
    if (horns) write_log("UNLOCKS: Forzathon horns unlocked (in memory).\r\n");
    if (spins) write_log("UNLOCKS: every Horizon Edition car can be won in Wheelspins (in memory).\r\n");
    if ((cars || editions || horns || spins) && !((SqliteCall)(image + SQLITE_CLOSE))(*database)) {
        *database = copy;
    } else {
        ((SqliteCall)(image + SQLITE_CLOSE))(copy);
        write_log("UNLOCKS: database edit failed; cars, horns and Wheelspins unchanged.\r\n");
    }
    return result;
}

/* Autodrive. The scripted SetCarAIDriving state hands a car to the AI through
   0x18b7040(car, enabled); car+0x7398 is 1 while a human drives and 0 under
   AI. The local player's cars are listed at 0x1724f70(manager, 0)+0x298. */
typedef void (*SatNavUpdate)(void *, float);
typedef void (*SetAIDriving)(void *, unsigned char);
typedef void *(*PlayerContext)(void *, unsigned);
typedef SHORT(WINAPI *KeyState)(int);
typedef HWND(WINAPI *ForegroundWindow)(void);
typedef DWORD(WINAPI *WindowProcess)(HWND, LPDWORD);

static KeyState key_state;
static ForegroundWindow foreground_window;
static WindowProcess window_process;
static int key_was_down = 1;
static void *autodrive_car;
static int autodrive_enabled;

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

/* Driving input is read straight from XInput (and the usual driving keys), so
   cancelling never depends on the native input contexts the AI owns. */
typedef struct {
    DWORD packet;
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT left_x, left_y, right_x, right_y;
} PadState;
typedef DWORD(WINAPI *GetPadState)(DWORD, PadState *);
enum { PAD_LEFT_THUMB = 0x40, PAD_RIGHT_THUMB = 0x80 };
static GetPadState get_pad_state;
static int input_armed;

static int load_pads(void) {
    if (!get_pad_state) {
        HMODULE xinput = LoadLibraryExW(L"xinput1_4.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        get_pad_state = xinput ? (GetPadState)(uintptr_t)GetProcAddress(xinput, "XInputGetState")
                               : NULL;
    }
    return get_pad_state != NULL;
}

/* D-pad Down belongs to ANNA in FH3, so the default controller toggle is both
   sticks clicked together (horn + look back on their own). */
static WORD toggle_buttons = PAD_LEFT_THUMB | PAD_RIGHT_THUMB;
static int toggle_key = VK_F2;

static int toggle_chord(void) {
    for (DWORD pad = 0; load_pads() && pad < 4; ++pad) {
        PadState state;
        if (get_pad_state(pad, &state) == ERROR_SUCCESS &&
            (state.buttons & toggle_buttons) == toggle_buttons)
            return 1;
    }
    return 0;
}

static int same_name(const wchar_t *text, size_t length, const char *name) {
    size_t i = 0;
    for (; i < length && name[i]; ++i) {
        wchar_t c = text[i] >= L'a' && text[i] <= L'z' ? text[i] - 32 : text[i];
        if (c != (wchar_t)name[i]) return 0;
    }
    return i == length && !name[i];
}

/* "L3+R3", "LB+Y", ... ; 0 when any part is unknown. */
static WORD parse_buttons(const wchar_t *text) {
    static const struct { const char *name; WORD bit; } names[] = {
        {"UP", 0x1}, {"DOWN", 0x2}, {"LEFT", 0x4}, {"RIGHT", 0x8}, {"MENU", 0x10},
        {"START", 0x10}, {"VIEW", 0x20}, {"BACK", 0x20}, {"L3", 0x40}, {"R3", 0x80},
        {"LB", 0x100}, {"RB", 0x200}, {"A", 0x1000}, {"B", 0x2000}, {"X", 0x4000},
        {"Y", 0x8000}};
    WORD mask = 0;
    while (*text) {
        while (*text == L' ' || *text == L'+') ++text;
        size_t length = 0;
        while (text[length] && text[length] != L'+' && text[length] != L' ') ++length;
        if (!length) break;
        WORD bit = 0;
        for (unsigned i = 0; i < ARRAY_COUNT(names) && !bit; ++i)
            if (same_name(text, length, names[i].name)) bit = names[i].bit;
        if (!bit) return 0;
        mask |= bit;
        text += length;
    }
    return mask;
}

/* "F2", "G", "7" or a virtual-key code such as "0x71"; 0 when unknown. */
static int parse_key(const wchar_t *text) {
    size_t length = 0;
    while (text[length]) ++length;
    if (length > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        int value = 0;
        for (size_t i = 2; i < length; ++i) {
            wchar_t c = text[i];
            int digit = c >= L'0' && c <= L'9' ? c - L'0'
                        : c >= L'a' && c <= L'f' ? c - L'a' + 10
                        : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
            if (digit < 0 || value > 0xff) return 0;
            value = value * 16 + digit;
        }
        return value > 0 && value < 0xff ? value : 0;
    }
    if (length == 1) {
        wchar_t c = text[0] >= L'a' && text[0] <= L'z' ? text[0] - 32 : text[0];
        return (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ? (int)c : 0;
    }
    if ((length == 2 || length == 3) && (text[0] == L'F' || text[0] == L'f')) {
        int number = text[1] - L'0';
        if (length == 3) number = number * 10 + (text[2] - L'0');
        return number >= 1 && number <= 12 && text[1] >= L'0' && text[1] <= L'9' &&
                       (length == 2 || (text[2] >= L'0' && text[2] <= L'9'))
                   ? VK_F1 + number - 1
                   : 0;
    }
    return 0;
}

static void read_autodrive_bindings(void) {
    wchar_t value[64];
    GetPrivateProfileStringW(L"Vehicle", L"AutodriveKey", L"F2", value, ARRAY_COUNT(value),
                             ini_path);
    int key = parse_key(value);
    if (key) toggle_key = key;
    else write_log("CONFIG: invalid AutodriveKey; using F2.\r\n");
    GetPrivateProfileStringW(L"Vehicle", L"AutodriveButtons", L"L3+R3", value,
                             ARRAY_COUNT(value), ini_path);
    WORD buttons = parse_buttons(value);
    if (buttons) toggle_buttons = buttons;
    else write_log("CONFIG: invalid AutodriveButtons; using L3+R3.\r\n");
}

static int driving_input(void) {
    for (DWORD pad = 0; load_pads() && pad < 4; ++pad) {
        PadState state;
        if (get_pad_state(pad, &state) == ERROR_SUCCESS &&
            (state.left_trigger > 40 || state.right_trigger > 40 || state.left_x > 12000 ||
             state.left_x < -12000))
            return 1;
    }
    static const int keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'W', 'A', 'S', 'D'};
    for (unsigned i = 0; i < ARRAY_COUNT(keys); ++i)
        if (focused_key(keys[i])) return 1;
    return 0;
}

/* Autodrive cinematic camera: ForzaMultiCam (singleton at 0x4e6caf0) holds
   its camera groups as 64-byte entries at +0x1a8 (camera at +0x30) and the
   current group on a list at +0x1d0 (count +0x1f0, back() via 0x447250).
   0x18f0f40(manager, group) switches group; group 13 is CarRelativeCams.
   CarRelativeCams (Camera::CTSCCamera) keeps its shots at [+0x1b8]: mode
   (+0) and 0x5b0-byte entries (+8 begin, +0x10 end) with label byte +0x20
   and start time +0x28. In mode 3 each shot starts at its index and the
   update picks the shot under the time at +0x890, so writing the time
   selects a shot. Labels 17 and 19 are the slow orbiting shots FH2 used;
   other shots are static, e.g. shot 0 looks back from the bumper. The anim
   player at +0x1c0 has the playing shot config at +0x1a8 (entry +0x80) and
   its keys (64 bytes, time first) at +0x1b0..+0x1b8. */
enum { CAMERA_MANAGER = 0x4e6caf0, CAMERA_SET_GROUP = 0x18f0f40, CAMERA_GROUP_BACK = 0x447250 };
typedef void (*SetCameraGroup)(void *, int);
typedef int *(*CameraGroupBack)(void *);
enum { CINEMATIC_GROUP = 13, SHOT_SIZE = 0x5b0 };
static int autodrive_cinematic = 1, cinematic_previous = -1;
static int cinematic_tried, cinematic_label;
static float autodrive_elapsed, shot_elapsed, shot_length;
static double cinematic_saved_time;
static unsigned shot_seed = 0x9e3779b9u;

static void *camera_manager(void) {
    void *manager;
    return memory_read_pointer(image + CAMERA_MANAGER, &manager) ? manager : NULL;
}

static int camera_group(void *manager) {
    size_t count;
    int group;
    if (!manager || !memory_read((unsigned char *)manager + 0x1f0, &count, sizeof(count)) ||
        !count)
        return -1;
    int *current = ((CameraGroupBack)(image + CAMERA_GROUP_BACK))((unsigned char *)manager + 0x1d0);
    return current && memory_read(current, &group, sizeof(group)) ? group : -1;
}

static int set_camera_group(void *manager, int group) {
    unsigned char *groups, *end;
    void *camera;
    if (!manager || group < 0 ||
        !memory_read_pointer((unsigned char *)manager + 0x1a8, (void **)&groups) ||
        !memory_read_pointer((unsigned char *)manager + 0x1b0, (void **)&end) || end < groups ||
        (size_t)(end - groups) / 64 <= (unsigned)group ||
        !memory_read_pointer(groups + (size_t)group * 64 + 0x30, &camera) || !camera)
        return 0;
    ((SetCameraGroup)(image + CAMERA_SET_GROUP))(manager, group);
    return camera_group(manager) == group;
}

/* The CarRelativeCams camera and its shot table, if it has the expected shape. */
static unsigned char *relative_camera(void *manager, unsigned char **shots, unsigned *count) {
    unsigned char *groups, *end, *camera, *data, *last;
    void *vtable;
    unsigned mode;
    if (!manager || !memory_read_pointer((unsigned char *)manager + 0x1a8, (void **)&groups) ||
        !memory_read_pointer((unsigned char *)manager + 0x1b0, (void **)&end) ||
        (size_t)(end - groups) / 64 <= CINEMATIC_GROUP ||
        !memory_read_pointer(groups + CINEMATIC_GROUP * 64 + 0x30, (void **)&camera) ||
        !memory_read_pointer(camera, &vtable) || vtable != image + 0x42ef648 ||
        !memory_read_pointer(camera + 0x1b8, (void **)&data) ||
        !memory_read(data, &mode, sizeof(mode)) || mode != 3 ||
        !memory_read_pointer(data + 8, (void **)shots) ||
        !memory_read_pointer(data + 0x10, (void **)&last) || last <= *shots ||
        (size_t)(last - *shots) % SHOT_SIZE || (size_t)(last - *shots) / SHOT_SIZE > 1024)
        return NULL;
    *count = (unsigned)((size_t)(last - *shots) / SHOT_SIZE);
    return camera;
}

/* Select a random shot with the given label; 0 when there is none. */
static int select_shot(unsigned char *camera, unsigned char *shots, unsigned count, int label) {
    unsigned matches = 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned char value;
        if (!memory_read(shots + (size_t)i * SHOT_SIZE + 0x20, &value, 1)) return 0;
        matches += value == label;
    }
    if (!matches) return 0;
    shot_seed = shot_seed * 1664525u + 1013904223u + (unsigned)GetTickCount64();
    unsigned pick = (shot_seed >> 8) % matches;
    for (unsigned i = 0; i < count; ++i) {
        unsigned char value;
        memory_read(shots + (size_t)i * SHOT_SIZE + 0x20, &value, 1);
        if (value != label || pick--) continue;
        float start;
        if (!memory_read(shots + (size_t)i * SHOT_SIZE + 0x28, &start, sizeof(start)) ||
            !float_is_finite(start))
            return 0;
        *(volatile double *)(camera + 0x890) = start + 0.5;
        cinematic_label = label;
        shot_elapsed = 0.0f;
        shot_length = 0.0f;
        return 1;
    }
    return 0;
}

/* Length of the shot the anim player is playing, once it has picked it up. */
static float playing_shot_length(unsigned char *camera, unsigned char *shots, unsigned count) {
    unsigned char *config, *keys, *keys_end;
    double time;
    float first, last;
    if (!memory_read(camera + 0x890, &time, sizeof(time)) || !(time >= 0 && time < count) ||
        !memory_read_pointer(camera + 0x1c0 + 0x1a8, (void **)&config) ||
        config != shots + (size_t)time * SHOT_SIZE + 0x80 ||
        !memory_read_pointer(camera + 0x1c0 + 0x1b0, (void **)&keys) ||
        !memory_read_pointer(camera + 0x1c0 + 0x1b8, (void **)&keys_end) || keys_end <= keys ||
        (size_t)(keys_end - keys) % 64 || !memory_read(keys, &first, sizeof(first)) ||
        !memory_read(keys_end - 64, &last, sizeof(last)) || !float_is_finite(first) ||
        !float_is_finite(last))
        return 0.0f;
    float length = last - first;
    return length < 3.0f ? 3.0f : length > 15.0f ? 15.0f : length;
}

static void hud_set_hidden(int hidden);

static void cinematic_enter(void) {
    cinematic_tried = 1;
    void *manager = camera_manager();
    int previous = camera_group(manager);
    unsigned char *shots, *camera;
    unsigned count;
    /* Only from the player's driving views (chase, driver, hood, bumper). */
    if (previous < 0 || previous > 4 || !(camera = relative_camera(manager, &shots, &count)) ||
        !memory_read(camera + 0x890, &cinematic_saved_time, sizeof(cinematic_saved_time))) {
        write_log("AUTODRIVE: cinematic camera unavailable here.\r\n");
        return;
    }
    if (!select_shot(camera, shots, count, 17) && !select_shot(camera, shots, count, 19)) {
        write_log("AUTODRIVE: no cinematic shots in CarRelativeCams.\r\n");
        return;
    }
    if (!set_camera_group(manager, CINEMATIC_GROUP)) {
        *(volatile double *)(camera + 0x890) = cinematic_saved_time;
        write_log("AUTODRIVE: cinematic camera unavailable here.\r\n");
        return;
    }
    cinematic_previous = previous;
    hud_set_hidden(1);
    write_log("AUTODRIVE: cinematic camera on.\r\n");
}

static void cinematic_tick(float dt) {
    void *manager = camera_manager();
    unsigned char *shots, *camera;
    unsigned count;
    if (cinematic_previous < 0) return;
    if (camera_group(manager) != CINEMATIC_GROUP) {
        cinematic_previous = -1; /* the player picked another camera */
        hud_set_hidden(0);
        return;
    }
    if (!(camera = relative_camera(manager, &shots, &count))) return;
    if (dt > 0.0f && dt < 1.0f) shot_elapsed += dt;
    if (!shot_length) shot_length = playing_shot_length(camera, shots, count);
    float length = shot_length ? shot_length : 8.0f;
    if (shot_elapsed < length) return;
    int next = cinematic_label == 17 ? 19 : 17;
    if (!select_shot(camera, shots, count, next)) select_shot(camera, shots, count, cinematic_label);
}

static void cinematic_leave(void) {
    void *manager = camera_manager();
    unsigned char *shots, *camera;
    unsigned count;
    if (cinematic_previous >= 0 && camera_group(manager) == CINEMATIC_GROUP) {
        set_camera_group(manager, cinematic_previous);
        if ((camera = relative_camera(manager, &shots, &count)))
            *(volatile double *)(camera + 0x890) = cinematic_saved_time;
    }
    cinematic_previous = -1;
    hud_set_hidden(0);
}

static void autodrive_stop(const char *reason) {
    cinematic_leave();
    if (autodrive_car && driver_mode(autodrive_car) == 0)
        ((SetAIDriving)(image + 0x18b7040))(autodrive_car, 0);
    autodrive_car = NULL;
    write_log(reason);
}

static void autodrive_start(void) {
    void *car = player_car();
    if (!car) {
        write_log("AUTODRIVE: no human-driven local car found.\r\n");
        return;
    }
    ((SetAIDriving)(image + 0x18b7040))(car, 1);
    if (driver_mode(car) != 0) {
        write_log("AUTODRIVE: the AI driver did not engage.\r\n");
        return;
    }
    autodrive_car = car;
    autodrive_elapsed = 0.0f;
    cinematic_tried = 0;
    /* The press that started the drive must be released before any driving
       input counts as a cancel. */
    input_armed = 0;
    write_log("AUTODRIVE: AI driver engaged on the local car.\r\n");
}

/* ANNA menu entries (D-pad Down). HUDAnnaComponent keeps the available command
   names in a std::vector<std::string> at +0xb8 and rebuilds its menu (+0xf8)
   with AddOption 0x10b0da0, which labels each item with the localized
   "IDS_SpeechItem_<name>" via 0x10b9870. On select, ANNAManualControlComponent
   reads the highlighted name through 0xaaa710 and hands it to the voice-command
   dispatcher. Custom names are appended after the native rebuild, labelled
   with our own text, and removed from the selection before it reaches the
   dispatcher. The select handler runs on the game thread, so actions run
   immediately. */
typedef struct {
    union {
        char text[16];
        char *pointer;
    };
    size_t size, capacity;
} MsvcString;
typedef void (*AddOption)(void *, const MsvcString *);
typedef void (*DestroyNames)(void *, void *, void *, unsigned char);
typedef void **(*MakeLabel)(void *, void **, const MsvcString *);
typedef void **(*CreateUIString)(void **);
typedef MsvcString *(*HighlightedName)(void *, MsvcString *);

typedef struct {
    const char *name; /* < 16 characters, so the native string stays inline */
    const wchar_t *label, *close_label; /* close_label: a car part that is open */
    void (*action)(void *component, int part);
    int enabled, part; /* part >= 0: listed in the Car submenu, not the top level */
} AnnaEntry;

/* Car parts: ForzaCar's animator (+0x73a8) points to the car's animation graph
   at +0x1e0 and takes named events through 0x1889b40(car, name), the
   path the game uses for "doorLF:close". The graph's events are a map at
   graph+0x268 (end node at +0x270), searched with 0x1d08e60. Cars define
   "<part>:open" and "<part>:close" (sometimes "<part>:toggle") for the parts
   they animate (the same clips ForzaVista plays). */
enum { PART_ROOF, PART_HOOD, PART_TRUNK, PART_DOOR_LF, PART_DOOR_RF, PART_DOOR_LR, PART_DOOR_RR,
       PART_COUNT };
static const char *const part_names[PART_COUNT] = {"roof",   "hood",   "trunk", "doorLF",
                                                   "doorRF", "doorLR", "doorRR"};
typedef void (*CarEvent)(void *, const char *);
typedef void *(*EventFind)(void *, void **, const MsvcString *);
static void *parts_car;
static unsigned parts_open;

static int car_has_event(void *car, const char *part, const char *action) {
    unsigned char *animator, *graph;
    void *end, *found = NULL;
    MsvcString key = {{{0}}, 0, 15};
    size_t part_length = text_length(part), action_length = text_length(action);
    if (!car || part_length + 1 + action_length > 15 ||
        !memory_read_pointer((unsigned char *)car + 0x73a8, (void **)&animator) || !animator ||
        !memory_read_pointer(animator + 0x1e0, (void **)&graph) || !graph ||
        !memory_read_pointer(graph + 0x270, &end) || !end)
        return 0;
    memory_copy(key.text, part, part_length);
    key.text[part_length] = ':';
    memory_copy(key.text + part_length + 1, action, action_length);
    key.size = part_length + 1 + action_length;
    ((EventFind)(image + 0x1d08e60))(graph + 0x268, &found, &key);
    return found && found != end;
}

/* PART_ALL is the submenu's Open All / Close All entry, listed when the car
   has at least two animated parts. Headlights (ForzaCar, read each frame
   into the light material packet by 0x1889140) come from +0x7852 while the
   manual flag +0x7851 is set, else from +0x7868. The car update rewrites
   them every frame, so the override is applied just before the packet is
   built: its one call (0x18b8e22) is wrapped. (The packet's hazard and
   indicator inputs, +0x786f..+0x7871, reach the renderer but no FH3 car
   model draws them.) */
enum { LIGHT_PACKET_THUNK = 0x18b2d61 };
enum { PART_ALL = PART_COUNT, LIGHT_HEAD, MENU_ITEMS };
enum { CAR_LIGHTS_MANUAL = 0x7851, CAR_LIGHTS_ON = 0x7852, CAR_LIGHTS_AUTO = 0x7868 };
static void *headlight_car;
static int headlight_override = -1; /* -1 native, else 0 off / 1 on */

static int is_light(int part) {
    return part >= LIGHT_HEAD && part < MENU_ITEMS;
}

static int headlights_on(void *car) {
    if (car == headlight_car && headlight_override >= 0) return headlight_override;
    unsigned char manual, value;
    if (!memory_read((unsigned char *)car + CAR_LIGHTS_MANUAL, &manual, 1) ||
        !memory_read((unsigned char *)car + (manual ? CAR_LIGHTS_ON : CAR_LIGHTS_AUTO), &value, 1))
        return 0;
    return value != 0;
}

static int single_part_supported(void *car, int part) {
    return car_has_event(car, part_names[part], "toggle") ||
           (car_has_event(car, part_names[part], "open") &&
            car_has_event(car, part_names[part], "close"));
}

static int part_supported(void *car, int part) {
    if (is_light(part)) return car != NULL;
    if (part != PART_ALL) return single_part_supported(car, part);
    int count = 0;
    for (int i = 0; i < PART_COUNT; ++i) count += single_part_supported(car, i);
    return count >= 2;
}

static int part_is_open(void *car, int part) {
    if (part == LIGHT_HEAD) return car && headlights_on(car);
    if (car != parts_car) return 0;
    return part == PART_ALL ? parts_open != 0 : (int)(parts_open >> part & 1);
}

/* Submenu order: lights, Open All, then the single parts. */
static const int menu_order[] = {LIGHT_HEAD,   PART_ALL,     PART_ROOF,    PART_HOOD,
                                 PART_TRUNK,   PART_DOOR_LF, PART_DOOR_RF, PART_DOOR_LR,
                                 PART_DOOR_RR};
_Static_assert(sizeof(menu_order) / sizeof(menu_order[0]) == MENU_ITEMS, "every menu item");

static int menu_part(int position) {
    return menu_order[position];
}

/* ForzaCar keeps its world velocity at +0x7600. */
static int car_stationary(void *car) {
    float velocity[3];
    if (!memory_read((unsigned char *)car + 0x7600, velocity, sizeof(velocity))) return 0;
    for (int i = 0; i < 3; ++i)
        if (!float_is_finite(velocity[i])) return 0;
    return velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2] < 0.25f;
}

static void toggle_part(void *component, int part);
static void open_car_submenu(void *component, int part);
static void relabel_car_menu(void *component);
static void anna_toggle_autodrive(void *component, int part);
enum { ANNA_AUTODRIVE, ANNA_CAR, ANNA_PARTS, ANNA_ALL = ANNA_PARTS + PART_ALL };
static AnnaEntry anna_entries[] = {
    [ANNA_AUTODRIVE] = {"EO_Autodrive", L"Autodrive", NULL, anna_toggle_autodrive, 0, -1},
    [ANNA_CAR] = {"EO_Car", L"Car", NULL, open_car_submenu, 0, -1},
    [ANNA_PARTS + PART_ROOF] = {"EO_Roof", L"Open Roof", L"Close Roof", toggle_part, 0, PART_ROOF},
    [ANNA_PARTS + PART_HOOD] = {"EO_Hood", L"Open Hood", L"Close Hood", toggle_part, 0, PART_HOOD},
    [ANNA_PARTS + PART_TRUNK] = {"EO_Trunk", L"Open Trunk", L"Close Trunk", toggle_part, 0,
                                 PART_TRUNK},
    [ANNA_PARTS + PART_DOOR_LF] = {"EO_DoorLF", L"Open Front Left Door",
                                   L"Close Front Left Door", toggle_part, 0, PART_DOOR_LF},
    [ANNA_PARTS + PART_DOOR_RF] = {"EO_DoorRF", L"Open Front Right Door",
                                   L"Close Front Right Door", toggle_part, 0, PART_DOOR_RF},
    [ANNA_PARTS + PART_DOOR_LR] = {"EO_DoorLR", L"Open Rear Left Door", L"Close Rear Left Door",
                                   toggle_part, 0, PART_DOOR_LR},
    [ANNA_PARTS + PART_DOOR_RR] = {"EO_DoorRR", L"Open Rear Right Door",
                                   L"Close Rear Right Door", toggle_part, 0, PART_DOOR_RR},
    [ANNA_ALL] = {"EO_AllParts", L"Open All", L"Close All", toggle_part, 0, PART_ALL},
    [ANNA_PARTS + LIGHT_HEAD] = {"EO_Headlights", L"Headlights On", L"Headlights Off",
                                 toggle_part, 0, LIGHT_HEAD},
};

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
    write_log("TREASURE MAP: unlocked for the current world.\r\n");
}
static volatile LONG anna_autodrive_requested;

static const char *msvc_text(const MsvcString *value) {
    return value->capacity >= 16 ? value->pointer : value->text;
}

/* An open car part is listed as "<name>+": ANNA reuses an option whose name
   it has already labelled, so the suffix makes it relabel Open/Close. */
static int anna_entry_index(const MsvcString *name) {
    const char *text = msvc_text(name);
    size_t size = name->size;
    for (unsigned i = 0; i < ARRAY_COUNT(anna_entries); ++i) {
        const char *own = anna_entries[i].name;
        size_t length = text_length(own);
        int suffixed = anna_entries[i].part >= 0 && size == length + 1 && text[length] == '+';
        if (anna_entries[i].enabled && (size == length || suffixed) &&
            memory_equal(text, own, length))
            return (int)i;
    }
    return -1;
}

/* Replaces both native calls that destroy the consumed name vector, right
   after the native AddOption loop: append ours, then finish natively. */
/* The same rebuild serves the top-level menu and ANNA's follow-up lists. Only
   the top level carries these native commands. */
static int anna_top_level(MsvcString *begin, MsvcString *end) {
    static const char *const markers[] = {"WhatShouldIDo", "RouteToNearest"};
    for (MsvcString *name = begin; name < end; ++name) {
        const char *text = msvc_text(name);
        for (unsigned i = 0; i < ARRAY_COUNT(markers); ++i) {
            size_t length = text_length(markers[i]);
            if (name->size == length && memory_equal(text, markers[i], length)) return 1;
        }
    }
    return 0;
}

static ULONGLONG car_submenu_until;
static const char car_submenu_carrier[] = "StartMatchmakingFor";

static void anna_append_entries(void *begin, void *end, void *names, unsigned char flag) {
    void *menu;
    if (anna_top_level(begin, end) &&
        memory_read_pointer((unsigned char *)names - 0xb8 + 0xf8, &menu)) {
        int parts = anna_entries[ANNA_CAR].enabled && player_car() != NULL;
        for (unsigned i = 0; i < ARRAY_COUNT(anna_entries); ++i) {
            if (!anna_entries[i].enabled || anna_entries[i].part >= 0) continue;
            if (i == ANNA_CAR && !parts) continue;
            MsvcString name = {{{0}}, text_length(anna_entries[i].name), 15};
            memory_copy(name.text, anna_entries[i].name, name.size);
            ((AddOption)(image + 0x10b0da0))(menu, &name);
        }
    }
    ((DestroyNames)(image + 0x2c4660))(begin, end, names, flag);
}

/* UIString text lives at +0x28 (length +0x30). Localized strings borrow it
   from their table (+0x38); without a table the base destructor frees it with
   the UI allocator, sized (length + 1) * 2. The UI allocator's alloc hook is
   0x1edbd00, so literal labels are copied into a buffer from there. */
static void **anna_make_label(void *self, void **output, const MsvcString *name) {
    int index = anna_entry_index(name);
    if (index < 0) return ((MakeLabel)(image + 0x10b9870))(self, output, name);
    ((CreateUIString)(image + 0x1214480))(output);
    unsigned char *label = *output;
    if (label) {
        const wchar_t *text = anna_entries[index].label;
        int part = anna_entries[index].part;
        if (part >= 0 && part_is_open(player_car(), part)) text = anna_entries[index].close_label;
        unsigned length = 0;
        while (text[length]) ++length;
        wchar_t *copy = ((Allocate)(image + 0x1edbd00))((length + 1) * sizeof(wchar_t));
        if (copy) {
            memory_copy(copy, text, (length + 1) * sizeof(wchar_t));
            *(wchar_t **)(label + 0x28) = copy;
            *(unsigned *)(label + 0x30) = length;
        }
    }
    return output;
}

static MsvcString *anna_selected_name(void *component, MsvcString *output) {
    ((HighlightedName)(image + 0xaaa710))(component, output);
    int index = anna_entry_index(output);
    if (index >= 0) {
        write_log("ANNA: custom entry selected.\r\n");
        anna_entries[index].action(component, anna_entries[index].part);
        if (index >= ANNA_CAR) {
            /* Assign (0x245070) the carrier command so ANNA opens a follow-up list. */
            MsvcString carrier = {{{0}}, sizeof(car_submenu_carrier) - 1,
                                  sizeof(car_submenu_carrier) - 1};
            carrier.pointer = (char *)car_submenu_carrier;
            ((void (*)(MsvcString *, const MsvcString *, size_t, size_t))(image + 0x245070))(
                output, &carrier, 0, (size_t)-1);
        } else {
            /* An empty name dispatches nothing natively. */
            ((char *)msvc_text(output))[0] = 0;
            output->size = 0;
        }
    }
    return output;
}

/* Consumed by the next SatNav update, which also runs the cancel checks. */
static void anna_toggle_autodrive(void *component, int part) {
    (void)component;
    (void)part;
    InterlockedExchange(&anna_autodrive_requested, 1);
}

/* The Car submenu borrows a native follow-up list so ANNA keeps it active
   and highlighted: selecting Car dispatches StartMatchmakingFor, whose
   follow-up names land in HUDAnnaComponent's
   vector at +0xb8, which is turned into menu options either by the list
   message handler 0xab3fb0(component, &title) or by the per-frame refresh
   0xab7f60(component, flag). Both are wrapped: while armed, the follow-up
   names are replaced with the car's parts before the native code adds them.
   The buffer is grown with the game allocator (0x1cd05f0; freed by 0x3680c0)
   when the car has more parts than it holds. */
typedef void (*AnnaListHandler)(void *, const MsvcString *);
typedef void (*AnnaRefresh)(void *, unsigned char);

static void open_car_submenu(void *component, int part) {
    (void)component;
    (void)part;
    car_submenu_until = GetTickCount64() + 2000;
}

static void free_game_string(MsvcString *value) {
    if (value->capacity > 15 && value->pointer) ((void (*)(void *))(image + 0x3680c0))(value->pointer);
}

static void replace_follow_up(void *component) {
    unsigned char *names = (unsigned char *)component + 0xb8;
    MsvcString *begin, *end, *capacity;
    if (!car_submenu_until || !memory_read_pointer(names, (void **)&begin) ||
        !memory_read_pointer(names + 8, (void **)&end) ||
        !memory_read_pointer(names + 16, (void **)&capacity) || !begin || begin == end ||
        anna_top_level(begin, end))
        return;
    int armed = GetTickCount64() < car_submenu_until;
    car_submenu_until = 0;
    void *car = player_car();
    unsigned count = 0;
    for (int k = 0; armed && car && k < MENU_ITEMS; ++k) count += part_supported(car, menu_part(k));
    if (!count) return; /* the native follow-up stays */
    for (MsvcString *name = begin; name < end; ++name) free_game_string(name);
    if ((size_t)(capacity - begin) < count) {
        void *heap = ((void *(*)(void))(image + 0x1cd25b0))();
        MsvcString *grown =
            heap ? ((MsvcString * (*)(void *, size_t, unsigned, unsigned))(image + 0x1cd05f0))(
                       heap, count * sizeof(MsvcString), 0, 0)
                 : NULL;
        if (!grown) {
            *(MsvcString **)(names + 8) = begin; /* nothing left to show */
            return;
        }
        ((void (*)(void *))(image + 0x3680c0))(begin);
        begin = grown;
        *(MsvcString **)names = begin;
        *(MsvcString **)(names + 16) = begin + count;
    }
    unsigned used = 0;
    for (int k = 0; k < MENU_ITEMS; ++k) {
        int i = menu_part(k);
        const AnnaEntry *entry = &anna_entries[ANNA_PARTS + i];
        if (!part_supported(car, i)) continue;
        MsvcString *name = begin + used++;
        *name = (MsvcString){{{0}}, text_length(entry->name), 15};
        memory_copy(name->text, entry->name, name->size);
        if (part_is_open(car, i)) name->text[name->size++] = '+';
    }
    *(MsvcString **)(names + 8) = begin + used;
}

/* A part selected inside the list is an answer to the borrowed command, so
   ANNA keeps showing the same list: re-add the options with their new labels.
   AddOption keeps the highlight at the menu's restore index (+0x21c) while its
   restore flag (+0x21a) is set; each option's highlight flag is at +0x40 of
   the items in [[menu+0x210]+0x50]+0x28 -> +0x28. */
static void relabel_car_menu(void *component) {
    void *car = player_car();
    unsigned char *menu, *view, *collection, *list, **items;
    if (!car || !memory_read_pointer((unsigned char *)component + 0xf8, (void **)&menu) || !menu ||
        !memory_read_pointer(menu + 0x210, (void **)&view) || !view ||
        !memory_read_pointer(view + 0x50, (void **)&collection) || !collection ||
        !memory_read_pointer(collection + 0x28, (void **)&list) || !list ||
        !memory_read_pointer(list + 0x28, (void **)&items))
        return;
    int highlighted = 0, count = 0;
    for (int k = 0; k < MENU_ITEMS; ++k) count += part_supported(car, menu_part(k));
    for (int i = 0; items && i < count; ++i) {
        unsigned char *item, selected;
        if (memory_read_pointer(items + i, (void **)&item) && item &&
            memory_read(item + 0x40, &selected, 1) && selected)
            highlighted = i;
    }
    ((void (*)(void *))(image + 0x10b2810))(menu);
    *(int *)(menu + 0x21c) = highlighted;
    menu[0x21a] = 1;
    for (int k = 0; k < MENU_ITEMS; ++k) {
        int i = menu_part(k);
        const AnnaEntry *entry = &anna_entries[ANNA_PARTS + i];
        if (!part_supported(car, i)) continue;
        MsvcString name = {{{0}}, text_length(entry->name), 15};
        memory_copy(name.text, entry->name, name.size);
        if (part_is_open(car, i)) name.text[name.size++] = '+';
        ((AddOption)(image + 0x10b0da0))(menu, &name);
    }
}

static void anna_list_message(void *component, const MsvcString *title) {
    replace_follow_up(component);
    ((AnnaListHandler)(image + 0xab3fb0))(component, title);
}

static void anna_refresh(void *component, unsigned char flag) {
    replace_follow_up(component);
    ((AnnaRefresh)(image + 0xab7f60))(component, flag);
}

/* In free roam the car graph runs its race LOD, whose LODSelect branch has
   no part animations; ForzaVista runs LOD 0 (Car_Autovista). The graph input
   GameInputs:AnimationLOD picks the branch: look it up with 0x21f7490(graph,
   &name) and set it with 0x21f83f0(input, value), as the car does for
   GameInputs:headLights. */
static void *lod_graph, *lod_input;
static ULONGLONG lod_restore_at;

static void close_all_parts(void *car);
static void toggle_light(void *car, int light);
/* Replaces the call to the light packet builder 0x1889140(car, dt). */
static void car_light_packet(void *car, float dt) {
    unsigned char *bytes = car;
    if (car && car == headlight_car && headlight_override >= 0) {
        bytes[CAR_LIGHTS_MANUAL] = 1;
        bytes[CAR_LIGHTS_ON] = (unsigned char)headlight_override;
    }
    ((void (*)(void *, float))(image + 0x1889140))(car, dt);
}

/* ForzaCar's world velocity (+0x7600) above about 1 m/s (3.6 km/h). */
static int car_moving(void *car) {
    float velocity[3];
    if (!memory_read((unsigned char *)car + 0x7600, velocity, sizeof(velocity))) return 0;
    for (int i = 0; i < 3; ++i)
        if (!float_is_finite(velocity[i])) return 0;
    return velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2] > 1.0f;
}

/* Tapping the throttle, or the car moving at all (rolling downhill, pushed,
   driven by the AI), closes every open part; the race LOD (Car_Race_Focus)
   returns once every part has closed. */
static void car_parts_tick(void) {
    if (parts_open && parts_car) {
        void *car = player_car();
        int close = car != parts_car || car_moving(car) || focused_key('W') || focused_key(VK_UP);
        for (DWORD pad = 0; !close && load_pads() && pad < 4; ++pad) {
            PadState state;
            close = get_pad_state(pad, &state) == ERROR_SUCCESS && state.right_trigger > 40;
        }
        if (close) {
            if (car == parts_car) close_all_parts(car);
            else parts_open = 0; /* the car changed; its parts reset with it */
        }
    }
    if (!lod_restore_at || GetTickCount64() < lod_restore_at) return;
    lod_restore_at = 0;
    void *car = player_car();
    unsigned char *animator;
    void *graph;
    if (lod_input && car && memory_read_pointer((unsigned char *)car + 0x73a8, (void **)&animator) &&
        animator && memory_read_pointer(animator + 0x1e0, &graph) && graph == lod_graph)
        ((void (*)(void *, float))(image + 0x21f83f0))(lod_input, 1.0f);
}

static void use_autovista_lod(void *car) {
    unsigned char *animator;
    void *graph;
    if (!memory_read_pointer((unsigned char *)car + 0x73a8, (void **)&animator) || !animator ||
        !memory_read_pointer(animator + 0x1e0, &graph) || !graph)
        return;
    if (graph != lod_graph) {
        static const char name[] = "GameInputs:AnimationLOD";
        MsvcString key = {{{0}}, sizeof(name) - 1, sizeof(name) - 1};
        key.pointer = (char *)name;
        lod_input = ((void *(*)(void *, const MsvcString *))(image + 0x21f7490))(graph, &key);
        lod_graph = graph;
    }
    if (lod_input) ((void (*)(void *, float))(image + 0x21f83f0))(lod_input, 0.0f);
}

static void send_part_event(void *car, int part, int open) {
    int currently = parts_open >> part & 1;
    if (currently == open) return;
    const char *action = car_has_event(car, part_names[part], "toggle") ? "toggle"
                         : open                                       ? "open"
                                                                      : "close";
    char event[16];
    size_t length = text_length(part_names[part]);
    memory_copy(event, part_names[part], length);
    event[length] = ':';
    memory_copy(event + length + 1, action, text_length(action) + 1);
    use_autovista_lod(car);
    ((CarEvent)(image + 0x1889b40))(car, event);
    write_log("CAR PARTS: sent ");
    write_log(event);
    write_log(".\r\n");
    parts_open ^= 1u << part;
    lod_restore_at = parts_open ? 0 : GetTickCount64() + 3000; /* let the close play out */
}

static void close_all_parts(void *car) {
    for (int i = 0; i < PART_COUNT; ++i)
        if (single_part_supported(car, i)) send_part_event(car, i, 0);
}

/* Lights: the headlight override. */
static void toggle_light(void *car, int light) {
    (void)light;
    int on = !headlights_on(car);
    headlight_car = car;
    headlight_override = on;
    write_log(on ? "LIGHTS: headlights on.\r\n" : "LIGHTS: headlights off.\r\n");
}

/* Parts open only while the car is stationary; closing always works. */
static void toggle_part(void *component, int part) {
    void *car = player_car();
    if (!car || !part_supported(car, part)) return;
    if (is_light(part)) {
        toggle_light(car, part);
        relabel_car_menu(component);
        return;
    }
    if (car != parts_car) {
        parts_car = car; /* a new car starts with everything closed */
        parts_open = 0;
    }
    int open = !part_is_open(car, part);
    if (open && !car_stationary(car)) {
        write_log("CAR PARTS: the car must be stationary to open parts.\r\n");
    } else if (part == PART_ALL) {
        if (open) {
            for (int i = 0; i < PART_COUNT; ++i)
                if (single_part_supported(car, i)) send_part_event(car, i, 1);
        } else {
            close_all_parts(car);
        }
    } else {
        send_part_event(car, part, open);
    }
    relabel_car_menu(component);
}

void *fh3_player_car(void) { return player_car(); }

static void satnav_update(void *self, float dt) {
    ((SatNavUpdate)(image + 0x8f7d10))(self, dt);
    if (unlock_treasure_map) treasure_map_tick();
    if (unrestricted_photo) photo_mode_tick();
    camera_fov_tick();
    car_parts_tick();
    fh3_mods_frame(dt);
    if (!autodrive_enabled) return;
    int down = focused_key(toggle_key) || toggle_chord();
    int pressed = (down && !key_was_down) || InterlockedExchange(&anna_autodrive_requested, 0);
    key_was_down = down;
    if (autodrive_car) {
        if (driver_mode(autodrive_car) != 0) {
            /* A native state (event start, reset, car change) took the car back. */
            autodrive_car = NULL;
            cinematic_leave();
            write_log("AUTODRIVE: the game ended the AI drive.\r\n");
            return;
        }
        int input = driving_input();
        if (!input) input_armed = 1;
        if (pressed) autodrive_stop("AUTODRIVE: toggle returned control to the player.\r\n");
        else if (input && input_armed)
            autodrive_stop("AUTODRIVE: driving input returned control to the player.\r\n");
        else {
            if (dt > 0.0f && dt < 1.0f) autodrive_elapsed += dt;
            /* Switch after a few seconds so the start of the drive stays visible. */
            if (autodrive_cinematic && !cinematic_tried && autodrive_elapsed >= 3.0f)
                cinematic_enter();
            else
                cinematic_tick(dt);
        }
        return;
    }
    if (pressed) autodrive_start();
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
    UI_CINEMATIC,
    UI_FPS,
    UI_FPS_GPU,
    UI_FPS_MS,
    UI_FPS_BOX,
    UI_LIVERY_4K, /* video options page from here on */
    UI_LIVERY_MSAA,
    UI_LIVERY_SUPERSAMPLING,
    UI_HUD_HIDDEN, /* A_Hud page */
    UI_MAKE_GRID,  /* PopupMenu: the manufacturer picker's grid layout */
    UI_FRAME_STATS, /* every page (base type 0x4e214e0): the FPS readout */
    UI_FPS_BOX_PAGE, /* every page: whether the readout draws its box */
    UI_COUNT
};
static const char *const ui_names[UI_COUNT] = {
    "EOChaseNearFOV", "EOChaseFarFOV", "EOHoodFOV", "EOBumperFOV", "EOCockpitFOV",
    "EOMinimapFollowsCamera", "EODriftCamera", "EODriftSensitivity", "EODriftSpeed",
    "EODriftRange", "EOAutodriveCinematic", "EOShowFPS", "EOShowGPUUsage", "EOShowFrameTime", "EOFPSBackground",
    "EOLivery4K", "EOLiveryMSAA", "EOLiverySupersampling",
    "EOHudHidden", "EOMakeGrid", "EOFrameStats", "EOFPSBox"};
static UiProperty ui_properties[UI_COUNT];
static UiLink ui_links[UI_COUNT];
static int ui_registered;

/* The HUD (ForzaUI.A_Hud, its own data context) binds LayoutRoot's
   Visibility to EOHudHidden. Its getter records the page so a change can
   raise PropertyChanged the way the native page setters do (0x56d4f0). */
static int hud_hidden;
static void *hud_page, *hud_page_vtable;

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
    if (index == UI_MINIMAP || index == UI_DRIFT || index == UI_CINEMATIC ||
        (index >= UI_FPS && index <= UI_FPS_BOX)) {
        int value = (index == UI_MINIMAP   ? minimap_follows_camera
                     : index == UI_DRIFT   ? drift_camera
                     : index == UI_FPS     ? frame_stats_enabled
                     : index == UI_FPS_GPU ? frame_stats_gpu
                     : index == UI_FPS_MS  ? frame_stats_ms
                     : index == UI_FPS_BOX ? frame_stats_box
                                           : autodrive_cinematic) != 0;
        return ((UiBox)(image + 0x5592b0))(output, &value);
    }
    if (index == UI_FRAME_STATS) return frame_stats_text(output, object);
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
    } else if (index == UI_CINEMATIC) {
        int value;
        if (!memory_read((unsigned char *)boxed + 0x28, &value, sizeof(value))) return;
        autodrive_cinematic = value != 0;
        /* Turning it off mid-drive hands the camera back right away. */
        if (!autodrive_cinematic) cinematic_leave();
        ui_save(L"Vehicle", L"AutodriveCinematic", autodrive_cinematic);
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
        write_log("FPS: GPU usage counters unavailable.\r\n");
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
    write_log("MENUS: jumped to the chosen manufacturer.\r\n");
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
    static const wchar_t heading[] = L"Jump to Manufacturer";
    void *label = make_ui_string(heading, sizeof(heading) / sizeof(heading[0]) - 1);
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
    write_log("MENUS: manufacturer picker opened.\r\n");
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
        write_log("SETTINGS: native property templates unavailable; menu unchanged.\r\n");
        return 0;
    }
    for (unsigned i = 0; i < UI_COUNT; ++i) {
        UiProperty *property = &ui_properties[i];
        memory_copy(property,
                    i == UI_FRAME_STATS                  ? text
                    : i == UI_FPS_BOX_PAGE               ? integer
                    : i >= UI_HUD_HIDDEN                 ? hud
                    : i >= UI_LIVERY_4K                  ? video
                    : i == UI_MINIMAP || i == UI_DRIFT || i == UI_CINEMATIC ||
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
                     : image + (i == UI_HUD_HIDDEN  ? 0x4e246c0
                                : i >= UI_LIVERY_4K ? 0x4e558f0
                                                    : 0x4e247a0);
        ((void (*)(void *, UiLink *))(image + 0x1fe9210))(type, &ui_links[i]);
    }
    ui_registered = 1;
    write_log("SETTINGS: Extra Options properties registered on the HUD and video options.\r\n");
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
    "\r\n<local:TileMenuDataItem x:Key=\"EO_Item_Cinematic\" Heading=\"Autodrive Cinematic Camera\" "
    "Description=\"Extra Options: switch to slow orbiting cinematic shots with the HUD hidden after a few seconds of Autodrive. Driving input brings your camera and HUD back.\" />"
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
    "<local:SpinnerControl Label=\"Autodrive Cinematic Camera\" Tag=\"{StaticResource EO_Item_Cinematic}\" "
    "SelectedIndex=\"{Binding EOAutodriveCinematic}\" ItemsSource=\"{Binding OnOffOptions}\" "
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
static const char hud_controls[] =
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
     "<StackPanel Orientation=\"Horizontal\" DockPanel.Dock=\"Right\"", NULL}};

static void hud_set_hidden(int hidden) {
    hidden = hidden != 0;
    if (hud_hidden == hidden) return;
    hud_hidden = hidden;
    void *vtable;
    if (hud_page && memory_read_pointer(hud_page, &vtable) && vtable == hud_page_vtable)
        ((void (*)(void *, void *))(image + 0x56d4f0))(hud_page, &ui_properties[UI_HUD_HIDDEN]);
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
    unsigned capacity =
        (unsigned)(size + text_length(page->resources) + text_length(page->controls) + 1);
    void *replacement = NULL;
    ((UiMakeBuffer)(image + 0x208f320))(&replacement, capacity);
    char *data;
    if (!replacement || !memory_read_pointer((unsigned char *)replacement + 0x28, (void **)&data))
        return;
    unsigned length = ui_extend(page, bytes, (unsigned)size, data, capacity);
    if (!length) {
        ui_release(replacement);
        write_log("SETTINGS: ");
        write_log(page->name);
        write_log(" layout did not match; original page kept.\r\n");
        return;
    }
    *(int *)((unsigned char *)replacement + 0x30) = (int)length;
    *(void **)((unsigned char *)loader + 0x38) = replacement;
    ui_release(buffer);
    write_log("SETTINGS: Extra Options extended the ");
    write_log(page->name);
    write_log(".\r\n");
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

static unsigned anna_prepare(Site *sites) {
    static const unsigned rebuilds[] = {0xab4071, 0xab8002}, labels[] = {0x10b0e39},
                          selects[] = {0xa01e2d};
    unsigned count = redirect_calls(sites, 0x1001612, 0x2c4660, rebuilds, 2, anna_append_entries);
    count += redirect_calls(sites + count, 0x10019f3, 0x10b9870, labels, 1, anna_make_label);
    count += redirect_calls(sites + count, 0x1002c93, 0xaaa710, selects, 1, anna_selected_name);
    if (anna_entries[ANNA_CAR].enabled) {
        static const unsigned packets[] = {0x18b8e22};
        count += redirect_calls(sites + count, LIGHT_PACKET_THUNK, 0x1889140, packets, 1,
                                car_light_packet);
        static const unsigned refreshes[] = {0xab2a72};
        count += redirect_calls(sites + count, 0xa9fe11, 0xab7f60, refreshes, 1, anna_refresh);
        /* The list handler is registered by address: lea rax, [rip + handler]. */
        Site jump = {0xaa0101, 12, {0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
                     {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}, 0};
        uintptr_t handler = (uintptr_t)anna_list_message;
        memory_copy(jump.replacement + 2, &handler, sizeof(handler));
        sites[count++] = jump;
        Site lea = {0xa9c1e7, 7, {0x48,0x8d,0x05}, {0x48,0x8d,0x05}, 0};
        int32_t native = (int32_t)(0xab3fb0 - (0xa9c1e7 + 7)),
                redirected = (int32_t)(0xaa0101 - (0xa9c1e7 + 7));
        memory_copy(lea.original + 3, &native, sizeof(native));
        memory_copy(lea.replacement + 3, &redirected, sizeof(redirected));
        sites[count++] = lea;
    }
    return count;
}

int fh3_initialize(unsigned title_id, unsigned char *game, const wchar_t *executable,
                   const wchar_t *directory) {
    if (title_id != FH3_TITLE_ID) return 0;
    if (!open_log(directory) || !path_join(ini_path, directory, MOD_INI_NAME))
        return rejected("REFUSED: cannot resolve the FH3 settings path. No FH3 patches applied.");
    write_log("TITLE: FH3 (4CE1DFAB) 1.0.125.0; independent title profile.\r\n");
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!memory_read(game, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 64 || dos.e_lfanew > 4096 ||
        !memory_read(game + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != FH3_TIMESTAMP ||
        nt.OptionalHeader.SizeOfImage != FH3_IMAGE_SIZE || !readable_image(game) ||
        !verify_file(executable))
        return rejected("REFUSED: unsupported or modified FH3 executable; only the unmodified "
                        "1.0.125.0 build is supported. No FH3 patches applied.");
    if (!option(L"Global", L"Enabled", 1)) {
        status_set("OK: FH3 Extra Options disabled in FH3_ExtraOptions.ini.");
        write_log(status_get());
        write_log("\r\n");
        return 1;
    }
    image = game;
    fh3_mods_configure(executable);
    static Site sites[128]; /* static: too large for the stack without __chkstk */
    unsigned count = 0;
    if (option(L"Frontend", L"SkipIntro", 1)) {
        /* The logo parade is the 6 s legal screen followed by the 21 s
           T10/Microsoft Studios video. Leave the video entry uncounted so the
           parade ends after the legal screen through its normal completion
           path. (Taking the developer no-parade branch instead hangs boot on
           the legal screen.) */
        sites[count++] = (Site){0x1729355, 2, {0xff,0xc6}, {0x66,0x90}, 0};
        write_log("INTRO: T10/Microsoft Studios startup video skipped; legal screen retained.\r\n");
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
        write_log("VIP: VIP license refresh and Main/IsVip reads forced on.\r\n");
    }
    autodrive_enabled = option(L"Vehicle", L"Autodrive", 1);
    unlock_treasure_map = option(L"Content", L"UnlockTreasureMap", 0);
    for (unsigned i = 0; i < ARRAY_COUNT(camera_fovs); ++i) {
        wchar_t value[32];
        GetPrivateProfileStringW(L"Camera", camera_fovs[i].key, L"0", value, ARRAY_COUNT(value),
                                 ini_path);
        camera_fovs[i].value = parse_degrees(value);
        camera_fov_used |= camera_fovs[i].value != 0.0f;
    }
    if (camera_fov_used) write_log("CAMERA: custom field of view applied.\r\n");
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
            write_log("HUD: minimap turns with the right-stick look camera.\r\n");
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
        if (drift_camera) write_log("CAMERA: drift camera follows the slide on chase cameras.\r\n");
    }
    if (in_game_menu || autodrive_enabled) {
        static const unsigned loads[] = {0x21db70c};
        count += redirect_calls(sites + count, 0x1004542, 0x21dd6c0, loads, 1, ui_load_resource);
        write_log("SETTINGS: settings rows and HUD scene extensions scheduled.\r\n");
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
        write_log(photo_no_collision
                      ? "PHOTO MODE: boundary and height limits removed; collision off.\r\n"
                      : "PHOTO MODE: boundary and height limits removed; collision kept.\r\n");
    if (autodrive_enabled || unlock_treasure_map || unrestricted_photo || camera_fov_used ||
        in_game_menu || option(L"Vehicle", L"CarPartsInAnna", 1) || fh3_mods_enabled()) {
        /* SatNavComponent vtable slot 2 is its per-frame update. */
        Site slot = {0x4378bd0, 8, {0}, {0}, 0};
        uintptr_t original = (uintptr_t)(image + 0x8f7d10), callback = (uintptr_t)satnav_update;
        memory_copy(slot.original, &original, sizeof(original));
        memory_copy(slot.replacement, &callback, sizeof(callback));
        sites[count++] = slot;
    }
    if (autodrive_enabled) {
        read_autodrive_bindings();
        autodrive_cinematic = option(L"Vehicle", L"AutodriveCinematic", 1);
        write_log("AUTODRIVE: AutodriveKey or AutodriveButtons toggles the AI driver; throttle, "
                  "brake or steering cancels.\r\n");
        anna_entries[ANNA_AUTODRIVE].enabled = option(L"Vehicle", L"AutodriveInAnna", 1);
    }
    if (option(L"Vehicle", L"CarPartsInAnna", 1)) {
        for (unsigned i = ANNA_CAR; i < ARRAY_COUNT(anna_entries); ++i) anna_entries[i].enabled = 1;
        write_log("CAR PARTS: roof, hood, trunk and doors in the ANNA Car submenu.\r\n");
    }
    if (unlock_treasure_map)
        write_log("TREASURE MAP: unlocks once per world after it has loaded.\r\n");
    int anna_used = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(anna_entries); ++i) anna_used |= anna_entries[i].enabled;
    if (anna_used) {
        count += anna_prepare(sites + count);
        write_log("ANNA: custom menu entries enabled.\r\n");
    }
    livery_4k = option(L"Livery", L"Resolution4K", 1);
    livery_msaa4 = GetPrivateProfileIntW(L"Livery", L"MSAA", 4, ini_path) == 4;
    livery_supersampling = option(L"Livery", L"Supersampling", 0);
    int livery_samples = livery_msaa4 ? 4 : 2;
    count += livery_prepare(sites + count, livery_4k, livery_samples, livery_supersampling);
    if (livery_4k || livery_samples == 4 || livery_supersampling) {
        write_log(livery_4k ? "LIVERY: 4096x4096 livery textures" : "LIVERY: native livery size");
        write_log(livery_samples == 4 ? ", 4x MSAA" : ", native 2x MSAA");
        write_log(livery_supersampling ? ", supersampled.\r\n" : ".\r\n");
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
    write_log(frame_stats_enabled ? "FPS: frame rate readout in the top-right corner of the HUD and menus.\r\n"
                                  : "FPS: frame rate readout off (Settings > HUD and Gameplay).\r\n");
    if (option(L"Frontend", L"OnlineTabs", 1)) {
        count += redirect_calls(sites + count, TILE_STATE_THUNK, TILE_STATE_SET, online_tab_calls,
                                ARRAYSIZE(online_tab_calls), tile_state_set);
        write_log("MENUS: Social, Clubs, Rivals and Marketplace pause tabs stay enabled offline.\r\n");
    }
    manufacturer_jump = option(L"Frontend", L"ManufacturerJump", 1);
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
        write_log("MENUS: View or Backspace on a car list opens Jump to Manufacturer.\r\n");
    media_overrides = option(L"Files", L"MediaOverrides", 1);
    if (media_overrides || fh3_mods_enabled()) {
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
        write_log("FILES: mediaoverride and mediapc mounts scheduled after native file-system "
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
    /* No DLC option: car packs are rebuilt from store licenses on every boot,
       and Azimuth already reports them as owned. */
    if (!apply(game, sites, count))
        return rejected("REFUSED: FH3 patch bytes or page protections changed; no FH3 patches "
                        "applied.");
#ifdef FH2_AZIMUTH
    if (option(L"Compatibility", L"DisableAzimuthSaveChecksums", 1)) {
        /* Host-side: Azimuth keeps parsing and writing saves natively; only the
           final size/hash rejection is skipped, and revisions copied in without
           a commit manifest get one built from their actual files. */
        int result = azimuth_disable_save_checks();
        write_log(result > 0   ? "AZIMUTH: save checksum and file-size comparisons disabled.\r\n"
                  : result < 0 ? "AZIMUTH: save checksum override could not be applied.\r\n"
                               : "AZIMUTH: save checksum override skipped; unsupported host build.\r\n");
        if (result > 0)
            azimuth_repair_title_markers((unsigned char *)GetModuleHandleW(NULL), FH3_TITLE_ID,
                                         write_log);
    }
#endif
    status_set("OK: FH3 Extra Options initialized (4CE1DFAB); FH2 features not loaded.");
    write_log(status_get());
    write_log("\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh3TestInitialize(unsigned title, void *game, const wchar_t *path,
                                            const wchar_t *directory) {
    return fh3_initialize(title, game, path, directory);
}
__declspec(dllexport) void Fh3TestProtectionFailure(int index) { fail_protection = index; }
#endif
