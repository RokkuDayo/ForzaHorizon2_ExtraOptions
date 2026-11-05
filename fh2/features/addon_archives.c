#include "addon_archives.h"
#include "addons.h"
#include "logging.h"
#include "video_redirect.h"
#include "media_files.h"

enum { MAX_ARCHIVES = 2048, MAX_ARCHIVE_DEPTH = 24, MAX_DIRECTORY_BYTES = 64 * 1024 * 1024 };

typedef struct {
    const char *data;
    uintptr_t reserved;
    size_t length, capacity;
} ArchiveString;

typedef void *(*Allocate)(size_t);
typedef void (*Reference)(void *);
typedef void *(*ConstructZip)(void *, const ArchiveString *, const ArchiveString *, unsigned,
                              unsigned, unsigned, unsigned);
typedef unsigned char (*Mount)(void *, const ArchiveString *, void **, unsigned, unsigned char,
                               unsigned char, const ArchiveString *);

static unsigned char *image;
static unsigned mounted_count;
static unsigned char scanned[128];

typedef struct ArchiveDiagnostic {
    struct ArchiveDiagnostic *next;
    void *volume;
    const char *physical, *prefix;
    wchar_t *physical_path;
    char backing[40];
    size_t prefix_length;
    volatile LONG reported;
    unsigned addon;
} ArchiveDiagnostic;

static ArchiveDiagnostic *volatile diagnostics;
typedef HANDLE(WINAPI *OpenArchive)(LPCWSTR, DWORD, DWORD, DWORD,
                                    LPCREATEFILE2_EXTENDED_PARAMETERS);
static OpenArchive original_open;
typedef HANDLE(WINAPI *OpenAnsi)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
static OpenAnsi original_ansi_open;
static int io_hook_ready, ansi_hook_ready;
static SRWLOCK io_lock = SRWLOCK_INIT;

static char *utf8_path(const wchar_t *path) {
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
    char *text = bytes ? HeapAlloc(GetProcessHeap(), 0, bytes) : NULL;
    if (text &&
        !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, text, bytes, NULL, NULL)) {
        HeapFree(GetProcessHeap(), 0, text);
        return NULL;
    }
    return text;
}

static void archive_log(const wchar_t *physical, const char *status, unsigned detail) {
    char *path = utf8_path(physical);
    log_append("MODS ZIP: ");
    log_append(path ? path : "[path conversion failed]");
    log_append(" | ");
    log_append_value(status, (int)detail);
    if (path) {
        HeapFree(GetProcessHeap(), 0, path);
    }
}

static void track_archive(void *volume, const wchar_t *physical, const ArchiveString *prefix,
                          const char *backing, unsigned addon) {
    char *path = utf8_path(physical);
    if (!path) {
        return;
    }
    size_t bytes = text_length(path) + 1;
    ArchiveDiagnostic *record =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*record) + bytes + prefix->length + 1);
    if (record) {
        size_t physical_bytes = ((size_t)lstrlenW(physical) + 1) * sizeof(wchar_t);
        record->physical_path = HeapAlloc(GetProcessHeap(), 0, physical_bytes);
        if (!record->physical_path) {
            HeapFree(GetProcessHeap(), 0, record);
            HeapFree(GetProcessHeap(), 0, path);
            return;
        }
        memory_copy(record->physical_path, physical, physical_bytes);
        record->volume = volume;
        record->addon = addon;
        memory_copy(record->backing, backing, text_length(backing) + 1);
        record->physical = (char *)(record + 1);
        memory_copy((char *)record->physical, path, bytes);
        record->prefix = record->physical + bytes;
        record->prefix_length = prefix->length;
        memory_copy((char *)record->prefix, prefix->data, prefix->length + 1);
        do {
            record->next =
                InterlockedCompareExchangePointer((void *volatile *)&diagnostics, NULL, NULL);
        } while (InterlockedCompareExchangePointer((void *volatile *)&diagnostics, record,
                                                   record->next) != record->next);
    }
    HeapFree(GetProcessHeap(), 0, path);
}

