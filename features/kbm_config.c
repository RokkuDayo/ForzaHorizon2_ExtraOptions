#include "kbm_config.h"
#include "support.h"

typedef struct {
    const char *name;
    unsigned short code;
} KeyName;

/* Set-1 make codes; 0x80 marks the E0 prefix. */
static const KeyName key_names[] = {
    {"A", 0x1e},          {"B", 0x30},          {"C", 0x2e},           {"D", 0x20},
    {"E", 0x12},          {"F", 0x21},          {"G", 0x22},           {"H", 0x23},
    {"I", 0x17},          {"J", 0x24},          {"K", 0x25},           {"L", 0x26},
    {"M", 0x32},          {"N", 0x31},          {"O", 0x18},           {"P", 0x19},
    {"Q", 0x10},          {"R", 0x13},          {"S", 0x1f},           {"T", 0x14},
    {"U", 0x16},          {"V", 0x2f},          {"W", 0x11},           {"X", 0x2d},
    {"Y", 0x15},          {"Z", 0x2c},          {"1", 0x02},           {"2", 0x03},
    {"3", 0x04},          {"4", 0x05},          {"5", 0x06},           {"6", 0x07},
    {"7", 0x08},          {"8", 0x09},          {"9", 0x0a},           {"0", 0x0b},
    {"Minus", 0x0c},      {"Equals", 0x0d},     {"Backspace", 0x0e},   {"Tab", 0x0f},
    {"LeftBracket", 0x1a}, {"RightBracket", 0x1b}, {"Enter", 0x1c},     {"LCtrl", 0x1d},
    {"Semicolon", 0x27},  {"Apostrophe", 0x28}, {"Grave", 0x29},       {"LShift", 0x2a},
    {"Backslash", 0x2b},  {"Comma", 0x33},      {"Period", 0x34},      {"Slash", 0x35},
    {"RShift", 0x36},     {"NumpadMultiply", 0x37}, {"Space", 0x39},   {"CapsLock", 0x3a},
    {"F1", 0x3b},         {"F3", 0x3d},         {"F4", 0x3e},          {"F5", 0x3f},
    {"F6", 0x40},         {"F7", 0x41},         {"F9", 0x43},          {"F10", 0x44},
    {"F11", 0x57},        {"F12", 0x58},        {"Numpad7", 0x47},     {"Numpad8", 0x48},
    {"Numpad9", 0x49},    {"NumpadMinus", 0x4a}, {"Numpad4", 0x4b},    {"Numpad5", 0x4c},
    {"Numpad6", 0x4d},    {"NumpadPlus", 0x4e}, {"Numpad1", 0x4f},     {"Numpad2", 0x50},
    {"Numpad3", 0x51},    {"Numpad0", 0x52},    {"NumpadDecimal", 0x53}, {"NumpadEnter", 0x9c},
    {"RCtrl", 0x9d},      {"NumpadDivide", 0xb5}, {"Home", 0xc7},      {"Up", 0xc8},
    {"PageUp", 0xc9},     {"Left", 0xcb},       {"Right", 0xcd},       {"End", 0xcf},
    {"Down", 0xd0},       {"PageDown", 0xd1},   {"Insert", 0xd2},      {"Delete", 0xd3},
};

/* Escape (Azimuth's quit prompt and the Autodrive cancel), F2 (Autodrive),
   F8 (rebuild), Alt and the Windows keys (system shortcuts) are not bindable. */

static const char *const binding_names[KBM_BINDING_COUNT] = {
    "Throttle", "Brake",     "SteerLeft", "SteerRight", "Handbrake", "ShiftUp",
    "ShiftDown", "Clutch",   "Camera",    "Rewind",     "LookBack",  "Activate",
    "PhotoMode", "RadioPrevious", "RadioNext", "Map",      "Pause",     "Confirm",   "Back",       "Up",        "Down",
    "Left",     "Right",     "TabLeft",   "TabRight",   "MenuX",     "MenuY",
    "MenuLeftTrigger", "MenuRightTrigger", "DPadUp", "DPadDown", "DPadLeft", "DPadRight"};

