#include "kbm_rebind.h"
#include "kbm_input.h"
#include "kbm_menu.h"
#include "kbm_win32.h"
#include "logging.h"
#include "support.h"

/* Labels of an action row's choice collection; the layout is fixed so a row
   only ever needs its value re-read. */
enum {
    LABEL_CAPTURE,     /* "Press a key..." */
    LABEL_BUILT,       /* the row's keys when the collection was built */
    LABEL_DEFAULT,     /* the row's default keys */
    LABEL_NONE,
    LABEL_UNAVAILABLE, /* Escape, F2, F8, Alt, Win and keys without a name */
    LABEL_NO_CAPTURE,  /* keyboard & mouse is not running */
    LABEL_SAVE_FAILED,
    LABEL_IN_USE,      /* + KbmBinding of the action that holds the key */
    LABEL_KEYS = LABEL_IN_USE + KBM_BINDING_COUNT /* + key table index */
};
/* The Reset row: the same word three times so Left and Right both act. */
enum { RESET_LABEL = 1, RESET_DONE = 3, RESET_SAVE_FAILED = 4, RESET_LABELS = 5 };

#define MESSAGE_MS 2500u
#define CAPTURE_TIMEOUT_MS 10000u
#define TICK_ALIVE_MS 500u
/* Values a row receives while the page binds are not the player's. */
#define PAGE_SETTLE_MS 750u
#define SCAN_ESCAPE 0x01u
#define SCAN_BACKSPACE 0x0eu
#define SCAN_DELETE 0xd3u

_Static_assert(KBM_BINDING_COUNT == KBM_TEXT_ACTIONS, "one caption per binding");

/* Every binding but Pause (Esc, Escape=1) has a row. */
static const unsigned char item_binding[KBM_REBIND_KEY_ITEMS] = {
    KBM_THROTTLE,    KBM_BRAKE,          KBM_STEER_LEFT,        KBM_STEER_RIGHT,
    KBM_HANDBRAKE,   KBM_SHIFT_UP,       KBM_SHIFT_DOWN,        KBM_CLUTCH,
    KBM_CAMERA,      KBM_REWIND,         KBM_LOOK_BACK,         KBM_ACTIVATE,
    KBM_PHOTO_MODE,  KBM_RADIO_PREVIOUS, KBM_RADIO_NEXT,        KBM_MAP,
    KBM_CONFIRM,     KBM_BACK,           KBM_UP,                KBM_DOWN,
    KBM_LEFT,        KBM_RIGHT,          KBM_TAB_LEFT,          KBM_TAB_RIGHT,
    KBM_MENU_X,      KBM_MENU_Y,         KBM_MENU_LEFT_TRIGGER, KBM_MENU_RIGHT_TRIGGER,
    KBM_DPAD_UP,     KBM_DPAD_DOWN,      KBM_DPAD_LEFT,         KBM_DPAD_RIGHT,
};

static wchar_t ini_path[PATH_CAPACITY];
static int loaded;
static unsigned short keys[KBM_BINDING_COUNT][KBM_KEYS_PER_BINDING];
static unsigned short defaults[KBM_BINDING_COUNT][KBM_KEYS_PER_BINDING];
static unsigned short built[KBM_REBIND_KEY_ITEMS][KBM_KEYS_PER_BINDING];
static unsigned char built_valid[KBM_REBIND_KEY_ITEMS];
static int capturing = -1, capture_on_top;
static uint64_t capture_started, last_tick, page_built_at;
static int message[KBM_REBIND_ITEMS];
static uint64_t message_until[KBM_REBIND_ITEMS];
static uint64_t dirty;
static int list_index, top_logged, top_matched, activation_on;
static uint64_t top_mismatch_since;
static wchar_t label[128];

/* ---------- log lines ---------- */

static char line[256];
static unsigned line_length;

static void put(const char *text) {
    while (*text && line_length < sizeof(line) - 3) {
        line[line_length++] = *text++;
    }
}