static char path_character(char value) {
    if (value == '/') {
        return '\\';
    }
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

static HANDLE WINAPI open_archive(LPCWSTR filename, DWORD access, DWORD sharing, DWORD disposition,
                                  LPCREATEFILE2_EXTENDED_PARAMETERS options) {
    int handled = 0;
    HANDLE asset = media_files_open(filename, access, sharing, disposition, options, &handled);
    if (handled) {
        return asset;
    }
    HANDLE video =
        video_redirect_open_file(filename, access, sharing, disposition, options, &handled);
    if (handled) {
        return video;
    }
    size_t length = filename ? (size_t)lstrlenW(filename) : 0;
    if (length > 4 && disposition == OPEN_EXISTING && access == GENERIC_READ &&
        !lstrcmpiW(filename + length - 4, L".zip")) {
        char *path = utf8_path(filename);
        ArchiveDiagnostic *record =
            InterlockedCompareExchangePointer((void *volatile *)&diagnostics, NULL, NULL);
        for (; path && record; record = record->next) {
            size_t bytes = text_length(path);
            if (bytes != text_length(record->backing)) {
                continue;
            }
            size_t i = 0;
            while (i < bytes && path_character(path[i]) == path_character(record->backing[i])) {
                ++i;
            }
            if (i != bytes) {
                continue;
            }
            HANDLE result = CreateFileW(record->physical_path, access, sharing,
                                        options ? options->lpSecurityAttributes : NULL, disposition,
                                        options ? options->dwFileAttributes | options->dwFileFlags |
                                                      options->dwSecurityQosFlags
                                                : FILE_ATTRIBUTE_NORMAL,
                                        options ? options->hTemplateFile : NULL);
            DWORD error = result == INVALID_HANDLE_VALUE ? GetLastError() : 0;
            if (!(InterlockedOr(&record->reported, 8) & 8)) {
                archive_log(record->physical_path,
                            "reopened from mod folder; Windows error=", error);
            }
            HeapFree(GetProcessHeap(), 0, path);
            SetLastError(error);
            return result;
        }
        if (path) {
            HeapFree(GetProcessHeap(), 0, path);
        }
    }
    return original_open(filename, access, sharing, disposition, options);
}

// AudioManager installs its own FMOD file callbacks. Its open callback uses
// CreateFileA, not FMOD's default CreateFile2 routine. Only our short ASCII
// aliases are translated here; ordinary paths keep the host's ANSI semantics.
static HANDLE WINAPI open_registered_ansi(LPCSTR filename,DWORD access,DWORD sharing,
    LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD flags,HANDLE template_file){
    static const char prefix[]="game:\\__fh2_mod_file__\\";
    if(filename){
        unsigned i=0;
        while(i<sizeof(prefix)-1 && path_character(filename[i])==prefix[i])++i;
        if(i==sizeof(prefix)-1){
            wchar_t wide[48];
            for(i=0;i<ARRAY_COUNT(wide);++i){
                unsigned char ch=(unsigned char)filename[i];
                if(ch>127)break;
                wide[i]=(wchar_t)ch;
                if(!ch){
                    CREATEFILE2_EXTENDED_PARAMETERS options={0};
                    options.dwSize=sizeof(options);options.dwFileAttributes=flags;
                    options.lpSecurityAttributes=security;options.hTemplateFile=template_file;
                    int handled=0;
                    HANDLE result=media_files_open(wide,access,sharing,disposition,&options,&handled);
                    if(handled)return result;
                    break;
                }
            }
        }
    }
    return original_ansi_open(filename,access,sharing,security,disposition,flags,template_file);
}

int addon_archives_prepare_io(void) {
    AcquireSRWLockExclusive(&io_lock);
    int ready = io_hook_ready;
    if (!ready && image) {
        void *volatile *slot = (void *volatile *)(image + 0x1dd1148);
        DWORD protection;
        if (VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &protection)) {
            original_open = (OpenArchive)*slot;
            InterlockedExchangePointer(slot, (void *)open_archive);
            DWORD ignored;
            VirtualProtect((void *)slot, sizeof(void *), protection, &ignored);
            ready = io_hook_ready = 1;
            log_append(
                "MODS: registered asset I/O redirect installed after host initialization.\r\n");
        }
    }
    if(!ansi_hook_ready && image){
        void *volatile *slot=(void *volatile *)(image+0x1dd11b0);
        DWORD protection;
        if(*slot && VirtualProtect((void *)slot,sizeof(void *),PAGE_READWRITE,&protection)){
            original_ansi_open=(OpenAnsi)*slot;
            InterlockedExchangePointer(slot,(void *)open_registered_ansi);
            DWORD ignored;VirtualProtect((void *)slot,sizeof(void *),protection,&ignored);
            ansi_hook_ready=1;
            log_append("MODS: registered asset ANSI I/O redirect ready for native FMOD callbacks.\r\n");
        }
    }
    ready=ready && ansi_hook_ready;
    ReleaseSRWLockExclusive(&io_lock);
    return ready;
}

