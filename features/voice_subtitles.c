#include "voice_subtitles.h"
#include "logging.h"
#include "support.h"
#include "voice_subtitles_code.h"

/* Ported from the Ukrainian localization's standalone winmm.dll proxy; it runs
   inside Extra Options, so the game keeps one proxy DLL. */

#define TABLE_FILE "VoiceSubtitles.str"
#define TABLE_FILE_W L"VoiceSubtitles.str"
#define GAME_PREFERRED_BASE 0x140000000ull /* the hook code is assembled for it */
#define TICKS_PER_SECOND 10000000.0       /* KUSER_SHARED_DATA.InterruptTime units */
#define DEFAULT_SECONDS 3.0
#define MIN_SECONDS 2.0
#define MAX_SECONDS 60.0
#define MARGIN_SECONDS 0.6
#define MAX_ARCHIVE_SIZE (512u << 20)

typedef struct {
    unsigned long long hash, text;
    unsigned duration, reserved;
} VoiceEntry;

static unsigned rd16(const unsigned char *p) {
    return p[0] | (p[1] << 8);
}

static unsigned rd32(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned char *load_file(const wchar_t *path, DWORD *size) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    LARGE_INTEGER length;
    unsigned char *data = NULL;
    DWORD read = 0;
    if (GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= MAX_ARCHIVE_SIZE) {
        data = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart);
        if (data && (!ReadFile(file, data, (DWORD)length.QuadPart, &read, NULL) ||
                     read != (DWORD)length.QuadPart)) {
            HeapFree(GetProcessHeap(), 0, data);
            data = NULL;
        }
    }
    CloseHandle(file);
    *size = read;
    return data;
}

