#include "addon_archives.h"
#include "addons.h"
#include "features.h"
#include "logging.h"
#include "support.h"
#include "carbin_patch.h"
#include "addon_database.h"
#include "sha256.h"
#include "media_files.h"
#include "plugins.h"

typedef struct {
    const char *data;
    uintptr_t reserved;
    size_t length, capacity;
} BorrowedGameString;

typedef unsigned char (*ResolveFile)(void *, void **, void *, const void *, unsigned char,
                                     unsigned char, unsigned char, void **, unsigned char);
typedef void *(*Allocate)(size_t);
typedef void *(*ConstructVolume)(void *, const void *, unsigned char);
typedef void (*Reference)(void *);
typedef unsigned char (*MountVolume)(void *, const void *, void **, unsigned, unsigned char,
                                     unsigned char, const void *);
typedef void (*SortVolumes)(void *);
typedef unsigned char (*FileExists)(void *, const BorrowedGameString *);

static unsigned char *image;
static ResolveFile original_resolve;
static Allocate allocate;
static ConstructVolume construct_volume;
static FileExists original_exists;
static void *folder_vtable[1 + 0x1f0 / sizeof(void *)];
static volatile LONG mount_state, mount_thread;
static SRWLOCK mount_lock = SRWLOCK_INIT;
static int overrides_enabled;
static void *carbin_volume, *priority_volumes[2];
static void *volatile media_filesystem;
static volatile LONG absolute_requests;
static wchar_t *carbin_cached_path(void *volume, const BorrowedGameString *path);
static void carbin_reset(void);

static struct {
    void *volume;
    unsigned addon;
} addon_volumes[128];

static unsigned addon_volume_count, archive_revision;
static unsigned char original_pointer[8], callback_pointer[8];
static const unsigned char *const accepted[] = {original_pointer, callback_pointer};

static BorrowedGameString borrowed_string(const char *text) {
    return (BorrowedGameString){text, 0, text_length(text), 31};
}

static int volume_addon(void *volume) {
    for (unsigned i = 0; i < addon_volume_count; ++i) {
        if (addon_volumes[i].volume == volume) {
            return (int)addon_volumes[i].addon;
        }
    }
    return -1;
}

static const char *string_text(const BorrowedGameString *path) {
    return path->capacity < 16 ? (const char *)path : path->data;
}

static int same_path(const char *text, size_t length, const char *expected) {
    for (size_t i = 0; i < length; ++i) {
        char a = text[i], b = expected[i];
        if (!b) {
            return 0;
        }
        if (a == '/') {
            a = '\\';
        }
        if (a >= 'A' && a <= 'Z') {
            a += 'a' - 'A';
        }
        if (b >= 'A' && b <= 'Z') {
            b += 'a' - 'A';
        }
        if (a != b) {
            return 0;
        }
    }
    return !expected[length];
}

static int safe_relative(const char *text, size_t length) {
    if (!length || length > 32767) {
        return 0;
    }
    size_t component = 0;
    for (size_t i = 0; i <= length; ++i) {
        unsigned char ch = i < length ? (unsigned char)text[i] : 0;
        if (i == length || ch == '\\' || ch == '/') {
            if (i == component || (i - component == 1 && text[component] == '.') ||
                (i - component == 2 && text[component] == '.' && text[component + 1] == '.') ||
                text[i - 1] == '.' || text[i - 1] == ' ') {
                return 0;
            }
            component = i + 1;
        } else if (ch < 32 || ch == ':' || ch == '<' || ch == '>' || ch == '*' || ch == '?' ||
                   ch == '"' || ch == '|') {
            return 0;
        }
    }
    return !same_path(text, length, "stripped\\gamedbRC.slt") &&
           !same_path(text, length, "zipmanifest.xml");
}