void addon_archives_log_lookup(const char *path, size_t length, int found, void *volume) {
    static const char extension[] = ".carbin";
    if (length < sizeof(extension) - 1) {
        return;
    }
    for (unsigned i = 0; i < sizeof(extension) - 1; ++i) {
        if (path_character(path[length - (sizeof(extension) - 1) + i]) != extension[i]) {
            return;
        }
    }
    ArchiveDiagnostic *record =
        InterlockedCompareExchangePointer((void *volatile *)&diagnostics, NULL, NULL);
    for (; record; record = record->next) {
        if (length <= record->prefix_length) {
            continue;
        }
        size_t i = 0;
        while (i < record->prefix_length &&
               path_character(path[i]) == path_character(record->prefix[i])) {
            ++i;
        }
        if (i != record->prefix_length) {
            continue;
        }
        LONG flag = !found ? 2 : volume == record->volume ? 1 : 4;
        if (InterlockedOr(&record->reported, flag) & flag) {
            continue;
        }
        log_append("MODS CAR: ");
        log_append(record->physical);
        log_append(!found ? " | carbin lookup FAILED\r\n"
                   : volume == record->volume
                       ? " | carbin resolved from this ZIP\r\n"
                       : " | carbin resolved from another volume (override or fallback)\r\n");
        log_append("MODS CAR request: ");
        log_append(path);
        log_append("\r\n");
        if (!found) {
            ArchiveString member = {path + record->prefix_length, 0, length - record->prefix_length,
                                    length + 16};
            typedef unsigned char (*Exists)(void *, const ArchiveString *);
            void **methods = *(void ***)record->volume;
            int exists = ((Exists)methods[0xe8 / 8])(record->volume, &member);
            log_append_value("MODS CAR direct ZIP member exists=", exists);
            unsigned char *state = record->volume;
            uintptr_t begin, end, deferred;
            unsigned seed, gate;
            HANDLE file;
            memory_copy(&begin, state + 0x1a8, sizeof(begin));
            memory_copy(&end, state + 0x1b0, sizeof(end));
            memory_copy(&deferred, state + 0x1c0, sizeof(deferred));
            memory_copy(&seed, state + 0x19c, sizeof(seed));
            memory_copy(&gate, state + 0x198, sizeof(gate));
            memory_copy(&file, state + 0x120, sizeof(file));
            log_append_value("MODS CAR native index entries=", (int)((end - begin) / 40));
            log_append_value("MODS CAR deferred index=", deferred != 0);
            log_append_value("MODS CAR hash seed=", (int)seed);
            log_append_value("MODS CAR content gate=", (int)gate);
            LARGE_INTEGER size;
            BOOL valid = GetFileSizeEx(file, &size);
            DWORD error = valid ? 0 : GetLastError();
            log_append_value("MODS CAR ZIP handle error=", (int)error);
        }
    }
}

static unsigned little16(const unsigned char *p) {
    return p[0] | (unsigned)p[1] << 8;
}

static unsigned little32(const unsigned char *p) {
    return little16(p) | little16(p + 2) << 16;
}