/* PC Forza conventions (FH4-FH6): Tab camera, Esc pause, M map, P photo. */
static const unsigned short default_keys[KBM_BINDING_COUNT][KBM_KEYS_PER_BINDING] = {
    {0x11},       /* Throttle W */
    {0x1f},       /* Brake S */
    {0x1e},       /* SteerLeft A */
    {0x20},       /* SteerRight D */
    {0x39},       /* Handbrake Space */
    {0x12},       /* ShiftUp E */
    {0x10},       /* ShiftDown Q */
    {0x2a},       /* Clutch LShift */
    {0x0f},       /* Camera Tab */
    {0x13},       /* Rewind R */
    {0xd0, 0x30}, /* LookBack Down, B */
    {0x1c, 0x21, 0x2d}, /* Activate Enter, F, X: world prompts show X for it (enter an event) */
    {0x19},       /* PhotoMode P */
    {0xcb},       /* RadioPrevious Left */
    {0xcd},       /* RadioNext Right */
    {0x32},       /* Map M */
    {0},          /* Pause: Esc (see Escape=1) */
    {0x1c},       /* Confirm Enter */
    {0},          /* Back: Esc only (FH6); Backspace is a page key */
    {0xc8},       /* Up */
    {0xd0},       /* Down */
    {0xcb},       /* Left */
    {0xcd},       /* Right */
    {0x10},       /* TabLeft Q */
    {0x12},       /* TabRight E */
    {0x2d},       /* MenuX X */
    {0x15},       /* MenuY Y */
    {0xc9},       /* MenuLeftTrigger PageUp */
    {0xd1},       /* MenuRightTrigger PageDown */
    {0x48},       /* DPadUp Numpad8 */
    {0x50},       /* DPadDown Numpad2 */
    {0x4b},       /* DPadLeft Numpad4 */
    {0x4d},       /* DPadRight Numpad6 */
};

void kbm_config_defaults(KbmSettings *settings) {
    /* Field by field: this freestanding DLL has no memcpy/memset. */
    settings->enabled = 0;
    settings->mode = KBM_MODE_AUTO;
    settings->mouse_look = 1;
    settings->mouse_capture = 1;
    settings->mouse_sensitivity = 80;
    settings->mouse_return_delay = 1200;
    settings->mouse_return_speed = 240;
    settings->steering_speed = 100;
    settings->invert_steering = 0;
    settings->trace = 0;
    settings->escape = 1;
    settings->menu_cursor = 1;
    settings->cursor_size = 250;
    settings->map_cursor_speed = 400;
    settings->menu_arrows_dpad = 0;
    settings->mouse_look_pitch = 0;
    settings->invert_mouse_y = 0;
    for (unsigned i = 0; i < KBM_BINDING_COUNT; ++i) {
        settings->keys[i][0] = default_keys[i][0];
        settings->keys[i][1] = default_keys[i][1];
        settings->keys[i][2] = default_keys[i][2];
    }
}

const char *kbm_config_binding_name(unsigned binding) {
    return binding < KBM_BINDING_COUNT ? binding_names[binding] : "?";
}

const char *kbm_config_key_name(unsigned code) {
    for (unsigned i = 0; i < ARRAY_COUNT(key_names); ++i) {
        if (key_names[i].code == code) {
            return key_names[i].name;
        }
    }
    return "?";
}

unsigned kbm_config_key_count(void) {
    return ARRAY_COUNT(key_names);
}

unsigned kbm_config_key_code(unsigned index) {
    return index < ARRAY_COUNT(key_names) ? key_names[index].code : 0;
}

void kbm_config_format_keys(const unsigned short keys[KBM_KEYS_PER_BINDING], char *out,
                            size_t capacity) {
    size_t used = 0;
    if (!capacity) {
        return;
    }
    for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
        const char *name = keys[k] ? kbm_config_key_name(keys[k]) : NULL;
        if (!name) {
            continue;
        }
        if (used && used + 1 < capacity) {
            out[used++] = ',';
        }
        for (; *name && used + 1 < capacity; ++name) {
            out[used++] = *name;
        }
    }
    if (!used) {
        const char *none = "None";
        for (; *none && used + 1 < capacity; ++none) {
            out[used++] = *none;
        }
    }
    out[used] = 0;
}

static int parse_key(const char *text, size_t length, unsigned short *code) {
    char name[24];
    while (length && (*text == ' ' || *text == '\t')) {
        ++text;
        --length;
    }
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t')) {
        --length;
    }
    if (!length || length >= sizeof(name)) {
        return 0;
    }
    memory_copy(name, text, length);
    name[length] = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(key_names); ++i) {
        if (text_equal_ignore_case(name, key_names[i].name)) {
            *code = key_names[i].code;
            return 1;
        }
    }
    return 0;
}

static int parse_keys(const char *value, unsigned short keys[KBM_KEYS_PER_BINDING]) {
    unsigned short parsed[KBM_KEYS_PER_BINDING] = {0};
    if (text_equal_ignore_case(value, "None")) {
        for (unsigned i = 0; i < KBM_KEYS_PER_BINDING; ++i) {
            keys[i] = 0;
        }
        return 1;
    }
    unsigned count = 0;
    const char *start = value;
    for (const char *cursor = value;; ++cursor) {
        if (*cursor == ',' || !*cursor) {
            if (count == KBM_KEYS_PER_BINDING ||
                !parse_key(start, (size_t)(cursor - start), &parsed[count])) {
                return 0;
            }
            ++count;
            if (!*cursor) {
                break;
            }
            start = cursor + 1;
        }
    }
    for (unsigned a = 0; a < count; ++a) {
        for (unsigned b = a + 1; b < count; ++b) {
            if (parsed[a] == parsed[b]) {
                return 0;
            }
        }
    }
    for (unsigned i = 0; i < KBM_KEYS_PER_BINDING; ++i) {
        keys[i] = parsed[i];
    }
    return 1;
}

