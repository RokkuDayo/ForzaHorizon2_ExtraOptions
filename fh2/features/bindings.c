#include "bindings.h"
#include "game_input.h"
#include "support.h"

typedef struct {
    unsigned xbox, keyboard;
} Binding;

#define DEFAULT_BINDINGS                                                                           \
    {                                                                                              \
        {2, VK_F2}, {0, VK_ESCAPE}, {0x80, 0}, {0x80, 0}, {4, 0}, {8, 0}, {1, 0}, {1, 0},          \
            {0x20, 0}, {0x4000, 0}, {                                                              \
            0x20, VK_TAB                                                                           \
        }, {0, VK_F1}, {0x8000, VK_TAB}, {0x4000, 0}                                                                              \
    }
static const Binding defaults[BIND_COUNT] = DEFAULT_BINDINGS;
static Binding bindings[BIND_COUNT] = DEFAULT_BINDINGS;
static const char *const actions[BIND_COUNT] = {
    "Autodrive",     "CancelAutodrive", "Headlights",        "Roof",
    "SignalLeft",    "SignalRight",     "Hazards",           "PhotoMode",
    "ShowroomPhoto", "ReturnToGarage",  "JumpToManufacturer", "ToggleUI", "RimJumpToManufacturer", "CarMeetPhoto"};

typedef struct {
    const char *name;
    unsigned value;
} InputName;

static const InputName buttons[] = {
    {"None", 0},    {"DPadUp", 1},        {"DPadDown", 2}, {"DPadLeft", 4}, {"DPadRight", 8},
    {"Menu", 0x10}, {"Start", 0x10},      {"View", 0x20},  {"Back", 0x20},  {"LeftStick", 0x40},
    {"LS", 0x40},   {"RightStick", 0x80}, {"RS", 0x80},    {"LB", 0x100},   {"RB", 0x200},
    {"A", 0x1000},  {"B", 0x2000},        {"X", 0x4000},   {"Y", 0x8000},   {"LT", 0x10000},
    {"RT", 0x20000}};
static const InputName keys[] = {
    {"None", 0},           {"Escape", VK_ESCAPE}, {"Esc", VK_ESCAPE},     {"Enter", VK_RETURN},
    {"Space", VK_SPACE},   {"Tab", VK_TAB},       {"Backspace", VK_BACK}, {"Insert", VK_INSERT},
    {"Delete", VK_DELETE}, {"Home", VK_HOME},     {"End", VK_END},        {"PageUp", VK_PRIOR},
    {"PageDown", VK_NEXT}, {"Up", VK_UP},         {"Down", VK_DOWN},      {"Left", VK_LEFT},
    {"Right", VK_RIGHT},   {"Shift", VK_SHIFT},   {"Ctrl", VK_CONTROL},   {"Alt", VK_MENU}};

void bindings_reset(void) {
    memory_copy(bindings, defaults, sizeof(bindings));
}

unsigned binding_xbox(GameAction action) {
    return (unsigned)action < BIND_COUNT ? bindings[action].xbox : 0;
}

unsigned binding_keyboard(GameAction action) {
    return (unsigned)action < BIND_COUNT ? bindings[action].keyboard : 0;
}

int binding_pad_down(GameAction action, unsigned buttons_down) {
    unsigned mask = binding_xbox(action);
    unsigned digital = mask & 0xffff;
    return mask && (buttons_down & digital) == digital && game_triggers_down(mask >> 16);
}

int binding_key_down(GameAction action) {
    return game_key_down(binding_keyboard(action));
}

int binding_down(GameAction action, unsigned buttons_down) {
    return binding_pad_down(action, buttons_down) || binding_key_down(action);
}

static int named_value(const InputName *names, size_t count, const char *text, unsigned *value) {
    for (size_t i = 0; i < count; ++i) {
        if (text_equal_ignore_case(text, names[i].name)) {
            *value = names[i].value;
            return 1;
        }
    }
    return 0;
}

static int keyboard_value(const char *text, unsigned *value) {
    size_t length = text_length(text);
    if (named_value(keys, ARRAY_COUNT(keys), text, value)) {
        return 1;
    }
    if (length == 1) {
        unsigned ch = (unsigned char)text[0];
        if (ch >= 'a' && ch <= 'z') {
            ch -= 'a' - 'A';
        }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            *value = ch;
            return 1;
        }
    }
    if (length >= 2 && length <= 3 && (text[0] == 'F' || text[0] == 'f')) {
        unsigned number = 0;
        for (size_t i = 1; i < length; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                return 0;
            }
            number = number * 10 + (unsigned)(text[i] - '0');
        }
        if (number >= 1 && number <= 24) {
            *value = VK_F1 + number - 1;
            return 1;
        }
    }
    if (length >= 3 && length <= 4 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        unsigned number = 0;
        for (size_t i = 2; i < length; ++i) {
            unsigned ch = (unsigned char)text[i];
            if (ch >= 'a' && ch <= 'f') {
                ch -= 'a' - 'A';
            }
            unsigned digit = ch >= '0' && ch <= '9'   ? ch - '0'
                             : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                                      : 16;
            if (digit == 16) {
                return 0;
            }
            number = number * 16 + digit;
        }
        *value = number;
        return 1;
    }
    return 0;
}

static int xbox_value(const char *text, unsigned *value) {
    char token[32];
    unsigned result = 0;
    for (;;) {
        while (*text == ' ' || *text == '\t') {
            ++text;
        }
        size_t length = 0;
        while (text[length] && text[length] != '+') {
            ++length;
        }
        size_t end = length;
        while (end && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
            --end;
        }
        if (!end || end >= sizeof(token)) {
            return 0;
        }
        memory_copy(token, text, end);
        token[end] = 0;
        unsigned mask;
        if (!named_value(buttons, ARRAY_COUNT(buttons), token, &mask) ||
            (!mask && (result || text[length]))) {
            return 0;
        }
        result |= mask;
        text += length;
        if (!*text) {
            break;
        }
        ++text;
    }
    *value = result;
    return 1;
}

int bindings_parse(const char *key, const char *value) {
    for (unsigned i = 0; i < BIND_COUNT; ++i) {
        size_t length = text_length(actions[i]);
        char name[48];
        memory_copy(name, actions[i], length);
        memory_copy(name + length, "Xbox", 5);
        unsigned parsed;
        if (text_equal_ignore_case(key, name)) {
            if (!xbox_value(value, &parsed)) {
                return 0;
            }
            bindings[i].xbox = parsed;
            return 1;
        }
        memory_copy(name + length, "Keyboard", 9);
        if (text_equal_ignore_case(key, name)) {
            if (!keyboard_value(value, &parsed)) {
                return 0;
            }
            bindings[i].keyboard = parsed;
            return 1;
        }
    }
    return -1;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestBindingsReset(void) {
    bindings_reset();
}

__declspec(dllexport) int Fh2TestBindingParse(const char *key, const char *value) {
    return bindings_parse(key, value);
}

__declspec(dllexport) unsigned Fh2TestBindingXbox(unsigned action) {
    return binding_xbox(action);
}

__declspec(dllexport) unsigned Fh2TestBindingKeyboard(unsigned action) {
    return binding_keyboard(action);
}

__declspec(dllexport) int Fh2TestBindingDown(unsigned action, unsigned buttons) {
    return binding_down(action, buttons);
}
#endif