static int read_at(HANDLE file, uint64_t offset, void *buffer, DWORD size) {
    OVERLAPPED position = {0};
    position.Offset = (DWORD)offset;
    position.OffsetHigh = (DWORD)(offset >> 32);
    position.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!position.hEvent) {
        return 0;
    }
    DWORD bytes = 0;
    BOOL result = ReadFile(file, buffer, size, &bytes, &position);
    if (!result && GetLastError() == ERROR_IO_PENDING) {
        result = GetOverlappedResult(file, &position, &bytes, TRUE);
    }
    CloseHandle(position.hEvent);
    return result && bytes == size;
}

static int directory_info(HANDLE file, unsigned *offset, unsigned *span, unsigned *entries) {
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 22 || size.QuadPart > 0xffffffffu) {
        return 0;
    }
    DWORD tail_size = size.QuadPart < 65557 ? (DWORD)size.QuadPart : 65557;
    unsigned char *tail = HeapAlloc(GetProcessHeap(), 0, tail_size);
    if (!tail) {
        return 0;
    }
    int valid = 0;
    if (read_at(file, (uint64_t)size.QuadPart - tail_size, tail, tail_size)) {
        for (size_t at = tail_size - 22;; --at) {
            if (little32(tail + at) == 0x06054b50 &&
                at + 22 + little16(tail + at + 20) == tail_size) {
                unsigned count = little16(tail + at + 10), start = little32(tail + at + 16);
                unsigned bytes = little32(tail + at + 12);
                uint64_t end = (uint64_t)size.QuadPart - tail_size + at;
                valid = !little16(tail + at + 4) && !little16(tail + at + 6) &&
                        little16(tail + at + 8) == count && count && count != 65535 && start &&
                        bytes && bytes < MAX_DIRECTORY_BYTES && (uint64_t)start + bytes == end;
                if (valid) {
                    *offset = start;
                    *span = (unsigned)size.QuadPart - start;
                    *entries = count;
                }
                break;
            }
            if (!at) {
                break;
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, tail);
    return valid;
}

static int mount_archive(void *filesystem, const wchar_t *physical, const wchar_t *relative,
                         unsigned addon) {
    if (!addon_archives_prepare_io()) {
        archive_log(physical, "archive I/O redirect failed; Windows error=", GetLastError());
        return 0;
    }
    if (mounted_count == MAX_ARCHIVES) {
        archive_log(physical, "skipped: archive limit=", MAX_ARCHIVES);
        return 0;
    }
    HANDLE file =
        CreateFileW(physical, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        archive_log(physical, "open failed: Windows error=", error);
        return 0;
    }
    unsigned offset, span, entries;
    if (!directory_info(file, &offset, &span, &entries)) {
        CloseHandle(file);
        archive_log(physical, "skipped: unsupported or invalid ZIP directory; mod index=", addon);
        return 0;
    }
    CloseHandle(file);
    int bytes =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, relative, -1, NULL, 0, NULL, NULL);
    char *path = bytes ? HeapAlloc(GetProcessHeap(), 0, (size_t)bytes + 12) : NULL;
    if (!path) {
        archive_log(physical, "path conversion/allocation failed; mod index=", addon);
        return 0;
    }
    memory_copy(path, "game:\\media\\", 12);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, relative, -1, path + 12, bytes, NULL, NULL);
    for (size_t i = 12; i < (size_t)bytes + 11; ++i) {
        if (path[i] == '/') {
            path[i] = '\\';
        }
    }
    ArchiveString source = {path, 0, (size_t)bytes + 11, (size_t)bytes + 16};
    char backing[] = "game:\\__fh2_addon_zip__\\0000.zip";
    unsigned serial = mounted_count;
    for (unsigned i = 0; i < 4; ++i) {
        backing[sizeof(backing) - 6 - i] = (char)('0' + serial % 10);
        serial /= 10;
    }
    ArchiveString archive_file = {backing, 0, sizeof(backing) - 1, sizeof(backing) + 15};
    Allocate allocate = (Allocate)(image + 0x3229c0);
    void *storage = allocate(0x1e8);
    if (!storage) {
        archive_log(physical, "ZIP allocation failed; mod index=", addon);
        HeapFree(GetProcessHeap(), 0, path);
        return 0;
    }
    void *volume = ((ConstructZip)(image + 0x11dfda0))(storage, &archive_file, &source, 0x8000001b,
                                                       offset, span, entries);
    void **methods = *(void ***)volume;
    ((Reference)methods[0x10 / 8])(volume);
    path[source.length - 4] = '\\';
    path[source.length - 3] = 0;
    source.length -= 3;
    void **fs_methods = *(void ***)filesystem;
    int mounted = ((Mount)fs_methods[0x1e8 / 8])(filesystem, &source, &volume,
                                                 0x70000000u + addon * 2, 0, 0, NULL);
    if (mounted) {
        track_archive(volume, physical, &source, backing, addon);
    }
    archive_log(physical,
                mounted ? "mounted; entries=" : "native mount rejected; entries=", entries);
    ((Reference)methods[0x18 / 8])(volume);
    HeapFree(GetProcessHeap(), 0, path);
    if (mounted) {
        ++mounted_count;
    }
    return mounted;
}

