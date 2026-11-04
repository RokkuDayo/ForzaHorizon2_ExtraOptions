#include "autodrive_notice.h"
#include "autodrive_game.h"
#include "support.h"

#define NOTICE_VALUE (-2147483647 - 1)
#define NOTICE_DURATION 2.2f
#define CONVERTER_SLOT 0x2ecf6c8
#define CONVERTER_FUNCTION 0x140a800
#define ENABLED_TEXT_KEY 0xdfd4u

typedef void **(*ConverterFn)(void *, void **, void *, void *, void *, void *);
typedef void **(*StatusFactory)(void **);
typedef void (*StatusCategory)(void *, void *);
typedef void **(*StringFactory)(void **, const char **);
typedef unsigned short (*StringTableId)(void *, const char *);
typedef const wchar_t *(*LocalizedText)(void *, unsigned short, unsigned);
typedef void **(*WideStringFactory)(void **, const wchar_t **);
typedef void (*StatusConfigure)(void *, int, void **);
typedef void (*StatusSubmit)(void *, void **);
typedef void (*StatusRemove)(void *, void *, void **, unsigned char);
typedef void (*StatusVisibility)(void *, unsigned char);
typedef void *(*TypeGetter)(int);
typedef unsigned char (*TypeCheck)(void *, void *);
typedef void (*UiDestroy)(void *);

static unsigned char *game;
static ConverterFn original_converter;
static unsigned char original_pointer[8], replacement_pointer[8];
static const unsigned char *accepted_pointers[2] = {original_pointer, replacement_pointer};

static struct {
    AutodriveHud hud;
    void *status;
} notice;

static int owner_registered(void) {
    unsigned char *begin, *end;
    unsigned char *hud = notice.hud.object.object;
    if (!hud || !memory_read(hud + 0x78, &begin, sizeof(begin)) ||
        !memory_read(hud + 0x80, &end, sizeof(end)) || end < begin ||
        (size_t)(end - begin) % 0x48 || (size_t)(end - begin) / 0x48 > 256) {
        return 0;
    }
    for (unsigned char *entry = begin; entry < end; entry += 0x48) {
        void *owner;
        if (!memory_read(entry, &owner, sizeof(owner))) {
            return 0;
        }
        if (owner == notice.hud.wrapper.object) {
            return 1;
        }
    }
    return 0;
}

static void release_ui(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0) {
        ((UiDestroy)(game + 0x257730))(object);
    }
}

static int is_notice_value(void *value, void *parameter) {
    int amount;
    if (!value || !parameter ||
        !memory_read((unsigned char *)value + 0x28, &amount, sizeof(amount)) ||
        amount != NOTICE_VALUE) {
        return 0;
    }
    TypeCheck is_type = (TypeCheck)(game + 0x14530e0);
    if (!is_type(value, ((TypeGetter)(game + 0x15c8b20))(0)) ||
        !is_type(parameter, ((TypeGetter)(game + 0x16a4b30))(0))) {
        return 0;
    }
    const wchar_t *unit;
    wchar_t text[17];
    static const wchar_t expected[] = L"experiencepoints";
    return memory_read((unsigned char *)parameter + 0x28, &unit, sizeof(unit)) && unit &&
           memory_read(unit, text, sizeof(text)) && memory_equal(text, expected, sizeof(text));
}

static void **notice_convert(void *self, void **out, void *value, void *type, void *parameter,
                             void *culture) {
    if (out && is_notice_value(value, parameter)) {
        void *manager;
        // GameOptionValues:IDS_DisplayName_29 is the singular "On" option value.
        if (memory_read(game + 0x3662990, &manager, sizeof(manager)) && manager) {
            unsigned short table = ((StringTableId)(game + 0x123d940))(manager, "GameOptionValues");
            const wchar_t *text =
                ((LocalizedText)(game + 0x123d780))(manager, table, ENABLED_TEXT_KEY);
            wchar_t first;
            if (text && memory_read(text, &first, sizeof(first)) && first) {
                ((WideStringFactory)(game + 0xb943d0))(out, &text);
                if (*out) {
                    return out;
                }
            }
        }
    }
    return original_converter(self, out, value, type, parameter, culture);
}

PatchDefinition autodrive_notice_hook_patch = {CONVERTER_SLOT,
                                               0,
                                               8,
                                               GROUP_AUTODRIVE,
                                               "autodrive_notice_text",
                                               original_pointer,
                                               replacement_pointer,
                                               replacement_pointer,
                                               accepted_pointers,
                                               2};

void autodrive_notice_prepare(unsigned char *image) {
    game = image;
    original_converter = (ConverterFn)(game + CONVERTER_FUNCTION);
    uintptr_t original = (uintptr_t)original_converter;
    uintptr_t replacement = (uintptr_t)notice_convert;
    memory_copy(original_pointer, &original, sizeof(original));
    memory_copy(replacement_pointer, &replacement, sizeof(replacement));
}

int autodrive_notice_show(void) {
    autodrive_notice_clear();
    unsigned category_ready;
    if (!memory_read(game + 0x34e1e38, &category_ready, sizeof(category_ready)) ||
        !(category_ready & 1) || !autodrive_game_get_hud(&notice.hud) || !owner_registered()) {
        autodrive_notice_clear();
        return 0;
    }
    const char *label = "AUTODRIVE";
    void *title = 0, *status = 0;
    ((StringFactory)(game + 0x3d9820))(&title, &label);
    if (title) {
        ((StatusFactory)(game + 0xac59b0))(&status);
    }
    if (!status) {
        release_ui(title);
        autodrive_notice_clear();
        return 0;
    }
    ((StatusCategory)(game + 0xaa8920))(status, game + 0x34e1e18);
    // Configure and submit both consume their AVUI pointer arguments.
    ((StatusConfigure)(game + 0xacc240))(status, NOTICE_VALUE, &title);
    notice.status = status;
    InterlockedIncrement((volatile LONG *)((unsigned char *)status + 0x10));
    ((StatusSubmit)(game + 0x777db0))(notice.hud.wrapper.object, &status);
    return 1;
}

void autodrive_notice_update(float elapsed) {
    if (notice.status && elapsed >= NOTICE_DURATION) {
        autodrive_notice_clear();
    }
}

void autodrive_notice_clear(void) {
    if (notice.status) {
        void *status = notice.status;
        if (!owner_registered()) {
            ((StatusVisibility)(game + 0xaaccc0))(status, 0);
        }
        InterlockedIncrement((volatile LONG *)((unsigned char *)notice.status + 0x10));
        ((StatusRemove)(game + 0x7858f0))(notice.hud.object.object, notice.hud.wrapper.object,
                                          &status, 0);
        release_ui(notice.status);
        notice.status = 0;
    }
    autodrive_game_release_shared(&notice.hud.object);
    autodrive_game_release_shared(&notice.hud.wrapper);
    notice.hud = (AutodriveHud){0};
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestAutodriveNoticeHookAddress(void) {
    return (void *)notice_convert;
}
#endif