static wchar_t *physical_path(const wchar_t *folder, const char *text, size_t length) {
    size_t prefix = 0;
    while (folder[prefix]) {
        ++prefix;
    }
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0);
    if (!size || prefix + (unsigned)size + 14 >= PATH_CAPACITY) {
        return NULL;
    }
    wchar_t *physical = HeapAlloc(GetProcessHeap(), 0, (prefix + size + 14) * sizeof(wchar_t));
    if (!physical) {
        return NULL;
    }
    memory_copy(physical, folder, prefix * sizeof(wchar_t));
    physical[prefix++] = L'\\';
    int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length,
                                        physical + prefix, size);
    physical[prefix + converted] = 0;
    for (size_t i = prefix; i < prefix + (unsigned)converted; ++i) {
        if (physical[i] == L'/') {
            physical[i] = L'\\';
        }
    }
    DWORD attributes = converted ? GetFileAttributesW(physical) : INVALID_FILE_ATTRIBUTES;
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        HeapFree(GetProcessHeap(), 0, physical);
        return NULL;
    }
    return physical;
}

static wchar_t *addon_file_path(unsigned addon, const BorrowedGameString *path) {
    if (!addons_media_active(addon)) {
        return NULL;
    }
    const char *text = string_text(path);
    size_t length = path->length;
    if (!safe_relative(text, length)) {
        return NULL;
    }
    const wchar_t *folder = addons_media_folder(addon);
    if (!folder) {
        return NULL;
    }
    wchar_t *physical=physical_path(folder,text,length);
    return physical?physical:plugins_media_file(addon,text,length);
}

char *media_override_video_path(const char *filename) {
    size_t length = text_length(filename);
    if (length < 11 || !same_path(filename + length - 4, 4, ".bk2")) {
        return NULL;
    }
    const char *relative = filename;
    if (length > 6 && same_path(filename, 6, "game:\\")) {
        relative += 6;
        length -= 6;
    }
    while (length && (*relative == '/' || *relative == '\\')) {
        ++relative;
        --length;
    }
    if (length <= 6 || !same_path(relative, 6, "media\\") ||
        !safe_relative(relative + 6, length - 6)) {
        return NULL;
    }
    relative += 6;
    length -= 6;
    unsigned count = addons_count();
    wchar_t *physical = NULL;
    if (overrides_enabled || count) {
        wchar_t *folder = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
        const wchar_t *root = addons_content_directory();
        if (folder && root[0] && path_join(folder, root, L"mediaoverride")) {
            physical = physical_path(folder, relative, length);
        }
        if (folder && !physical && root[0] && path_join(folder, root, L"mediapc")) {
            physical = physical_path(folder, relative, length);
        }
        if (folder) {
            HeapFree(GetProcessHeap(), 0, folder);
        }
    }
    for (unsigned i = count; !physical && i; --i) {
        BorrowedGameString path = {relative, 0, length, 31};
        physical = addon_file_path(i - 1, &path);
    }
    if (!physical) {
        return NULL;
    }
    int bytes =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, physical, -1, NULL, 0, NULL, NULL);
    char *result = bytes ? HeapAlloc(GetProcessHeap(), 0, (size_t)bytes) : NULL;
    if (result) {
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, physical, -1, result, bytes, NULL, NULL);
    }
    HeapFree(GetProcessHeap(), 0, physical);
    return result;
}

typedef DWORD (*FileAttributes)(void *, const BorrowedGameString *);
static FileAttributes original_attributes;

static void *absolute_media_filesystem(void *volume, const BorrowedGameString *path) {
    const char prefix[] = "game:\\media\\";
    if (path->length <= sizeof(prefix) - 1 ||
        !same_path(string_text(path), sizeof(prefix) - 1, prefix) ||
        (volume != carbin_volume && volume != priority_volumes[0] &&
         volume != priority_volumes[1] && volume_addon(volume) < 0)) {
        return NULL;
    }
    return InterlockedCompareExchangePointer(&media_filesystem, NULL, NULL);
}

