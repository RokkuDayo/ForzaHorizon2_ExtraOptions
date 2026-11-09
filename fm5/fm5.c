#include "shared/boot_diagnostics.h"
#include "fm5/fm5.h"
#include "fm5/fm5_patch.h"
#include "fm5/fm5_mods.h"
#include "fm5/fm5_camera.h"
#include "fm5/fm5_manufacturer.h"
#include "fm5/fm5_video.h"
#include "fm5/fm5_audio.h"
#include "fm5/fm5_vehicle.h"
#include "fm5/fm5_ui.h"
#include "fm5/fm5_driver.h"
#include "fm5/fm5_badge.h"
#include "fm5/fm5_start.h"
#include "fm5/fm5_designs.h"
#include "shared/sha256.h"
#include "shared/support.h"
#ifdef FH2_AZIMUTH
#include "shared/adapters/azimuth_markers.h"
#endif

typedef struct {
    const char *data;
    uintptr_t reserved;
    size_t length, capacity;
} GameString;
typedef unsigned char (*InitializeFiles)(void *);
typedef int (*LoadProfile)(void *, void *, unsigned);
typedef void (*SetVip)(void *, unsigned char);
typedef void *(*ProfileData)(void *);
typedef void *(*Allocate)(size_t);
typedef void *(*ConstructVolume)(void *, const GameString *, unsigned char);
typedef void (*Reference)(void *);
typedef unsigned char (*Mount)(void *, const GameString *, void **, unsigned,
                               unsigned char, unsigned char, const GameString *);
typedef void (*Sort)(void *);
typedef Fm5Site Site;

static unsigned char *image;
static int override_folders_enabled;
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

static int rejected(const char *reason) {
    status_set(reason);
    write_log(reason);
    write_log("\r\n");
    return 0;
}

static int option(const wchar_t *section, const wchar_t *key) {
    int default_value=CompareStringOrdinal(key,-1,L"E3StartScreen",-1,FALSE)!=CSTR_EQUAL;
    wchar_t value[16];
    GetPrivateProfileStringW(section, key, L"", value, ARRAY_COUNT(value), ini_path);
    /* Accept the previous FM5 spelling while publishing FH2's canonical name. */
    if(!value[0] && CompareStringOrdinal(key,-1,L"EnableMediaOverrides",-1,FALSE)==CSTR_EQUAL)
        GetPrivateProfileStringW(section,L"MediaOverrides",L"1",value,ARRAY_COUNT(value),ini_path);
    if(!value[0]){value[0]=default_value?L'1':L'0';value[1]=0;}
    if (value[0] == L'0' && !value[1]) return 0;
    if (value[0] == L'1' && !value[1]) return 1;
    write_log("CONFIG: invalid boolean; expected 0 or 1, using feature default.\r\n");
    return default_value;
}

static int verify_file_impl(const wchar_t *path) {
    static const unsigned char expected[32] = {
        0x94,0xda,0xd2,0x18,0x51,0xd3,0x35,0xe9,0xa3,0x32,0x31,0xdc,0x2a,0x1d,0xe2,0xec,
        0xd4,0xb9,0x3e,0xef,0xdf,0x5f,0x67,0x72,0x08,0x5b,0xf6,0x88,0x6b,0xf4,0x7d,0xb7};
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER size;
    int valid = GetFileSizeEx(file, &size) && size.QuadPart == 39317504;
    Sha256Context hash;
    sha256_initialize(&hash);
    DWORD count;
    uint64_t total = 0;
    while (valid && total < 39317504) {
        valid = ReadFile(file, file_buffer, sizeof(file_buffer), &count, NULL) && count;
        if (valid) {
            sha256_update(&hash, file_buffer, count);
            total += count;
        }
    }
    CloseHandle(file);
    unsigned char digest[32];
    sha256_finalize(&hash, digest);
    return valid && total == 39317504 && memory_equal(digest, expected, sizeof(expected));
}

static int verify_file(const wchar_t *path){
    uint64_t begin=boot_diag_now();int result=verify_file_impl(path);
    boot_diag_end("boot.verify_game_executable",NULL,begin);return result;
}

static GameString borrowed(const char *text) {
    return (GameString){text, 0, text_length(text), 31};
}

