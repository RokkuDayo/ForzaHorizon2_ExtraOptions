#include "offline_event_division.h"
#include "support.h"
#include "logging.h"
#include "video_options_text.h"

typedef struct {
    const char *pointer;
    uintptr_t unused;
    size_t length, capacity;
} QueryString;

static unsigned char *image;
static void *division_item;
static int division_ids[512];
static unsigned division_count;

static QueryString borrowed(const char *text) {
    size_t length = text_length(text);
    return (QueryString){text, 0, length, length < 16 ? 16 : length};
}

static void release_ui(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0) {
        ((void (*)(void *))(image + 0x257730))(object);
    }
}

static void release_reference(void *object) {
    if (object) {
        ((void (*)(void *))(*(void ***)object)[3])(object);
    }
}

static void *localized_label(const char *table, unsigned key) {
    void *reference = NULL, *label = NULL;
    ((void *(*)(void *, const char *, unsigned))(image + 0x123a570))(&reference, table, key);
    if (reference) {
        ((void *(*)(void *, void *))(image + 0x140af10))(&label, &reference);
        release_reference(reference);
    }
    return label;
}

static void *string_choices(void) {
    void *type = ((void *(*)(void *))(image + 0x1476110))(NULL);
    void *items = ((void *(*)(size_t, void *))(image + 0x14424e0))(0x58, type);
    if (!items) {
        return NULL;
    }
    ((void (*)(void *))(image + 0xaefd60))(items);
    static const unsigned vtables[] = {0x2db73f8, 0x2db7470, 0x2db74d0, 0x2db74e8, 0x2db7508};
    static const unsigned offsets[] = {0, 0x30, 0x38, 0x40, 0x48};
    for (unsigned i = 0; i < ARRAY_COUNT(vtables); ++i) {
        *(void **)((unsigned char *)items + offsets[i]) = image + vtables[i];
    }
    return items;
}

// The selector is read when the setup is committed. Its native per-item callback
// must not change one of the game's unrelated online event options.
static void division_changed(void *setup, int index) {
    (void)setup;
    (void)index;
}

void offline_event_division_initialize(unsigned char *base) {
    if (image != base) {
        release_ui(division_item);
        division_item = NULL;
        division_count = 0;
        image = base;
    }
}

void offline_event_division_add(void *page, int enabled) {
    release_ui(division_item);
    division_item = NULL;
    division_count = 0;
    if (!enabled) {
        return;
    }
    void *manager = NULL;
    if (!memory_read_pointer(image + 0x34aba60, &manager) || !manager) {
        return;
    }
    void *database = *(void **)((unsigned char *)manager + 0x40);
    void *options = *(void **)((unsigned char *)page + 0x410);
    if (!database || !options) {
        return;
    }
    unsigned char *storage = *(void **)((unsigned char *)options + 0x50);
    if (!storage || *(int *)(storage + 0x30) <= 0) {
        return;
    }
    static const char sql[] =
        "SELECT B.Id,B.Name FROM CarBuckets B WHERE B.ParentBucketId<>0 AND EXISTS("
        "SELECT 1 FROM Data_Car_Buckets D JOIN Drivable_Data_Car C ON C.Id=D.CarId "
        "WHERE D.CarBucket=B.Id AND IsCarInstalled(C.Id) AND HasCarMedia(C.MediaName)) "
        "ORDER BY (SELECT SortPriority FROM CarBuckets WHERE Id=B.ParentBucketId),"
        "B.SortPriority,B.Id";
    QueryString query = borrowed(sql), id = borrowed("Id"), name = borrowed("Name");
    void *rows = NULL;
    ((void *(*)(void *, void *, const QueryString *))(*(void ***)database)[9])(database, &rows,
                                                                               &query);
    if (!rows) {
        return;
    }
    void *items = string_choices(), *any = localized_label("GameOptions2", 17999);
    if (!items || !any) {
        release_ui(items);
        release_ui(any);
        release_reference(rows);
        return;
    }
    ((void (*)(void *, int, void *))(image + 0xb52170))(items, 0, any);
    release_ui(any);
    division_ids[division_count++] = 0;
    int count = ((int (*)(void *))(*(void ***)rows)[0x148 / 8])(rows);
    for (int i = 0; i < count && division_count < ARRAY_COUNT(division_ids); ++i) {
        int value = ((int (*)(void *, const QueryString *))(*(void ***)rows)[0x110 / 8])(rows, &id);
        void *reference = NULL, *label = NULL;
        ((void *(*)(void *, void *, const QueryString *))(*(void ***)rows)[0x100 / 8])(
            rows, &reference, &name);
        if (value > 0 && reference) {
            ((void *(*)(void *, void *))(image + 0x140af10))(&label, &reference);
        }
        if (label) {
            ((void (*)(void *, int, void *))(image + 0xb52170))(items, (int)division_count, label);
            division_ids[division_count++] = value;
            release_ui(label);
        }
        release_reference(reference);
        ((void (*)(void *, unsigned char))(*(void ***)rows)[0x160 / 8])(rows, 1);
    }
    release_reference(rows);
    void *title = NULL;
    ((void *(*)(void *, const wchar_t *))(image + 0x13fe8e0))(
        &title, video_options_text(image)->division_label);
    if (!title || division_count <= 1) {
        release_ui(title);
        release_ui(items);
        division_count = 0;
        return;
    }
    // Reuse the native std::function wrapper for void(Setup&, int). Its existing
    // copy/move/destruction implementations retain the callback correctly.
    uintptr_t callback[4] = {(uintptr_t)(image + 0x2e105c0), (uintptr_t)division_changed, 0, 0};
    callback[3] = (uintptr_t)callback;
    ((void *(*)(void *, void *, int, void *, void *))(image + 0xd709f0))(&division_item, &title, 0,
                                                                         &items, callback);
    if (division_item) {
        ((void (*)(void *, int, void *))(*(void ***)options)[0x40 / 8])(options, 1, division_item);
        log_append_value("EVENTS: available car divisions", (int)division_count - 1);
    }
    release_ui(title);
    release_ui(items);
}

int offline_event_division_selected(void) {
    int index = -1;
    if (division_item &&
        memory_read((unsigned char *)division_item + 0x78, &index, sizeof(index)) && index >= 0 &&
        (unsigned)index < division_count) {
        return division_ids[index];
    }
    return 0;
}