static DWORD addon_file_attributes(void *volume, const BorrowedGameString *path) {
    void *fs = absolute_media_filesystem(volume, path);
    if (fs) {
        return ((FileAttributes)(*(void ***)fs)[0xf8 / 8])(fs, path);
    }
    int addon = volume_addon(volume);
    if (addon < 0 && volume != carbin_volume) {
        return original_attributes(volume, path);
    }
    wchar_t *physical = volume == carbin_volume ? carbin_cached_path(volume, path)
                                                : addon_file_path((unsigned)addon, path);
    if (!physical) {
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD result = GetFileAttributesW(physical);
    HeapFree(GetProcessHeap(), 0, physical);
    return result;
}

static unsigned char folder_file_exists(void *volume, const BorrowedGameString *path) {
    void *fs = absolute_media_filesystem(volume, path);
    if (fs) {
        return ((FileExists)(*(void ***)fs)[0xe8 / 8])(fs, path);
    }
    const char *text = string_text(path);
    // Archive selectors such as <*>0 are virtual paths, not disk filenames.
    for (size_t i = 0; i < path->length; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch == '<' || ch == '>' || ch == '*' || ch == '?' || ch == '"' || ch == '|') {
            return 0;
        }
    }
    int addon = volume_addon(volume);
    if (addon < 0 && volume != carbin_volume) {
        return original_exists(volume, path);
    }
    wchar_t *physical = volume == carbin_volume ? carbin_cached_path(volume, path)
                                                : addon_file_path((unsigned)addon, path);
    if (!physical) {
        return 0;
    }
    HeapFree(GetProcessHeap(), 0, physical);
    return 1;
}

typedef unsigned (*FileHash)(void *,const BorrowedGameString *);
static FileHash original_hash;
static unsigned addon_file_hash(void *volume,const BorrowedGameString *path){
    // Native engine-bank discovery rejects a zero hash before queueing the bank.
    // Unhashed native loose-folder scans register an existing file with value 1
    // (11fb080); use that same marker for mod banks, without scanning the folder.
    const char *text=string_text(path);
    if(path->length<5 || !same_path(text+path->length-5,5,".bank"))return original_hash(volume,path);
    void *fs=absolute_media_filesystem(volume,path);
    if(fs)return ((FileHash)(*(void ***)fs)[0x120/8])(fs,path);
    if(volume_addon(volume)<0)return original_hash(volume,path);
    return folder_file_exists(volume,path)?1:0;
}

typedef void **(*OpenNativeFile)(void *, void **, const BorrowedGameString *, unsigned,
                                 unsigned char, unsigned char, void *);
static OpenNativeFile original_open;

typedef struct {
    DWORD disposition, access, sharing, attributes;
    unsigned char valid;
} NativeFileMode;

static int native_read_mode(unsigned flags, NativeFileMode *mode) {
    typedef void *(*FileMode)(void *, unsigned);
    ((FileMode)(image + 0x11e1290))(mode, flags);
    return mode->valid && mode->disposition == OPEN_EXISTING && mode->access == GENERIC_READ;
}

static void **open_addon_file(void *volume, void **output, const BorrowedGameString *path,
                              unsigned flags, unsigned char a, unsigned char b, void *extra) {
    void *fs = absolute_media_filesystem(volume, path);
    if (fs) {
        NativeFileMode mode;
        *output = NULL;
        if (native_read_mode(flags, &mode)) {
            // Render parts retain the carbin's owner, then request full game
            // paths from it. Resolve those against all volumes, not just the
            // patch's folder/cache (which lacks the original ZIP's models).
            if (!InterlockedExchange(&absolute_requests, 1)) {
                log_append(
                    "FILES: owning-volume full asset paths use the complete filesystem.\r\n");
            }
            return ((OpenNativeFile)(*(void ***)fs)[0x138 / 8])(fs, output, path, flags, a, b,
                                                                extra);
        }
        return output;
    }
    int addon = volume_addon(volume);
    if (addon < 0 && volume != carbin_volume) {
        return original_open(volume, output, path, flags, a, b, extra);
    }
    *output = NULL;

    struct {
        DWORD disposition, access, sharing, attributes;
        unsigned char valid;
    } mode;

    typedef void *(*FileMode)(void *, unsigned);
    ((FileMode)(image + 0x11e1290))(&mode, flags);
    if (!mode.valid || mode.disposition != OPEN_EXISTING || mode.access != GENERIC_READ) {
        return output;
    }
    wchar_t *physical = volume == carbin_volume ? carbin_cached_path(volume, path)
                                                : addon_file_path((unsigned)addon, path);
    if (!physical) {
        return output;
    }
    HANDLE handle = CreateFileW(
        physical, mode.access,
        mode.sharing | (volume == carbin_volume ? FILE_SHARE_WRITE | FILE_SHARE_DELETE : 0), NULL,
        mode.disposition, FILE_ATTRIBUTE_NORMAL | mode.attributes, NULL);
    HeapFree(GetProcessHeap(), 0, physical);
    if (handle == INVALID_HANDLE_VALUE) {
        return output;
    }
    void *storage = allocate(0x90);
    if (!storage) {
        CloseHandle(handle);
        return output;
    }
    void **methods = *(void ***)volume;
    ((Reference)methods[0x10 / 8])(volume);
    void *owner = volume;
    typedef void *(*ConstructFile)(void *, HANDLE, void **, unsigned);
    void *file = ((ConstructFile)(image + 0x11e04a0))(storage, handle, &owner, flags);
    methods = *(void ***)file;
    typedef void (*SetFilename)(void *, const BorrowedGameString *);
    ((SetFilename)methods[0x280 / 8])(file, path);
    ((Reference)methods[0x10 / 8])(file);
    *output = file;
    return output;
}

