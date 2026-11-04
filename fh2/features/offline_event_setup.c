#include "offline_event_setup.h"
#include "autodrive_game.h"
#include "support.h"
#include "video_options_text.h"

static unsigned char *image;
static void *selectors[4];
static int street;

static void release_ui(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0) {
        ((void (*)(void *))(image + 0x257730))(object);
    }
}

static void clear(void) {
    for (unsigned i = 0; i < ARRAY_COUNT(selectors); ++i) {
        release_ui(selectors[i]);
        selectors[i] = NULL;
    }
    street = 0;
}

void offline_event_setup_initialize(unsigned char *base) {
    if (image != base) {
        clear();
        image = base;
    }
}

static void *label(const wchar_t *text) {
    void *result = NULL;
    ((void *(*)(void *, const wchar_t *))(image + 0x13fe8e0))(&result, text);
    return result;
}

static void *localized(unsigned key) {
    void *reference = NULL, *result = NULL;
    ((void *(*)(void *, const char *, unsigned))(image + 0x123a570))(&reference, "GameOptions2",
                                                                     key);
    if (reference) {
        ((void *(*)(void *, void *))(image + 0x140af10))(&result, &reference);
        ((void (*)(void *))(*(void ***)reference)[3])(reference);
    }
    return result;
}

static void *choices(void) {
    void *type = ((void *(*)(void *))(image + 0x1476110))(NULL);
    void *items = ((void *(*)(size_t, void *))(image + 0x14424e0))(0x58, type);
    if (items) {
        ((void (*)(void *))(image + 0xaefd60))(items);
        static const unsigned tables[] = {0x2db73f8, 0x2db7470, 0x2db74d0, 0x2db74e8, 0x2db7508};
        static const unsigned offsets[] = {0, 0x30, 0x38, 0x40, 0x48};
        for (unsigned i = 0; i < ARRAY_COUNT(tables); ++i) {
            *(void **)((unsigned char *)items + offsets[i]) = image + tables[i];
        }
    }
    return items;
}

static int append(void *items, unsigned index, void *text) {
    if (!text) {
        return 0;
    }
    ((void (*)(void *, int, void *))(image + 0xb52170))(items, (int)index, text);
    release_ui(text);
    return 1;
}

static void changed(void *setup, int index) {
    (void)setup;
    (void)index;
}

static void add(void *options, unsigned index, unsigned position, const wchar_t *title,
                void *values, int selected) {
    void *text = label(title);
    if (values && text) {
        uintptr_t callback[4] = {(uintptr_t)(image + 0x2e105c0), (uintptr_t)changed, 0, 0};
        callback[3] = (uintptr_t)callback;
        ((void *(*)(void *, void *, int, void *, void *))(image + 0xd709f0))(
            &selectors[index], &text, selected, &values, callback);
        if (selectors[index]) {
            unsigned char *storage = *(void **)((unsigned char *)options + 0x50);
            unsigned count = *(unsigned *)(storage + 0x30);
            if (position > count) {
                position = count;
            }
            ((void (*)(void *, int, void *))(*(void ***)options)[0x40 / 8])(options, (int)position,
                                                                            selectors[index]);
        }
    }
    release_ui(text);
    release_ui(values);
}

static int equal_label(void *left, void *right) {
    if (!left || !right) {
        return 0;
    }
    int length = *(int *)((unsigned char *)left + 0x30);
    return length > 0 && length <= 256 && length == *(int *)((unsigned char *)right + 0x30) &&
           memory_equal(*(void **)((unsigned char *)left + 0x28),
                        *(void **)((unsigned char *)right + 0x28),
                        (size_t)length * sizeof(wchar_t));
}

static void remove_unsupported(void *options) {
    void *teams = localized(24658), *collisions = localized(17841);
    unsigned char *storage = *(void **)((unsigned char *)options + 0x50);
    int count = *(int *)(storage + 0x30);
    for (int i = count - 1; i >= 0; --i) {
        unsigned char *array = *(void **)(storage + 0x28);
        void **data = *(void ***)(array + 0x28);
        void *title = data[i] ? *(void **)((unsigned char *)data[i] + 0x70) : NULL;
        if (equal_label(title, teams) || equal_label(title, collisions)) {
            ((void (*)(void *, int))(*(void ***)options)[0x60 / 8])(options, i);
        }
    }
    release_ui(teams);
    release_ui(collisions);
}

void offline_event_setup_add(void *page, int enabled) {
    clear();
    if (!enabled) {
        return;
    }
    void *options = *(void **)((unsigned char *)page + 0x410);
    if (!options) {
        return;
    }
    unsigned char *storage = *(void **)((unsigned char *)options + 0x50);
    if (!storage || *(int *)(storage + 0x30) <= 0) {
        return;
    }
    GameShared route = {0};
    ((void *(*)(void *, GameShared *))(image + 0x1212820))((unsigned char *)page + 0x418, &route);
    // This is the route's database RaceModes.Id, not its circuit/P2P template.
    street = route.object && *(int *)((unsigned char *)route.object + 0x738) == 1;
    autodrive_game_release_shared(&route);
    remove_unsupported(options);

    const VideoOptionsText *text = video_options_text(image);
    void *items = choices();
    if (items) {
        static const wchar_t *numbers[] = {L"0", L"1", L"2", L"3", L"4",  L"5",
                                           L"6", L"7", L"8", L"9", L"10", L"11"};
        for (unsigned i = 0; i < ARRAY_COUNT(numbers); ++i) {
            append(items, i, label(numbers[i]));
        }
    }
    add(options, 0, 2, text->event_labels[0], items, 11);
    items = choices();
    if (items) {
        append(items, 0, label(text->default_choice));
        for (unsigned i = 0; i < 3; ++i) {
            append(items, i + 1, label(text->grid_choices[i]));
        }
    }
    add(options, 1, 3, text->event_labels[1], items, 0);
    for (unsigned i = 2; i < (street ? 4u : 3u); ++i) {
        items = choices();
        if (items) {
            append(items, 0, localized(2491));
            append(items, 1, localized(29301));
        }
        add(options, i, i + 2, text->event_labels[i], items, i == 3);
    }
}

OfflineEventSetup offline_event_setup_selected(void) {
    OfflineEventSetup value = {11, 0, 0, 0, street};
    int indices[4] = {11, 0, 0, street};
    const int maximum[] = {11, 3, 1, 1};
    for (unsigned i = 0; i < ARRAY_COUNT(selectors); ++i) {
        int selected;
        if (selectors[i] &&
            memory_read((unsigned char *)selectors[i] + 0x78, &selected, sizeof(selected)) &&
            selected >= 0 && selected <= maximum[i]) {
            indices[i] = selected;
        }
    }
    value.drivatars = (unsigned)indices[0];
    value.grid_order = (unsigned)indices[1];
    value.stock_only = indices[2];
    value.traffic = street && indices[3];
    return value;
}
