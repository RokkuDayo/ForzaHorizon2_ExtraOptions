#include "fm5/fm5_patch.h"
#include "fm5/fm5_vehicle.h"
#include "shared/logging.h"
#include "shared/support.h"

enum { MAX_POOL = 16, MAX_ROSTER = 32, ROSTER_STRIDE = 0x2640 };
typedef struct {
    union { wchar_t small[8]; const wchar_t *data; };
    size_t length, capacity;
} WideString;
typedef struct {
    CRITICAL_SECTION lock;
    void *owner;
    WideString name;
} Livery;
_Static_assert(sizeof(Livery) == 0x50, "FM5 livery layout");

typedef struct {
    uint64_t driver;
    void *car;
    unsigned ordinal;
} Candidate;
typedef struct {
    wchar_t name[MAX_PATH];
    unsigned ordinal;
} DesignSlot;
typedef struct {
    volatile LONG references, cancelled, done;
    DesignSlot *slots;
    size_t count;
    wchar_t pattern[PATH_CAPACITY], path[PATH_CAPACITY], revisions[PATH_CAPACITY],
            revision_pattern[PATH_CAPACITY];
} LineupScan;
typedef struct {
    wchar_t slot[MAX_PATH];
    Livery livery;
    unsigned ordinal;
    int render_requested;
} Resident;
typedef struct {
    uint64_t driver;
    void *car;
    Resident *design;
    void *presentation;
    unsigned attempts;
} Assignment;

static unsigned char *image;
static wchar_t library[PATH_CAPACITY];
static void (*report)(const char *);
static unsigned limit, resident_count, assignment_count, applied_count;
static Resident residents[MAX_POOL];
static Assignment assignments[MAX_POOL];
static LineupScan *library_scan;
static volatile LONG lineup_generation;
static LONG seen_generation;
static void *seen_manager;
static uintptr_t seen_end;
static void *seen_simulation;
static unsigned seen_cars;
static Candidate seen_candidates[MAX_ROSTER];
static unsigned seen_candidate_count;
static ULONGLONG until, next_update, next_check, next_lineup_check, lineup_deadline;

static void *pointer(const void *object, size_t offset) {
    void *result = NULL;
    if (object) memory_read_pointer((const unsigned char *)object + offset, &result);
    return result;
}

static void *method(void *object, size_t offset) {
    return pointer(pointer(object, 0), offset);
}

static void release(void *object) {
    if (object) ((void (*)(void *))method(object, 0x10))(object);
}

static void release_scan(LineupScan *scan) {
    if (InterlockedDecrement(&scan->references) != 0) return;
    if (scan->slots) HeapFree(GetProcessHeap(), 0, scan->slots);
    HeapFree(GetProcessHeap(), 0, scan);
}

static WideString borrow_wide(const wchar_t *text) {
    WideString string = {0};
    string.length = (size_t)lstrlenW(text);
    string.capacity = string.length < 8 ? 7 : string.length;
    if (string.length < 8) memory_copy(string.small, text, (string.length + 1) * sizeof(wchar_t));
    else string.data = text;
    return string;
}

static void *active_profile(void) {
    void *service = pointer(image, 0x25d4138);
    void *get = method(service, 0x50);
    return get ? ((void *(*)(void *))get)(service) : NULL;
}

static int designs_enabled(void *profile) {
    void *get = method(profile, 0xc0);
    void *options = get ? ((void *(*)(void *))get)(profile) : NULL;
    // This getter returns true when EnableDrivatarLiveries is disabled.
    return pointer(options, 0x20) && !((unsigned char (*)(void *))(image + 0x1a81c0))(options);
}