typedef void **(*OpenAsyncFile)(void *, void **, const BorrowedGameString *, unsigned, void *,
                                unsigned, unsigned, unsigned char);
static OpenAsyncFile original_async_open;

static const char *registered_file_path(void *volume, const BorrowedGameString *path) {
    int addon = volume_addon(volume);
    wchar_t *physical = volume == carbin_volume ? carbin_cached_path(volume, path)
                        : addon >= 0            ? addon_file_path((unsigned)addon, path)
                                                : NULL;
    const char *alias = physical && addon_archives_prepare_io()
                            ? media_files_register(physical, volume == carbin_volume)
                            : NULL;
    if (physical) {
        HeapFree(GetProcessHeap(), 0, physical);
    }
    return alias;
}

static void **open_addon_async(void *volume, void **output, const BorrowedGameString *path,
                               unsigned flags, void *device, unsigned a, unsigned b,
                               unsigned char c) {
    void *fs = absolute_media_filesystem(volume, path);
    if (fs) {
        NativeFileMode mode;
        *output = NULL;
        if (native_read_mode(flags, &mode)) {
            return ((OpenAsyncFile)(*(void ***)fs)[0x148 / 8])(fs, output, path, flags, device, a,
                                                               b, c);
        }
        return output;
    }
    if (volume_addon(volume) < 0 && volume != carbin_volume) {
        return original_async_open(volume, output, path, flags, device, a, b, c);
    }
    *output = NULL;

    struct {
        DWORD disposition, access, sharing, attributes;
        unsigned char valid;
    } mode;

    typedef void *(*FileMode)(void *, unsigned);
    ((FileMode)(image + 0x11e1290))(&mode, flags);
    const char *alias =
        mode.valid && mode.disposition == OPEN_EXISTING && mode.access == GENERIC_READ
            ? registered_file_path(volume, path)
            : NULL;
    if (!alias) {
        return output;
    }
    BorrowedGameString filename = borrowed_string(alias);
    original_async_open(volume, output, &filename, flags, device, a, b, c);
    if (*output) {
        typedef void (*SetFilename)(void *, const BorrowedGameString *);
        ((SetFilename)(*(void ***)*output)[0x280 / 8])(*output, path);
    } else {
        log_append("MODS: native asynchronous media open failed.\r\n");
    }
    return output;
}

static int mount_folder(void *filesystem, const char *folder, unsigned priority, int addon) {
    void *storage = allocate(0x50);
    if (!storage) {
        return 0;
    }
    BorrowedGameString source = borrowed_string(folder);
    BorrowedGameString destination = borrowed_string("game:\\media\\");
    void *volume = construct_volume(storage, &source, 1);
    if (!volume) {
        return 0;
    }
    *(void ***)volume = folder_vtable + 1;
    void **methods = *(void ***)volume;
    ((Reference)methods[0x10 / 8])(volume);
    void **filesystem_methods = *(void ***)filesystem;
    // Native mounts retain the volume. Do not enable hash-manifest filtering.
    int mounted = ((MountVolume)filesystem_methods[0x1e8 / 8])(filesystem, &destination, &volume,
                                                               priority, 0, 0, 0);
    if (mounted && addon >= 0 && addon_volume_count < ARRAY_COUNT(addon_volumes)) {
        addon_volumes[addon_volume_count].volume = volume;
        addon_volumes[addon_volume_count++].addon = (unsigned)addon;
    }
    if (mounted && addon == -2) {
        carbin_volume = volume;
    }
    if (mounted && priority == 0x7ffffffe) {
        priority_volumes[0] = volume;
    }
    if (mounted && priority == 0x7ffffffd) {
        priority_volumes[1] = volume;
    }
    ((Reference)methods[0x18 / 8])(volume);
    return mounted;
}

