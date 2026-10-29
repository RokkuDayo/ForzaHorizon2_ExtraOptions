#include "azimuth_markers.h"
#include "azimuth_host.h"
#include "logging.h"
#include "sha256.h"
#include "support.h"

#define MARKER_CAPACITY 65536u
#define MAX_BLOBS 128u

typedef struct {
    wchar_t search[PATH_CAPACITY], child[PATH_CAPACITY], canonical[PATH_CAPACITY];
    WIN32_FIND_DATAW entry;
} DirectoryScan;

static unsigned marker_count, marker_failures;

static int missing_file(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static void append_hex(wchar_t *text, unsigned *length, uint64_t value, unsigned digits) {
    static const wchar_t hexadecimal[] = L"0123456789abcdef";
    for (unsigned index = digits; index; --index) {
        text[(*length)++] = hexadecimal[(value >> ((index - 1) * 4)) & 15];
    }
}

static int publish_marker(const wchar_t *revision, const wchar_t *commit, const char *text,
                          size_t size) {
    wchar_t name[64] = L"commit.extra-options-";
    unsigned length = ARRAY_COUNT(L"commit.extra-options-") - 1;
    append_hex(name, &length, GetCurrentProcessId(), 8);
    name[length++] = '-';
    append_hex(name, &length, GetCurrentThreadId(), 8);
    name[length++] = '-';
    append_hex(name, &length, GetTickCount64(), 16);
    name[length++] = '.';
    name[length++] = 't';
    name[length++] = 'm';
    name[length++] = 'p';
    name[length] = 0;
    wchar_t *temporary = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (!temporary) {
        return 0;
    }
    int valid = path_join(temporary, revision, name);
    HANDLE file = valid ? CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, NULL)
                        : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, temporary);
        return 0;
    }
    size_t written = 0;
    while (valid && written < size) {
        DWORD count = 0;
        valid = WriteFile(file, text + written, (DWORD)(size - written), &count, NULL) && count;
        written += count;
    }
    if (valid) {
        valid = FlushFileBuffers(file) != 0;
    }
    valid &= CloseHandle(file) != 0;
    if (valid) {
        valid = MoveFileExW(temporary, commit, MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!valid) {
        DeleteFileW(temporary);
    }
    HeapFree(GetProcessHeap(), 0, temporary);
    return valid;
}

typedef struct {
    char *bytes;
    size_t size;
    int valid;
} MarkerText;

static void append_text(MarkerText *text, const char *value, size_t length) {
    if (!text->valid || length >= MARKER_CAPACITY - text->size) {
        text->valid = 0;
        return;
    }
    memory_copy(text->bytes + text->size, value, length);
    text->size += length;
}

static void append_number(MarkerText *text, uint64_t value) {
    char digits[20];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        char digit = digits[--count];
        append_text(text, &digit, 1);
    }
}

static int regular_name(const wchar_t *name, char output[256]) {
    size_t index = 0;
    for (; name[index] && index < 255; ++index) {
        if (name[index] <= ' ' || name[index] >= 127 || name[index] == '/' || name[index] == '\\') {
            return 0;
        }
        output[index] = (char)name[index];
    }
    output[index] = 0;
    return index && !name[index] && !(index == 1 && output[0] == '.') &&
           !(index == 2 && output[0] == '.' && output[1] == '.');
}

static int hash_blob(const wchar_t *path, uint64_t expected_size, unsigned char digest[32],
                     unsigned char *buffer) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    LARGE_INTEGER size;
    int valid = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
                (uint64_t)size.QuadPart == expected_size;
    Sha256Context hash;
    sha256_initialize(&hash);
    uint64_t total = 0;
    while (valid) {
        DWORD loaded = 0;
        valid = ReadFile(file, buffer, 65536, &loaded, NULL) != 0;
        if (!valid || !loaded) {
            break;
        }
        total += loaded;
        if (total > expected_size) {
            valid = 0;
            break;
        }
        sha256_update(&hash, buffer, loaded);
    }
    CloseHandle(file);
    if (!valid || total != expected_size) {
        return 0;
    }
    sha256_finalize(&hash, digest);
    return 1;
}