static int name_equal_ignore_case(const unsigned char *name, unsigned length, const char *text) {
    if (length != text_length(text)) {
        return 0;
    }
    for (unsigned i = 0; i < length; ++i) {
        unsigned char a = name[i], b = (unsigned char)text[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

/* Data of a stored entry in a zip image; NULL when missing or compressed. */
static const unsigned char *zip_entry(const unsigned char *zip, DWORD size, const char *name,
                                      DWORD *entry_size) {
    if (size < 22) {
        return NULL;
    }
    DWORD end = size - 22, lowest = size > 70000 ? size - 70000 : 0;
    while (end > lowest && rd32(zip + end) != 0x06054b50u) {
        --end;
    }
    if (rd32(zip + end) != 0x06054b50u) {
        return NULL;
    }
    unsigned count = rd16(zip + end + 10), position = rd32(zip + end + 16);
    for (unsigned i = 0; i < count && position + 46 <= size; ++i) {
        unsigned name_length = rd16(zip + position + 28), extra = rd16(zip + position + 30),
                 comment = rd16(zip + position + 32), local = rd32(zip + position + 42);
        if (position + 46 + name_length > size) {
            return NULL;
        }
        if (name_equal_ignore_case(zip + position + 46, name_length, name)) {
            unsigned method = rd16(zip + position + 10), stored = rd32(zip + position + 20);
            if (method != 0) {
                log_append("VOICE: " TABLE_FILE " is compressed in the archive; it must be stored.\r\n");
                return NULL;
            }
            if ((unsigned long long)local + 30 > size) {
                return NULL;
            }
            unsigned long long data = (unsigned long long)local + 30 + rd16(zip + local + 26) +
                                      rd16(zip + local + 28);
            if (data + stored > size) {
                return NULL;
            }
            *entry_size = stored;
            return zip + data;
        }
        position += 46 + name_length + extra + comment;
    }
    return NULL;
}

static unsigned long long fnv1a64(const char *text, size_t length) {
    unsigned long long hash = 0xcbf29ce484222325ull;
    while (length--) {
        hash = (hash ^ (unsigned char)*text++) * 0x100000001b3ull;
    }
    return hash;
}

static unsigned display_ticks(double seconds) {
    if (!(seconds >= MIN_SECONDS - MARGIN_SECONDS)) {
        seconds = MIN_SECONDS - MARGIN_SECONDS;
    }
    if (seconds > MAX_SECONDS) {
        seconds = MAX_SECONDS;
    }
    return (unsigned)(long long)((seconds + MARGIN_SECONDS) * TICKS_PER_SECOND + 0.5);
}

/* "<digits>[.<digits>]" in UTF-16 (the <id>~t rows); negative or junk gives 0. */
static double parse_seconds(const unsigned char *text, const unsigned char *end) {
    double value = 0, scale = 0;
    for (; text + 1 < end && rd16(text); text += 2) {
        unsigned c = rd16(text);
        if (c == '.' || c == ',') {
            if (scale) break;
            scale = 1;
        } else if (c >= '0' && c <= '9') {
            if (scale) {
                scale /= 10;
                value += (c - '0') * scale;
            } else {
                value = value * 10 + (c - '0');
            }
        } else if (c != ' ') {
            break;
        }
    }
    return value;
}

/* LSB2 string table -> hook table and text pool. `pool_address` is where the
   hook code sees the pool. Returns the entry count. */
static unsigned fill_table(const unsigned char *table, DWORD size, VoiceEntry *entries,
                           unsigned char *pool, unsigned long long pool_address) {
    const unsigned char *end = table + size;
    if (size < 40 || !memory_equal(table, "LSB2", 4)) {
        log_append("VOICE: " TABLE_FILE " is not an LSB2 string table.\r\n");
        return 0;
    }
    unsigned text_section = rd32(table + 20), id_section = rd32(table + 24);
    if ((unsigned long long)text_section + 8 > size || (unsigned long long)id_section + 8 > size) {
        return 0;
    }
    unsigned text_count = rd32(table + text_section + 4), id_count = rd32(table + id_section + 4);
    const unsigned char *texts = table + text_section + 8, *ids = table + id_section + 8;
    if ((unsigned long long)text_section + 8 + 6ull * (text_count + 1ull) > size ||
        (unsigned long long)id_section + 8 + 8ull * id_count > size) {
        return 0;
    }
    const unsigned char *blob = texts + 6 * (text_count + 1), *names = ids + 8 * id_count;
    unsigned count = 0, used = 0;
    /* Pass 0 adds the texts, pass 1 sets the durations from the <id>~t rows. */
    for (int pass = 0; pass < 2; ++pass) {
        for (unsigned i = 0; i < id_count; ++i) {
            unsigned hash = rd16(ids + 8 * i), name_offset = rd32(ids + 8 * i + 4), k = 0;
            const char *id = (const char *)names + name_offset;
            if ((const unsigned char *)id >= end) continue;
            size_t id_length = 0;
            while ((const unsigned char *)id + id_length < end && id[id_length]) ++id_length;
            if ((const unsigned char *)id + id_length >= end) continue;
            int is_time = id_length > 2 && id[id_length - 2] == '~' && id[id_length - 1] == 't';
            if (is_time != pass) continue;
            while (k < text_count && rd16(texts + 6 * k) != hash) ++k;
            if (k >= text_count) continue;
            const unsigned char *first = blob + 2ull * rd32(texts + 6 * k + 2),
                                *last = blob + 2ull * rd32(texts + 6 * (k + 1) + 2);
            if (first > last || last > end) continue;
            if (!is_time) {
                unsigned bytes = (unsigned)(last - first);
                if (count >= VC_TABLE_MAX || used + bytes + 2 > VC_POOL_SIZE) {
                    log_append_value("VOICE: subtitle table full at lines ", (int)count);
                    return count;
                }
                memory_copy(pool + used, first, bytes);
                pool[used + bytes] = pool[used + bytes + 1] = 0;
                entries[count].hash = fnv1a64(id, id_length);
                entries[count].text = pool_address + used;
                entries[count].duration = display_ticks(DEFAULT_SECONDS);
                used += bytes + 2;
                ++count;
            } else {
                unsigned long long owner = fnv1a64(id, id_length - 2);
                for (unsigned j = 0; j < count; ++j) {
                    if (entries[j].hash == owner) {
                        entries[j].duration = display_ticks(parse_seconds(first, last));
                        break;
                    }
                }
            }
        }
    }
    return count;
}

static int sites_match(unsigned char *game_image) {
    for (unsigned i = 0; i < VC_N_HOOKS; ++i) {
        unsigned char *site = game_image + (vc_hooks[i].site - GAME_PREFERRED_BASE);
        unsigned char current[8];
        if (!memory_read(site, current, vc_hooks[i].ndisp) ||
            !memory_equal(current, vc_hooks[i].orig, vc_hooks[i].ndisp)) {
            return 0;
        }
    }
    return 1;
}

#define REACH 0x7FF00000ll /* rel32 reach, with a margin */

static unsigned image_size(const unsigned char *image) {
    unsigned header = 0, size = 0;
    if (!memory_read(image + 0x3c, &header, 4) || header > 0x1000 ||
        !memory_read(image + header + 0x50, &size, 4)) { /* OptionalHeader.SizeOfImage */
        return 0;
    }
    return size;
}

/* Lowest and highest game address the cave code reaches by rel32: the hook
   sites (jumps into the cave) and the targets of the blob's calls and jumps. */
static void game_targets(unsigned long long *low, unsigned long long *high) {
    *low = ~0ull;
    *high = 0;
    for (unsigned i = 0; i < VC_N_HOOKS + VC_N_REL_REL32; ++i) {
        unsigned long long target;
        if (i < VC_N_HOOKS) {
            target = vc_hooks[i].site;
        } else {
            unsigned offset = vc_rel_rel32[i - VC_N_HOOKS];
            int displacement;
            memory_copy(&displacement, vc_code + (offset - VC_OFF_CODE), 4);
            target = VC_LINK_CAVE + offset + 4 + (long long)displacement;
        }
        if (target < *low) *low = target;
        if (target > *high) *high = target;
    }
}

static int cave_reaches(unsigned long long cave, unsigned long long low, unsigned long long high) {
    return (long long)(cave + VC_CAVE_SIZE - low) < REACH && (long long)(high - cave) < REACH;
}

/* Commits the cave in the nearest free memory past the game image (then below
   it) from which every rel32 of the hook code reaches its target. */
static unsigned char *allocate_cave(unsigned char *game_image) {
    SYSTEM_INFO system;
    unsigned long long low, high, granularity, size = image_size(game_image);
    GetSystemInfo(&system);
    granularity = system.dwAllocationGranularity ? system.dwAllocationGranularity : 0x10000;
    game_targets(&low, &high);
    if (!size) {
        return NULL;
    }
    /* Upward from the image end. */
    for (unsigned long long address = (uintptr_t)game_image + size;;) {
        MEMORY_BASIC_INFORMATION region;
        address = (address + granularity - 1) & ~(granularity - 1);
        if (!cave_reaches(address, low, high) ||
            !VirtualQuery((void *)(uintptr_t)address, &region, sizeof(region))) {
            break;
        }
        unsigned long long end = (uintptr_t)region.BaseAddress + region.RegionSize;
        if (region.State == MEM_FREE && address + VC_CAVE_SIZE <= end) {
            void *cave = VirtualAlloc((void *)(uintptr_t)address, VC_CAVE_SIZE, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
            if ((uintptr_t)cave == address) return (unsigned char *)cave;
            if (cave) VirtualFree(cave, 0, MEM_RELEASE);
        }
        if (end <= address) break;
        address = end;
    }
    /* Downward from the image base. */
    for (unsigned long long top = (uintptr_t)game_image; top > VC_CAVE_SIZE + granularity;) {
        MEMORY_BASIC_INFORMATION region;
        if (!VirtualQuery((void *)(uintptr_t)(top - 1), &region, sizeof(region))) {
            break;
        }
        unsigned long long base = (uintptr_t)region.BaseAddress;
        unsigned long long address = (top - VC_CAVE_SIZE) & ~(granularity - 1);
        if (!cave_reaches(address, low, high)) {
            break;
        }
        if (region.State == MEM_FREE && address >= base) {
            void *cave = VirtualAlloc((void *)(uintptr_t)address, VC_CAVE_SIZE, MEM_COMMIT | MEM_RESERVE,
                                      PAGE_EXECUTE_READWRITE);
            if ((uintptr_t)cave == address) return (unsigned char *)cave;
            if (cave) VirtualFree(cave, 0, MEM_RELEASE);
        }
        if (base >= top) break;
        top = base;
    }
    return NULL;
}

/* Copies the hook code into the cave and applies the relocation table. */
static void place_code(unsigned char *cave) {
    long long delta = (long long)((uintptr_t)cave - VC_LINK_CAVE);
    memory_copy(cave, vc_head, sizeof(vc_head));
    memory_copy(cave + VC_OFF_CODE, vc_code, sizeof(vc_code));
    for (unsigned i = 0; i < VC_N_REL_ABS64; ++i) {
        unsigned long long value;
        memory_copy(&value, cave + vc_rel_abs64[i], 8);
        value += (unsigned long long)delta;
        memory_copy(cave + vc_rel_abs64[i], &value, 8);
    }
    for (unsigned i = 0; i < VC_N_REL_REL32; ++i) {
        int value;
        memory_copy(&value, cave + vc_rel_rel32[i], 4);
        value = (int)(value - delta); /* cave_reaches() keeps it in range */
        memory_copy(cave + vc_rel_rel32[i], &value, 4);
    }
}

static void log_cave(const unsigned char *cave) {
    static const char digits[] = "0123456789abcdef";
    char line[64] = "VOICE: hook code at 0x";
    unsigned length = (unsigned)text_length(line);
    unsigned long long value = (uintptr_t)cave;
    for (int shift = 60; shift >= 0; shift -= 4) line[length++] = digits[(value >> shift) & 15];
    memory_copy(line + length, ".\r\n", 4);
    log_append(line);
}

/* Returns the number of sites written; all of them on success. */
static unsigned write_hooks(const unsigned char *cave) {
    for (unsigned i = 0; i < VC_N_HOOKS; ++i) {
        long long relative = (long long)((uintptr_t)cave + vc_hooks[i].hook) - (long long)(vc_hooks[i].site + 5);
        if (relative != (int)relative || vc_hooks[i].ndisp < 5 || vc_hooks[i].ndisp > 8) {
            return 0;
        }
    }
    for (unsigned i = 0; i < VC_N_HOOKS; ++i) {
        unsigned char *site = (unsigned char *)(uintptr_t)vc_hooks[i].site, patch[8];
        DWORD protection, ignored;
        int displacement = (int)((long long)((uintptr_t)cave + vc_hooks[i].hook) - (long long)(vc_hooks[i].site + 5));
        patch[0] = 0xe9;
        memory_copy(patch + 1, &displacement, 4);
        for (unsigned k = 5; k < sizeof(patch); ++k) patch[k] = 0x90;
        if (!VirtualProtect(site, vc_hooks[i].ndisp, PAGE_EXECUTE_READWRITE, &protection)) {
            return i;
        }
        memory_copy(site, patch, vc_hooks[i].ndisp);
        VirtualProtect(site, vc_hooks[i].ndisp, protection, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, vc_hooks[i].ndisp);
    }
    return VC_N_HOOKS;
}

/* Loose <archive>\VoiceSubtitles.str first, then the stored entry in <archive>.zip.
   Returns the table inside *file (free it with HeapFree), or NULL. */
static const unsigned char *load_table(const wchar_t *stringtables, const wchar_t *archive,
                                       unsigned char **file, DWORD *table_size) {
    static wchar_t path[PATH_CAPACITY], name[64];
    DWORD size = 0;
    const unsigned char *table = NULL;
    unsigned length = 0;
    *file = NULL;
    *table_size = 0;
    while (archive[length] && length + 20 < ARRAY_COUNT(name)) {
        name[length] = archive[length];
        ++length;
    }
    memory_copy(name + length, L"\\" TABLE_FILE_W, sizeof(L"\\" TABLE_FILE_W));
    if (path_join(path, stringtables, name) && (*file = load_file(path, &size)) != NULL) {
        table = *file;
        *table_size = size;
    } else {
        memory_copy(name + length, L".zip", sizeof(L".zip"));
        if (path_join(path, stringtables, name) && (*file = load_file(path, &size)) != NULL) {
            table = zip_entry(*file, size, TABLE_FILE, table_size);
        }
    }
    if (!table && *file) {
        HeapFree(GetProcessHeap(), 0, *file);
        *file = NULL;
    }
    return table;
}

unsigned voice_subtitles_install(unsigned char *game_image, const wchar_t *stringtables,
                                 const wchar_t *archive) {
    if ((uintptr_t)game_image != GAME_PREFERRED_BASE) {
        log_append("VOICE: the game image is relocated; voice subtitles are off.\r\n");
        return 0;
    }
    if (!sites_match(game_image)) {
        log_append("VOICE: unexpected code at the speech hooks; voice subtitles are off.\r\n");
        return 0;
    }
    DWORD table_size = 0;
    unsigned char *file;
    const unsigned char *table = load_table(stringtables, archive, &file, &table_size);
    if (!table) {
        log_append("VOICE: no " TABLE_FILE " in the language pack; voice subtitles are off.\r\n");
        return 0;
    }
    unsigned char *cave = allocate_cave(game_image);
    unsigned long long count = 0;
    if (cave) {
        log_cave(cave);
        place_code(cave);
        count = fill_table(table, table_size, (VoiceEntry *)(cave + VC_OFF_TABLE),
                           cave + VC_OFF_POOL, (uintptr_t)cave + VC_OFF_POOL);
        /* Published before the hooks go in: they match only from now on. */
        memory_copy(cave + VC_OFF_COUNT, &count, 8);
    } else {
        log_append("VOICE: no free memory for the hook code near the game image; voice subtitles are off.\r\n");
    }
    HeapFree(GetProcessHeap(), 0, file);
    if (!cave) {
        return 0;
    }
    if (!count) {
        VirtualFree(cave, 0, MEM_RELEASE);
        log_append("VOICE: " TABLE_FILE " has no lines; voice subtitles are off.\r\n");
        return 0;
    }
    unsigned written = write_hooks(cave);
    if (written != VC_N_HOOKS) {
        /* Sites already written jump into the cave, so it stays mapped. */
        if (!written) VirtualFree(cave, 0, MEM_RELEASE);
        log_append_value("VOICE: cannot write all speech hooks; written ", (int)written);
        return 0;
    }
    log_append_value("VOICE: subtitles for spoken lines installed, lines ", (int)count);
    return (unsigned)count;
}

#ifdef FH2_TWEAKS_TEST
/* Reads <stringtables>\<archive>\VoiceSubtitles.str (or the stored entry in
   <archive>.zip) as voice_subtitles_install does, without touching the game.
   Fills up to `capacity` hashes and display times (100 ns units) and copies
   the first line's text. Returns the number of lines. */
__declspec(dllexport) unsigned Fh2TestVoiceSubtitles(const wchar_t *stringtables,
                                                     const wchar_t *archive,
                                                     unsigned long long *hashes,
                                                     unsigned *durations, unsigned capacity,
                                                     wchar_t *first_text, unsigned text_capacity) {
    DWORD table_size = 0;
    unsigned char *file;
    const unsigned char *table = load_table(stringtables, archive, &file, &table_size);
    if (!table) {
        return 0;
    }
    SIZE_T bytes = VC_TABLE_MAX * sizeof(VoiceEntry) + VC_POOL_SIZE;
    unsigned char *memory = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    unsigned count = 0;
    if (memory) {
        VoiceEntry *entries = (VoiceEntry *)memory;
        unsigned char *pool = memory + VC_TABLE_MAX * sizeof(VoiceEntry);
        count = fill_table(table, table_size, entries, pool, (uintptr_t)pool);
        for (unsigned i = 0; i < count && i < capacity; ++i) {
            hashes[i] = entries[i].hash;
            durations[i] = entries[i].duration;
        }
        if (text_capacity) {
            first_text[0] = 0;
        }
        if (count && text_capacity) {
            const wchar_t *text = (const wchar_t *)(uintptr_t)entries[0].text;
            unsigned i = 0;
            for (; text[i] && i + 1 < text_capacity; ++i) {
                first_text[i] = text[i];
            }
            first_text[i] = 0;
        }
        HeapFree(GetProcessHeap(), 0, memory);
    }
    HeapFree(GetProcessHeap(), 0, file);
    return count;
}
#endif