#include "carbin_runtime.h"

static void ensure_folders_mounted(void *filesystem) {
    void *root;
    if (!memory_read((unsigned char *)filesystem + 0x68, &root, sizeof(root)) || !root) {
        return;
    }
    InterlockedExchangePointer(&media_filesystem, filesystem);
    LONG thread = (LONG)GetCurrentThreadId();
    if ((InterlockedCompareExchange(&mount_state, 0, 0) == 2 &&
         archive_revision == addons_media_revision()) ||
        InterlockedCompareExchange(&mount_thread, 0, 0) == thread) {
        return;
    }
    AcquireSRWLockExclusive(&mount_lock);
    LONG state = InterlockedCompareExchange(&mount_state, 0, 0);
    if (state == 2 && archive_revision == addons_media_revision()) {
        ReleaseSRWLockExclusive(&mount_lock);
        return;
    }
    InterlockedExchange(&mount_thread, thread);
    unsigned mods = addons_count();
    if (!overrides_enabled && !mods) {
        archive_revision = addons_media_revision();
        InterlockedExchange(&mount_thread, 0);
        InterlockedExchange(&mount_state, 2);
        ReleaseSRWLockExclusive(&mount_lock);
        return;
    }
    int override = 1, pc = 1;
    if (!state) {
        override = mount_folder(filesystem, "game:\\mediaoverride\\", 0x7ffffffe, -1);
        pc = mount_folder(filesystem, "game:\\mediapc\\", 0x7ffffffd, -1);
        for (unsigned i = 0; i < mods; ++i) {
            const wchar_t *folder = addons_media_folder(i);
            if (!folder) {
                continue;
            }
            char source[] = "game:\\__fh2_mods__\\000\\";
            size_t number = sizeof(source) - 5;
            source[number] = (char)('0' + i / 100);
            source[number + 1] = (char)('0' + i / 10 % 10);
            source[number + 2] = (char)('0' + i % 10);
            if (!mount_folder(filesystem, source, 0x70000001u + i * 2, (int)i)) {
                log_append("MODS: media folder mount failed.\r\n");
            }
        }
    }
    addon_archives_mount(filesystem);
    archive_revision = addons_media_revision();
    void **methods = *(void ***)filesystem;
    ((SortVolumes)methods[0x1f8 / 8])(filesystem);
    InterlockedExchange(&mount_thread, 0);
    InterlockedExchange(&mount_state, 2);
    ReleaseSRWLockExclusive(&mount_lock);
    log_append(override && pc ? "FILES: mediaoverride > mediapc > mods (last first) > original "
                                "assets; native folder mounts ready.\r\n"
                              : "FILES: an override mount failed; original asset "
                                "lookup remains available.\r\n");
}

static unsigned char resolve_file(void *filesystem, void **volume, void *relative_path,
                                  const void *path, unsigned char flag1, unsigned char flag2,
                                  unsigned char flag3, void **mount, unsigned char flag4) {
    plugins_start();
    ensure_folders_mounted(filesystem);
    carbin_prepare(filesystem, path);
    unsigned char found = original_resolve(filesystem, volume, relative_path, path, flag1, flag2,
                                           flag3, mount, flag4);
    const BorrowedGameString *filename = path;
    addon_archives_log_lookup(string_text(filename), filename->length, found,
                              found ? *volume : NULL);
    return found;
}