static void repair_revision(const wchar_t *revision) {
    DirectoryScan *scan = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*scan));
    char *bytes = HeapAlloc(GetProcessHeap(), 0, MARKER_CAPACITY);
    unsigned char *hash_buffer = HeapAlloc(GetProcessHeap(), 0, 65536);
    if (!scan || !bytes || !hash_buffer) {
        goto done;
    }
    if (!path_join(scan->child, revision, L"commit")) {
        goto done;
    }
    if (GetFileAttributesW(scan->child) != INVALID_FILE_ATTRIBUTES ||
        !missing_file(GetLastError())) {
        goto done;
    }
    DWORD path_length = GetFullPathNameW(scan->child, PATH_CAPACITY, scan->canonical, NULL);
    if (!path_length || path_length >= PATH_CAPACITY ||
        !path_join(scan->child, revision, L"data") || !path_join(scan->search, scan->child, L"*")) {
        goto done;
    }
    DWORD attributes = GetFileAttributesW(scan->child);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        goto done;
    }
    HANDLE search = FindFirstFileW(scan->search, &scan->entry);
    if (search == INVALID_HANDLE_VALUE) {
        goto done;
    }
    MarkerText text = {bytes, 64, 1};
    unsigned blobs = 0;
    static const char hexadecimal[] = "0123456789abcdef";
    do {
        if (scan->entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (scan->entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            text.valid = 0;
            break;
        }
        char name[256];
        if (++blobs > MAX_BLOBS || !regular_name(scan->entry.cFileName, name)) {
            text.valid = 0;
            break;
        }
        uint64_t blob_size = ((uint64_t)scan->entry.nFileSizeHigh << 32) | scan->entry.nFileSizeLow;
        unsigned char digest[32];
        char hash[65];
        if (!path_join(scan->search, scan->child, scan->entry.cFileName) ||
            !hash_blob(scan->search, blob_size, digest, hash_buffer)) {
            text.valid = 0;
            break;
        }
        for (unsigned index = 0; index < 32; ++index) {
            hash[index * 2] = hexadecimal[digest[index] >> 4];
            hash[index * 2 + 1] = hexadecimal[digest[index] & 15];
        }
        hash[64] = ' ';
        append_text(&text, hash, sizeof(hash));
        append_number(&text, blob_size);
        append_text(&text, " ", 1);
        append_text(&text, name, text_length(name));
        append_text(&text, "\n", 1);
    } while (FindNextFileW(search, &scan->entry));
    DWORD error = GetLastError();
    FindClose(search);
    if (!text.valid || !blobs || error != ERROR_NO_MORE_FILES) {
        goto done;
    }
    size_t body_size = text.size - 64;
    text.size = 0;
    static const char header[] = "Azimuth complete save revision v2\nblobs ";
    append_text(&text, header, sizeof(header) - 1);
    append_number(&text, blobs);
    append_text(&text, "\n", 1);
    memory_copy(bytes + text.size, bytes + 64, body_size);
    text.size += body_size;
    bytes[text.size] = 0;
    if (publish_marker(revision, scan->canonical, bytes, text.size)) {
        ++marker_count;
    } else {
        ++marker_failures;
    }
done:
    if (hash_buffer) {
        HeapFree(GetProcessHeap(), 0, hash_buffer);
    }
    if (bytes) {
        HeapFree(GetProcessHeap(), 0, bytes);
    }
    if (scan) {
        HeapFree(GetProcessHeap(), 0, scan);
    }
}

static int revision_name(const wchar_t *name) {
    unsigned count = 0;
    while (name[count]) {
        wchar_t c = name[count++];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return count == 16;
}

static void scan_directories(const wchar_t *root, unsigned level) {
    DirectoryScan *scan = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*scan));
    if (!scan) {
        return;
    }
    if (!path_join(scan->search, root, L"*")) {
        goto done;
    }
    HANDLE search = FindFirstFileW(scan->search, &scan->entry);
    if (search == INVALID_HANDLE_VALUE) {
        goto done;
    }
    do {
        if (!(scan->entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (scan->entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            scan->entry.cFileName[0] == '.' ||
            !path_join(scan->child, root, scan->entry.cFileName)) {
            continue;
        }
        if (level == 2) {
            if (revision_name(scan->entry.cFileName)) {
                repair_revision(scan->child);
            }
        } else if (level == 1) {
            if (path_join(scan->search, scan->child, L"revisions")) {
                scan_directories(scan->search, 2);
            }
        } else {
            scan_directories(scan->child, 1);
        }
    } while (FindNextFileW(search, &scan->entry));
    FindClose(search);
done:
    HeapFree(GetProcessHeap(), 0, scan);
}

static void repair_root(const wchar_t *root) {
    marker_count = marker_failures = 0;
    scan_directories(root, 0);
}

static void repair_title(const wchar_t *directory, unsigned title_id,
                         void (*write_log)(const char *)) {
    const wchar_t *relative = title_id == 0x1a96545bu ? L"data\\titles\\1A96545B\\saves" : NULL;
#ifdef FH2_ENABLE_FM5
    if (title_id == 0x7b35d917u) relative = L"data\\titles\\7B35D917\\saves";
#endif
    if (!relative) return;
    wchar_t *root = HeapAlloc(GetProcessHeap(), 0, PATH_CAPACITY * sizeof(wchar_t));
    if (!root) return;
    if (path_join(root, directory, relative)) {
        repair_root(root);
        if (write_log) {
            write_log(marker_count ? "AZIMUTH: missing save commit markers rebuilt from actual blob "
                                    "sizes and SHA-256 hashes.\r\n"
                                   : "AZIMUTH: no missing save commit markers required repair.\r\n");
            if (marker_failures)
                write_log("AZIMUTH: some missing markers could not be published; original save folders retained.\r\n");
        }
    }
    HeapFree(GetProcessHeap(), 0, root);
}

void azimuth_repair_title_markers(unsigned char *host, unsigned title_id,
                                 void (*write_log)(const char *)) {
    AzimuthBuild build = azimuth_host_build(host);
    if (build != AZIMUTH_101 && build != AZIMUTH_102 &&
        build != AZIMUTH_103 && build != AZIMUTH_104) return;
    DirectoryScan *scan = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*scan));
    if (!scan) {
        return;
    }
    DWORD length = GetModuleFileNameW(NULL, scan->child, PATH_CAPACITY);
    if (!length || length >= PATH_CAPACITY) {
        goto done;
    }
    while (length && scan->child[length - 1] != '\\' && scan->child[length - 1] != '/') {
        --length;
    }
    if (!length) {
        goto done;
    }
    scan->child[length - 1] = 0;
    repair_title(scan->child, title_id, write_log);
done:
    HeapFree(GetProcessHeap(), 0, scan);
}

void azimuth_repair_missing_markers(unsigned char *host) {
    azimuth_repair_title_markers(host, 0x1a96545bu, log_append);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fh2TestAzimuthMarkerRepair(const wchar_t *root) {
    repair_root(root);
    return marker_count;
}
__declspec(dllexport) unsigned Fh2TestAzimuthTitleMarkerRepair(const wchar_t *directory,
                                                             unsigned title_id) {
    marker_count = marker_failures = 0;
    repair_title(directory, title_id, NULL);
    return marker_count;
}
#endif