static void *read_design(void *profile, const WideString *slot) {
    void *manager = pointer(image, 0x25e7a20);
    void *get = method(profile, 0xa0);
    void *context = get ? pointer(((void *(*)(void *))get)(profile), 0) : NULL;
    if (!manager || !context) return NULL;

    // The native caller at 0x1db34a prepends its compression prefix, "C_",
    // before requesting the blob. The request consumes context.
    WideString blob = borrow_wide(L"C_livery");
    void *container = NULL;
    struct { void *vtable; void **output; void *reserved; void *target; }
        callback = {image + 0x22806e0, &container, NULL, &callback};
    void *future[2] = {0};
    ((void (*)(void *))method(context, 8))(context);
    ((void (*)(void *, void **, const WideString *, const WideString *, void **, void *,
               unsigned char, void *))(image + 0x772540))
        (manager, future, slot, &blob, &context, &callback, 0, NULL);
    unsigned char *control = future[1];
    if (control && InterlockedDecrement((volatile LONG *)(control + 8)) == 0) {
        ((void (*)(void *))method(control, 0))(control);
        if (InterlockedDecrement((volatile LONG *)(control + 12)) == 0)
            ((void (*)(void *))method(control, 8))(control);
    }
    if (callback.target)
        ((void (*)(void *, unsigned char))method(callback.target, 0x20))
            (callback.target, callback.target != &callback);
    return container;
}

static void retire_library_scan(void) {
    if (!library_scan) return;
    InterlockedExchange(&library_scan->cancelled, 1);
    release_scan(library_scan);
    library_scan = NULL;
}

static void clear_pool(void) {
    retire_library_scan();
    for (unsigned i = 0; i < resident_count; ++i) {
        Livery *livery = &residents[i].livery;
        release(livery->owner);
        if (livery->name.capacity >= 8)
            ((void (*)(const void *))(image + 0x1cdc30))(livery->name.data);
        DeleteCriticalSection(&livery->lock);
    }
    resident_count = assignment_count = applied_count = 0;
    seen_candidate_count = 0;
}

static unsigned field(const void *object, size_t offset) {
    unsigned value = 0;
    if (object) memory_read((const unsigned char *)object + offset, &value, sizeof(value));
    return value;
}

static int appearance_ready(const void *owner) {
    // CarPresentation uses the blocking native resource acquire. Check its
    // ready conditions here so a partially loaded design cannot stall it.
    return field(owner, 0x34) == 4 && field(owner, 0x38) == 4 && !field(owner, 0x4c);
}

static Resident *resident_design(const wchar_t *name) {
    for (unsigned i = 0; i < resident_count; ++i)
        if (CompareStringOrdinal(name, -1, residents[i].slot, -1, TRUE) == CSTR_EQUAL)
            return &residents[i];
    return NULL;
}

static Resident *load_design(void *profile, const wchar_t *name, unsigned ordinal) {
    WideString slot = borrow_wide(name);
    void *container = read_design(profile, &slot);
    if (!container) return NULL;
    Resident *resident = &residents[resident_count];
    Livery *livery = &resident->livery;
    *livery = (Livery){0};
    if (!InitializeCriticalSectionAndSpinCount(&livery->lock, 0x600)) {
        release(container);
        return NULL;
    }
    livery->name.capacity = 7;
    struct { void *allocation; uintptr_t reserved; size_t length, capacity; } narrow = {0};
    ((void (*)(void *, const wchar_t *))(image + 0x686a30))(&narrow, name);
    ((void (*)(Livery *, const void *, void **))(image + 0x7bff30))(livery, &narrow, &container);
    if (narrow.capacity >= 16 && narrow.allocation)
        ((void (*)(void *))(image + 0x1cdc30))(narrow.allocation);
    if (!livery->owner) {
        DeleteCriticalSection(&livery->lock);
        return NULL;
    }
    ((void (*)(WideString *, const WideString *, size_t, size_t))(image + 0x149780))
        (&livery->name, &slot, 0, SIZE_MAX);

    lstrcpyW(resident->slot, name);
    resident->ordinal = ordinal;
    resident->render_requested = 0;
    ++resident_count;
    return resident;
}