typedef void *(*DiskPath)(void *,const BorrowedGameString *,BorrowedGameString *);
static unsigned char disk_path_original[8],disk_path_callback[8];
static const unsigned char *const disk_path_accepted[]={disk_path_original,disk_path_callback};
static volatile LONG bank_path_reports;
static void *addon_disk_path(void *filesystem,const BorrowedGameString *path,BorrowedGameString *output){
    const char *text=string_text(path);
    if(path->length>=5 && same_path(text+path->length-5,5,".bank")){
        ensure_folders_mounted(filesystem);
        void *volume=NULL;BorrowedGameString relative={0};relative.capacity=15;
        unsigned char found=original_resolve(filesystem,&volume,&relative,path,0,0,0,NULL,0);
        int addon=found && volume?volume_addon(volume):-1;
        wchar_t *physical=addon>=0?addon_file_path((unsigned)addon,&relative):NULL;
        const char *alias=physical && addon_archives_prepare_io()?media_files_register(physical,0):NULL;
        if(alias && InterlockedIncrement(&bank_path_reports)<=16){
            log_append("AUDIO BANK: native disk load redirected: ");
            log_append(text);log_append("\r\n");
        }
        if(physical)HeapFree(GetProcessHeap(),0,physical);
        if(volume)((Reference)(*(void ***)volume)[0x18/8])(volume);
        if(relative.capacity>=16)((void (*)(void *))(image+0x31c7b0))((void *)relative.data);
        if(alias){
            BorrowedGameString resolved=borrowed_string(alias);
            // Copy into the caller's initialized native string. 0x228ed0 is
            // a MOVE operation: using it with this borrowed registry alias
            // makes AudioContentBankFmod's destructor free somebody else's
            // memory (and clear the permanent alias). Keep a game-owned copy.
            return ((void *(*)(void *,const void *,size_t,size_t))(image+0x22c1d0))(
                output,&resolved,0,(size_t)-1);
        }
    }
    return ((DiskPath)(image+0x11f1460))(filesystem,path,output);
}
PatchDefinition media_overrides_disk_path_hook_patch={0x2e6eec8,0,8,GROUP_MEDIA_OVERRIDES,
    "mod_bank_native_disk_path",disk_path_original,disk_path_callback,disk_path_callback,disk_path_accepted,2};

// Keep the native bank loader and its asynchronous flags; report FMOD's actual
// result for redirected files rather than assuming a successful path means sound.
static int bank_load_result(void *studio,const char *filename,unsigned flags,void **bank){
    int result=((int (*)(void *,const char *,unsigned,void **))(image+0x1a99530))(studio,filename,flags,bank);
    if(filename && text_length(filename)>=sizeof("game:\\__fh2_mod_file__\\")-1 && memory_equal(filename,"game:\\__fh2_mod_file__\\",sizeof("game:\\__fh2_mod_file__\\")-1)){
        log_append("AUDIO BANK: FMOD load: ");log_append(filename);log_append("\r\n");
        log_append_value("AUDIO BANK: FMOD result=",result);
        log_append_value("AUDIO BANK: FMOD flags=",(int)flags);
    }
    return result;
}
static int bank_loading_state(void *object){
    int state=((int (*)(void *))(image+0x10070f0))(object);
    const BorrowedGameString *name=(const void *)((unsigned char *)object+8);
    const char *filename=string_text(name);
    if(name->length>=sizeof("game:\\__fh2_mod_file__\\")-1 && memory_equal(filename,"game:\\__fh2_mod_file__\\",sizeof("game:\\__fh2_mod_file__\\")-1)){
        // The native queue already polls this getter. Observe its result without
        // adding another FMOD query, retaining banks, or waiting on the mixer.
        static SRWLOCK lock=SRWLOCK_INIT;
        static struct {void *object,*bank;int state;} seen[32];
        static unsigned next;
        void *bank=NULL;memory_read_pointer((unsigned char *)object+0x28,&bank);
        AcquireSRWLockExclusive(&lock);
        unsigned i;for(i=0;i<ARRAY_COUNT(seen);++i)if(seen[i].object==object && seen[i].bank==bank)break;
        if(i==ARRAY_COUNT(seen) || seen[i].state!=state){
            if(i==ARRAY_COUNT(seen))i=next++%ARRAY_COUNT(seen);
            seen[i].object=object;seen[i].bank=bank;seen[i].state=state;
            log_append("AUDIO BANK: loading state: ");log_append(filename);log_append("\r\n");
            log_append_value("AUDIO BANK: native state=",state);
        }
        ReleaseSRWLockExclusive(&lock);
    }
    return state;
}
static unsigned char bank_originals[3][12],bank_replacements[3][12];
static const unsigned char *bank_accepted[3][2];
PatchDefinition media_overrides_bank_diagnostics[3];