int addon_archives_volume_addon(void *volume) {
    ArchiveDiagnostic *record =
        InterlockedCompareExchangePointer((void *volatile *)&diagnostics, NULL, NULL);
    for (; record; record = record->next) {
        if (record->volume == volume) {
            return (int)record->addon;
        }
    }
    return -1;
}

static int scan_folder(void *filesystem, const wchar_t *folder, const wchar_t *relative,
                       unsigned addon, unsigned depth) {
    if (depth > MAX_ARCHIVE_DEPTH || mounted_count == MAX_ARCHIVES) {
        return 0;
    }
    wchar_t *search = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (!search) {
        return 0;
    }
    int mounted = 0;
    WIN32_FIND_DATAW entry;
    HANDLE find =
        path_join(search, folder, L"*") ? FindFirstFileW(search, &entry) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (entry.cFileName[0] == L'.' ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                continue;
            }
            wchar_t *next = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
            if (!next) {
                break;
            }
            if (path_join(search, folder, entry.cFileName) &&
                path_join(next, relative, entry.cFileName)) {
                if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    mounted += scan_folder(filesystem, search, next, addon, depth + 1);
                } else {
                    size_t chars = (size_t)lstrlenW(entry.cFileName);
                    if (chars > 4 && !lstrcmpiW(entry.cFileName + chars - 4, L".zip")) {
                        mounted += mount_archive(filesystem, search, next, addon);
                    }
                }
            }
            HeapFree(GetProcessHeap(), 0, next);
        } while (FindNextFileW(find, &entry));
        FindClose(find);
    }
    HeapFree(GetProcessHeap(), 0, search);
    return mounted;
}

int addon_archives_mount(void *filesystem) {
    unsigned count = addons_count();
    int mounted = 0;
    for (unsigned i = 0; i < count; ++i) {
        const wchar_t *folder = addons_media_folder(i);
        if (!scanned[i] && folder && addons_media_active(i)) {
            scanned[i] = 1;
            mounted += scan_folder(filesystem, folder, L"", i, 0);
        }
    }
    if (mounted) {
        log_append("MODS: addon ZIP archives registered with the native reader.\r\n");
    }
    return mounted;
}

void addon_archives_bind(unsigned char *base) {
    if (image != base) {
        io_hook_ready = ansi_hook_ready = 0;
    }
    image = base;
}

void addon_archives_reset(void) {
    mounted_count = 0;
    InterlockedExchangePointer((void *volatile *)&diagnostics, NULL);
    for (unsigned i = 0; i < ARRAY_COUNT(scanned); ++i) {
        scanned[i] = 0;
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestArchiveMount(void *filesystem, const wchar_t *physical,
                                              const wchar_t *relative, unsigned addon) {
    return mount_archive(filesystem, physical, relative, addon);
}
#endif