static void put_keys(const unsigned short value[KBM_KEYS_PER_BINDING]) {
    char text[64];
    kbm_config_format_keys(value, text, sizeof(text));
    put(text);
}

static void put_hex(unsigned code) {
    static const char digits[] = "0123456789abcdef";
    char hex[5] = {'0', 'x', digits[(code >> 4) & 15], digits[code & 15], 0};
    put(hex);
}

static void flush(void) {
    line[line_length++] = '\r';
    line[line_length++] = '\n';
    line[line_length] = 0;
    log_append(line);
    line_length = 0;
}

/* ---------- binding table ---------- */

static int same_keys(const unsigned short *a, const unsigned short *b) {
    for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
        if (a[k] != b[k]) {
            return 0;
        }
    }
    return 1;
}

static void copy_keys(unsigned short *to, const unsigned short *from) {
    for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
        to[k] = from[k];
    }
}

static void widen(const char *text, wchar_t *out, unsigned capacity) {
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, out, (int)capacity);
    if (count <= 0) {
        out[0] = 0;
    }
}

/* The section as the mod's own parser reads it (an inline ; or # comment
   ends the value), so the round trip matches. */
static void load(void) {
    KbmSettings settings;
    kbm_config_defaults(&settings);
    for (unsigned b = 0; b < KBM_BINDING_COUNT; ++b) {
        copy_keys(defaults[b], settings.keys[b]);
    }
    for (unsigned b = 0; ini_path[0] && b < KBM_BINDING_COUNT; ++b) {
        wchar_t name[32], value[96];
        char narrow[96];
        widen(kbm_config_binding_name(b), name, ARRAY_COUNT(name));
        if (!GetPrivateProfileStringW(L"KeyboardMouse", name, L"", value, ARRAY_COUNT(value),
                                      ini_path) ||
            !WideCharToMultiByte(CP_UTF8, 0, value, -1, narrow, sizeof(narrow), NULL, NULL)) {
            continue;
        }
        unsigned end = 0;
        while (narrow[end] && narrow[end] != ';' && narrow[end] != '#') {
            ++end;
        }
        while (end && (narrow[end - 1] == ' ' || narrow[end - 1] == '\t')) {
            --end;
        }
        narrow[end] = 0;
        kbm_config_parse(&settings, kbm_config_binding_name(b), narrow);
    }
    if (!kbm_config_validate(&settings)) {
        log_append("KBM REBIND: [KeyboardMouse] binds a key twice in one group; showing the "
                   "defaults.\r\n");
        kbm_config_defaults(&settings);
    }
    for (unsigned b = 0; b < KBM_BINDING_COUNT; ++b) {
        copy_keys(keys[b], settings.keys[b]);
    }
    loaded = 1;
}

static void ensure_loaded(void) {
    if (!loaded) {
        load();
    }
}

void kbm_rebind_set_directory(const wchar_t *directory) {
    if (!directory || !path_join(ini_path, directory, MOD_INI_NAME)) {
        ini_path[0] = 0;
    }
    loaded = 0;
    for (unsigned i = 0; i < KBM_REBIND_KEY_ITEMS; ++i) {
        built_valid[i] = 0;
    }
}

static int group_of(unsigned binding) {
    return binding >= KBM_FIRST_MENU_BINDING;
}

/* The action in the same group that already uses `code`, or -1. */
static int conflict(unsigned binding, unsigned code) {
    for (unsigned other = 0; other < KBM_BINDING_COUNT; ++other) {
        if (other == binding || group_of(other) != group_of(binding)) {
            continue;
        }
        for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
            if (keys[other][k] == code) {
                return (int)other;
            }
        }
    }
    return -1;
}

static int key_index(unsigned code) {
    for (unsigned i = 0; i < kbm_config_key_count(); ++i) {
        if (kbm_config_key_code(i) == code) {
            return (int)i;
        }
    }
    return -1;
}

static void mark(unsigned item) {
    dirty |= 1ull << item;
}