PatchDefinition media_overrides_hook_patch = {0x2e6ef28,
                                              0,
                                              8,
                                              GROUP_MEDIA_OVERRIDES,
                                              "media_override_native_resolver",
                                              original_pointer,
                                              callback_pointer,
                                              callback_pointer,
                                              accepted,
                                              2};

void media_overrides_prepare_hook(unsigned char *game_image) {
    if (image != game_image) {
        InterlockedExchangePointer(&media_filesystem, NULL);
        InterlockedExchange(&absolute_requests, 0);
        carbin_reset();
        carbin_volume = NULL;
        InterlockedExchange(&mount_state, 0);
        addon_volume_count = 0;
        archive_revision = 0;
        addon_archives_reset();
    }
    image = game_image;
    static const unsigned bank_sites[]={0x10121d4,0x101218d,0x2e72260};
    for(unsigned i=0;i<3;++i){
        unsigned size=i==2?8:i?5:12;
        if(!i){
            for(unsigned j=0;j<12;++j)bank_originals[i][j]=0xcc;
            static const unsigned char jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
            uintptr_t callback=(uintptr_t)bank_load_result;
            memory_copy(bank_replacements[i],jump,12);memory_copy(bank_replacements[i]+2,&callback,8);
        }else if(i==2){
            uintptr_t original=(uintptr_t)(image+0x10070f0),callback=(uintptr_t)bank_loading_state;
            memory_copy(bank_originals[i],&original,8);memory_copy(bank_replacements[i],&callback,8);
        }else{
            bank_originals[i][0]=bank_replacements[i][0]=0xe8;
            int32_t before=0x1a99530-bank_sites[i]-5,after=bank_sites[0]-bank_sites[i]-5;
            memory_copy(bank_originals[i]+1,&before,4);memory_copy(bank_replacements[i]+1,&after,4);
        }
        bank_accepted[i][0]=bank_originals[i];bank_accepted[i][1]=bank_replacements[i];
        media_overrides_bank_diagnostics[i]=(PatchDefinition){bank_sites[i],0,size,GROUP_MEDIA_OVERRIDES,
            "mod_bank_load_result",bank_originals[i],bank_replacements[i],bank_replacements[i],bank_accepted[i],2};
    }
    addon_archives_bind(game_image);
    original_resolve = (ResolveFile)(image + 0x11f49b0);
    allocate = (Allocate)(image + 0x3229c0);
    construct_volume = (ConstructVolume)(image + 0x11e05a0);
    // Preserve the native RTTI locator preceding the method table.
    memory_copy(folder_vtable, image + 0x2eab910, sizeof(folder_vtable));
    original_exists = (FileExists)folder_vtable[1 + 0xe8 / 8];
    folder_vtable[1 + 0xe8 / 8] = (void *)folder_file_exists;
    original_hash=(FileHash)folder_vtable[1+0x120/8];
    folder_vtable[1+0x120/8]=(void *)addon_file_hash;
    original_attributes = (FileAttributes)folder_vtable[1 + 0xf8 / 8];
    folder_vtable[1 + 0xf8 / 8] = (void *)addon_file_attributes;
    original_open = (OpenNativeFile)folder_vtable[1 + 0x138 / 8];
    folder_vtable[1 + 0x138 / 8] = (void *)open_addon_file;
    original_async_open = (OpenAsyncFile)folder_vtable[1 + 0x148 / 8];
    folder_vtable[1 + 0x148 / 8] = (void *)open_addon_async;
    uintptr_t original = (uintptr_t)original_resolve, callback = (uintptr_t)resolve_file;
    memory_copy(original_pointer, &original, sizeof(original));
    memory_copy(callback_pointer, &callback, sizeof(callback));
    original=(uintptr_t)(image+0x11f1460);callback=(uintptr_t)addon_disk_path;
    memory_copy(disk_path_original,&original,8);memory_copy(disk_path_callback,&callback,8);

}

void media_overrides_set_enabled(int enabled) {
    overrides_enabled = enabled;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestMediaOverridesHookAddress(void) {
    return (void *)resolve_file;
}

__declspec(dllexport) void Fh2TestMediaOverridesReset(void) {
    carbin_reset();
    InterlockedExchange(&mount_state, 0);
    addon_volume_count = 0;
    archive_revision = 0;
    addon_archives_reset();
}
#endif