static int readable_image(unsigned char *base) {
    uintptr_t cursor = (uintptr_t)base, end = cursor + FM5_IMAGE_SIZE;
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

static int mount_folder(void *filesystem, const char *folder, unsigned priority) {
    void *storage = ((Allocate)(image + 0x1cdbb0))(0x50);
    if (!storage) return 0;
    GameString source = borrowed(folder), destination = borrowed("game:\\media\\");
    void *volume = ((ConstructVolume)(image + 0x794a50))(storage, &source, 1);
    if (!volume) return 0;
    void **methods = *(void ***)volume;
    ((Reference)methods[0x08 / 8])(volume);
    void **fs_methods = *(void ***)filesystem;
    /* FM5's native CMultiVolume retains the reference and owns all subsequent
       reads, async work and destruction. Zero flags disable hash.manifest
       filtering, so files newly added to these folders are visible too. */
    int success = ((Mount)fs_methods[0x1a8 / 8])(filesystem, &destination, &volume,
                                               priority, 0, 0, NULL);
    ((Reference)methods[0x10 / 8])(volume);
    return success;
}

static unsigned char initialize_files(void *filesystem) {
    unsigned char result = ((InitializeFiles)(image + 0x5fe0e0))(filesystem);
    if (!result) {
        write_log("FILES: native initialization failed; override mounts skipped.\r\n");
        return result;
    }
    int override = !override_folders_enabled || mount_folder(filesystem, "game:\\mediaoverride\\", 0x7ffffffe);
    int pc = !override_folders_enabled || mount_folder(filesystem, "game:\\mediapc\\", 0x7ffffffd);
    ((Sort)(*(void ***)filesystem)[0x1b8 / 8])(filesystem);
    fm5_mods_files(filesystem);
    fm5_mods_builtin_files(image);
    fm5_badge_files(filesystem);
    write_log(override && pc ? "FILES: native mounts ready: mediaoverride > mediapc > original media.\r\n"
                            : "FILES: an override mount failed; native asset fallback retained.\r\n");
    return result;
}

static int load_profile(void *profile, void *owner, unsigned mode) {
    int result = ((LoadProfile)(image + 0x1a0490))(profile, owner, mode);
    if (result == 0) {
        /* This call is the local user's successful profile-load boundary. The
           network VIP callback need not run when playing offline. Synchronize
           the stored Main/IsVip value AND the native gamer-data flag before
           the caller publishes them to badge and reward consumers. */
        void *manager = *(void **)((unsigned char *)profile + 0x48);
        void *data = ((ProfileData)(*(void ***)manager)[0xe8 / 8])(manager);
        unsigned char flags = *((unsigned char *)data + 0x228);
        ((SetVip)(image + 0x1a12f0))(profile, (unsigned char)(flags | 1));
        write_log("VIP: local profile loaded; native VIP value and gamer flag synchronized.\r\n");
    }
    return result;
}

/* Startup-only transaction: check every enabled site before making any changes,
   acquire all write permissions first, and restore original bytes on failure. */
static int apply(Site *sites, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        unsigned char actual[16];
        if (sites[i].size > sizeof(actual) || !memory_read(image + sites[i].rva, actual, sites[i].size) ||
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
    int success = FlushInstructionCache(GetCurrentProcess(), image, FM5_IMAGE_SIZE) != 0;
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
        FlushInstructionCache(GetCurrentProcess(), image, FM5_IMAGE_SIZE);
        for (unsigned i = count; i; --i) {
            Site *site = sites + i - 1;
            DWORD unused;
            VirtualProtect(image + site->rva, site->size, site->protection, &unused);
        }
    }
    return success;
}

int fm5_initialize(unsigned title_id, unsigned char *game, const wchar_t *executable,
                   const wchar_t *directory) {
    if (title_id != FM5_TITLE_ID) return 0;
    if (!path_join(log_path, directory, L"FM5_ExtraOptions.log") ||
        !path_join(ini_path, directory, L"FM5_ExtraOptions.ini")) return 0;
    HANDLE log = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    write_log("Forza Motorsport 5 Extra Options " MOD_VERSION "\r\nTITLE: FM5 (7B35D917); independent title profile.\r\n");
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!memory_read(game, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 64 || dos.e_lfanew > 4096 ||
        !memory_read(game + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.TimeDateStamp != FM5_TIMESTAMP || nt.OptionalHeader.SizeOfImage != FM5_IMAGE_SIZE ||
        !readable_image(game) || !verify_file(executable))
        return rejected("REFUSED: unsupported or modified FM5 executable; no FM5 patches applied.");
    if (!option(L"Global", L"Enabled")) {
        status_set("OK: FM5 Extra Options disabled in FM5_ExtraOptions.ini.");
        write_log(status_get());
        write_log("\r\n");
        return 1;
    }
    image = game;
    static Site sites[192];
    unsigned count = 0;
    if (option(L"Frontend", L"SkipIntro")) {
        /* Follow all three native DisableSplashIntro=true paths, including the
           normal completion notification. The press-start screen is retained. */
        sites[count++] = (Site){0x13a345, 6, {0x0f,0x85,0x48,3,0,0}, {0xe9,0x49,3,0,0,0x90}, 0};
        sites[count++] = (Site){0x13a82c, 2, {0x75,0x0b}, {0xeb,0x0b}, 0};
        sites[count++] = (Site){0x13b9d5, 2, {0x74,0x2d}, {0x90,0x90}, 0};
        /* The early renderer installs its own legal background before the
           splash queue exists. Use its native no-intro loading background;
           keep renderer creation and all startup work after it intact. */
        sites[count++] = (Site){0x13c59f, 2, {0x75,0x5e}, {0xeb,0x5e}, 0};
        write_log("INTRO: splash intro and early legal disclaimer skip enabled.\r\n");
    }
    if(option(L"Frontend",L"EnableMarketplace")){
        /* Enable the UI property and its matching button action only. Never
           forge global connectivity, catalog responses or purchase results. */
        sites[count++]=(Site){.rva=0x362bf4,.size=7,
            .original={0x0f,0xb6,0x05,0x73,0x09,0x27,0x02},
            .replacement={0xb8,1,0,0,0,0x90,0x90}};
        sites[count++]=(Site){.rva=0x366025,.size=2,
            .original={0x74,0x09},.replacement={0x90,0x90}};
        write_log("MARKETPLACE: main-menu tile and native entry action enabled offline.\r\n");
    }
    if (option(L"Compatibility", L"DisableParentalLocks")) {
        /* CGamerProfile's UGC (247) and multiplayer (254) requests store
           their results in distinct fields. Use the native Allowed value. */
        sites[count++] = (Site){0x7d5670, 10,
            {0x48,0x89,0x4c,0x24,0x08,0x57,0x48,0x83,0xec,0x30},
            {0xc7,0x41,0x40,4,0,0,0,0xc3,0x90,0x90}, 0};
        sites[count++] = (Site){0x7d57e0, 10,
            {0x48,0x89,0x4c,0x24,0x08,0x57,0x48,0x83,0xec,0x30},
            {0xc7,0x41,0x3c,4,0,0,0,0xc3,0x90,0x90}, 0};
        write_log("PARENTAL: local UGC and multiplayer privileges allowed.\r\n");
    }
    if (option(L"Content", L"OfflineVIP")) {
        /* Preserve the incoming gamer flags in EBP while enabling VIP bit 0.
           ESI is the separate boolean used by the native profile-value setter. */
        sites[count++] = (Site){0x1a130e, 13,
            {0x0f,0xb6,0xea,0x48,0x8b,0xf9,0x0f,0xb6,0xf2,0x40,0x80,0xe6,1},
            {0x89,0xd5,0x83,0xcd,1,0x48,0x8b,0xf9,0xbe,1,0,0,0}, 0};
        Site thunk = {0x22e9f2, 12,
            {0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
            {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}, 0};
        uintptr_t callback = (uintptr_t)load_profile;
        memory_copy(thunk.replacement + 2, &callback, sizeof(callback));
        sites[count++] = thunk;
        Site call = {0x22ea79, 5, {0xe8,0x12,0x1a,0xf7,0xff}, {0xe8,0,0,0,0}, 0};
        int32_t delta = (int32_t)(0x22e9f2 - 0x22ea79 - 5);
        memory_copy(call.replacement + 1, &delta, sizeof(delta));
        sites[count++] = call;
        write_log("VIP: native VIP setter and offline profile-load synchronization enabled.\r\n");
    }
    unsigned mod_sites=option(L"Mods",L"Enabled")?fm5_mods_prepare(game,sites+count,executable,directory,option(L"Mods",L"DatabaseCache")):0;
    count+=mod_sites;override_folders_enabled=option(L"Files",L"EnableMediaOverrides");
    {
        Site thunk = {0x5cf963, 12, {0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
                       {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}, 0};
        uintptr_t callback = (uintptr_t)initialize_files;
        memory_copy(thunk.replacement + 2, &callback, sizeof(callback));
        sites[count++] = thunk;
        Site call = {0x5cfc68, 5, {0xe8,0x73,0xe4,2,0}, {0xe8,0,0,0,0}, 0};
        int32_t delta = (int32_t)(0x5cf963 - 0x5cfc68 - 5);
        memory_copy(call.replacement + 1, &delta, sizeof(delta));
        sites[count++] = call;
        write_log("FILES: built-in assets and configured override mounts scheduled after native file-system initialization.\r\n");
    }
    if (option(L"Frontend", L"AllRimsCategory")) {
        count += fm5_rims_prepare(game, sites + count);
        write_log("RIMS: all aftermarket and factory rim categories enabled; native donor assets.\r\n");
    }
    count += fm5_camera_prepare(game, sites + count, directory, 1, 1, write_log);
    write_log("CAMERA: configured drift/FOV hooks and Video cockpit motion-blur control enabled.\r\n");
    fm5_manufacturer_prepare(game,NULL,write_log);
    if(option(L"Frontend",L"JumpToManufacturer")){
        count+=fm5_manufacturer_prepare(game,sites+count,write_log);
        write_log("MANUFACTURERS: Native manufacturer grid enabled in all car selectors and Wheel Styles.\r\n");
    }
    fm5_camera_menu_hide_level_faster(option(L"Frontend",L"HideLevelFasterNotification"));
    fm5_camera_menu_hide_drivatar_designs(option(L"Frontend",L"HideDrivatarDesignsButton"));
    fm5_start_configure(option(L"Frontend",L"E3StartScreen"));
    count += fm5_camera_menu_prepare(game, sites + count, 1, 1, write_log);
    count += fm5_vehicle_prepare(game,sites+count,option(L"Camera",L"Headlights"),ini_path);
    count += fm5_video_prepare(game, sites + count, directory, write_log);
    count += fm5_audio_prepare(game,sites+count);
    count += fm5_ui_prepare(game, sites + count, ini_path);
    count += fm5_driver_prepare(game,sites+count,mod_sites!=0);
    count += fm5_badge_prepare(game,sites+count,directory);
    count += fm5_designs_prepare(game,sites+count,write_log);
    unsigned livery_sites = 0;
    int local_liveries = GetPrivateProfileIntW(L"Experiments", L"LocalDrivatarLiveries", 0, ini_path) != 0;
    if (local_liveries) {
        int maximum = (int)GetPrivateProfileIntW(L"Experiments", L"MaxDrivatarLiveries", 6, ini_path);
        if (maximum > 0)
            livery_sites = fm5_local_liveries_prepare(game, sites + count, directory, (unsigned)maximum, write_log);
        count += livery_sites;
    }
    if (!apply(sites, count))
        return rejected("REFUSED: FM5 patch bytes or page protections changed; no FM5 patches applied.");
    if (local_liveries)
        write_log(livery_sites ? "LOCAL DESIGNS: FM5 local Drivatar liveries enabled.\r\n"
                               : "LOCAL DESIGNS: FM5 local Drivatar liveries unavailable. Check the lineup cap and save path.\r\n");
#ifdef FH2_AZIMUTH
    if (option(L"Compatibility", L"GenerateMissingSaveCommits")){
        uint64_t begin=boot_diag_now();azimuth_repair_title_markers((unsigned char *)GetModuleHandleW(NULL), FM5_TITLE_ID, write_log);
        boot_diag_end("boot.generate_missing_save_commits",NULL,begin);
    }
#endif
    status_set("OK: FM5 Extra Options initialized (7B35D917); FH2 features not loaded.");
    write_log(status_get());
    write_log("\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fm5TestInitialize(unsigned title, void *game, const wchar_t *path,
                                         const wchar_t *directory) {
    return fm5_initialize(title, game, path, directory);
}
__declspec(dllexport) void Fm5TestProtectionFailure(int index) { fail_protection = index; }
#endif