static void show_message(unsigned item, int label_index) {
    message[item] = label_index;
    message_until[item] = GetTickCount64() + MESSAGE_MS;
    mark(item);
}

/* Writes one binding (or deletes it with NULL) in [KeyboardMouse]. */
static int write_binding(unsigned binding, const unsigned short *value) {
    wchar_t name[32], text[96];
    char narrow[64];
    if (!ini_path[0]) {
        return 0;
    }
    widen(kbm_config_binding_name(binding), name, ARRAY_COUNT(name));
    if (value) {
        kbm_config_format_keys(value, narrow, sizeof(narrow));
        widen(narrow, text, ARRAY_COUNT(text));
    }
    return WritePrivateProfileStringW(L"KeyboardMouse", name, value ? text : NULL, ini_path) != 0;
}

/* Puts `value` on `binding` after the same checks the INI loader makes:
   1 saved and applied, 0 refused, -1 the INI could not be written. */
static int commit(unsigned item, unsigned binding, const unsigned short *value) {
    KbmSettings check;
    kbm_config_defaults(&check);
    for (unsigned b = 0; b < KBM_BINDING_COUNT; ++b) {
        copy_keys(check.keys[b], b == binding ? value : keys[b]);
    }
    if (!kbm_config_validate(&check)) {
        show_message(item, LABEL_UNAVAILABLE);
        return 0;
    }
    if (!write_binding(binding, value)) {
        show_message(item, LABEL_SAVE_FAILED);
        return -1;
    }
    copy_keys(keys[binding], value);
    kbm_set_keys((const unsigned short(*)[KBM_KEYS_PER_BINDING])keys);
    return 1;
}

static void cancel_capture(const char *reason) {
    if (capturing < 0) {
        return;
    }
    kbm_win32_capture_disarm();
    put("KBM REBIND: ");
    put(kbm_config_binding_name(item_binding[capturing]));
    put(" capture cancelled (");
    put(reason);
    put("); binding unchanged.");
    flush();
    mark((unsigned)capturing);
    capturing = -1;
}

/* The key the player pressed while `item` was waiting. */
static void finish_capture(unsigned item, unsigned code) {
    unsigned binding = item_binding[item];
    static const unsigned short none[KBM_KEYS_PER_BINDING] = {0};
    unsigned short value[KBM_KEYS_PER_BINDING] = {0};
    capturing = -1;
    message_until[item] = 0; /* this result replaces an earlier message */
    mark(item);
    if (code == SCAN_ESCAPE) {
        capturing = (int)item;
        cancel_capture("Esc");
        return;
    }
    put("KBM REBIND: ");
    put(kbm_config_binding_name(binding));
    if (code == SCAN_BACKSPACE || code == SCAN_DELETE) {
        int result = commit(item, binding, none);
        put(result > 0   ? "=None (cleared), saved to [KeyboardMouse] and applied."
            : result < 0 ? ": could not write [KeyboardMouse]; binding unchanged."
                         : ": clearing refused; binding unchanged.");
        flush();
        return;
    }
    if (key_index(code) < 0) {
        put(": key ");
        put_hex(code);
        put(" cannot be bound (F2, F8, Alt, Win or unnamed); ignored, still waiting for a key.");
        flush();
        /* Stay in capture mode: the row keeps showing "Press a key...". */
        if (kbm_win32_capture_arm()) {
            capturing = (int)item;
            capture_started = GetTickCount64();
        } else {
            show_message(item, LABEL_UNAVAILABLE);
        }
        return;
    }
    const char *name = kbm_config_key_name(code);
    int holder = conflict(binding, code);
    if (holder >= 0) {
        put(": ");
        put(name);
        put(" refused, already bound to ");
        put(kbm_config_binding_name((unsigned)holder));
        put(binding < KBM_FIRST_MENU_BINDING ? " (driving group)" : " (menu group)");
        put("; binding unchanged.");
        flush();
        show_message(item, LABEL_IN_USE + holder);
        return;
    }
    value[0] = (unsigned short)code;
    int result = commit(item, binding, value);
    put(result > 0 ? "=" : ": ");
    put(name);
    put(result > 0   ? " saved to [KeyboardMouse] and applied."
        : result < 0 ? " not saved, [KeyboardMouse] could not be written; binding unchanged."
                     : " refused by the INI check; binding unchanged.");
    flush();
}