static void request_render(void) {
    void *manager = pointer(image, 0x25e7ad0);
    if (!manager) return;
    for (unsigned i = 0; i < resident_count; ++i) {
        Resident *design = &residents[i];
        void *owner = design->livery.owner;
        if (design->render_requested || !appearance_ready(owner) ||
            field(owner, 0x104) != design->ordinal)
            continue;
        // Match the opponent setup path's scoped livery-manager flag.
        unsigned char previous = ((unsigned char (*)(void *))(image + 0x182840))(manager);
        ((void (*)(void *, unsigned char))(image + 0x182830))(manager, 1);
        ((void (*)(Livery *, unsigned))(image + 0x7c0b00))(&design->livery, 5);
        ((void (*)(void *, unsigned char))(image + 0x182830))(manager, previous);
        design->render_requested = 1;
        return;
    }
}

static unsigned read_ordinal(LineupScan *scan, const wchar_t *slot) {
    wchar_t *path = scan->path, *revisions = scan->revisions, *pattern = scan->revision_pattern;
    if (!path_join(path, library, slot) || !path_join(revisions, path, L"revisions") ||
        !path_join(pattern, revisions, L"*")) return 0;

    wchar_t latest[17] = {0};
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || lstrlenW(data.cFileName) != 16 ||
            CompareStringOrdinal(data.cFileName, -1, latest, -1, TRUE) != CSTR_GREATER_THAN ||
            !path_join(path, revisions, data.cFileName) || !path_join(pattern, path, L"commit")) continue;
        DWORD attrs = GetFileAttributesW(pattern);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY))
            lstrcpyW(latest, data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (!latest[0] || !path_join(path, revisions, latest) ||
        !path_join(pattern, path, L"data\\header")) return 0;

    unsigned ordinal = 0;
    HANDLE file = CreateFileW(pattern, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size, tail = {.QuadPart = -12};
        DWORD read;
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 32 ||
            !SetFilePointerEx(file, tail, NULL, FILE_END) ||
            !ReadFile(file, &ordinal, sizeof(ordinal), &read, NULL) || read != sizeof(ordinal))
            ordinal = 0;
        CloseHandle(file);
    }
    return ordinal;
}

// The worker only reads save headers. Native save and render calls remain on
// the game thread, and no native objects are retained by the worker.
static DWORD WINAPI scan_library(void *context) {
    LineupScan *scan = context;
    size_t capacity = 0;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(scan->pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (InterlockedCompareExchange(&scan->cancelled, 0, 0)) break;
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            unsigned ordinal = read_ordinal(scan, data.cFileName);
            if (!ordinal) continue;
            if (scan->count == capacity) {
                if (capacity > SIZE_MAX / sizeof(DesignSlot) / 2) break;
                size_t next = capacity ? capacity * 2 : 32;
                HANDLE heap = GetProcessHeap();
                DesignSlot *slots = scan->slots
                    ? HeapReAlloc(heap, 0, scan->slots, next * sizeof(*slots))
                    : HeapAlloc(heap, 0, next * sizeof(*slots));
                if (!slots) break;
                scan->slots = slots;
                capacity = next;
            }
            DesignSlot *slot = &scan->slots[scan->count++];
            slot->ordinal = ordinal;
            lstrcpyW(slot->name, data.cFileName);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    InterlockedExchange(&scan->done, 1);
    release_scan(scan);
    return 0;
}

static int library_ready(void) {
    if (library_scan)
        return InterlockedCompareExchange(&library_scan->done, 0, 0) != 0;
    LineupScan *scan = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*scan));
    if (!scan) return 0;
    if (!path_join(scan->pattern, library, L"Livery_*")) {
        HeapFree(GetProcessHeap(), 0, scan);
        return 0;
    }
    scan->references = 2;
    HANDLE worker = CreateThread(NULL, 0, scan_library, scan, 0, NULL);
    if (!worker) {
        HeapFree(GetProcessHeap(), 0, scan);
        return 0;
    }
    CloseHandle(worker);
    library_scan = scan;
    return 0;
}

