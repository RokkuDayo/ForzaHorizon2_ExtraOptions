#include "fh2/loose_media_index.h"

typedef struct LooseMediaName {
    struct LooseMediaName *next;
    uint64_t hash;
    size_t length;
    char path[];
} LooseMediaName;

struct LooseMediaIndex {
    LooseMediaName **buckets;
    size_t capacity, count;
};

static unsigned fold_path(unsigned ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + ('a' - 'A');
    return ch == '/' ? '\\' : ch;
}

static uint64_t hash_character(uint64_t hash, unsigned ch) {
    return (hash ^ fold_path(ch)) * UINT64_C(1099511628211);
}

static int grow_index(LooseMediaIndex *index) {
    size_t capacity = index->capacity ? index->capacity * 2 : 64;
    if (capacity > (1u << 21)) return 0;
    HANDLE heap = GetProcessHeap();
    LooseMediaName **buckets = HeapAlloc(heap, HEAP_ZERO_MEMORY, capacity * sizeof(*buckets));
    if (!buckets) return 0;
    for (size_t i = 0; i < index->capacity; ++i) {
        LooseMediaName *entry = index->buckets[i];
        while (entry) {
            LooseMediaName *next = entry->next;
            size_t slot = entry->hash & (capacity - 1);
            entry->next = buckets[slot];
            buckets[slot] = entry;
            entry = next;
        }
    }
    if (index->buckets) HeapFree(heap, 0, index->buckets);
    index->buckets = buckets;
    index->capacity = capacity;
    return 1;
}

static int add_file(LooseMediaIndex *index, const wchar_t *path, size_t length, uint64_t hash) {
    if (index->count >= index->capacity && !grow_index(index)) return 0;
    LooseMediaName *entry = HeapAlloc(GetProcessHeap(), 0, sizeof(*entry) + length);
    if (!entry) return 0;
    entry->hash = hash;
    entry->length = length;
    for (size_t i = 0; i < length; ++i) entry->path[i] = (char)fold_path(path[i]);
    size_t slot = hash & (index->capacity - 1);
    entry->next = index->buckets[slot];
    index->buckets[slot] = entry;
    ++index->count;
    return 1;
}

static int scan_directory(LooseMediaIndex *index, wchar_t *path, size_t root, size_t end,
                          unsigned depth, uint64_t prefix_hash) {
    if (end + 2 >= PATH_CAPACITY || depth >= 64) return 0;
    path[end] = L'*';
    path[end + 1] = 0;
    WIN32_FIND_DATAW data;
    HANDLE scan = FindFirstFileW(path, &data);
    if (scan == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    int complete = 1;
    do {
        const wchar_t *name = data.cFileName;
        if (name[0] == L'.' && (!name[1] || (name[1] == L'.' && !name[2]))) continue;
        size_t length = (size_t)lstrlenW(name);
        if (end + length + 2 >= PATH_CAPACITY || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            complete = 0;
            break;
        }
        // A short alias containing '~' uses the native path fallback at lookup.
        const wchar_t *alias = data.cAlternateFileName;
        while (*alias && *alias != L'~') ++alias;
        if (data.cAlternateFileName[0] && !*alias) {
            complete = 0;
            break;
        }
        uint64_t hash = prefix_hash;
        for (size_t i = 0; i < length; ++i) {
            if (name[i] >= 128) {
                complete = 0;
                break;
            }
            hash = hash_character(hash, name[i]);
        }
        if (!complete) break;
        memory_copy(path + end, name, length * sizeof(*path));
        size_t next = end + length;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            path[next] = L'\\';
            complete = scan_directory(index, path, root, next + 1, depth + 1,
                                      hash_character(hash, '\\'));
        } else {
            complete = add_file(index, path + root, next - root, hash);
        }
    } while (complete && FindNextFileW(scan, &data));
    if (complete && GetLastError() != ERROR_NO_MORE_FILES) complete = 0;
    FindClose(scan);
    return complete;
}

LooseMediaIndex *loose_media_index_create(const wchar_t *folder) {
    HANDLE heap = GetProcessHeap();
    LooseMediaIndex *index = HeapAlloc(heap, HEAP_ZERO_MEMORY, sizeof(*index));
    wchar_t *path = HeapAlloc(heap, 0, PATH_CAPACITY * sizeof(*path));
    size_t length = (size_t)lstrlenW(folder);
    int complete = 0;
    if (index && path && length + 3 < PATH_CAPACITY) {
        memory_copy(path, folder, length * sizeof(*path));
        path[length++] = L'\\';
        complete = scan_directory(index, path, length, length, 0, UINT64_C(14695981039346656037));
    }
    if (path) HeapFree(heap, 0, path);
    if (complete) return index;
    loose_media_index_destroy(index);
    return NULL;
}

void loose_media_index_destroy(LooseMediaIndex *index) {
    if (!index) return;
    HANDLE heap = GetProcessHeap();
    for (size_t i = 0; i < index->capacity; ++i) {
        LooseMediaName *entry = index->buckets[i];
        while (entry) {
            LooseMediaName *next = entry->next;
            HeapFree(heap, 0, entry);
            entry = next;
        }
    }
    if (index->buckets) HeapFree(heap, 0, index->buckets);
    HeapFree(heap, 0, index);
}

int loose_media_may_exist(const LooseMediaIndex *index, const char *path, size_t length) {
    if (!index) return 1;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; ++i) {
        unsigned ch = (unsigned char)path[i];
        if (ch >= 128 || ch == '~') return 1;
        hash = hash_character(hash, ch);
    }
    if (!index->capacity) return 0;
    size_t slot = hash & (index->capacity - 1);
    for (const LooseMediaName *entry = index->buckets[slot]; entry; entry = entry->next) {
        if (entry->hash != hash || entry->length != length) continue;
        size_t i = 0;
        while (i < length && (unsigned char)entry->path[i] == fold_path((unsigned char)path[i])) ++i;
        if (i == length) return 1;
    }
    return 0;
}