static int tick_alive(void) {
    return last_tick && GetTickCount64() - last_tick < TICK_ALIVE_MS;
}

static void start_capture(unsigned item, int on_top) {
    if (capturing >= 0) {
        cancel_capture("another row chosen");
    }
    mark(item);
    if (!tick_alive() || !kbm_win32_capture_arm()) {
        show_message(item, LABEL_NO_CAPTURE);
        put("KBM REBIND: ");
        put(kbm_config_binding_name(item_binding[item]));
        put(": key capture needs keyboard & mouse on with Raw Input; binding unchanged.");
        flush();
        return;
    }
    message_until[item] = 0;
    capturing = (int)item;
    capture_on_top = on_top;
    capture_started = GetTickCount64();
    put("KBM REBIND: press a key for ");
    put(kbm_config_binding_name(item_binding[item]));
    put(" (now ");
    put_keys(keys[item_binding[item]]);
    put("); Esc cancels, Backspace/Delete clears.");
    flush();
}

static void reset_all(void) {
    if (capturing >= 0) {
        cancel_capture("reset");
    }
    int written = 1;
    for (unsigned b = 0; b < KBM_BINDING_COUNT; ++b) {
        /* Removing the keys leaves the defaults to the INI loader. */
        written &= write_binding(b, NULL);
        copy_keys(keys[b], defaults[b]);
    }
    kbm_set_keys((const unsigned short(*)[KBM_KEYS_PER_BINDING])keys);
    for (unsigned item = 0; item < KBM_REBIND_KEY_ITEMS; ++item) {
        mark(item);
    }
    show_message(KBM_REBIND_RESET_ITEM, written ? RESET_DONE : RESET_SAVE_FAILED);
    log_append(written ? "KBM REBIND: key bindings reset to the defaults (binding keys removed "
                         "from [KeyboardMouse]).\r\n"
                       : "KBM REBIND: key bindings reset to the defaults for this session; the "
                         "INI could not be written.\r\n");
}

/* ---------- row interface (kbm_menu.c) ---------- */

static int message_active(unsigned item) {
    return message_until[item] && GetTickCount64() < message_until[item];
}

/* The row's label index (LABEL_* or RESET_*); kbm_rebind_get spreads them. */
static int label_index(unsigned item) {
    ensure_loaded();
    if (item == KBM_REBIND_LIST_ITEM) {
        return list_index;
    }
    if (item == KBM_REBIND_RESET_ITEM) {
        return message_active(item) ? message[item] : RESET_LABEL;
    }
    if (item >= KBM_REBIND_KEY_ITEMS) {
        return 0;
    }
    if ((int)item == capturing) {
        return LABEL_CAPTURE;
    }
    if (message_active(item)) {
        return message[item];
    }
    const unsigned short *current = keys[item_binding[item]];
    if (built_valid[item] && same_keys(current, built[item])) {
        return LABEL_BUILT;
    }
    if (same_keys(current, defaults[item_binding[item]])) {
        return LABEL_DEFAULT;
    }
    if (!current[0] && !current[1] && !current[2]) {
        return LABEL_NONE;
    }
    unsigned count = 0, code = 0;
    for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
        if (current[k]) {
            ++count;
            code = current[k];
        }
    }
    int index = count == 1 ? key_index(code) : -1;
    return index >= 0 ? LABEL_KEYS + index : LABEL_BUILT;
}

/* Choice lists put each label at an odd index, 2 * label + 1, and the
   "Press a key..." text (Reset row: its own word) at every even index, so a
   Left/Right step always lands on that text and no other label flashes up
   before the row is read again. */
