#include "media_files.h"
#include "logging.h"

enum { MAX_FILES = 16384 };

typedef struct MediaFile {
    struct MediaFile *next;
    wchar_t *physical;
    char alias[48];
    int temporary;
} MediaFile;

static SRWLOCK files_lock = SRWLOCK_INIT;
static MediaFile *files;
static unsigned file_count;
static const wchar_t prefix[] = L"game:\\__fh2_mod_file__\\";

const char *media_files_register(const wchar_t *physical, int temporary) {
    const char *result = NULL;
    AcquireSRWLockExclusive(&files_lock);
    for (MediaFile *p = files; p; p = p->next) {
        if (!lstrcmpiW(p->physical, physical)) {
            result = p->alias;
            break;
        }
    }
    if (!result && file_count < MAX_FILES) {
        size_t bytes = ((size_t)lstrlenW(physical) + 1) * sizeof(wchar_t);
        MediaFile *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p) + bytes);
        if (p) {
            p->physical = (wchar_t *)(p + 1);
            memory_copy(p->physical, physical, bytes);
            const char pattern[] = "game:\\__fh2_mod_file__\\00000000.bin";
            memory_copy(p->alias, pattern, sizeof(pattern));
            size_t first = sizeof(pattern) - 13;
            unsigned index = file_count++;
            for (unsigned i = 0; i < 8; ++i) {
                p->alias[first + i] = "0123456789abcdef"[(index >> ((7 - i) * 4)) & 15];
            }
            p->temporary = temporary;
            p->next = files;
            files = p;
            result = p->alias;
        }
    }
    ReleaseSRWLockExclusive(&files_lock);
    return result;
}

HANDLE media_files_open(LPCWSTR filename, DWORD access, DWORD sharing, DWORD disposition,
                        LPCREATEFILE2_EXTENDED_PARAMETERS options, int *handled) {
    *handled = 0;
    if (!filename) {
        return INVALID_HANDLE_VALUE;
    }
    for (size_t i = 0; i < ARRAY_COUNT(prefix) - 1; ++i) {
        wchar_t ch = filename[i];
        if (ch == L'/') {
            ch = L'\\';
        }
        if (ch >= L'A' && ch <= L'Z') {
            ch += L'a' - L'A';
        }
        if (ch != prefix[i]) {
            return INVALID_HANDLE_VALUE;
        }
    }
    *handled = 1;
    if (access != GENERIC_READ || disposition != OPEN_EXISTING) {
        SetLastError(ERROR_ACCESS_DENIED);
        return INVALID_HANDLE_VALUE;
    }
    MediaFile *found = NULL;
    AcquireSRWLockShared(&files_lock);
    for (MediaFile *p = files; p; p = p->next) {
        size_t length = text_length(p->alias), i = 0;
        if (lstrlenW(filename) != (int)length) {
            continue;
        }
        for (; i < length; ++i) {
            wchar_t ch = filename[i];
            if (ch == L'/') {
                ch = L'\\';
            }
            if (ch >= L'A' && ch <= L'Z') {
                ch += L'a' - L'A';
            }
            if (ch != (wchar_t)p->alias[i]) {
                break;
            }
        }
        if (i == length) {
            found = p;
            break;
        }
    }
    ReleaseSRWLockShared(&files_lock);
    if (!found) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    if (found->temporary) {
        sharing |= FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    }
    HANDLE result=CreateFileW(found->physical, access, sharing,
                       options ? options->lpSecurityAttributes : NULL, disposition,
                       options ? options->dwFileAttributes | options->dwFileFlags |
                                     options->dwSecurityQosFlags
                               : FILE_ATTRIBUTE_NORMAL,
                       options ? options->hTemplateFile : NULL);
    DWORD error=result==INVALID_HANDLE_VALUE?GetLastError():0;
    size_t length=(size_t)lstrlenW(found->physical);
    static volatile LONG bank_reports;
    if(length>=5 && !lstrcmpiW(found->physical+length-5,L".bank") && InterlockedIncrement(&bank_reports)<=32){
        log_append("AUDIO BANK: disk open: ");log_append(found->alias);log_append("\r\n");
        log_append_value("AUDIO BANK: disk error=",(int)error);
    }
    if(error)SetLastError(error);
    return result;
}