static int parse_number(const char *text, int minimum, int maximum, int *value) {
    int result = 0;
    if (!*text) {
        return 0;
    }
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || result > 100000) {
            return 0;
        }
        result = result * 10 + (*text - '0');
    }
    if (result < minimum || result > maximum) {
        return 0;
    }
    *value = result;
    return 1;
}

static int parse_boolean(const char *text, int *value) {
    if (text_equal_ignore_case(text, "0") || text_equal_ignore_case(text, "1")) {
        *value = text[0] - '0';
        return 1;
    }
    return 0;
}

int kbm_config_parse(KbmSettings *settings, const char *key, const char *value) {
    if (text_equal_ignore_case(key, "Enabled")) {
        return parse_boolean(value, &settings->enabled);
    }
    if (text_equal_ignore_case(key, "Mode")) {
        if (text_equal_ignore_case(value, "Auto")) {
            settings->mode = KBM_MODE_AUTO;
        } else if (text_equal_ignore_case(value, "KBM")) {
            settings->mode = KBM_MODE_KBM;
        } else if (text_equal_ignore_case(value, "PAD")) {
            settings->mode = KBM_MODE_PAD;
        } else {
            return 0;
        }
        return 1;
    }
    if (text_equal_ignore_case(key, "MouseLook")) {
        return parse_boolean(value, &settings->mouse_look);
    }
    if (text_equal_ignore_case(key, "MouseLookPitch")) {
        return parse_boolean(value, &settings->mouse_look_pitch);
    }
    if (text_equal_ignore_case(key, "InvertMouseY")) {
        return parse_boolean(value, &settings->invert_mouse_y);
    }
    if (text_equal_ignore_case(key, "MouseCapture")) {
        return parse_boolean(value, &settings->mouse_capture);
    }
    if (text_equal_ignore_case(key, "MouseSensitivity")) {
        return parse_number(value, 1, 2000, &settings->mouse_sensitivity);
    }
    if (text_equal_ignore_case(key, "MouseReturnDelay")) {
        return parse_number(value, 0, 60000, &settings->mouse_return_delay);
    }
    if (text_equal_ignore_case(key, "MouseReturnSpeed")) {
        return parse_number(value, 10, 3600, &settings->mouse_return_speed);
    }
    if (text_equal_ignore_case(key, "SteeringSpeed")) {
        return parse_number(value, 25, 400, &settings->steering_speed);
    }
    if (text_equal_ignore_case(key, "InvertSteering")) {
        return parse_boolean(value, &settings->invert_steering);
    }
    if (text_equal_ignore_case(key, "Trace")) {
        return parse_boolean(value, &settings->trace);
    }
    if (text_equal_ignore_case(key, "Escape")) {
        return parse_boolean(value, &settings->escape);
    }
    if (text_equal_ignore_case(key, "MenuCursor")) {
        return parse_boolean(value, &settings->menu_cursor);
    }
    if (text_equal_ignore_case(key, "MapCursorSpeed")) {
        return parse_number(value, 50, 800, &settings->map_cursor_speed);
    }
    if (text_equal_ignore_case(key, "CursorSize")) {
        return parse_number(value, 50, 800, &settings->cursor_size);
    }
    if (text_equal_ignore_case(key, "MenuArrows")) {
        if (text_equal_ignore_case(value, "Stick")) {
            settings->menu_arrows_dpad = 0;
        } else if (text_equal_ignore_case(value, "DPad")) {
            settings->menu_arrows_dpad = 1;
        } else {
            return 0;
        }
        return 1;
    }
    for (unsigned i = 0; i < KBM_BINDING_COUNT; ++i) {
        if (text_equal_ignore_case(key, binding_names[i])) {
            return parse_keys(value, settings->keys[i]);
        }
    }
    return 0;
}

int kbm_config_validate(const KbmSettings *settings) {
    for (unsigned group = 0; group < 2; ++group) {
        unsigned first = group ? KBM_FIRST_MENU_BINDING : 0;
        unsigned last = group ? KBM_BINDING_COUNT : KBM_FIRST_MENU_BINDING;
        for (unsigned a = first; a < last; ++a) {
            for (unsigned i = 0; i < KBM_KEYS_PER_BINDING; ++i) {
                unsigned short key = settings->keys[a][i];
                if (!key) {
                    continue;
                }
                for (unsigned b = a; b < last; ++b) {
                    for (unsigned j = 0; j < KBM_KEYS_PER_BINDING; ++j) {
                        if ((b != a || j > i) && settings->keys[b][j] == key) {
                            return 0;
                        }
                    }
                }
            }
        }
    }
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestKbmParse(const char *key, const char *value) {
    KbmSettings settings;
    kbm_config_defaults(&settings);
    return kbm_config_parse(&settings, key, value) && kbm_config_validate(&settings);
}
#endif