int kbm_rebind_get(unsigned item) {
    int value = label_index(item);
    return item == KBM_REBIND_LIST_ITEM ? value : 2 * value + 1;
}

static unsigned label_count(unsigned item) {
    return item < KBM_REBIND_KEY_ITEMS     ? LABEL_KEYS + kbm_config_key_count()
           : item == KBM_REBIND_RESET_ITEM ? RESET_LABELS
           : item == KBM_REBIND_LIST_ITEM  ? 1
                                           : 0;
}

unsigned kbm_rebind_choice_count(unsigned item) {
    unsigned count = label_count(item);
    return item == KBM_REBIND_LIST_ITEM ? count : count ? 2 * count + 1 : 0;
}

int kbm_rebind_set(unsigned item, int value) {
    ensure_loaded();
    if (item == KBM_REBIND_LIST_ITEM) {
        if (value < 0) {
            return 0; /* the list clears its selection between two focused rows */
        }
        if (value != list_index) {
            int focused = value - KBM_MENU_OPTION_ROWS;
            char digits[12];
            unsigned n = 0, v = (unsigned)value;
            do {
                digits[n++] = (char)('0' + v % 10);
                v /= 10;
            } while (v);
            put("KBM REBIND: focused row ");
            while (n) {
                char digit[2] = {digits[--n], 0};
                put(digit);
            }
            put(focused >= 0 && focused < KBM_REBIND_KEY_ITEMS ? " (key binding: "
                : focused == KBM_REBIND_RESET_ITEM             ? " (reset row"
                                                               : " (option row");
            if (focused >= 0 && focused < KBM_REBIND_KEY_ITEMS) {
                put(kbm_config_binding_name(item_binding[focused]));
            }
            put(").");
            flush();
        }
        list_index = value;
        return 0;
    }
    if (item > KBM_REBIND_RESET_ITEM || value == kbm_rebind_get(item)) {
        return 0;
    }
    if (value < 0 || (unsigned)value >= kbm_rebind_choice_count(item) ||
        GetTickCount64() - page_built_at < PAGE_SETTLE_MS) {
        return 0; /* not the player's choice: ignored, as other rows ignore it */
    }
    if (item == KBM_REBIND_RESET_ITEM) {
        reset_all();
    } else if ((int)item != capturing) {
        void *page = video_options_rebind_page(0), *top = kbm_input_top_page();
        start_capture(item, page && top == page);
    }
    /* Always re-read: a refused change must snap back. */
    return 1;
}

static void append(wchar_t *out, unsigned capacity, const wchar_t *text) {
    unsigned length = 0;
    while (out[length]) {
        ++length;
    }
    while (*text && length + 1 < capacity) {
        out[length++] = *text++;
    }
    out[length] = 0;
}

static const wchar_t *keys_label(const unsigned short *value, const VideoOptionsText *text) {
    label[0] = 0;
    for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
        if (!value[k]) {
            continue;
        }
        wchar_t name[32];
        widen(kbm_config_key_name(value[k]), name, ARRAY_COUNT(name));
        if (label[0]) {
            append(label, ARRAY_COUNT(label), L", ");
        }
        append(label, ARRAY_COUNT(label), name);
    }
    return label[0] ? label : text->kbm_labels[KBM_LABEL_NONE];
}

