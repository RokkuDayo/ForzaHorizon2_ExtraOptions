#include "features.h"
#include "support.h"
#include "addon_archives.h"
#include "video_redirect.h"
#include "logging.h"

typedef void **(*OpenVideo)(void *, void **, const char *, unsigned char);
extern char *media_override_video_path(const char *filename);

static unsigned char *image;
static unsigned char original[8], replacement[8];
static const unsigned char *const accepted[] = {original, replacement};

enum { MAX_VIDEO_REDIRECTS = 256 };

typedef struct {
    char name[48];
    wchar_t *physical;
} VideoFile;

static SRWLOCK video_lock = SRWLOCK_INIT;
static VideoFile videos[MAX_VIDEO_REDIRECTS];
static unsigned video_count;
static const wchar_t video_prefix[] = L"game:\\__fh2_mod_video__\\";

static const char *register_video(const char *physical) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, physical, -1, NULL, 0);
    wchar_t *path = count ? HeapAlloc(GetProcessHeap(), 0, count * sizeof(wchar_t)) : NULL;
    if (!path || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, physical, -1, path, count)) {
        if (path) {
            HeapFree(GetProcessHeap(), 0, path);
        }
        return NULL;
    }
    const char *name = NULL;
    AcquireSRWLockExclusive(&video_lock);
    for (unsigned i = 0; i < video_count; ++i) {
        if (!lstrcmpiW(videos[i].physical, path)) {
            name = videos[i].name;
            break;
        }
    }
    if (!name && video_count < MAX_VIDEO_REDIRECTS) {
        unsigned index = video_count++;
        VideoFile *file = &videos[index];
        const char pattern[] = "game:\\__fh2_mod_video__\\000.bk2";
        memory_copy(file->name, pattern, sizeof(pattern));
        size_t digits = sizeof(pattern) - 8;
        file->name[digits] = '0' + index / 100;
        file->name[digits + 1] = '0' + index / 10 % 10;
        file->name[digits + 2] = '0' + index % 10;
        file->physical = path;
        path = NULL;
        name = file->name;
    }
    ReleaseSRWLockExclusive(&video_lock);
    if (path) {
        HeapFree(GetProcessHeap(), 0, path);
    }
    return name;
}

HANDLE video_redirect_open_file(LPCWSTR filename, DWORD access, DWORD sharing, DWORD disposition,
                                LPCREATEFILE2_EXTENDED_PARAMETERS options, int *handled) {
    *handled = 0;
    if (!filename) {
        return INVALID_HANDLE_VALUE;
    }
    size_t prefix_length = ARRAY_COUNT(video_prefix) - 1;
    for (size_t i = 0; i < prefix_length; ++i) {
        wchar_t value = filename[i];
        if (value == '/') {
            value = L'\\';
        }
        if (value >= L'A' && value <= L'Z') {
            value += L'a' - L'A';
        }
        if (value != video_prefix[i]) {
            return INVALID_HANDLE_VALUE;
        }
    }
    *handled = 1;
    if (disposition != OPEN_EXISTING || access != GENERIC_READ) {
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    const wchar_t *physical = NULL;
    AcquireSRWLockShared(&video_lock);
    for (unsigned i = 0; i < video_count; ++i) {
        const char *name = videos[i].name + prefix_length;
        if (lstrlenW(filename + prefix_length) != (int)text_length(name)) {
            continue;
        }
        size_t j = 0;
        while (name[j] && filename[prefix_length + j] == (wchar_t)name[j]) {
            ++j;
        }
        if (!name[j]) {
            physical = videos[i].physical;
            break;
        }
    }
    ReleaseSRWLockShared(&video_lock);
    if (!physical) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return CreateFileW(
        physical, access, sharing, options ? options->lpSecurityAttributes : NULL, disposition,
        options ? options->dwFileAttributes | options->dwFileFlags | options->dwSecurityQosFlags
                : FILE_ATTRIBUTE_NORMAL,
        options ? options->hTemplateFile : NULL);
}

static void **open_video(void *system, void **output, const char *filename, unsigned char flags) {
    char *physical = filename ? media_override_video_path(filename) : NULL;
    const char *redirect =
        physical && addon_archives_prepare_io() ? register_video(physical) : NULL;
    if (physical) {
        if (!redirect) {
            log_append("MODS VIDEO: could not register override; using original video.\r\n");
        }
        HeapFree(GetProcessHeap(), 0, physical);
    }
    return ((OpenVideo)(image + 0x11a0710))(system, output, redirect ? redirect : filename, flags);
}

PatchDefinition video_redirect_hook_patch = {0x2ea8650,
                                             0,
                                             8,
                                             GROUP_MEDIA_OVERRIDES,
                                             "mod_video_file_redirect",
                                             original,
                                             replacement,
                                             replacement,
                                             accepted,
                                             2};

void video_redirect_prepare_hook(unsigned char *game) {
    image = game;
    uintptr_t native = (uintptr_t)(game + 0x11a0710), callback = (uintptr_t)open_video;
    memory_copy(original, &native, 8);
    memory_copy(replacement, &callback, 8);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) char *Fh2TestVideoOverridePath(const char *filename) {
    return media_override_video_path(filename);
}
#endif