static unsigned random_next(unsigned *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static unsigned car_array(void *simulation, size_t offset, void ***cars) {
    uintptr_t begin = (uintptr_t)pointer(simulation, offset);
    uintptr_t end = (uintptr_t)pointer(simulation, offset + sizeof(void *));
    if (!begin || end <= begin || (end - begin) % sizeof(void *) ||
        end - begin > MAX_ROSTER * sizeof(void *)) return 0;
    *cars = (void **)begin;
    return (unsigned)((end - begin) / sizeof(void *));
}

static int offline_opponent(void *car) {
    unsigned role;
    unsigned char non_participant;
    // Offline Drivatars use role 1 in FM5. A network-owned proxy uses role 2.
    if (!car || !memory_read((unsigned char *)car + 0x4c00, &role, sizeof(role)) || role != 1 ||
        !memory_read((unsigned char *)car + 0x3d2, &non_participant, 1) || non_participant)
        return 0;

    // The native race setup checks this owner method for player control.
    // The AI controller can be absent even on an offline opponent.
    void *driver = pointer(car, 0x4bf8);
    void *is_player = method(driver, 0x170);
    return !driver || (is_player && !((unsigned char (*)(void *))is_player)(driver));
}

static unsigned collect_candidates(Candidate *candidates, void **cars, unsigned car_count,
                                   uintptr_t begin, unsigned count) {
    unsigned eligible = 0;
    int use_live = !assignment_count || assignments[0].car;
    for (unsigned i = 0; use_live && i < car_count; ++i) {
        void *car = pointer(cars + i, 0);
        unsigned ordinal;
        if (offline_opponent(car) &&
            memory_read((unsigned char *)car + 0x100, &ordinal, sizeof(ordinal)) && ordinal)
            candidates[eligible++] = (Candidate){.car = car, .ordinal = ordinal};
    }
    if (eligible || (assignment_count && assignments[0].car)) return eligible;

    for (unsigned i = 0; i < count; ++i) {
        const unsigned char *entry = (const unsigned char *)(begin + i * ROSTER_STRIDE);
        unsigned char drivatar = 0;
        uint64_t driver;
        unsigned ordinal;
        if (memory_read(entry + 0x20, &drivatar, 1) && drivatar &&
            memory_read(entry + 0x28, &driver, sizeof(driver)) && driver &&
            memory_read(entry + 0x30, &ordinal, sizeof(ordinal)) && ordinal)
            candidates[eligible++] = (Candidate){.driver = driver, .ordinal = ordinal};
    }
    return eligible;
}

static void prepare_lineup(void *profile, const Candidate *candidates, unsigned eligible) {
    // Group unassigned cars by model. Each group samples up to one unique
    // design per car in a single pass over the library.
    unsigned group[MAX_ROSTER], order[MAX_ROSTER], size[MAX_ROSTER] = {0};
    unsigned group_of[MAX_ROSTER], found[MAX_ROSTER] = {0}, pending = 0;
    for (unsigned i = 0; i < eligible; ++i) {
        const Candidate *candidate = &candidates[i];
        unsigned j;
        for (j = 0; j < assignment_count; ++j) {
            const Assignment *assignment = &assignments[j];
            if (candidate->car ? candidate->car == assignment->car :
                candidate->driver == assignment->driver) break;
        }
        if (j < assignment_count) continue;
        for (j = 0; j < pending && candidates[order[j]].ordinal != candidate->ordinal; ++j) {}
        group_of[pending] = j;
        ++size[j];
        order[pending++] = i;
    }
    if (!pending) return;
    unsigned offset = 0;
    for (unsigned i = 0; i < pending; ++i) {
        group[i] = offset;
        offset += size[i];
    }
    if (!library_scan->count) {
        if (report) report("LOCAL DESIGNS: FM5 local design library is empty or unavailable.\r\n");
        return;
    }
    const DesignSlot *slots[MAX_ROSTER];
    unsigned random = (unsigned)GetTickCount64() | 1;
    for (size_t index = 0; index < library_scan->count; ++index) {
        const DesignSlot *design = &library_scan->slots[index];
        for (unsigned i = 0; i < pending; ++i) {
            if (size[i] && design->ordinal == candidates[order[i]].ordinal) {
                if (!resident_design(design->name)) {
                    unsigned n = found[i]++;
                    unsigned slot = n < size[i] ? n : random_next(&random) % (n + 1);
                    if (slot < size[i]) slots[group[i] + slot] = design;
                }
                break;
            }
        }
    }

    // Sample without replacement so a fixed starting-grid position is not favored.
    unsigned used[MAX_ROSTER] = {0}, cars_order[MAX_ROSTER];
    unsigned matched = eligible - pending;
    unsigned previous = assignment_count;
    for (unsigned i = 0; i < pending; ++i) {
        cars_order[i] = i;
        unsigned ordinal = candidates[order[i]].ordinal;
        if (found[group_of[i]]) {
            ++matched;
            continue;
        }
        for (unsigned j = 0; j < resident_count; ++j) {
            if (residents[j].ordinal != ordinal) continue;
            ++matched;
            break;
        }
    }
    for (unsigned i = 0; i < pending && assignment_count < limit; ++i) {
        unsigned j = i + random_next(&random) % (pending - i);
        unsigned entry = cars_order[j], model = group_of[entry];
        const Candidate *candidate = &candidates[order[entry]];
        cars_order[j] = cars_order[i];
        Resident *design = NULL;
        while (!design && used[model] < size[model] && used[model] < found[model]) {
            const wchar_t *slot = slots[group[model] + used[model]++]->name;
            design = load_design(profile, slot, candidate->ordinal);
        }
        if (!design) {
            unsigned choices = 0;
            for (unsigned k = 0; k < resident_count; ++k)
                if (residents[k].ordinal == candidate->ordinal && random_next(&random) % ++choices == 0)
                    design = &residents[k];
        }
        if (design) assignments[assignment_count++] =
            (Assignment){.driver = candidate->driver, .car = candidate->car, .design = design};
    }
    if (report) {
        char line[] = "LOCAL DESIGNS: FM5 lineup eligible=## matched=## queued=##.\r\n";
        const unsigned counts[] = {eligible, matched, assignment_count};
        unsigned i = 0;
        for (char *p = line; *p; ++p) {
            if (*p != '#') continue;
            unsigned value = counts[i++];
            *p++ = (char)('0' + value / 10);
            *p = (char)('0' + value % 10);
        }
        report(line);
    }
    if (assignment_count > previous) until = GetTickCount64() + 15000;
}

static int lineup_changed(const Candidate *candidates, unsigned count) {
    int changed = count != seen_candidate_count;
    for (unsigned i = 0; !changed && i < count; ++i) {
        const Candidate *old = &seen_candidates[i], *current = &candidates[i];
        changed = old->car != current->car || old->driver != current->driver ||
                  old->ordinal != current->ordinal;
    }
    if (changed) {
        memory_copy(seen_candidates, candidates, count * sizeof(*candidates));
        seen_candidate_count = count;
    }
    return changed;
}

static void apply_ready(void **cars, unsigned count, int live) {
    applied_count = 0;
    int attached = 0;
    for (unsigned index = 0; index < count; ++index) {
        void *car = pointer(cars + index, 0);
        void *presentation = pointer(car, 0x4c18);
        void *attach = method(presentation, 0x2a8);
        // The null and default presentations share a stub that discards the
        // design. Wait for native car setup and a working presentation.
        void *base_owner = pointer(car, 0x2d0);
        if (!base_owner || !attach || attach == image + 0x82fb50) continue;
        void *driver = pointer(car, 0x4bf8);
        uint64_t id = 0;
        if (live) {
            if (!offline_opponent(car)) continue;
        } else {
            void *identity = method(driver, 0x38);
            if (!identity || fm5_vehicle_local_car(car)) continue;
            id = ((uint64_t (*)(void *))identity)(driver);
        }
        for (unsigned i = 0; i < assignment_count; ++i) {
            Assignment *assignment = &assignments[i];
            if (assignment->car ? assignment->car != car : !id || assignment->driver != id) continue;
            Livery *livery = &assignment->design->livery;
            if (assignment->presentation == presentation && base_owner == livery->owner) {
                ++applied_count;
                break;
            }
            if (attached || assignment->attempts >= 2 || !assignment->design->render_requested ||
                !appearance_ready(livery->owner) || field(livery->owner, 0x16c) != 13 ||
                !pointer(livery->owner, 0xd8)) continue;
            // Native Drivatar attachment makes and consumes a copy. The
            // original remains resident until the lineup is retired.
            Livery copy;
            Livery *argument = ((Livery *(*)(Livery *, Livery *))(image + 0x137750))
                (&copy, livery);
            ((void (*)(void *, Livery *))(image + 0x5e5480))(car, argument);
            attached = 1;
            ++assignment->attempts;
            assignment->presentation = presentation;
            if (pointer(car, 0x2d0) == livery->owner) ++applied_count;
            break;
        }
    }
}

static void lineup_started(void *manager, unsigned char flag) {
    ((void (*)(void *, unsigned char))(image + 0x1fa620))(manager, flag);
    InterlockedIncrement(&lineup_generation);
}

static void lineup_retired(void *manager) {
    ((void (*)(void *))(image + 0x1fe3c0))(manager);
    InterlockedIncrement(&lineup_generation);
}

static void update(void *manager) {
    ((void (*)(void *))(image + 0x1fcd80))(manager);
    ULONGLONG now = GetTickCount64();
    LONG generation = InterlockedCompareExchange(&lineup_generation, 0, 0);
    if (manager == seen_manager && generation == seen_generation && now < next_update)
        return;
    void *simulation = pointer(pointer(pointer(image, 0x25d0398), 0x48), 0x90);
    if (manager != seen_manager || generation != seen_generation ||
        (seen_simulation && simulation != seen_simulation)) {
        clear_pool();
        seen_manager = manager;
        seen_generation = generation;
        seen_end = 0;
        seen_cars = 0;
        until = next_check = next_lineup_check = lineup_deadline = 0;
    }
    seen_simulation = simulation;
    // Offline Free Play and career opponents are in the simulation's main
    // car array. The download roster can be empty in both modes.
    void **cars = NULL;
    unsigned car_count = car_array(simulation, 0x2c8, &cars);
    uintptr_t begin = (uintptr_t)pointer(manager, 0x2698), end = (uintptr_t)pointer(manager, 0x26a0);
    unsigned count = 0;
    if (begin && end > begin && !((end - begin) % ROSTER_STRIDE) &&
        (end - begin) / ROSTER_STRIDE <= MAX_ROSTER)
        count = (unsigned)((end - begin) / ROSTER_STRIDE);
    if (car_count <= 1 && !count && (seen_cars > 1 || seen_end)) {
        clear_pool();
        seen_cars = 0;
        seen_end = 0;
        until = next_check = next_lineup_check = lineup_deadline = 0;
    }
    if (assignment_count < limit && (car_count > 1 || count) &&
        (car_count != seen_cars || (count ? end : 0) != seen_end)) {
        seen_cars = car_count;
        seen_end = count ? end : 0;
        // Cars can enter the array before their roles are initialized.
        next_lineup_check = count ? now : now + 250;
        lineup_deadline = now + 15000;
    }
    if (assignment_count < limit && lineup_deadline && now >= next_lineup_check) {
        next_lineup_check = now + 250;
        void *profile = active_profile();
        if (profile && !designs_enabled(profile)) {
            lineup_deadline = 0;
            retire_library_scan();
            if (report) report("LOCAL DESIGNS: FM5 Drivatar Designs is disabled in HUD settings.\r\n");
        } else if (profile) {
            // The array can contain all cars before all their roles are set.
            // Keep filling the same pool as eligible opponents appear.
            Candidate candidates[MAX_ROSTER];
            unsigned eligible = collect_candidates(candidates, cars, car_count, begin, count);
            if (!eligible)
                lineup_changed(candidates, 0);
            else if (library_ready() && lineup_changed(candidates, eligible))
                prepare_lineup(profile, candidates, eligible);
        }
        if (lineup_deadline && now >= lineup_deadline) {
            lineup_deadline = 0;
            if (library_scan && !InterlockedCompareExchange(&library_scan->done, 0, 0)) {
                retire_library_scan();
                if (report)
                    report("LOCAL DESIGNS: FM5 library scan exceeded the lineup setup window.\r\n");
            } else if (!seen_candidate_count && report) {
                report(profile ? "LOCAL DESIGNS: FM5 lineup has no eligible Drivatars after setup.\r\n"
                               : "LOCAL DESIGNS: FM5 profile unavailable during lineup setup.\r\n");
            }
        }
    }
    next_update = now + (until ? 100 : 250);
    if (!assignment_count) return;
    if (until && now >= until) {
        until = 0;
        if (applied_count != assignment_count && report) {
            report("LOCAL DESIGNS: FM5 lineup ended its application window with pending designs.\r\n");
            for (unsigned i = 0; i < resident_count; ++i) {
                Resident *design = &residents[i];
                void *owner = design->livery.owner;
                log_append_value("LOCAL DESIGNS: FM5 expected design model=", (int)design->ordinal);
                log_append_value("LOCAL DESIGNS: FM5 decoded design model=", (int)field(owner, 0x104));
                log_append_value("LOCAL DESIGNS: FM5 render state=", (int)field(owner, 0x16c));
            }
        }
    } else if (until && now >= next_check) {
        next_check = now + 100;
        unsigned previous = applied_count;
        request_render();
        if (assignments[0].car) apply_ready(cars, car_count, 1);
        else {
            unsigned remote_count = car_array(simulation, 0x460, &cars);
            apply_ready(cars, remote_count, 0);
        }
        if (previous != assignment_count && applied_count == assignment_count && report)
            report("LOCAL DESIGNS: FM5 native car presentations accepted their local designs.\r\n");
    }
}

static void hook(Fm5Site *site, unsigned at, unsigned native, unsigned cave, void *callback,
                 unsigned char opcode) {
    site[0] = (Fm5Site){.rva=cave, .size=12,
        .original={0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},
        .replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    memory_copy(site[0].replacement + 2, &callback, sizeof(callback));
    site[1] = (Fm5Site){.rva=at, .size=5, .original={opcode}, .replacement={opcode}};
    int32_t original = (int32_t)(native - at - 5), replacement = (int32_t)(cave - at - 5);
    memory_copy(site[1].original + 1, &original, 4);
    memory_copy(site[1].replacement + 1, &replacement, 4);
}

unsigned fm5_local_liveries_prepare(unsigned char *game, Fm5Site *sites, const wchar_t *directory,
                                    unsigned maximum, void (*log)(const char *)) {
    if (!maximum || !path_join(library, directory, L"data\\titles\\7B35D917\\saves\\player-1")) return 0;
    image = game;
    report = log;
    limit = maximum < MAX_POOL ? maximum : MAX_POOL;
    hook(sites, 0x14002d, 0x1fcd80, 0x1fa9e2, update, 0xe8);
    hook(sites + 2, 0x48f078, 0x1fa620, 0x48f061, lineup_started, 0xe9);
    hook(sites + 4, 0x1c2452, 0x1fe3c0, 0x1c2471, lineup_retired, 0xe8);
    sites[6] = (Fm5Site){.rva=0x1fe2fe,.size=5,.original={0xe9,0xbd,0,0,0},.replacement={0xe9}};
    int32_t delta = 0x1c2471 - 0x1fe2fe - 5;
    memory_copy(sites[6].replacement + 1, &delta, 4);
    return 7;
}