static const wchar_t *label_text(unsigned item, unsigned index, const VideoOptionsText *text) {
    if (item == KBM_REBIND_RESET_ITEM) {
        return text->kbm_labels[index == RESET_DONE          ? KBM_LABEL_DONE
                                : index == RESET_SAVE_FAILED ? KBM_LABEL_SAVE_FAILED
                                                             : KBM_LABEL_RESET];
    }
    unsigned binding = item_binding[item];
    switch (index) {
        case LABEL_CAPTURE:
            return text->kbm_labels[KBM_LABEL_PRESS_KEY];
        case LABEL_BUILT:
            copy_keys(built[item], keys[binding]);
            built_valid[item] = 1;
            return keys_label(built[item], text);
        case LABEL_DEFAULT:
            return keys_label(defaults[binding], text);
        case LABEL_NONE:
            return text->kbm_labels[KBM_LABEL_NONE];
        case LABEL_UNAVAILABLE:
            return text->kbm_labels[KBM_LABEL_UNAVAILABLE];
        case LABEL_NO_CAPTURE:
            return text->kbm_labels[KBM_LABEL_OFF];
        case LABEL_SAVE_FAILED:
            return text->kbm_labels[KBM_LABEL_SAVE_FAILED];
    }
    if (index < LABEL_KEYS) {
        wchar_t name[64];
        widen(text->kbm_actions[index - LABEL_IN_USE], name, ARRAY_COUNT(name));
        label[0] = 0;
        append(label, ARRAY_COUNT(label), text->kbm_labels[KBM_LABEL_IN_USE]);
        append(label, ARRAY_COUNT(label), L" (");
        append(label, ARRAY_COUNT(label), name);
        append(label, ARRAY_COUNT(label), L")");
        return label;
    }
    widen(kbm_config_key_name(kbm_config_key_code(index - LABEL_KEYS)), label, ARRAY_COUNT(label));
    return label;
}

const wchar_t *kbm_rebind_choice(unsigned item, unsigned index, const VideoOptionsText *text) {
    ensure_loaded();
    if (index >= kbm_rebind_choice_count(item)) {
        return NULL;
    }
    if (item == KBM_REBIND_LIST_ITEM) {
        return L"-";
    }
    return label_text(item,
                      index % 2                        ? (index - 1) / 2
                      : item == KBM_REBIND_RESET_ITEM ? RESET_LABEL
                                                       : LABEL_CAPTURE,
                      text);
}

const char *kbm_rebind_name(unsigned item, const VideoOptionsText *text) {
    return item < KBM_REBIND_KEY_ITEMS     ? text->kbm_actions[item_binding[item]]
           : item == KBM_REBIND_RESET_ITEM ? text->kbm_reset
                                           : NULL;
}

const char *kbm_rebind_description(unsigned item, const VideoOptionsText *text) {
    return item < KBM_REBIND_KEY_ITEMS
               ? text->kbm_key_descriptions[item_binding[item] >= KBM_FIRST_MENU_BINDING]
           : item == KBM_REBIND_RESET_ITEM ? text->kbm_reset_description
                                           : NULL;
}

int kbm_rebind_labels_stale(unsigned item) {
    ensure_loaded();
    return item < KBM_REBIND_KEY_ITEMS &&
           (!built_valid[item] || !same_keys(built[item], keys[item_binding[item]]));
}

int kbm_rebind_take_dirty(unsigned item) {
    uint64_t bit = 1ull << item;
    if (item >= KBM_REBIND_ITEMS || !(dirty & bit)) {
        return 0;
    }
    dirty &= ~bit;
    return 1;
}

void kbm_rebind_page_built(void) {
    if (capturing >= 0) {
        cancel_capture("page rebuilt");
    }
    list_index = 0;
    top_matched = 0;
    top_logged = 0;
    top_mismatch_since = 0;
    dirty = 0;
    page_built_at = GetTickCount64();
}

/* The focused list row as a rebind item, or -1. */
static int focused_item(void) {
    int item = list_index - KBM_MENU_OPTION_ROWS;
    return item >= 0 && item <= KBM_REBIND_RESET_ITEM ? item : -1;
}

/* Enter (a Confirm key) on a focused key binding row. kbm_win32.c holds the
   press and its release back from the game while such a row is focused, so
   the page never sees it. Mouse clicks do not start a capture: the row under
   the pointer is not known here. */
static void activation(void *page, void *top) {
    int item = focused_item();
    int want = page && top == page && capturing < 0 && item >= 0;
    /* The frame shows the page a moment after its values are first read, so
       a mismatch only counts when it lasts with no match since the page was
       built. */
    if (page && top == page) {
        top_matched = 1;
        top_mismatch_since = 0;
    } else if (page && top && !top_matched && !top_logged) {
        uint64_t now = GetTickCount64();
        if (!top_mismatch_since) {
            top_mismatch_since = now;
        } else if (now - top_mismatch_since > 3000) {
            top_logged = 1;
            log_append("KBM REBIND: for 3 s the frame's top page has not been the KEYBOARD & MOUSE "
                       "page object; Enter does not start key capture, Left/Right do.\r\n");
        }
    }
    if (want != activation_on) {
        activation_on = want;
        kbm_win32_set_activation_keys(keys[KBM_CONFIRM], want ? KBM_KEYS_PER_BINDING : 0);
    }
    unsigned code = kbm_win32_take_activation();
    if (!code) {
        return;
    }
    if (!want) {
        log_append("KBM REBIND: Enter was held back but no key binding row is focused any "
                   "more; ignored.\r\n");
        return;
    }
    if (item == KBM_REBIND_RESET_ITEM) {
        reset_all();
    } else {
        start_capture((unsigned)item, 1);
    }
}

void kbm_rebind_ui_tick(void) {
    uint64_t now = GetTickCount64();
    last_tick = now;
    ensure_loaded();
    void *page = video_options_rebind_page(1);
    void *top = kbm_input_top_page();
    if (capturing >= 0) {
        unsigned code, ignored = kbm_win32_capture_ignored();
        if (ignored) {
            put("KBM REBIND: key ");
            put_hex(ignored);
            put(" ignored while waiting (Alt/Win, or pressed within 150 ms of the start); still "
                "waiting.");
            flush();
        }
        int result = kbm_win32_capture_take(&code);
        if (result == 1) {
            finish_capture((unsigned)capturing, code);
        } else if (result == 2) {
            cancel_capture("mouse button");
        } else if (now - capture_started > CAPTURE_TIMEOUT_MS) {
            cancel_capture("timed out");
        } else if (!page || (capture_on_top && top != page)) {
            cancel_capture("page closed");
        }
    }
    activation(page, top);
    for (unsigned item = 0; item < KBM_REBIND_ITEMS; ++item) {
        if (message_until[item] && now >= message_until[item]) {
            message_until[item] = 0;
            mark(item);
        }
    }
    video_options_rebind_tick();
}

#ifdef FH2_TWEAKS_TEST
/* Offline checks: the press the capture would deliver, without the UI. */
__declspec(dllexport) int Fh2TestRebindCapture(unsigned item, unsigned code) {
    ensure_loaded();
    if (item >= KBM_REBIND_KEY_ITEMS) {
        return -1;
    }
    capturing = (int)item;
    finish_capture(item, code);
    return kbm_rebind_get(item);
}

__declspec(dllexport) void Fh2TestRebindReset(void) {
    ensure_loaded();
    reset_all();
}

__declspec(dllexport) void Fh2TestRebindReload(void) {
    loaded = 0;
    for (unsigned i = 0; i < KBM_REBIND_KEY_ITEMS; ++i) {
        built_valid[i] = 0;
    }
    for (unsigned i = 0; i < KBM_REBIND_ITEMS; ++i) {
        message_until[i] = 0;
    }
}

/* The item's keys packed one byte each, as the page holds them. */
__declspec(dllexport) unsigned Fh2TestRebindKeys(unsigned item) {
    ensure_loaded();
    if (item >= KBM_REBIND_KEY_ITEMS) {
        return 0;
    }
    const unsigned short *value = keys[item_binding[item]];
    return value[0] | (unsigned)value[1] << 8 | (unsigned)value[2] << 16;
}

__declspec(dllexport) const wchar_t *Fh2TestRebindChoice(unsigned item, unsigned index) {
    return kbm_rebind_choice(item, index, video_options_text(NULL));
}

__declspec(dllexport) int Fh2TestRebindGet(unsigned item) {
    return kbm_rebind_get(item);
}
#endif
