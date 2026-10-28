#ifndef FH2_AZIMUTH
/* The hooks need the Azimuth host; a Mezcal build leaves their installers unused. */
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "kbm_input.h"
#include "controller_pause.h"
#include "game_addresses.h"
#include "kbm_win32.h"
#include "kbm_pitch.h"
#include "kbm_menu.h"
#include "logging.h"
#include "support.h"

/* Game side of native keyboard & mouse (FH2 1.0.0.64, RVAs of
   forza_durango_release_final.exe; evidence in src/research/native-input).

   The game polls its pads into CControllerManager (global 0x365d7c8):
   eight controller objects at +0x10 + i * 0x1300 and an "any controller"
   object at +0x9810. Gameplay and UI code does not read pad bits; it asks a
   controller object for an action of a context through these functions:

     0x1072510 (thunk 0x1072500)  bool pressed(controller, ctx, action, consume)
     0x106f040                    float axis(controller, ctx, action)
     0x106e3e0                    float trigger(controller, ctx, action)
     0x106e690                    int state(controller, ctx, action, consume)
     0x106d640                    void consume(controller, ctx, action)

   Each tests the context's bit in the controller's active context (front of
   the deque at +0x38), resolves the pad bindings, and applies the action's
   trigger type from the registration table at manager+0xab10 (registered by
   0xf78330: CONTEXT_RACING 0 holds Gas 0, Brake 1, Steering 2, Handbrake 3,
   ShiftUp 4, ShiftDown 5, Clutch 6, LookStraightBack 0xd, SwitchCamera 0x11,
   Rewind 0x14 and ACTIVATE_PRESS 0x15; CONTEXT_UI 7 holds the OK, CANCEL,
   BACK, START, direction, D-pad, bumper, trigger, X and Y families whose
   default pad bindings 0xf62900 creates).

   While keyboard & mouse is the active source the hooks answer those same
   questions from the keyboard for the local player's controller objects,
   with the same context gate, trigger types, repeat timing (manager+0xab40,
   +0xab44) and consume rules. Pad readings, controller state buffers and
   Azimuth's gamepad objects are never written. While the gamepad is the
   active source every query runs the original code unchanged.

   The mouse turns the native driving look (0x103f9d0, which the right stick
   drives through LookForward/LookBack/LookLeft/LookRight): after the game
   computes its look yaw, the mouse yaw replaces it. */

#define MANAGER_RVA 0x365d7c8u
#define MANAGER_VTABLE_RVA 0x2e7a2e8u
#define MANAGER_FIRST_CONTROLLER 0x10u
#define CONTROLLER_STRIDE 0x1300u
#define CONTROLLER_SLOTS 8u
#define MANAGER_ANY_CONTROLLER 0x9810u
#define MANAGER_ACTION_TABLES 0xab10u
#define MANAGER_SELECTED 0xab28u
#define MANAGER_REPEAT_DELAY 0xab40u
#define MANAGER_REPEAT_INTERVAL 0xab44u
#define MANAGER_PAD_ACTIVITY 0xab98u
#define CONTROLLER_CONNECTED 0x04u
#define CONTROLLER_CONTEXTS 0x38u
#define CONTROLLER_GAMEPAD 0x60u
#define CONTROLLER_BUTTONS 0xc0u
#define IMPULSE_QUERY_START 0x106f2b0u
#define IMPULSE_QUERY_END 0x106f3cau
#define DIGITAL_BODY_RVA 0x1072510u

#define CONTEXTS 10u
#define ACTIONS 0xb0u
#define CONTEXT_RACING 0u
#define CONTEXT_UI 7u
#define ACTION_STEERING 2u

#define PI_F 3.14159265f
#define AXIS_SWITCH_DEBOUNCE_MS 150u
#define MOUSE_INTENT_WINDOW_MS 50u
#define MOUSE_INTENT_COUNTS 3
#define MOUSE_WORLD_GRACE_MS 750u
static const char *mouse_intent_why = "mouse";
#define STUCK_KEY_CHECK_MS 250u
#define STEER_PRESS_RATE 4.0f   /* full lock in 0.25 s */
#define STEER_RELEASE_RATE 6.0f /* back to centre from full lock in 0.17 s */

typedef enum { SOURCE_KBM, SOURCE_PAD } Source;
typedef enum { QUERY_DIGITAL, QUERY_AXIS, QUERY_TRIGGER, QUERY_STATE, QUERY_KINDS } QueryKind;

typedef unsigned char (*DigitalFn)(void *, unsigned, unsigned, unsigned char);
typedef float (*AxisFn)(void *, unsigned, unsigned);
typedef int (*StateFn)(void *, unsigned, unsigned, unsigned char);
typedef void (*ConsumeFn)(void *, unsigned, unsigned);
typedef void (*UpdateFn)(void *, float);
typedef void (*LookFn)(void *, unsigned char, void *);
typedef void (*ObjectFn1)(void *);

static unsigned char digital_hook(void *, unsigned, unsigned, unsigned char);
static float axis_hook(void *, unsigned, unsigned);
static float trigger_hook(void *, unsigned, unsigned);
static int state_hook(void *, unsigned, unsigned, unsigned char);
static void consume_hook(void *, unsigned, unsigned);
static void update_hook(void *, float);
static void look_hook(void *, unsigned char, void *);

typedef struct {
    uint32_t rva, size;
    const unsigned char *expected;
    const char *name;
    void *hook;
    int trampoline;
    void *original;
} InlineHook;

static const unsigned char update_bytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x48, 0x08,
                                             0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55};
static const unsigned char digital_bytes[] = {0xe9, 0x0b, 0x00, 0x00, 0x00, 0xcc, 0xcc, 0xcc,
                                              0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char axis_bytes[] = {0x40, 0x53, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                           0x48, 0x83, 0xec, 0x68, 0x44, 0x8b, 0xfa};
static const unsigned char trigger_bytes[] = {0x40, 0x53, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                              0x48, 0x83, 0xec, 0x58, 0x44, 0x8b, 0xfa};
static const unsigned char state_bytes[] = {0x40, 0x56, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                            0x48, 0x83, 0xec, 0x48, 0x44, 0x8b, 0xf2};
static const unsigned char consume_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c,
                                              0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18};
static const unsigned char look_bytes[] = {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x40, 0x18,
                                           0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x60};
/* The thunk's target, the digital query body, is reached directly. */
static const unsigned char digital_body[] = {0x44, 0x88, 0x4c, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54};

enum {
    HOOK_UPDATE, HOOK_DIGITAL, HOOK_AXIS, HOOK_TRIGGER, HOOK_STATE, HOOK_CONSUME, HOOK_LOOK,
    HOOK_TEXTURE, HOOK_WINDOW, HOOK_MOUSE_OVER, HOOK_TEXTURE_BYTES, HOOKS
};
static void texture_bytes_hook(void *, const void *, int, unsigned char, void *);
static const unsigned char texture_bytes_bytes[] = {0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0x68, 0xa9,
                                                    0x48, 0x81, 0xec, 0xf0, 0x00, 0x00, 0x00};
static void *texture_load_hook(void *, const char *, void *, void **);
static void window_source_hook(void *);
static void mouse_over_hook(void *, void *);
static void prompts_tick(void);
static void trace_texture(const char *, const char *, size_t);
static unsigned char *ui_windows[16];
static volatile LONG ui_window_count;
static SRWLOCK ui_windows_lock = SRWLOCK_INIT;
static int prompts_installed;
static const unsigned char texture_bytes[] = {0x4c, 0x89, 0x4c, 0x24, 0x20, 0x56, 0x57, 0x41,
                                              0x56, 0x48, 0x83, 0xec, 0x40, 0x48, 0xc7, 0x44,
                                              0x24, 0x38, 0xfe, 0xff, 0xff, 0xff};
static const unsigned char window_bytes[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x78,
                                             0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff, 0xff, 0xff};
/* AVUI MouseDevice::ChangeMouseOver(device, element): push rbp..r15; lea rbp,[rsp-0x27] */
static const unsigned char mouse_over_bytes[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
                                                 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xd9};

static InlineHook hooks[HOOKS] = {
    {0x1075490u, sizeof(update_bytes), update_bytes, "input_manager_update", (void *)update_hook, 1,
     NULL},
    {0x1072500u, sizeof(digital_bytes), digital_bytes, "digital_action_query_thunk",
     (void *)digital_hook, 0, NULL},
    {0x106f040u, sizeof(axis_bytes), axis_bytes, "axis_action_query", (void *)axis_hook, 1, NULL},
    {0x106e3e0u, sizeof(trigger_bytes), trigger_bytes, "trigger_action_query",
     (void *)trigger_hook, 1, NULL},
    {0x106e690u, sizeof(state_bytes), state_bytes, "action_state_query", (void *)state_hook, 1,
     NULL},
    {0x106d640u, sizeof(consume_bytes), consume_bytes, "action_consume", (void *)consume_hook, 1,
     NULL},
    {0x103f9d0u, sizeof(look_bytes), look_bytes, "driving_look_update", (void *)look_hook, 1, NULL},
    {0x1104e70u, sizeof(texture_bytes), texture_bytes, "ui_texture_load", (void *)texture_load_hook, 1,
     NULL},
    {0x154b9e0u, sizeof(window_bytes), window_bytes, "avui_window_source", (void *)window_source_hook,
     1, NULL},
    {0x14483d0u, sizeof(mouse_over_bytes), mouse_over_bytes, "avui_change_mouse_over",
     (void *)mouse_over_hook, 1, NULL},
    {0x1105510u, sizeof(texture_bytes_bytes), texture_bytes_bytes, "ui_texture_upload",
     (void *)texture_bytes_hook, 1, NULL},
};

/* Keyboard bindings: each binding answers these actions of one context. */
typedef struct {
    unsigned char binding, context, count;
    unsigned char actions[5];
} ActionFamily;

static const ActionFamily families[] = {
    {KBM_THROTTLE, CONTEXT_RACING, 1, {0x00}},
    {KBM_BRAKE, CONTEXT_RACING, 1, {0x01}},
    {KBM_HANDBRAKE, CONTEXT_RACING, 1, {0x03}},
    {KBM_SHIFT_UP, CONTEXT_RACING, 1, {0x04}},
    {KBM_SHIFT_DOWN, CONTEXT_RACING, 1, {0x05}},
    {KBM_CLUTCH, CONTEXT_RACING, 1, {0x06}},
    {KBM_LOOK_BACK, CONTEXT_RACING, 1, {0x0d}},
    {KBM_CAMERA, CONTEXT_RACING, 1, {0x11}},
    {KBM_REWIND, CONTEXT_RACING, 1, {0x14}},
    {KBM_ACTIVATE, CONTEXT_RACING, 1, {0x15}},
    {KBM_PHOTO_MODE, CONTEXT_RACING, 1, {0x0f}},
    {KBM_RADIO_PREVIOUS, CONTEXT_RACING, 1, {0x17}},
    {KBM_RADIO_NEXT, CONTEXT_RACING, 1, {0x18}},
    /* In the world the View button's map is racing action 0x16: WaitForPause
       (0x964810) takes onMap (+0x118, its sixth transition after onPause
       +0x28, onMulligan +0x58 = Rewind 0x14, onPhotoMode +0x88 = 0x0f) when
       0x1072500(player controller, 0, 0x16, consume) fires (0x964b74). The UI
       View family below is only asked in menus, so M never opened the map. */
    {KBM_MAP, CONTEXT_RACING, 1, {0x16}},
    /* View button: BACK press/release/repeat and HOME. */
    {KBM_MAP, CONTEXT_UI, 4, {0x50, 0x51, 0x52, 0x53}},
    /* Menu button: START press/release/repeat. */
    {KBM_PAUSE, CONTEXT_UI, 3, {0x54, 0x55, 0x56}},
    {KBM_CONFIRM, CONTEXT_UI, 3, {0x4a, 0x4b, 0x4c}},
    {KBM_BACK, CONTEXT_UI, 3, {0x4d, 0x4e, 0x4f}},
    /* The game binds each direction family to the D-pad and the left stick.
       Its UI (0x142cdfb, table 0x2254390) sends one key per frame and checks
       the D-pad families (NumPad keys) first, so arrows raise only the
       stick-direction families, which become arrow keys. */
    {KBM_UP, CONTEXT_UI, 3, {0x57, 0x58, 0x59}},
    {KBM_DOWN, CONTEXT_UI, 3, {0x5a, 0x5b, 0x5c}},
    {KBM_LEFT, CONTEXT_UI, 3, {0x5d, 0x5e, 0x5f}},
    {KBM_RIGHT, CONTEXT_UI, 3, {0x60, 0x61, 0x62}},
    {KBM_DPAD_UP, CONTEXT_UI, 2, {0x79, 0x7a}},
    {KBM_DPAD_DOWN, CONTEXT_UI, 2, {0x7b, 0x7c}},
    {KBM_DPAD_LEFT, CONTEXT_UI, 2, {0x7d, 0x7e}},
    {KBM_DPAD_RIGHT, CONTEXT_UI, 2, {0x7f, 0x80}},
    {KBM_TAB_LEFT, CONTEXT_UI, 3, {0x87, 0x88, 0x89}},
    {KBM_TAB_RIGHT, CONTEXT_UI, 3, {0x8a, 0x8b, 0x8c}},
    {KBM_MENU_X, CONTEXT_UI, 3, {0x8d, 0x8e, 0x8f}},
    {KBM_MENU_Y, CONTEXT_UI, 3, {0x90, 0x91, 0x92}},
    {KBM_MENU_LEFT_TRIGGER, CONTEXT_UI, 4, {0x81, 0x82, 0x83, 0x99}},
    {KBM_MENU_RIGHT_TRIGGER, CONTEXT_UI, 4, {0x84, 0x85, 0x86, 0x9a}},
};

static unsigned char *image;
static KbmSettings config;
static int installed;
static unsigned char *trampolines;

/* Binding table: binding + 1 per (context, action); steering is special. */
static unsigned char action_binding[CONTEXTS][ACTIONS];
/* Configured keys plus the virtual Escape codes below, per binding. */
enum { BINDING_KEYS = KBM_KEYS_PER_BINDING + 2 };
static unsigned short binding_list[KBM_BINDING_COUNT][BINDING_KEYS];
/* Escape is contextual: in the world it is the Menu button (pause), anywhere
   else it is B (back). Each press keeps the role it started with. */
#define ESCAPE_SCAN 0x01u
#define ESCAPE_AS_PAUSE 0xf0u
#define ESCAPE_AS_BACK 0xf1u
/* The right mouse button is B (back) outside the world. */
#define RIGHT_AS_BACK 0xf2u
static unsigned right_role;
static unsigned escape_role;
static unsigned char key_contexts[256]; /* bit per context a key is bound in */
/* World overlays (Car Meet and its car card): see world_overlay_tick. */
static const unsigned short *world_prompt_keys(unsigned context, unsigned action);
static int world_prompt_steals(unsigned context, unsigned action, unsigned code);
static void note_poll(unsigned context, unsigned action, const void *caller);
static volatile int world_overlay; /* 1: a world screen takes the UI buttons */
static unsigned char prompt_consumed[256];

/* Per-key logical state, written by the input tick on the game thread. */
static unsigned char key_held[256], key_tapped[256], key_blocked[256];
static unsigned char key_current[256], key_previous[256], key_consumed[256];
static uint64_t key_pressed_at[256];
static uint32_t repeat_deadline[CONTEXTS][ACTIONS];

static volatile Source source = SOURCE_KBM;
static volatile int kbm_active;       /* keyboard owns the routed controllers */
static void *volatile routed_controller;
static float steering, mouse_yaw, look_limit;
static uint64_t last_tick, last_switch, last_mouse_move, mouse_window_start;
static long mouse_window_counts;
static int was_focused = -1, was_prompt = -1, previous_pad_activity, mouse_look_logged;
static unsigned short previous_pad_buttons[CONTROLLER_SLOTS];
static uint16_t trigger_cache[CONTEXTS][ACTIONS];
static uint32_t answered[QUERY_KINDS][CONTEXTS][(ACTIONS + 31) / 32];
static uint32_t traced[CONTROLLER_SLOTS + 2][QUERY_KINDS][CONTEXTS][(ACTIONS + 31) / 32];
static unsigned char unowned_logged[CONTROLLER_SLOTS + 2];
static int routed_logged_slot = -1;
static volatile uint32_t input_ticks;
static volatile LONG pause_entered;
static int pointer_gate_logged = -1;
static uint64_t pad_attached_at[CONTROLLER_SLOTS];

/* ---------- small text helpers for the log ---------- */

/* Hooks can run on more than one game thread; one line is built at a time. */
static SRWLOCK line_lock = SRWLOCK_INIT;
static char line[512];
static unsigned line_length;

static void begin_line(void) {
    AcquireSRWLockExclusive(&line_lock);
    line_length = 0;
}

static void text(const char *value) {
    while (*value && line_length < sizeof(line) - 3) {
        line[line_length++] = *value++;
    }
}

static void number(uint64_t value, unsigned radix) {
    static const char digits[] = "0123456789abcdef";
    char buffer[24];
    unsigned count = 0;
    if (radix == 16) {
        text("0x");
    }
    do {
        buffer[count++] = digits[value % radix];
        value /= radix;
    } while (value);
    while (count && line_length < sizeof(line) - 3) {
        line[line_length++] = buffer[--count];
    }
}

static void flush_line(void) {
    line[line_length++] = '\r';
    line[line_length++] = '\n';
    line[line_length] = 0;
    log_append(line);
    line_length = 0;
    ReleaseSRWLockExclusive(&line_lock);
}

/* ---------- game object access ---------- */

static unsigned char *manager(void) {
    unsigned char *object;
    void *vtable;
    if (!memory_read_pointer(image + MANAGER_RVA, (void **)&object) ||
        !memory_read_pointer(object, &vtable) || vtable != image + MANAGER_VTABLE_RVA) {
        return NULL;
    }
    return object;
}

/* 0..7 = pad slots, 8 = the any-controller object, -1 = not a controller. */
static int controller_index(const unsigned char *owner, const void *controller) {
    uintptr_t first = (uintptr_t)owner + MANAGER_FIRST_CONTROLLER, value = (uintptr_t)controller;
    if (!owner || value < first || (value - first) % CONTROLLER_STRIDE) {
        return -1;
    }
    uintptr_t index = (value - first) / CONTROLLER_STRIDE;
    return index <= CONTROLLER_SLOTS ? (int)index : -1;
}

static unsigned char *controller_at(unsigned char *owner, unsigned index) {
    return owner + MANAGER_FIRST_CONTROLLER + (size_t)index * CONTROLLER_STRIDE;
}

static int pad_attached(unsigned char *controller) {
    void *gamepad;
    unsigned char connected;
    return memory_read_pointer(controller + CONTROLLER_GAMEPAD, &gamepad) &&
           memory_read(controller + CONTROLLER_CONNECTED, &connected, 1) && connected == 1;
}

/* Same test as the queries' prologue: the context bit of the active context. */
static int context_allowed(const void *controller, unsigned context) {
    struct {
        void *proxy, **blocks;
        size_t capacity, start, count;
    } contexts;
    if (!controller || context >= 32 ||
        !memory_read((const unsigned char *)controller + CONTROLLER_CONTEXTS, &contexts,
                     sizeof(contexts)) ||
        !contexts.count || !contexts.blocks || !contexts.capacity || contexts.capacity > 4096 ||
        (contexts.capacity & (contexts.capacity - 1)) || contexts.count > contexts.capacity * 2) {
        return 0;
    }
    void *block;
    uint32_t permissions;
    size_t slot = (contexts.start / 2) & (contexts.capacity - 1);
    return memory_read_pointer(contexts.blocks + slot, &block) &&
           memory_read((unsigned char *)block + (contexts.start % 2) * 8 + 4, &permissions, 4) &&
           (permissions >> context) & 1;
}

/* Trigger type of a registered action (1 pressed, 2 released, 4 down,
   8 repeat, 0x10 up, 0x20 changed, 0x40/0x80/0x100 aliases, 0x200 always). */
static unsigned action_trigger(unsigned char *owner, unsigned context, unsigned action) {
    if (trigger_cache[context][action]) {
        return trigger_cache[context][action];
    }
    unsigned char *tables, *begin, *end;
    if (!memory_read_pointer(owner + MANAGER_ACTION_TABLES, (void **)&tables) ||
        !memory_read_pointer(tables + (size_t)context * 0x20 + 8, (void **)&begin) ||
        !memory_read_pointer(tables + (size_t)context * 0x20 + 0x10, (void **)&end) ||
        end < begin || (size_t)(end - begin) % 12 || (size_t)(end - begin) / 12 > 1024) {
        return 0;
    }
    for (unsigned char *entry = begin; entry < end; entry += 12) {
        uint32_t fields[3];
        if (!memory_read(entry, fields, sizeof(fields))) {
            return 0;
        }
        if (fields[2] == action) {
            trigger_cache[context][action] = (uint16_t)fields[1];
            return fields[1];
        }
    }
    return 0;
}

static int simulation_running(void) {
    void *game, *simulation;
    unsigned char paused;
    return memory_read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game) &&
           memory_read_pointer((unsigned char *)game + 0x48, &simulation) &&
           memory_read((unsigned char *)simulation + 0x28, &paused, 1) && !paused;
}

/* The objects whose queries the keyboard answers: the any-controller object,
   the selected (local player) controller, and slots with a pad attached so a
   paired pad's user also hears the keyboard while it is the active source. */
static int keyboard_owns(const void *controller) {
    if (!kbm_active || !controller) {
        return 0;
    }
    unsigned char *owner = manager();
    int index = controller_index(owner, controller);
    if (index < 0) {
        return 0;
    }
    if (index == (int)CONTROLLER_SLOTS || controller == routed_controller) {
        return 1;
    }
    void *selected;
    if (memory_read_pointer(owner + MANAGER_SELECTED, &selected) && selected == controller) {
        return 1;
    }
    return pad_attached((unsigned char *)controller);
}

static int impulse_caller(const void *return_address) {
    uintptr_t address = (uintptr_t)return_address;
    return address >= (uintptr_t)image + IMPULSE_QUERY_START &&
           address < (uintptr_t)image + IMPULSE_QUERY_END;
}

/* ---------- key state ---------- */

/* 0 up, 1 pressed this tick, 2 held, 4 released this tick. */
static unsigned key_state(unsigned code) {
    if (!code || key_consumed[code]) {
        return 0;
    }
    int now = key_current[code], before = key_previous[code];
    return now ? (before ? 2u : 1u) : (before ? 4u : 0u);
}

/* key_state for a world prompt's extra key (world_prompt_keys). */
static unsigned prompt_key_state(unsigned code) {
    if (!code || prompt_consumed[code]) {
        return 0;
    }
    int now = key_current[code], before = key_previous[code];
    return now ? (before ? 2u : 1u) : (before ? 4u : 0u);
}

static const unsigned short *binding_keys(unsigned context, unsigned action) {
    unsigned binding = context < CONTEXTS && action < ACTIONS ? action_binding[context][action] : 0;
    return binding ? binding_list[binding - 1] : NULL;
}

static int binding_down(unsigned binding) {
    for (unsigned i = 0; i < BINDING_KEYS; ++i) {
        unsigned code = binding_list[binding][i];
        if (code && key_current[code]) {
            return 1;
        }
    }
    return 0;
}

static int trigger_fires(unsigned trigger, unsigned state, unsigned context, unsigned action,
                         unsigned char *owner) {
    switch (trigger) {
        case 1:
        case 0x100:
            return state == 1;
        case 2:
        case 0x40:
            return state == 4;
        case 4:
        case 0x80:
            return state >= 1 && state <= 3;
        case 0x10:
            return state == 0 || state == 4;
        case 0x20:
            return state == 1 || state == 3 || state == 4;
        case 0x200:
            return 1;
        case 8: {
            uint32_t delay = 400, interval = 100, now = (uint32_t)GetTickCount64();
            memory_read(owner + MANAGER_REPEAT_DELAY, &delay, 4);
            memory_read(owner + MANAGER_REPEAT_INTERVAL, &interval, 4);
            if (state == 1) {
                repeat_deadline[context][action] = now + delay;
                return 1;
            }
            if ((state == 2 || state == 3) && (int32_t)(now - repeat_deadline[context][action]) >= 0) {
                repeat_deadline[context][action] = now + interval;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

static void note_answer(QueryKind kind, unsigned context, unsigned action, const void *caller) {
    uint32_t bit = 1u << (action % 32);
    if (answered[kind][context][action / 32] & bit) {
        return;
    }
    answered[kind][context][action / 32] |= bit;
    static const char *const kinds[] = {"pressed", "axis", "trigger", "state"};
    begin_line();
    text("KBM: keyboard answered ");
    text(kinds[kind]);
    text(" query, context ");
    number(context, 10);
    text(" action ");
    number(action, 16);
    text(" (caller ");
    number((uintptr_t)caller - (uintptr_t)image, 16);
    text(").");
    flush_line();
}

static void trace_query(QueryKind kind, unsigned context, unsigned action, const void *controller,
                        const void *caller) {
    if (!config.trace || context >= CONTEXTS || action >= ACTIONS) {
        return;
    }
    uint32_t bit = 1u << (action % 32);
    unsigned slot = (unsigned)(controller_index(manager(), controller) + 1);
    if (traced[slot][kind][context][action / 32] & bit) {
        return;
    }
    traced[slot][kind][context][action / 32] |= bit;
    static const char *const kinds[] = {"pressed", "axis", "trigger", "state"};
    begin_line();
    text("KBM TRACE: ");
    text(kinds[kind]);
    text(" context ");
    number(context, 10);
    text(" action ");
    number(action, 16);
    text(" controller ");
    number(slot, 10);
    text(" caller ");
    number((uintptr_t)caller - (uintptr_t)image, 16);
    flush_line();
}

/* While the keyboard is active, a query on a controller object it does not
   answer for is logged once per object, to find screens that use another one. */
static void note_unowned(const void *controller, const void *caller) {
    if (!kbm_active || !config.trace) {
        return;
    }
    int index = controller_index(manager(), controller);
    unsigned slot = (unsigned)(index + 1);
    if (unowned_logged[slot]) {
        return;
    }
    unowned_logged[slot] = 1;
    begin_line();
    text("KBM TRACE: keyboard does not answer for controller ");
    number(slot, 10);
    text(index < 0 ? " (not a manager controller)" : "");
    text(", caller ");
    number((uintptr_t)caller - (uintptr_t)image, 16);
    flush_line();
}

/* With Trace=1, every key the UI translator (0x142cdfb) raises from a
   keyboard answer is logged with the input tick, to follow menu navigation. */
static void trace_ui_key(unsigned action, const void *caller, int pad) {
    uintptr_t at = (uintptr_t)caller - (uintptr_t)image;
    if (!config.trace || (at != 0x142ceae && at != 0x142cee1)) {
        return;
    }
    begin_line();
    text(pad ? "KBM TRACE: pad " : "KBM TRACE: keyboard ");
    text(at == 0x142ceae ? "UI key down from action " : "UI key up from action ");
    number(action, 16);
    text(" at input tick ");
    number(input_ticks, 10);
    flush_line();
}

/* ---------- query hooks ---------- */

static unsigned char digital_hook(void *controller, unsigned context, unsigned action,
                                  unsigned char consume) {
    void *caller = __builtin_return_address(0);
    trace_query(QUERY_DIGITAL, context, action, controller, caller);
    note_poll(context, action, caller);
    if (context >= CONTEXTS || action >= ACTIONS || !keyboard_owns(controller)) {
        note_unowned(controller, caller);
        unsigned char result =
            ((DigitalFn)hooks[HOOK_DIGITAL].original)(controller, context, action, consume);
        if (result) {
            trace_ui_key(action, caller, 1);
        }
        return result;
    }
    unsigned char *owner = manager();
    const unsigned short *keys = binding_keys(context, action);
    /* The world prompt handler (0x78b74a Activate 0x15, 0x78b776 0x16,
       0x78b78b Photo 0x0f) asks while an event's "X Enter event" prompt has
       the racing context switched off (2026-10-23 trace: X held, world 0);
       the pad answers it there, so the keyboard does too. */
    uintptr_t at = (uintptr_t)caller - (uintptr_t)image;
    int prompt_handler = context == CONTEXT_RACING && at >= 0x78b740u && at < 0x78b790u;
    if (!keys || !owner || (!prompt_handler && !context_allowed(controller, context))) {
        return 0;
    }
    if (prompt_handler) {
        /* There the input tick does not advance key_previous, so a held key
           would look newly pressed every frame and run the prompt's callback
           each frame (2026-10-23 trace: Activate consumed by 0x78b871 on
           every frame; the event never started). One answer per press: a
           key answers again only after it was seen released. */
        static unsigned char prompt_latched[ACTIONS][256];
        unsigned char *latched = prompt_latched[action];
        int fired = 0;
        for (unsigned i = 0; i < BINDING_KEYS; ++i) {
            unsigned code = keys[i];
            if (!code || code >= 256) {
                continue;
            }
            if (!key_current[code]) {
                latched[code] = 0;
            } else if (!latched[code] && !fired) {
                latched[code] = 1;
                fired = 1;
            }
        }
        if (fired) {
            note_answer(QUERY_DIGITAL, context, action, caller);
        }
        return (unsigned char)fired;
    }
    unsigned trigger = action_trigger(owner, context, action);
    for (unsigned i = 0; trigger && i < BINDING_KEYS; ++i) {
        unsigned code = keys[i];
        if (code && !world_prompt_steals(context, action, code) &&
            trigger_fires(trigger, key_state(code), context, action, owner)) {
            if (consume) {
                key_consumed[code] = 1;
            }
            note_answer(QUERY_DIGITAL, context, action, caller);
            trace_ui_key(action, caller, 0);
            return 1;
        }
    }
    const unsigned short *extra = world_prompt_keys(context, action);
    for (unsigned i = 0; trigger && extra && i < BINDING_KEYS; ++i) {
        unsigned code = extra[i];
        if (code && trigger_fires(trigger, prompt_key_state(code), context, action, owner)) {
            if (consume) {
                prompt_consumed[code] = 1;
            }
            note_answer(QUERY_DIGITAL, context, action, caller);
            trace_ui_key(action, caller, 0);
            return 1;
        }
    }
    return 0;
}

static float keyboard_axis(void *controller, unsigned context, unsigned action, void *caller,
                           QueryKind kind) {
    unsigned char *owner = manager();
    if (!owner || !context_allowed(controller, context) || !action_trigger(owner, context, action)) {
        return 0.0f;
    }
    float value = 0.0f;
    if (context == CONTEXT_RACING && action == ACTION_STEERING) {
        value = steering;
    } else {
        const unsigned short *keys = binding_keys(context, action);
        for (unsigned i = 0; keys && i < BINDING_KEYS; ++i) {
            if (keys[i] && key_current[keys[i]]) {
                value = 1.0f;
            }
        }
    }
    if (value != 0.0f) {
        note_answer(kind, context, action, caller);
    }
    return value;
}

/* ---------- the world map under the mouse ----------

   The pause map page (input at 0xd867b0) moves its own cursor with UI axis
   actions 0x9b/0x9c (cursor x/y, passed to MapViewControllerInteractive
   0xb8d790) and zooms with 0x72 + RT 0x84 - LT 0x81. While the menu pointer
   is on, the mouse answers 0x9b/0x9c like a stick (each input tick's motion,
   scaled by MouseSensitivity) and the wheel answers 0x72 for a moment per
   notch. The page asks 0x9b only while the map is shown, so that query also
   tells the pointer code that the map is up: there a left click is a plain A
   (the map selects what is under its own cursor) and the Windows cursor is
   hidden. */
#define MAP_CURSOR_X 0x9bu
#define MAP_CURSOR_Y 0x9cu
#define MAP_ZOOM 0x72u
#define MAP_PAGE_VTABLE 0x2dfed48u /* the map page; anything over it takes the pointer */
#define MAP_ZOOM_PULSE_MS 120u
static volatile float map_mouse_x, map_mouse_y, map_zoom;
static volatile LONG64 map_seen_at, map_zoom_until;
static volatile int map_mouse_live;

static int map_shown(void) {
    return (uint64_t)GetTickCount64() - (uint64_t)map_seen_at < 300u;
}

/* ---------- car viewer orbit (garage11) ----------

   The UI car cameras (showroom / car select preview, POKAZ SAMOCHOD; the
   camera update at 0xaa02e0) orbit with the right stick: UI-context axis
   actions 0x71 (x) and 0x72 (y), read at 0xaa03fd / 0xaa044a / 0xaa0463.
   While the menu pointer is on and the map is not shown, dragging with the
   left button held answers them from the mouse motion, like a stick that
   glides out after the mouse stops; W/A/S/D (the driving keys' codes) answer
   them as a full stick. */
#define ORBIT_X 0x71u
#define ORBIT_Y 0x72u
static volatile float orbit_x, orbit_y;
static volatile int orbit_live, mouse_left_held, mouse_right_held;
static int binding_down(unsigned binding);

/* ---------- photo mode free camera (garage12) ----------

   CCamPhotoMode (and CCamFree) read their movement in 0x103f660 from the
   camera's controller, in context 1 or 2 depending on the camera's scheme
   flag (+0x22d):
     context 1: axis 0x1d forward (+0x1f0), axis 0x20 sideways (+0x1f4),
                triggers 0x1b - 0x1c up / down (+0x1f8), axis 0x22 yaw (+0x1fc),
                axis 0x23 pitch (+0x200), axis 0x24 (+0x204), axis 0x25 (+0x208),
                triggers 0x28 / 0x29 field of view (+0x20c);
     context 2: triggers 0x1e - 0x1f forward, axis 0x20 sideways, axis 0x21
                up / down, axes 0x22 / 0x23 yaw / pitch, 0x24, 0x25, triggers
                0x29 / 0x2a field of view.
   Keyboard & mouse answer them: W/S (Throttle/Brake keys) forward and back,
   A/D (Steer keys) sideways, R / F up and down, Z / C the 0x24 axis (roll),
   a left-button drag yaw and pitch, and each wheel notch a short pulse on
   the field-of-view triggers. The photo page's own keys (Enter, Esc, Q/E,
   X, Y) are UI-context actions and stay as they are. */
#define PHOTO_KEY_UP 0x39u   /* Space (R is Reset Cosmetic Damage, as in FH6) */
#define PHOTO_KEY_DOWN 0x1du /* LCtrl */
#define PHOTO_KEY_ROLL_LEFT 0x2cu  /* Z */
#define PHOTO_KEY_ROLL_RIGHT 0x2eu /* C */
static volatile LONG64 photo_seen_at;
/* Photo mode: plain mouse motion turns the camera (no button needed). */
static volatile float photo_x, photo_y;
/* Car viewer zoom (garage12): the viewer (0xaa02e0) zooms with the left stick
   Y (axis 0x70) while the left stick is held in (state 2 of UI action 0x93
   puts it in that mode, 0xaa0379). Each wheel notch holds the stick in and
   pushes it for a moment. */
static volatile LONG64 viewer_seen_at, viewer_zoom_until;
static volatile int viewer_zoom_in;
#define VIEWER_CAMERA_START 0xaa02e0u
#define VIEWER_CAMERA_END 0xaa07d0u

static int viewer_active(void) {
    return (uint64_t)GetTickCount64() - (uint64_t)viewer_seen_at < 300u;
}

static int viewer_zooming(void) {
    return (uint64_t)GetTickCount64() < (uint64_t)viewer_zoom_until;
}

static int photo_active(void) {
    return (uint64_t)GetTickCount64() - (uint64_t)photo_seen_at < 300u;
}

static float photo_key(unsigned code) {
    return key_current[code] ? 1.0f : 0.0f;
}

/* Only the photo / free camera's own reads (0x103f660); the car viewer's
   other camera (0xaa01b0) reads context 1 too and must not get these. */
#define PHOTO_CAMERA_START 0x103f660u
#define PHOTO_CAMERA_END 0x103f8e9u
static volatile float photo_zoom;

static int photo_axis(unsigned context, unsigned action, const void *caller, float *value) {
    if ((context != 1 && context != 2) || (uintptr_t)caller < (uintptr_t)image + PHOTO_CAMERA_START ||
        (uintptr_t)caller >= (uintptr_t)image + PHOTO_CAMERA_END) {
        return 0;
    }
    InterlockedExchange64(&photo_seen_at, (LONG64)GetTickCount64());
    float forward = (float)(binding_down(KBM_THROTTLE) - binding_down(KBM_BRAKE));
    float side = (float)(binding_down(KBM_STEER_RIGHT) - binding_down(KBM_STEER_LEFT));
    float vertical = photo_key(PHOTO_KEY_UP) - photo_key(PHOTO_KEY_DOWN);
    float v = 0.0f;
    switch (action) {
        case 0x1d: v = context == 1 ? forward : 0.0f; break;
        case 0x1e: v = context == 2 && forward > 0 ? 1.0f : 0.0f; break;
        case 0x1f: v = context == 2 && forward < 0 ? 1.0f : 0.0f; break;
        case 0x1b: v = context == 1 ? photo_key(PHOTO_KEY_UP) : 0.0f; break;
        case 0x1c: v = context == 1 ? photo_key(PHOTO_KEY_DOWN) : 0.0f; break;
        case 0x20: v = side; break;
        case 0x21: v = context == 2 ? vertical : 0.0f; break;
        case 0x22: v = photo_x; break;
        case 0x23: v = photo_y; break;
        case 0x24: v = photo_zoom; break; /* zoom ("Powieksz"): + in */
        case 0x25: v = photo_key(PHOTO_KEY_ROLL_RIGHT) - photo_key(PHOTO_KEY_ROLL_LEFT); break; /* roll? */
        default:
            return 0;
    }
    if (v == 0.0f) {
        return 0;
    }
    *value = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
    return 1;
}

static int orbit_axis(unsigned context, unsigned action, float *value) {
    if (context != CONTEXT_UI || (action != ORBIT_X && action != ORBIT_Y) || !orbit_live || map_shown()) {
        return 0;
    }
    float v = action == ORBIT_X ? orbit_x : orbit_y;
    if (action == ORBIT_X) {
        v += (float)(binding_down(KBM_STEER_RIGHT) - binding_down(KBM_STEER_LEFT));
    } else {
        v += (float)(binding_down(KBM_THROTTLE) - binding_down(KBM_BRAKE));
    }
    v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
    if (v == 0.0f) {
        return 0;
    }
    *value = v;
    return 1;
}

static int map_axis(unsigned context, unsigned action, float *value) {
    if (context != CONTEXT_UI || (action != MAP_CURSOR_X && action != MAP_CURSOR_Y && action != MAP_ZOOM)) {
        return 0;
    }
    if (action == MAP_CURSOR_X) {
        InterlockedExchange64(&map_seen_at, (LONG64)GetTickCount64());
    }
    if (!map_mouse_live) {
        return 0;
    }
    float v;
    if (action == MAP_ZOOM) {
        v = map_zoom;
    } else {
        v = action == MAP_CURSOR_X ? map_mouse_x : map_mouse_y;
    }
    *value = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
    return *value != 0.0f;
}

static float axis_hook(void *controller, unsigned context, unsigned action) {
    void *caller = __builtin_return_address(0);
    trace_query(QUERY_AXIS, context, action, controller, caller);
    note_poll(context, action, caller);
    if (context >= CONTEXTS || action >= ACTIONS || impulse_caller(caller) ||
        !keyboard_owns(controller)) {
        return ((AxisFn)hooks[HOOK_AXIS].original)(controller, context, action);
    }
    float map_value = 0.0f;
    if ((context == 1 || context == 2) && photo_axis(context, action, caller, &map_value)) {
        note_answer(QUERY_AXIS, context, action, caller);
        return map_value;
    }
    if (context == CONTEXT_UI && (uintptr_t)caller >= (uintptr_t)image + VIEWER_CAMERA_START &&
        (uintptr_t)caller < (uintptr_t)image + VIEWER_CAMERA_END) {
        InterlockedExchange64(&viewer_seen_at, (LONG64)GetTickCount64());
        if (action == 0x70u && viewer_zooming()) {
            note_answer(QUERY_AXIS, context, action, caller);
            return viewer_zoom_in ? 1.0f : -1.0f;
        }
    }
    if (orbit_axis(context, action, &map_value)) {
        note_answer(QUERY_AXIS, context, action, caller);
        return map_value;
    }
    if (map_axis(context, action, &map_value)) {
        return map_value;
    }
    return keyboard_axis(controller, context, action, caller, QUERY_AXIS);
}

static float trigger_hook(void *controller, unsigned context, unsigned action) {
    void *caller = __builtin_return_address(0);
    trace_query(QUERY_TRIGGER, context, action, controller, caller);
    note_poll(context, action, caller);
    if (context >= CONTEXTS || action >= ACTIONS || !keyboard_owns(controller)) {
        return ((AxisFn)hooks[HOOK_TRIGGER].original)(controller, context, action);
    }
    float photo = 0.0f;
    if ((context == 1 || context == 2) && photo_axis(context, action, caller, &photo)) {
        note_answer(QUERY_TRIGGER, context, action, caller);
        return photo > 0.0f ? photo : 0.0f;
    }
    float value = keyboard_axis(controller, context, action, caller, QUERY_TRIGGER);
    return value > 0.0f ? value : 0.0f;
}

static int state_hook(void *controller, unsigned context, unsigned action, unsigned char consume) {
    void *caller = __builtin_return_address(0);
    trace_query(QUERY_STATE, context, action, controller, caller);
    note_poll(context, action, caller);
    if (context >= CONTEXTS || action >= ACTIONS || !keyboard_owns(controller)) {
        return ((StateFn)hooks[HOOK_STATE].original)(controller, context, action, consume);
    }
    if (context == CONTEXT_UI && action == 0x93u && viewer_zooming()) {
        note_answer(QUERY_STATE, context, action, caller);
        return 2; /* left stick held in: the viewer's zoom mode */
    }
    unsigned char *owner = manager();
    const unsigned short *keys = binding_keys(context, action);
    if (!keys || !owner || !context_allowed(controller, context) ||
        !action_trigger(owner, context, action)) {
        return 0;
    }
    unsigned code = 0, state = 0;
    for (unsigned i = 0; i < BINDING_KEYS && !state; ++i) {
        if (keys[i] && !world_prompt_steals(context, action, keys[i])) {
            code = code ? code : keys[i];
            state = key_state(keys[i]);
            code = state ? keys[i] : code;
        }
    }
    if (consume && code) {
        key_consumed[code] = 1;
    }
    const unsigned short *extra = state ? NULL : world_prompt_keys(context, action);
    for (unsigned i = 0; extra && i < BINDING_KEYS && !state; ++i) {
        if (extra[i] && (state = prompt_key_state(extra[i])) != 0 && consume) {
            prompt_consumed[extra[i]] = 1;
        }
    }
    if (state) {
        note_answer(QUERY_STATE, context, action, caller);
    }
    return (int)state;
}

static void consume_hook(void *controller, unsigned context, unsigned action) {
    ((ConsumeFn)hooks[HOOK_CONSUME].original)(controller, context, action);
    const unsigned short *keys = binding_keys(context, action);
    if (keys && keyboard_owns(controller)) {
        for (unsigned i = 0; i < BINDING_KEYS; ++i) {
            if (keys[i]) {
                key_consumed[keys[i]] = 1;
            }
        }
        const unsigned short *extra = world_prompt_keys(context, action);
        for (unsigned i = 0; extra && i < BINDING_KEYS; ++i) {
            if (extra[i]) {
                prompt_consumed[extra[i]] = 1;
            }
        }
    }
}

/* ---------- mouse look ---------- */

static void signed_number(int value);

static void look_hook(void *look, unsigned char active, void *functor) {
    ((LookFn)hooks[HOOK_LOOK].original)(look, active, functor);
    if (config.trace && kbm_active) {
        /* LookStraightBack (0xd): what the look object made of it, a few
           lines per press (garage7 diagnostics). */
        static int back_was = -1;
        static unsigned lines;
        int back = binding_down(KBM_LOOK_BACK);
        if (back != back_was) {
            back_was = back;
            lines = 0;
        }
        if (back && lines < 6) {
            unsigned char rear = 0xff;
            float look_yaw = 0.0f, limit = 0.0f;
            memory_read((unsigned char *)look + 0x10, &rear, 1);
            memory_read((unsigned char *)look + 0x1c, &look_yaw, 4);
            memory_read((unsigned char *)look + 0x20, &limit, 4);
            ++lines;
            begin_line();
            text("KBM TRACE: look back held: look object ");
            number((uintptr_t)look & 0xffffffu, 16);
            text(active ? " active" : " inactive");
            text(", rear flag ");
            number(rear, 10);
            text(", yaw ");
            signed_number(float_is_finite(look_yaw) ? (int)(look_yaw * 57.2958f) : 0);
            text(" deg, limit ");
            signed_number(float_is_finite(limit) ? (int)(limit * 57.2958f) : 0);
            text(" deg");
            flush_line();
        }
    }
    float yaw = mouse_yaw;
    if (!active || yaw == 0.0f || !kbm_active) {
        return;
    }
    unsigned char *object = look, rear;
    void *controller;
    float limit;
    if (!memory_read_pointer(object + 0x28, &controller) || !keyboard_owns(controller) ||
        !memory_read(object + 0x10, &rear, 1) || rear || !memory_read(object + 0x20, &limit, 4) ||
        !float_is_finite(limit) || !memory_is_writable(object + 0x18, 8)) {
        return;
    }
    /* The native look clamps its yaw to +0x20 when that limit is set. */
    if (limit > 0.0f) {
        look_limit = limit;
        yaw = yaw > limit ? limit : yaw < -limit ? -limit : yaw;
    }
    *(volatile float *)(object + 0x18) = yaw;
    *(volatile float *)(object + 0x1c) = yaw;
    if (!mouse_look_logged) {
        mouse_look_logged = 1;
        log_append("KBM: mouse look is turning the native driving look.\r\n");
    }
}

/* ---------- menu pointer through AVUI's own mouse input ---------- */

/* The game UI is an AVUI (WPF-like) tree. UserInterface (global 0x3684b90,
   vtable 0x2ecfb80) owns its UIWindow at +0x118 (vtable 0x2ec68a8); every
   AVUI::Window owns a PresentationSource at +0x260 (vtable 0x2edfd98, built
   by 0x154b9e0) whose MouseInputProvider sits at +0x90 (vtable 0x2edfcb0,
   0x147c7c0 creates it). MouseInputProvider::ReportInput (0x149a2f0) builds
   a RawMouseInputReport (vtable 0x2ed21c8) from (mode, timestamp, WPF
   RawMouseActions, Point*, wheel) and hands it to the AVUI InputManager,
   which hit-tests, hovers, captures and clicks as for a desktop mouse.
   Reports are sent from UserInterface::Update (vtable slot 0x78, 0x142c5e0),
   i.e. on the UI thread, before the UI processes its input. */
#define UI_UPDATE_SLOT 0x2ecfbf8u
#define UI_UPDATE 0x142c5e0u
#define UI_WINDOW 0x118u
/* The UI root object (0x1411fb8 hands it to the UIWindow factory 0x1412220). */
#define UI_ROOT_RVA 0x3684b88u
#define UIWINDOW_VTABLE 0x2ec68a8u
#define WINDOW_SOURCE 0x260u
#define WINDOW_RENDER_SIZE 0x134u
#define SOURCE_VTABLE 0x2edfd98u
#define SOURCE_MOUSE 0x90u
#define MOUSE_PROVIDER_VTABLE 0x2edfcb0u
#define MOUSE_REPORT 0x149a2f0u
#define RAW_DEACTIVATE 0x4u
#define RAW_ABSOLUTE_MOVE 0x10u
#define RAW_BUTTON1_PRESS 0x40u
#define RAW_BUTTON1_RELEASE 0x80u
#define RAW_BUTTON3_PRESS 0x400u
#define RAW_BUTTON3_RELEASE 0x800u
#define RAW_WHEEL 0x10000u

typedef unsigned char (*MouseReportFn)(void *, int, unsigned, unsigned, const float *, int);
static const unsigned char mouse_report_bytes[] = {0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0x68, 0xe8,
                                                   0x48, 0x81, 0xec, 0x10, 0x01, 0x00, 0x00};
static UpdateFn original_ui_update;
static volatile int pointer_mode, pointer_installed;
static volatile LONG pointer_intent;
static struct {
    unsigned char code, down;
} pointer_ring[64];
static volatile LONG pointer_head, pointer_tail;
static int pump_active;
static float pointer_x = -1.0f, pointer_y = -1.0f;

static void queue_pointer_button(unsigned code, int down) {
    if (!pointer_installed || (down && !pointer_mode)) {
        return;
    }
    LONG head = pointer_head;
    if ((ULONG)(head - pointer_tail) >= ARRAY_COUNT(pointer_ring)) {
        return;
    }
    pointer_ring[(ULONG)head % ARRAY_COUNT(pointer_ring)].code = (unsigned char)code;
    pointer_ring[(ULONG)head % ARRAY_COUNT(pointer_ring)].down = (unsigned char)down;
    InterlockedExchange(&pointer_head, head + 1);
    if (down) {
        InterlockedExchange(&pointer_intent, 1);
    }
}

static void *checked(void *object, unsigned vtable_rva) {
    void *vtable;
    return object && memory_read_pointer(object, &vtable) && vtable == image + vtable_rva ? object
                                                                                      : NULL;
}

/* A window is any object whose PresentationSource (+0x260) and mouse provider
   (+0x90 of the source) carry the AVUI vtables. */
static void *window_provider(unsigned char *window) {
    unsigned char *source = NULL;
    void *provider = NULL;
    if (!window || !memory_read(window + WINDOW_SOURCE, &source, sizeof(source)) ||
        !checked(source, SOURCE_VTABLE) ||
        !memory_read(source + SOURCE_MOUSE, &provider, sizeof(provider))) {
        return NULL;
    }
    return checked(provider, MOUSE_PROVIDER_VTABLE);
}

static void log_object(const char *what, unsigned offset, void *object) {
    void *vtable = NULL;
    memory_read_pointer(object, &vtable);
    begin_line();
    text(what);
    number(offset, 16);
    text(" holds an object with vtable ");
    number((uintptr_t)vtable - (uintptr_t)image, 16);
    text(".");
    flush_line();
}

/* The UI window: UserInterface+0x118 per 0x1412276; if that slot holds
   something else in the running build, the UserInterface object is searched
   once for the pointer to an object with an AVUI mouse provider. */
static void *mouse_provider(void *ui, unsigned char **window_out) {
    static unsigned window_offset = UI_WINDOW;
    static int searched;
    static unsigned deep_member, deep_inner = ~0u;
    unsigned char *window = NULL;
    void *provider = NULL;
    unsigned char *root = NULL;
    if (memory_read(image + UI_ROOT_RVA, &root, sizeof(root)) && root &&
        memory_read(root + UI_WINDOW, &window, sizeof(window))) {
        provider = window_provider(window);
    }
    if (!provider) {
        memory_read((unsigned char *)ui + window_offset, &window, sizeof(window));
        provider = window_provider(window);
    }
    for (LONG i = ui_window_count - 1; !provider && i >= 0; --i) {
        if (window_provider(ui_windows[i]) && checked(ui_windows[i], UIWINDOW_VTABLE)) {
            window = ui_windows[i];
            provider = window_provider(window);
        }
    }
    for (LONG i = ui_window_count - 1; !provider && i >= 0; --i) {
        if (window_provider(ui_windows[i])) {
            window = ui_windows[i];
            provider = window_provider(window);
        }
    }
    if (!provider && !searched) {
        searched = 1;
        log_object("KBM: menu pointer: UserInterface+", window_offset, window);
        for (unsigned offset = 0; offset < 0x800; offset += 8) {
            unsigned char *candidate = NULL;
            if (memory_read((unsigned char *)ui + offset, &candidate, sizeof(candidate)) &&
                window_provider(candidate)) {
                window_offset = offset;
                window = candidate;
                provider = window_provider(candidate);
                log_object("KBM: menu pointer: using the UI window at UserInterface+", offset,
                           candidate);
                break;
            }
        }
        /* One level deeper: a member object that owns the window. */
        for (unsigned offset = 0; !provider && offset < 0x800; offset += 8) {
            unsigned char *member = NULL;
            if (!memory_read((unsigned char *)ui + offset, &member, sizeof(member)) || !member) {
                continue;
            }
            for (unsigned inner = 0; inner < 0x400; inner += 8) {
                unsigned char *candidate = NULL;
                if (memory_read(member + inner, &candidate, sizeof(candidate)) &&
                    window_provider(candidate)) {
                    log_object("KBM: menu pointer: UI window found one level down at member offset ",
                               inner, candidate);
                    log_object("KBM: menu pointer: ... inside UserInterface+", offset, member);
                    deep_member = offset;
                    deep_inner = inner;
                    provider = window_provider(candidate);
                    window = candidate;
                    break;
                }
            }
        }
    } else if (!provider && deep_inner != ~0u) {
        unsigned char *member = NULL;
        if (memory_read((unsigned char *)ui + deep_member, &member, sizeof(member)) && member &&
            memory_read(member + deep_inner, &window, sizeof(window))) {
            provider = window_provider(window);
        }
    }
    *window_out = provider ? window : NULL;
    return provider;
}

static void report_mouse(void *provider, unsigned actions, int wheel) {
    float point[2];
    point[0] = pointer_x;
    point[1] = pointer_y;
    ((MouseReportFn)(void *)(image + MOUSE_REPORT))(provider, 0, (unsigned)GetTickCount64(),
                                                     actions, point, wheel);
}

/* ---------- the tile hub under the pointer ---------- */

/* The game's tile hubs (TileMenuNavigationManager) are not driven by AVUI
   focus but by UserInterface's own grid: +0x80/+0x88 vector of grids, +0xac
   the grid shown, +0x9c/+0xa0 its columns/rows, +0xa4/+0xa8 the focused
   column/row and +0xb0 the focus point in grid units. A grid holds its stride
   at +0 and 0x20-byte cells at +0x50 {element, column, row, width, height,
   centre}; a tile spanning several cells repeats its element. 0x140cc50(ui,
   grid, column, row) focuses a cell as the stick does (0x14239de).
   The pointer is hit-tested by its own arithmetic, with no AVUI call: each
   tile element is an AVUI Visual whose parent is at +0x70 and whose layout
   offset in the parent (VisualOffset, set by 0x1582ef0) is the float pair at
   +0x90; its RenderSize is at +0x134. Summing the offsets up to the UI
   window gives the tile's rectangle in the same layout units as the pointer.
   A tile counts only if that walk reaches the window (its page is shown).
   Hovering a tile focuses it; a left press on a tile focuses it and the
   release is A. A click on no tile does nothing. */
#define UI_GRIDS 0x80u
#define UI_GRIDS_END 0x88u
#define UI_GRID_COLUMNS 0x9cu
#define UI_GRID_ROWS 0xa0u
#define UI_FOCUS_COLUMN 0xa4u
#define UI_FOCUS_ROW 0xa8u
#define UI_GRID_INDEX 0xacu
#define UI_FOCUS_POINT 0xb0u
#define GRID_CELLS 0x50u
#define CELL_BYTES 0x20u
#define CELL_COLUMN 0x08u
#define CELL_ROW 0x0cu
#define CELL_CENTRE 0x18u
#define VISUAL_PARENT 0x70u
#define VISUAL_OFFSET 0x90u
#define ELEMENT_RENDER_SIZE 0x134u
#define SELECT_CELL 0x140cc50u

typedef void (*SelectCellFn)(void *, int, int, int);
static const unsigned char select_cell_bytes[] = {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x83, 0xec, 0x70,
                                                  0x48, 0xc7, 0x40, 0xa8, 0xfe, 0xff, 0xff, 0xff};
static int hub_select_ok;
static volatile LONG pointer_confirm, avui_wheel, stats_on_top, popup_on_top, right_stick_click_request;
/* FH6 page keys, raised by the pump for the page on top (garage17). */
enum { PAGE_KEY_COUNT = 4 };
static const unsigned char page_key_codes[PAGE_KEY_COUNT] = {0x19 /* P */, 0x13 /* R */, 0x26 /* L */,
                                                             0x0e /* Backspace */};
static const char *const page_key_names[PAGE_KEY_COUNT] = {"P", "R", "L", "Backspace"};
static volatile LONG page_key_requests;
static volatile float nav_wheel_direction;
static volatile LONG64 nav_wheel_until;
static unsigned char *hub_last_hover;
static int hub_rects_logged, click_pending, click_tab = -1, click_tab_current, click_prompt = -1;
static unsigned char *selector_pending;
static int selector_index;
#define LIST_SELECT 0xbafe20u
typedef void (*ListSelectFn)(void *, int);
/* The grid belongs to the shown page's TileMenuNavigationManager (page+0x440
   or +0x410, updated by 0x142bf20), not to UserInterface: that manager is
   the object handed to 0x1408be0 at 0x142bfaa, so nav_vector_hook records
   it every frame the hub navigates. */
static unsigned char *volatile nav_manager;
static volatile LONG64 nav_manager_at;

/* A hub belongs to a page (its nav manager at page+0x440 or +0x410); while a
   page shown over that page (show-popup) is still linked to it, the hub is
   covered and the pointer leaves it alone. */
static void *volatile cover_owner, *volatile cover_box;
static int hub_covered(unsigned char *hub) {
    unsigned char *owner = cover_owner;
    void *box = cover_box, *link = NULL, *a = NULL, *b = NULL;
    return owner && box && memory_read(owner + 0x2a0, &link, sizeof(link)) && link == box &&
           memory_read(owner + 0x440, &a, sizeof(a)) && memory_read(owner + 0x410, &b, sizeof(b)) &&
           ((unsigned char *)a == hub || (unsigned char *)b == hub);
}

/* Every hub is a TileMenuNavigationManager (vtable 0x2ed0a20, the only
   vtable its three constructors 0x1400cc0 / 0x1400ef0 / 0x1403c80 store);
   anything else seen by the navigation hook is not used as a hub. */
#define TILE_NAV_VTABLE 0x2ed0a20u
static unsigned char *current_hub(void) {
    unsigned char *hub = nav_manager;
    if (!hub || (uint64_t)GetTickCount64() - (uint64_t)nav_manager_at >= 500u || hub_covered(hub)) {
        return NULL;
    }
    if (!checked(hub, TILE_NAV_VTABLE)) {
        static unsigned char *logged;
        if (logged != hub) {
            void *vtable = NULL;
            logged = hub;
            memory_read_pointer(hub, &vtable);
            begin_line();
            text("KBM: menu pointer: navigation object ");
            number((uintptr_t)hub, 16);
            text(" has vtable ");
            number((uintptr_t)vtable - (uintptr_t)image, 16);
            text(", not TileMenuNavigationManager 0x2ed0a20; not used as a hub.");
            flush_line();
        }
        return NULL;
    }
    return hub;
}

/* Logged when the pointer turns on: what the hub grid looks like. */
static void hub_log(void) {
    unsigned char *hub = current_hub(), *begin = NULL, *end = NULL;
    int index = -1, columns = 0, rows = 0;
    if (hub) {
        memory_read(hub + 0x80u, &begin, sizeof(begin));
        memory_read(hub + 0x88u, &end, sizeof(end));
        memory_read(hub + 0xacu, &index, sizeof(index));
        memory_read(hub + 0x9cu, &columns, sizeof(columns));
        memory_read(hub + 0xa0u, &rows, sizeof(rows));
    }
    begin_line();
    text("KBM: menu pointer: hub navigation manager ");
    number((uintptr_t)hub, 16);
    text(", grids ");
    number(begin && end >= begin ? (uint64_t)(end - begin) / 8u : 0u, 10);
    text(", shown grid ");
    number((uint64_t)(index < 0 ? 0 : index), 10);
    text(", ");
    number((uint64_t)(columns < 0 ? 0 : columns), 10);
    text("x");
    number((uint64_t)(rows < 0 ? 0 : rows), 10);
    text(".");
    flush_line();
}

typedef struct {
    int grid, columns, rows, cells, column, row;
    unsigned char *element, *cell, *focused;
    float left, top, right, bottom;
} HubHit;

/* The element's rectangle in its window's layout units, and that window's
   layout size; 0 if the element is not under a UIWindow (vtable 0x2ec68a8:
   the hub may live in another UIWindow instance than the pointer's) or its
   layout is unreadable. */
static unsigned char *last_ui_root;
static int element_rect(unsigned char *element, float rect[4], float root[2]) {
    float size[2], x = 0.0f, y = 0.0f;
    if (!memory_read(element + ELEMENT_RENDER_SIZE, size, sizeof(size)) ||
        !float_is_finite(size[0]) || !float_is_finite(size[1]) || size[0] < 1.0f ||
        size[1] < 1.0f || size[0] > 8192.0f || size[1] > 8192.0f) {
        return 0;
    }
    unsigned char *visual = element;
    for (unsigned depth = 0; visual && !checked(visual, UIWINDOW_VTABLE); ++depth) {
        float offset[2];
        if (depth >= 64 || !memory_read(visual + VISUAL_OFFSET, offset, sizeof(offset)) ||
            !float_is_finite(offset[0]) || !float_is_finite(offset[1]) ||
            !memory_read(visual + VISUAL_PARENT, &visual, sizeof(visual))) {
            return 0;
        }
        x += offset[0];
        y += offset[1];
    }
    if (!visual || x < -16384.0f || y < -16384.0f || x > 16384.0f || y > 16384.0f) {
        return 0;
    }
    last_ui_root = visual;
    if (!memory_read(visual + WINDOW_RENDER_SIZE, root, 2 * sizeof(float)) ||
        !float_is_finite(root[0]) || !float_is_finite(root[1]) || root[0] < 160.0f ||
        root[1] < 90.0f || root[0] > 16384.0f || root[1] > 16384.0f) {
        root[0] = 1920.0f;
        root[1] = 1080.0f;
    }
    rect[0] = x;
    rect[1] = y;
    rect[2] = x + size[0];
    rect[3] = y + size[1];
    return 1;
}

/* Finds the tile of the shown grid under the pointer, given as fractions
   (fx, fy) of the game's client area. Returns 1 on a hit. */
static int hub_hit(unsigned char *ui, unsigned char *window, float fx, float fy, HubHit *hit) {
    unsigned char *begin = NULL, *end = NULL, *grid = NULL, *cells = NULL;
    int stride = 0, focus_column = 0, focus_row = 0;
    hit->cells = 0;
    hit->element = hit->cell = hit->focused = NULL;
    if (!hub_select_ok || !window || !memory_read(ui + UI_GRIDS, &begin, sizeof(begin)) ||
        !memory_read(ui + UI_GRIDS_END, &end, sizeof(end)) ||
        !memory_read(ui + UI_GRID_INDEX, &hit->grid, sizeof(hit->grid)) || !begin || end < begin ||
        hit->grid < 0 || (uintptr_t)hit->grid >= (uintptr_t)(end - begin) / sizeof(void *) ||
        !memory_read(begin + (size_t)hit->grid * sizeof(void *), &grid, sizeof(grid)) || !grid ||
        !memory_read(grid, &stride, sizeof(stride)) ||
        !memory_read(grid + GRID_CELLS, &cells, sizeof(cells)) || !cells ||
        !memory_read(ui + UI_GRID_COLUMNS, &hit->columns, sizeof(hit->columns)) ||
        !memory_read(ui + UI_GRID_ROWS, &hit->rows, sizeof(hit->rows)) ||
        !memory_read(ui + UI_FOCUS_COLUMN, &focus_column, sizeof(focus_column)) ||
        !memory_read(ui + UI_FOCUS_ROW, &focus_row, sizeof(focus_row)) || hit->columns < 1 ||
        hit->rows < 1 || hit->columns > 32 || hit->rows > 32 || stride < hit->columns || stride > 64) {
        return 0;
    }
    int log_rects = !hub_rects_logged;
    unsigned char *previous = NULL, *listed[64];
    unsigned listed_count = 0;
    for (int row = 0; row < hit->rows; ++row) {
        for (int column = 0; column < hit->columns; ++column) {
            unsigned char *cell = cells + ((size_t)stride * (size_t)row + (size_t)column) * CELL_BYTES;
            unsigned char *element = NULL;
            int anchor_column = 0, anchor_row = 0;
            float rect[4], root[2];
            if (!memory_read(cell, &element, sizeof(element)) || !element) {
                continue;
            }
            if (row == focus_row && column == focus_column) {
                hit->focused = element;
            }
            if (element == previous) {
                continue; /* the next cell of a wide tile */
            }
            previous = element;
            int repeated = 0;
            for (unsigned k = 0; k < listed_count; ++k) {
                repeated |= listed[k] == element; /* a tall tile's lower cell */
            }
            if (repeated) {
                continue;
            }
            if (listed_count < 64) {
                listed[listed_count++] = element;
            }
            if (!memory_read(cell + CELL_COLUMN, &anchor_column, 4) ||
                !memory_read(cell + CELL_ROW, &anchor_row, 4) || anchor_column < 0 ||
                anchor_column >= hit->columns || anchor_row < 0 || anchor_row >= hit->rows ||
                !element_rect(element, rect, root)) {
                continue;
            }
            float x = fx * root[0], y = fy * root[1];
            ++hit->cells;
            if (log_rects) {
                begin_line();
                text("KBM: menu pointer: tile ");
                number((uint64_t)anchor_column, 10);
                text(",");
                number((uint64_t)anchor_row, 10);
                text(" at ");
                number((uint64_t)(rect[0] < 0.0f ? 0.0f : rect[0]), 10);
                text(",");
                number((uint64_t)(rect[1] < 0.0f ? 0.0f : rect[1]), 10);
                text(" size ");
                number((uint64_t)(rect[2] - rect[0]), 10);
                text("x");
                number((uint64_t)(rect[3] - rect[1]), 10);
                text(".");
                flush_line();
            }
            if (!hit->element && x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3]) {
                hit->element = element;
                hit->cell = cell;
                hit->column = anchor_column;
                hit->row = anchor_row;
                hit->left = rect[0];
                hit->top = rect[1];
                hit->right = rect[2];
                hit->bottom = rect[3];
            }
        }
    }
    if (log_rects && hit->cells) {
        hub_rects_logged = 1;
    } else if (log_rects && previous) {
        /* Tiles exist but none resolved: show where the first one's walk ends. */
        hub_rects_logged = 1;
        unsigned char *visual = previous, *top = previous;
        unsigned depth = 0;
        float size[2] = {0.0f, 0.0f};
        memory_read(previous + ELEMENT_RENDER_SIZE, size, sizeof(size));
        while (visual && depth < 64) {
            top = visual;
            if (!memory_read(visual + VISUAL_PARENT, &visual, sizeof(visual))) {
                break;
            }
            ++depth;
        }
        void *top_vtable = NULL, *element_vtable = NULL;
        memory_read_pointer(top, &top_vtable);
        memory_read_pointer(previous, &element_vtable);
        begin_line();
        text("KBM: menu pointer: tiles not placed under the UI window; tile vtable ");
        number((uintptr_t)element_vtable - (uintptr_t)image, 16);
        text(" size ");
        number((uint64_t)(float_is_finite(size[0]) && size[0] > 0.0f ? size[0] : 0.0f), 10);
        text("x");
        number((uint64_t)(float_is_finite(size[1]) && size[1] > 0.0f ? size[1] : 0.0f), 10);
        text(", walk ends after ");
        number(depth, 10);
        text(" parents at vtable ");
        number((uintptr_t)top_vtable - (uintptr_t)image, 16);
        text(top == window ? " (the window)." : ".");
        flush_line();
    }
    return hit->element != NULL;
}

/* Focuses the hit tile unless it already has focus. */
static void hub_focus(unsigned char *ui, const HubHit *hit) {
    if (hit->element == hit->focused) {
        return;
    }
    uint64_t centre = 0;
    if (memory_read(hit->cell + CELL_CENTRE, &centre, sizeof(centre))) {
        memory_copy(ui + UI_FOCUS_POINT, &centre, sizeof(centre));
    }
    ((SelectCellFn)(void *)(image + SELECT_CELL))(ui, hit->grid, hit->column, hit->row);
}

/* ---------- tab headers ----------

   A hub's TileMenuNavigationManager keeps its tab strip (the StackPanel
   PART_MenuGroups, a focus scope) at +0x78: 0x142d2e0 looks the group
   headers up there by name ("PART_GROUP_" + group name) to focus them. The
   strip's children are, in order, the LB icon button, a divider, the group
   header buttons with dividers between them, and the RB icon button. The
   children are found by memory reads only: a pointer array, held by the
   panel directly or one object down, whose entries have the panel as their
   visual parent (+0x70). The buttons share one class (the LB button's
   vtable), the dividers do not. Group i is the i-th visible header button;
   that mapping is used only when the number of headers equals the number of
   grids. A click on a header sends LB/RB taps until that group is shown; a
   click on the LB/RB icon is one tap. */
#define NAV_TAB_STRIP 0x78u
#define TAB_MAX 24u
/* Group changes, as the nav manager's PageUp/PageDown key handler 0x1418750
   does them: 0x1416c80(nav, column, row) = previous group, 0x1416b30 = next.
   Both set the pending group (+0xd0, column +0xc8, row +0xcc, direction
   +0xe4) and focus its header through 0x142d2e0(nav, group); the update then
   switches. A clicked group is set the same way. */
#define NAV_PREVIOUS_GROUP 0x1416c80u
#define NAV_NEXT_GROUP 0x1416b30u
#define NAV_FOCUS_GROUP 0x142d2e0u
#define NAV_PENDING_GROUP 0xd0u
#define NAV_PENDING_COLUMN 0xc8u
#define NAV_PENDING_ROW 0xccu
#define NAV_PENDING_BACKWARD 0xe4u
typedef void (*NavGroupStepFn)(void *, int, int);
typedef void (*NavFocusGroupFn)(void *, int);
static const unsigned char nav_step_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10};
static const unsigned char nav_focus_group_bytes[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56};
static int nav_groups_ok;

static void go_to_group(unsigned char *hub, int target) {
    int pending = 0;
    memory_read(hub + NAV_PENDING_GROUP, &pending, sizeof(pending));
    int backward = target <= pending;
    int zero = 0;
    memory_copy(hub + NAV_PENDING_COLUMN, &zero, sizeof(zero));
    memory_copy(hub + NAV_PENDING_ROW, &zero, sizeof(zero));
    memory_copy(hub + NAV_PENDING_BACKWARD, &backward, sizeof(backward));
    memory_copy(hub + NAV_PENDING_GROUP, &target, sizeof(target));
    ((NavFocusGroupFn)(void *)(image + NAV_FOCUS_GROUP))(hub, target);
}

/* AVUI Panel: children collection (UIElementCollection, vtable 0x2edc4a8) at
   +0x1d8; its element array at +0x70 and count at +0x7c (the collection's
   Count, 0xb70dc0 on its +0x30 interface); Panel::GetVisualChild 0x148da80
   reads the same array. */
#define PANEL_CHILDREN 0x1d8u
#define ELEMENT_COLLECTION_VTABLE 0x2edc4a8u
#define COLLECTION_ITEMS 0x70u
#define COLLECTION_COUNT 0x7cu
static int tab_logged;

/* Fills children[] with the panel's children, in order. */
static unsigned strip_children(unsigned char *panel, unsigned char **children) {
    unsigned char *collection = NULL, *items = NULL;
    unsigned count = 0;
    if (!memory_read(panel + PANEL_CHILDREN, &collection, sizeof(collection)) ||
        !checked(collection, ELEMENT_COLLECTION_VTABLE) ||
        !memory_read(collection + COLLECTION_ITEMS, &items, sizeof(items)) || !items ||
        !memory_read(collection + COLLECTION_COUNT, &count, sizeof(count)) || count > TAB_MAX) {
        return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!memory_read(items + i * 8u, &children[i], sizeof(children[i])) || !children[i]) {
            return 0;
        }
    }
    return count;
}

/* Which tab is under the client fractions (fx, fy): -2 = the LB icon,
   -3 = the RB icon, 0.. = a group, -1 = none. */
static int tab_hit(unsigned char *hub, float fx, float fy, int *current, int *groups) {
    unsigned char *panel = NULL, *begin = NULL, *end = NULL, *children[TAB_MAX];
    void *button_vtable = NULL;
    static int no_panel_logged;
    if (hub && !no_panel_logged && (!memory_read(hub + NAV_TAB_STRIP, &panel, sizeof(panel)) || !panel)) {
        no_panel_logged = 1;
        log_append("KBM: menu pointer: this hub has no tab strip at +0x78.\r\n");
    }
    if (!hub || !memory_read(hub + NAV_TAB_STRIP, &panel, sizeof(panel)) || !panel ||
        !memory_read(hub + UI_GRIDS, &begin, sizeof(begin)) ||
        !memory_read(hub + UI_GRIDS_END, &end, sizeof(end)) || !begin || end < begin ||
        !memory_read(hub + UI_GRID_INDEX, current, sizeof(*current))) {
        return -1;
    }
    *groups = (int)((end - begin) / 8);
    unsigned count = strip_children(panel, children);
    static int search_logged;
    if (!search_logged) {
        search_logged = 1;
        void *panel_vtable = NULL, *parent = NULL;
        memory_read_pointer(panel, &panel_vtable);
        memory_read(panel + VISUAL_PARENT, &parent, sizeof(parent));
        begin_line();
        text("KBM: menu pointer: tab strip panel ");
        number((uintptr_t)panel, 16);
        text(" vtable ");
        number((uintptr_t)panel_vtable - (uintptr_t)image, 16);
        text(parent ? ", in the tree" : ", no parent");
        text(", children found ");
        number(count, 10);
        text(", groups ");
        number((uint64_t)*groups, 10);
        text(".");
        flush_line();
    }
    if (count < 3 || !memory_read_pointer(children[0], &button_vtable)) {
        return -1;
    }
    unsigned char *buttons[TAB_MAX];
    float rects[TAB_MAX][4];
    unsigned button_count = 0;
    for (unsigned i = 0; i < count; ++i) {
        void *vtable = NULL;
        float rect[4], root[2];
        if (memory_read_pointer(children[i], &vtable) && vtable == button_vtable &&
            element_rect(children[i], rect, root)) {
            buttons[button_count] = children[i];
            rects[button_count][0] = rect[0] / root[0];
            rects[button_count][1] = rect[1] / root[1];
            rects[button_count][2] = rect[2] / root[0];
            rects[button_count][3] = rect[3] / root[1];
            ++button_count;
        }
    }
    if (!tab_logged && button_count) {
        tab_logged = 1;
        begin_line();
        text("KBM: menu pointer: tab strip with ");
        number(button_count, 10);
        text(" buttons for ");
        number((uint64_t)*groups, 10);
        text(" groups (current ");
        number((uint64_t)(*current < 0 ? 0 : *current), 10);
        text(").");
        flush_line();
    }
    for (unsigned i = 0; i < button_count; ++i) {
        if (fx < rects[i][0] || fx >= rects[i][2] || fy < rects[i][1] || fy >= rects[i][3]) {
            continue;
        }
        if (i == 0) {
            return -2;
        }
        if (i == button_count - 1) {
            return -3;
        }
        return (int)button_count - 2 == *groups ? (int)i - 1 : -1;
    }
    return -1;
}

static volatile LONG tab_request;

/* A tile is an AVUI::Button (vtable 0x2d99b98); Button::OnClick (slot 0x480,
   0x15a7420) raises Click exactly as a key or a mouse release on the button
   does, which runs the handler the page bound with 0xd9fc10. */
#define BUTTON_VTABLE 0x2d99b98u
#define BUTTON_ON_CLICK_SLOT 0x480u
#define BUTTON_ON_CLICK 0x15a7420u
static void click_element(unsigned char *element) {
    void **vtable = NULL;
    void *on_click = NULL;
    if (memory_read(element, &vtable, sizeof(vtable)) && (unsigned char *)vtable == image + BUTTON_VTABLE &&
        memory_read((unsigned char *)vtable + BUTTON_ON_CLICK_SLOT, &on_click, sizeof(on_click)) &&
        on_click == image + BUTTON_ON_CLICK) {
        ((ObjectFn1)on_click)(element);
        return;
    }
    static int logged;
    if (!logged) {
        logged = 1;
        begin_line();
        text("KBM: tile click: element vtable ");
        number((uintptr_t)vtable - (uintptr_t)image, 16);
        text(" is not a plain Button; using A instead.");
        flush_line();
    }
    InterlockedExchange(&pointer_confirm, 1);
}

/* ---------- UI keys, help-bar prompts and the frame's page ----------

   UI key raise: the pad-to-UI translator (0x142cdfb) turns each UI action
   into a key and hands it to the window layer manager (0x157d110() ->
   vtable slot 0xd8, 0x1422de0(manager, up, key, 0, 0, 0), after 0x1514530
   converts the key), which gives it to the page on top or to AVUI focus.
   The map's select and the help-bar prompts use that same entry (WPF keys:
   A Space 0x12, B 0x2d, X 0x43, Y 0x44, Menu 0x07, View 0x0d, LB PageUp 0x13,
   RB PageDown 0x14, LT 0x74, RT 0x75). */
#define UI_KEY_TARGET 0x157d110u
#define UI_KEY_CONVERT 0x1514530u
#define UI_KEY_RAISE_SLOT 0xd8u
#define UI_KEY_RAISE 0x1422de0u
typedef void *(*UiKeyTargetFn)(void);
typedef int (*UiKeyConvertFn)(int);
typedef unsigned char (*UiKeyRaiseFn)(void *, unsigned char, int, void *, unsigned char, unsigned char);
static const unsigned char ui_key_target_bytes[] = {0x48, 0x8b, 0x0d};
static int ui_keys_ok;

static int ui_key(int key) {
    void *target = ui_keys_ok ? ((UiKeyTargetFn)(void *)(image + UI_KEY_TARGET))() : NULL;
    void **vtable = NULL;
    void *raise = NULL;
    if (!target || !memory_read(target, &vtable, sizeof(vtable)) ||
        !memory_read((unsigned char *)vtable + UI_KEY_RAISE_SLOT, &raise, sizeof(raise)) ||
        raise != image + UI_KEY_RAISE) {
        return 0;
    }
    int code = ((UiKeyConvertFn)(void *)(image + UI_KEY_CONVERT))(key);
    ((UiKeyRaiseFn)raise)(target, 0, code, NULL, 0, 0);
    ((UiKeyRaiseFn)raise)(target, 1, code, NULL, 0, 0);
    return 1;
}

/* Visual children through the Visual virtuals (slot 0x98 count, 0xa0 child
   i; Panel's are 0xb72ca0 / 0x148da80). Read-only, UI thread, in-tree only. */
#define VISUAL_CHILD_COUNT_SLOT 0x98u
#define VISUAL_CHILD_SLOT 0xa0u
typedef int (*ChildCountFn)(void *);
typedef void *(*ChildAtFn)(void *, int);

/* Before a node's child virtuals are called, its class must derive from
   AVUI::Visual: the primary vtable's RTTI complete object locator (at
   vtable-8, signature 1, offset 0) lists ".?AVVisual@AVUI@@" (type descriptor
   0x2fad580) among its base classes. A stale or foreign object whose first
   qword merely points into the image (the 10:47 crash: slot +0x98 was an
   event-registration stub) is refused. Results are cached per vtable. */
#define VISUAL_TYPE_DESCRIPTOR 0x2fad580u
static const char visual_type_name[] = ".?AVVisual@AVUI@@";
static int visual_rtti_ok = -1;
static struct {
    const void *vtable;
    int visual;
} visual_cache[128];
static unsigned visual_cache_next;

static int in_game_image(const void *address, size_t size) {
    return (const unsigned char *)address >= image &&
           (const unsigned char *)address + size <= image + GAME_IMAGE_SIZE;
}

static int class_derives(void **vtable, uint32_t type_descriptor);

static int class_is_visual(void **vtable) {
    return class_derives(vtable, VISUAL_TYPE_DESCRIPTOR);
}

/* The class of this primary vtable lists `type_descriptor` among its bases. */
static int class_derives(void **vtable, uint32_t type_descriptor) {
    uint32_t col[6], hierarchy[4], descriptor = 0;
    uintptr_t locator = 0;
    if (!memory_read((unsigned char *)vtable - 8, &locator, sizeof(locator)) ||
        !in_game_image((void *)locator, sizeof(col)) || !memory_read((void *)locator, col, sizeof(col)) ||
        col[0] != 1 || col[1] != 0 || !in_game_image(image + col[4], sizeof(hierarchy)) ||
        !memory_read(image + col[4], hierarchy, sizeof(hierarchy)) || hierarchy[2] > 64 ||
        !in_game_image(image + hierarchy[3], 4u * hierarchy[2])) {
        return 0;
    }
    for (uint32_t i = 0; i < hierarchy[2]; ++i) {
        uint32_t base = 0;
        if (memory_read(image + hierarchy[3] + 4u * i, &base, sizeof(base)) &&
            in_game_image(image + base, 4) && memory_read(image + base, &descriptor, sizeof(descriptor)) &&
            descriptor == type_descriptor) {
            return 1;
        }
    }
    return 0;
}

/* The node's child virtuals if it is a live AVUI Visual, else 0. */
static int visual_node(unsigned char *node, void **vtable_out, void **count_fn, void **child_fn) {
    void **vtable = NULL;
    if (visual_rtti_ok < 0) {
        char name[sizeof(visual_type_name)];
        visual_rtti_ok = memory_read(image + VISUAL_TYPE_DESCRIPTOR + 0x10, name, sizeof(name)) &&
                         memory_equal(name, visual_type_name, sizeof(name));
        log_append(visual_rtti_ok ? "KBM: menu pointer walks only AVUI Visuals (RTTI 0x2fad580).\r\n"
                                  : "KBM: AVUI Visual RTTI not as expected; menu pointer element "
                                    "walks off.\r\n");
    }
    if (!visual_rtti_ok || !node || !memory_read(node, &vtable, sizeof(vtable)) ||
        !in_game_image(vtable, VISUAL_CHILD_SLOT + 8)) {
        return 0;
    }
    int visual = -1;
    for (unsigned i = 0; i < ARRAY_COUNT(visual_cache); ++i) {
        if (visual_cache[i].vtable == vtable) {
            visual = visual_cache[i].visual;
            break;
        }
    }
    if (visual < 0) {
        visual = class_is_visual(vtable);
        visual_cache[visual_cache_next % ARRAY_COUNT(visual_cache)].vtable = vtable;
        visual_cache[visual_cache_next % ARRAY_COUNT(visual_cache)].visual = visual;
        ++visual_cache_next;
    }
    if (!visual || !memory_read((unsigned char *)vtable + VISUAL_CHILD_COUNT_SLOT, count_fn, sizeof(*count_fn)) ||
        !memory_read((unsigned char *)vtable + VISUAL_CHILD_SLOT, child_fn, sizeof(*child_fn)) ||
        !in_game_image(*count_fn, 1) || !in_game_image(*child_fn, 1)) {
        return 0;
    }
    if (vtable_out) {
        *vtable_out = vtable;
    }
    return 1;
}

/* Help-bar prompts are ControllerButtonControl elements (vtable 0x2d5e7f0)
   with a Button property (DP at 0x34e35f0; boxed value +0x28: A 0, B 1, X 2,
   Y 3, Menu 4, View 5, LB 6, RB 7, LT 8, RT 9). */
#define PROMPT_VTABLE 0x2d5e7f0u
#define PROMPT_BUTTON_DP 0x34e35f0u
#define GET_VALUE 0x15e0f10u
typedef void **(*GetValueFn)(void *, void **, void *);
/* Prompt Button values (the ControllerButton table built at 0xb61c20): A 0 .. RT 9, L3 10, R3 11,
   ..., RightThumb 20. A click on an R3 / RightThumb prompt is the right stick click (0x77). */
static const int prompt_keys[] = {0x12, 0x2d, 0x43, 0x44, 0x07, 0x0d, 0x13, 0x14, 0x74, 0x75, -1,
                                  0x77, -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   0x77};
static const char *const prompt_names[] = {"A",  "B",  "X",  "Y",  "Menu", "View", "LB",
                                           "RB", "LT", "RT", "L3", "R3",   "?",    "?",
                                           "?",  "?",  "?",  "?",  "?",    "?",    "RightThumb"};
static void release_reference(void *object);

typedef struct {
    float fx, fy;
    unsigned visited;
    unsigned char *found;
    unsigned target; /* vtable RVA of the class looked for */
} PromptSearch;

static void prompt_walk(unsigned char *visual, PromptSearch *search, unsigned depth) {
    void *vtable = NULL;
    void *count_fn = NULL, *child_fn = NULL;
    if (!visual || depth > 40 || ++search->visited > 3000 || search->found ||
        !visual_node(visual, &vtable, &count_fn, &child_fn)) {
        return;
    }
    if ((unsigned char *)vtable == image + search->target) {
        float rect[4], root[2];
        if (element_rect(visual, rect, root)) {
            float x = search->fx * root[0], y = search->fy * root[1];
            if (x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3]) {
                search->found = visual;
            }
        }
        return;
    }
    int count = ((ChildCountFn)count_fn)(visual);
    for (int i = 0; i < count && i < 256 && !search->found; ++i) {
        unsigned char *child = (unsigned char *)((ChildAtFn)child_fn)(visual, i), *parent = NULL;
        /* Only children that name this node as their visual parent (built
           and attached) are visited. */
        if (child && memory_read(child + VISUAL_PARENT, &parent, sizeof(parent)) && parent == visual) {
            prompt_walk(child, search, depth + 1);
        }
    }
}

/* The prompt's button, or -1. */
static int prompt_button(unsigned char *prompt) {
    void *dp = NULL, *value = NULL;
    int button = -1;
    if (!memory_read(image + PROMPT_BUTTON_DP, &dp, sizeof(dp)) || !dp) {
        return -1;
    }
    ((GetValueFn)(void *)(image + GET_VALUE))(prompt, &value, dp);
    if (value && memory_read((unsigned char *)value + 0x28, &button, sizeof(button)) &&
        (button < 0 || button >= (int)ARRAY_COUNT(prompt_keys) || prompt_keys[button] < 0)) {
        button = -1;
    }
    release_reference(value);
    return button;
}

/* ---------- generic tab strips and lists ----------

   Text tab strips such as STATYSTYKI's categories are ItemsSelector controls
   (vtable 0x2ecdfc0, an AVUI Selector); a click on one of its items sets its
   SelectedIndex with 0xbafe20(selector, index), the setter the game's lists
   use. The item under the pointer is found in the selector's items panel (a
   Panel, child count virtual 0xb72ca0) by the tile layout walk. */
#define ITEMS_SELECTOR_VTABLE 0x2ecdfc0u
#define PANEL_COUNT_FN 0xb72ca0u
#define SELECTED_INDEX_DP 0x36d1dd8u

typedef struct {
    float fx, fy;
    unsigned visited;
    int index, count;
} PanelSearch;

static void panel_walk(unsigned char *visual, PanelSearch *search, unsigned depth) {
    void *count_fn = NULL, *child_fn = NULL;
    if (!visual || depth > 24 || ++search->visited > 1500 || search->index >= 0 ||
        !visual_node(visual, NULL, &count_fn, &child_fn)) {
        return;
    }
    int count = ((ChildCountFn)count_fn)(visual);
    int is_panel = count_fn == image + PANEL_COUNT_FN && count >= 2;
    for (int i = 0; i < count && i < 256 && search->index < 0; ++i) {
        unsigned char *child = (unsigned char *)((ChildAtFn)child_fn)(visual, i), *parent = NULL;
        if (!child || !memory_read(child + VISUAL_PARENT, &parent, sizeof(parent)) || parent != visual) {
            continue;
        }
        if (is_panel) {
            float rect[4], root[2];
            if (element_rect(child, rect, root)) {
                float x = search->fx * root[0], y = search->fy * root[1];
                if (x >= rect[0] && x < rect[2] && y >= rect[1] && y < rect[3]) {
                    search->index = i;
                    search->count = count;
                    return;
                }
            }
            continue;
        }
        panel_walk(child, search, depth + 1);
    }
}

static int selected_index(void *selector) {
    void *dp = NULL, *value = NULL;
    int index = -1;
    if (!memory_read(image + SELECTED_INDEX_DP, &dp, sizeof(dp)) || !dp) {
        return -1;
    }
    ((GetValueFn)(void *)(image + GET_VALUE))(selector, &value, dp);
    if (!value || !memory_read((unsigned char *)value + 0x28, &index, sizeof(index))) {
        index = -1;
    }
    release_reference(value);
    return index;
}

/* The top page, read the way the game's key dispatch reads it (0x1422e11):
   the window manager (0x157d110(), vtable 0x2ecf920 WindowLayerManager)
   holds its active AVUI window at +0x68 (set by 0x156c530, which also moves
   the window's input source) and 0xb70d30(window, &page) is GetValue(window,
   Content) - the page that gets the UI keys (vtable slot 0x578). The window
   is used only if it carries a valid PresentationSource and mouse provider
   (window_provider), so 0xb70d30 never sees a stale or foreign object. Only
   while the menu pointer is on. */
#define LAYER_MANAGER_VTABLE 0x2ecf920u
#define LAYER_ACTIVE_WINDOW 0x68u
#define FRAME_PAGE 0xb70d30u
typedef void **(*FramePageFn)(void *, void **);
static const unsigned char frame_page_bytes[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x80, 0x3d};
static int frame_page_ok;
static unsigned char *top_page(void) {
    void *manager = ui_keys_ok ? ((UiKeyTargetFn)(void *)(image + UI_KEY_TARGET))() : NULL;
    unsigned char *frame = NULL;
    void *page = NULL, *page_vtable = NULL;
    if (!frame_page_ok || !checked(manager, LAYER_MANAGER_VTABLE) ||
        !memory_read((unsigned char *)manager + LAYER_ACTIVE_WINDOW, &frame, sizeof(frame)) || !frame) {
        return NULL;
    }
    if (!window_provider(frame)) {
        static unsigned char *logged;
        if (logged != frame) {
            void *vtable = NULL;
            logged = frame;
            memory_read_pointer(frame, &vtable);
            begin_line();
            text("KBM: menu pointer: active window ");
            number((uintptr_t)frame, 16);
            text(" (vtable ");
            number((uintptr_t)vtable - (uintptr_t)image, 16);
            text(") has no AVUI input source; top page unknown.");
            flush_line();
        }
        return NULL;
    }
    ((FramePageFn)(void *)(image + FRAME_PAGE))(frame, &page);
    release_reference(page); /* the window keeps its own reference */
    if (!page || !memory_read_pointer(page, &page_vtable) || (unsigned char *)page_vtable < image ||
        (unsigned char *)page_vtable >= image + GAME_IMAGE_SIZE) {
        return NULL;
    }
    return (unsigned char *)page;
}

/* A hub belongs to the page that built it: the TileMenuNavigationManager
   constructor 0x1400cc0(nav, &page, columns, rows) stores the page at
   nav+0x70 (0x1400d60: mov rax,[r14]; mov [rbx+0x70],rax); PauseMenuTiled
   (vtable 0x2e13e88) builds its hub that way (0xda5f60, kept at +0x458, so
   the old +0x440/+0x410 owner test never matched it). A page's navigation
   state is at +0x308 (3 = shown, set by the transition 0x14113a0; 2/4 =
   navigated away, 0x1409e20). The hub is used while the top page is its
   owner. If the top page is another page, the hub is dropped when its owner
   is no longer shown (state != 3) or when the top page is one the pointer
   drives itself (STATYSTYKI). Unknown cases keep the hub. */
#define NAV_OWNER 0x70u
#define PAGE_STATE 0x308u
#define MYSTATS_VTABLE 0x2e0b6e8u
static const unsigned char nav_owner_bytes[] = {0x49, 0x8b, 0x06, 0x48, 0x89, 0x43, 0x70};
static int nav_owner_ok;
static int hub_on_top(unsigned char *hub, unsigned char *page) {
    unsigned char *owner = NULL;
    int state = -1, on_top = 1;
    static unsigned char *logged_page, *logged_hub;
    if (nav_owner_ok) {
        memory_read(hub + NAV_OWNER, &owner, sizeof(owner));
    }
    if (page && owner && owner != page) {
        memory_read(owner + PAGE_STATE, &state, sizeof(state));
        on_top = !(state != 3 || checked(page, MYSTATS_VTABLE));
    }
    if (page != logged_page || hub != logged_hub) {
        void *page_vtable = NULL, *owner_vtable = NULL;
        logged_page = page;
        logged_hub = hub;
        memory_read_pointer(page, &page_vtable);
        memory_read_pointer(owner, &owner_vtable);
        begin_line();
        text("KBM TRACE: hub check: top page ");
        number((uintptr_t)page, 16);
        text(" vtable ");
        number(page ? (uintptr_t)page_vtable - (uintptr_t)image : 0u, 16);
        text(", hub ");
        number((uintptr_t)hub, 16);
        text(" owner page ");
        number((uintptr_t)owner, 16);
        text(" vtable ");
        number(owner ? (uintptr_t)owner_vtable - (uintptr_t)image : 0u, 16);
        if (state >= 0) {
            text(" state ");
            number((uint64_t)state, 10);
        }
        text(on_top ? " -> hub on top" : " -> hub covered, pointer leaves it alone");
        flush_line();
    }
    return on_top;
}

/* ---------- STATYSTYKI (MyStats, vtable 0x2e0b6e8) ----------

   MyStats::OnLoaded (vtable slot 0x488, 0xd818e0) keeps the category
   ItemsSelector PART_ItemsSelector_Category at page+0x418, the card list
   PART_CommonListBox_StatsList (CommonListBox, vtable 0x2ec8e50) at +0x420
   and a list scroll helper at +0x428 (built by 0xc2ffa0(helper, list, page,
   0): +0x30 the list ScrollViewer's IScrollInfo, +0x48 0 = vertical). It
   binds PageUp (0x13, Q/LB) to 0xd96310 and PageDown (0x14, E/RB) to
   0xd96200; each does
       if (0x14092d0(selector, -1 / +1)) 0xc39ca0(helper, 0.0f);
   0x14092d0(selector, delta) is ItemsSelector's step: SelectedIndex
   (DP 0x36d1dd8) + delta, clamped, and scroll into view; 0xc39ca0 sets the
   list's vertical offset back to 0 (IScrollInfo slot 0xc0).
   The pointer does exactly that: a click on a category tab steps by (tab -
   SelectedIndex), a click on the Q / E icon (the Buttons beside the selector
   in its StackPanel) steps by -1 / +1. The tab under the pointer is found in
   the selector's items host (VirtualizingStackPanel, vtable 0x2f000a8): its
   realized containers ranked left to right give the item index. The wheel
   calls the list's IScrollInfo MouseWheelUp / MouseWheelDown (slots 0x48 /
   0x50, 48 layout px per notch, as AVUI's ScrollViewer does for a wheel
   notch), only for the two IScrollInfo implementations verified here. */
#define MYSTATS_INIT_SLOT 0x488u
#define MYSTATS_INIT 0xd818e0u
#define MYSTATS_SELECTOR 0x418u
#define MYSTATS_SCROLL 0x428u
#define MYSTATS_PREVIOUS 0xd96310u
#define MYSTATS_NEXT 0xd96200u
#define MYSTATS_LIST_READ 0xd8193fu
#define SELECTOR_STEP 0x14092d0u
#define SCROLL_RESET 0xc39ca0u
#define SCROLL_HELPER_INFO 0x30u
#define SCROLL_HELPER_MODE 0x48u
#define VSP_VTABLE 0x2f000a8u
#define SELECTOR_ITEMS 0x1f8u
#define ITEMS_COUNT_SLOT 0xb8u
#define SCROLL_WHEEL_UP_SLOT 0x48u
#define SCROLL_WHEEL_DOWN_SLOT 0x50u
#define SCROLL_OFFSET_SLOT 0xa0u
typedef unsigned char (*SelectorStepFn)(void *, int);
typedef void (*ScrollResetFn)(void *, float);
typedef void (*ScrollInfoFn)(void *);
typedef float (*ScrollOffsetFn)(void *);
/* Both lambdas, whole (offsets 0x418 / 0x428 and the calls checked). */
static const unsigned char mystats_previous_bytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x49, 0x08, 0xba, 0x01, 0x00,
    0x00, 0x00, 0x48, 0x8b, 0x89, 0x18, 0x04, 0x00, 0x00, 0xe8, 0x22, 0xd1, 0x68, 0x00, 0x84, 0xc0,
    0x74, 0x1c, 0x48, 0x8b, 0x4b, 0x08, 0xc5, 0xf0, 0x57, 0xc9, 0x48, 0x8b, 0x89, 0x28, 0x04, 0x00,
    0x00, 0xe8, 0x5a, 0x39, 0xea, 0xff, 0xb0, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3, 0x32, 0xc0,
    0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
static const unsigned char mystats_next_bytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x48, 0x8b, 0x49, 0x08, 0xba, 0x01, 0x00,
    0x00, 0x00, 0x48, 0x8b, 0x89, 0x18, 0x04, 0x00, 0x00, 0xe8, 0x22, 0xd2, 0x68, 0x00, 0x84, 0xc0,
    0x74, 0x1c, 0x48, 0x8b, 0x4b, 0x08, 0xc5, 0xf0, 0x57, 0xc9, 0x48, 0x8b, 0x89, 0x28, 0x04, 0x00,
    0x00, 0xe8, 0x6a, 0x3a, 0xea, 0xff, 0xb0, 0x01, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3, 0x32, 0xc0,
    0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};
/* mov rcx,[rsi+0x420] in MyStats::OnLoaded */
static const unsigned char mystats_list_bytes[] = {0x48, 0x8b, 0x8e, 0x20, 0x04, 0x00, 0x00};
static const unsigned char selector_step_bytes[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56,
                                                    0x48, 0x83, 0xec, 0x40};
static const unsigned char scroll_reset_bytes[] = {
    0x8b, 0x51, 0x48, 0x85, 0xd2, 0x74, 0x12, 0xff, 0xca, 0x75, 0x1c, 0x48, 0x8b, 0x49, 0x30, 0x48,
    0x8b, 0x01, 0x48, 0xff, 0xa0, 0xb8, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x49, 0x30, 0x48, 0x8b, 0x01,
    0x48, 0xff, 0xa0, 0xc0, 0x00, 0x00, 0x00, 0xc3};
/* IScrollInfo implementations: interface vtable and its wheel-up, wheel-down
   and VerticalOffset slots. */
static const struct {
    uint32_t vtable, up, down, offset;
    const char *name;
} scroll_infos[] = {
    {0x2f23140u, 0xb55ad0u, 0xb559b0u, 0xb72bc0u, "ScrollContentPresenter"},
    {0x2f00808u, 0x1552e20u, 0x1552c40u, 0x15813c0u, "VirtualizingStackPanel"},
};
static int stats_ok;

static void verify_stats(void) {
    void *init = NULL;
    stats_ok = memory_equal(image + MYSTATS_PREVIOUS, mystats_previous_bytes, sizeof(mystats_previous_bytes)) &&
               memory_equal(image + MYSTATS_NEXT, mystats_next_bytes, sizeof(mystats_next_bytes)) &&
               memory_equal(image + MYSTATS_LIST_READ, mystats_list_bytes, sizeof(mystats_list_bytes)) &&
               memory_equal(image + SELECTOR_STEP, selector_step_bytes, sizeof(selector_step_bytes)) &&
               memory_equal(image + SCROLL_RESET, scroll_reset_bytes, sizeof(scroll_reset_bytes)) &&
               memory_read(image + MYSTATS_VTABLE + MYSTATS_INIT_SLOT, &init, sizeof(init)) &&
               init == image + MYSTATS_INIT && frame_page_ok;
    nav_owner_ok = memory_equal(image + 0x1400d60u, nav_owner_bytes, sizeof(nav_owner_bytes));
    log_append(stats_ok ? "KBM: STATYSTYKI pointer: MyStats selector step 0x14092d0, list reset 0xc39ca0 "
                          "and page offsets 0x418/0x420/0x428 verified.\r\n"
                        : "KBM: STATYSTYKI pointer: MyStats code changed; its tabs and wheel are off.\r\n");
    log_append(nav_owner_ok ? "KBM: menu pointer: hub owner page (nav+0x70) verified.\r\n"
                            : "KBM: menu pointer: hub owner store 0x1400d60 changed; hub top check "
                              "limited.\r\n");
}

static unsigned char *stats_selector(unsigned char *page) {
    unsigned char *selector = NULL;
    return memory_read(page + MYSTATS_SELECTOR, &selector, sizeof(selector))
               ? (unsigned char *)checked(selector, ITEMS_SELECTOR_VTABLE)
               : NULL;
}

static void signed_number(int value) {
    if (value < 0) {
        text("-");
        number((uint64_t)(-(int64_t)value), 10);
    } else {
        number((uint64_t)value, 10);
    }
}

/* What Q / E do, with any delta. */
static void stats_step(unsigned char *page, int delta, const char *why) {
    unsigned char *selector = stats_selector(page), *helper = NULL, *info = NULL;
    if (!selector) {
        log_append("KBM TRACE: stats category: page+0x418 is not an ItemsSelector; ignored.\r\n");
        return;
    }
    int before = selected_index(selector);
    unsigned char moved = ((SelectorStepFn)(void *)(image + SELECTOR_STEP))(selector, delta);
    int reset = moved && memory_read(page + MYSTATS_SCROLL, &helper, sizeof(helper)) && helper &&
                memory_read(helper + SCROLL_HELPER_INFO, &info, sizeof(info)) && info;
    if (reset) {
        ((ScrollResetFn)(void *)(image + SCROLL_RESET))(helper, 0.0f);
    }
    int after = selected_index(selector);
    begin_line();
    text("KBM TRACE: stats category ");
    text(why);
    text(": step ");
    signed_number(delta);
    text(" via ItemsSelector 0x14092d0 on ");
    number((uintptr_t)selector, 16);
    text(", SelectedIndex ");
    signed_number(before);
    text(" -> ");
    signed_number(after);
    text(moved ? ", list scroll reset" : ", not moved");
    text(moved && !reset ? " skipped (no scroll helper)" : "");
    text(".");
    flush_line();
}

/* The list's IScrollInfo and its index in scroll_infos, or NULL. */
static unsigned char *stats_scroll_info(unsigned char *page, unsigned *kind) {
    unsigned char *helper = NULL, *info = NULL;
    void **vtable = NULL;
    int mode = -1;
    if (!memory_read(page + MYSTATS_SCROLL, &helper, sizeof(helper)) || !helper ||
        !memory_read(helper + SCROLL_HELPER_INFO, &info, sizeof(info)) || !info ||
        !memory_read(helper + SCROLL_HELPER_MODE, &mode, sizeof(mode)) ||
        !memory_read(info, &vtable, sizeof(vtable))) {
        log_append("KBM TRACE: stats wheel: no list scroll helper at page+0x428.\r\n");
        return NULL;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(scroll_infos); ++i) {
        void *up = NULL, *down = NULL, *offset = NULL;
        if ((unsigned char *)vtable == image + scroll_infos[i].vtable &&
            memory_read((unsigned char *)vtable + SCROLL_WHEEL_UP_SLOT, &up, sizeof(up)) &&
            memory_read((unsigned char *)vtable + SCROLL_WHEEL_DOWN_SLOT, &down, sizeof(down)) &&
            memory_read((unsigned char *)vtable + SCROLL_OFFSET_SLOT, &offset, sizeof(offset)) &&
            up == image + scroll_infos[i].up && down == image + scroll_infos[i].down &&
            offset == image + scroll_infos[i].offset && mode == 0) {
            *kind = i;
            return info;
        }
    }
    begin_line();
    text("KBM TRACE: stats wheel: list scroll info ");
    number((uintptr_t)info, 16);
    text(" has vtable ");
    number((uintptr_t)vtable - (uintptr_t)image, 16);
    text(" mode ");
    signed_number(mode);
    text(", not a verified vertical IScrollInfo; wheel ignored.");
    flush_line();
    return NULL;
}

static void stats_wheel(unsigned char *page, long wheel) {
    unsigned kind = 0;
    unsigned char *info = stats_scroll_info(page, &kind);
    if (!info) {
        return;
    }
    void **vtable = *(void ***)info;
    int notches = (int)((wheel < 0 ? -wheel : wheel) + 60) / 120;
    notches = notches < 1 ? 1 : notches > 4 ? 4 : notches;
    float before = ((ScrollOffsetFn)vtable[SCROLL_OFFSET_SLOT / 8])(info);
    for (int i = 0; i < notches; ++i) {
        ((ScrollInfoFn)vtable[(wheel > 0 ? SCROLL_WHEEL_UP_SLOT : SCROLL_WHEEL_DOWN_SLOT) / 8])(info);
    }
    float after = ((ScrollOffsetFn)vtable[SCROLL_OFFSET_SLOT / 8])(info);
    begin_line();
    text("KBM TRACE: stats wheel ");
    text(wheel > 0 ? "up " : "down ");
    number((uint64_t)notches, 10);
    text(" notch(es) via ");
    text(scroll_infos[kind].name);
    text(" IScrollInfo ");
    number((uintptr_t)info, 16);
    text(wheel > 0 ? " MouseWheelUp" : " MouseWheelDown");
    text(", vertical offset ");
    number((uint64_t)(float_is_finite(before) && before > 0.0f ? before : 0.0f), 10);
    text(" -> ");
    number((uint64_t)(float_is_finite(after) && after > 0.0f ? after : 0.0f), 10);
    text(".");
    flush_line();
}

/* Visual children of a node (Visual virtuals 0x98 / 0xa0), attached ones only. */
static unsigned visual_children(unsigned char *visual, unsigned char **children, unsigned max) {
    void *count_fn = NULL, *child_fn = NULL;
    unsigned found = 0;
    if (!visual || !visual_node(visual, NULL, &count_fn, &child_fn)) {
        return 0;
    }
    int count = ((ChildCountFn)count_fn)(visual);
    for (int i = 0; i < count && i < 256 && found < max; ++i) {
        unsigned char *child = (unsigned char *)((ChildAtFn)child_fn)(visual, i), *parent = NULL;
        if (child && memory_read(child + VISUAL_PARENT, &parent, sizeof(parent)) && parent == visual) {
            children[found++] = child;
        }
    }
    return found;
}

static unsigned char *find_descendant(unsigned char *visual, unsigned vtable_rva, unsigned depth,
                                      unsigned *visited) {
    unsigned char *children[64];
    if (depth > 16 || ++*visited > 400) {
        return NULL;
    }
    unsigned count = visual_children(visual, children, ARRAY_COUNT(children));
    for (unsigned i = 0; i < count; ++i) {
        if (checked(children[i], vtable_rva)) {
            return children[i];
        }
        unsigned char *found = find_descendant(children[i], vtable_rva, depth + 1, visited);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static void log_rect(const char *what, unsigned char *element, const float rect[4]) {
    begin_line();
    text(what);
    number((uintptr_t)element, 16);
    text(" at ");
    number((uint64_t)(rect[0] < 0.0f ? 0.0f : rect[0]), 10);
    text(",");
    number((uint64_t)(rect[1] < 0.0f ? 0.0f : rect[1]), 10);
    text(" - ");
    number((uint64_t)(rect[2] < 0.0f ? 0.0f : rect[2]), 10);
    text(",");
    number((uint64_t)(rect[3] < 0.0f ? 0.0f : rect[3]), 10);
    flush_line();
}

/* A press on the STATYSTYKI strip: the step to make (*delta) and what was
   hit (*why). 1 if the press hit a category tab or the Q / E icon. */
static int stats_hit(unsigned char *page, float fx, float fy, int *delta, const char **why) {
    unsigned char *selector = stats_selector(page), *strip = NULL, *strip_items[TAB_MAX];
    float sel[4], bar[4], root[2], root2[2];
    if (!selector) {
        log_append("KBM TRACE: stats click: page+0x418 is not an ItemsSelector.\r\n");
        return 0;
    }
    if (!element_rect(selector, sel, root)) {
        unsigned char *visual = selector, *top = selector;
        void *top_vtable = NULL;
        unsigned depth = 0;
        while (visual && depth < 64) {
            top = visual;
            if (!memory_read(visual + VISUAL_PARENT, &visual, sizeof(visual))) {
                break;
            }
            ++depth;
        }
        memory_read_pointer(top, &top_vtable);
        begin_line();
        text("KBM TRACE: stats click: selector ");
        number((uintptr_t)selector, 16);
        text(" not placed under a UIWindow; walk ends after ");
        number(depth, 10);
        text(" parents at vtable ");
        number((uintptr_t)top_vtable - (uintptr_t)image, 16);
        flush_line();
        return 0;
    }
    float x = fx * root[0], y = fy * root[1];
    memory_read(selector + VISUAL_PARENT, &strip, sizeof(strip));
    if (!strip || !element_rect(strip, bar, root2)) {
        for (unsigned i = 0; i < 4; ++i) {
            bar[i] = sel[i];
        }
    }
    log_rect("KBM TRACE: stats click: category selector ", selector, sel);
    /* A little slack: the page is tilted 9 degrees about its bottom. */
    if (y < bar[1] - 24.0f || y >= bar[3] + 24.0f || x < bar[0] - 8.0f || x >= bar[2] + 8.0f) {
        return 0;
    }
    if (x < sel[0] || x >= sel[2]) {
        /* The Q / E Buttons beside the selector in the strip's StackPanel. */
        unsigned count = strip ? strip_children(strip, strip_items) : 0, at = count;
        for (unsigned i = 0; i < count; ++i) {
            at = strip_items[i] == selector ? i : at;
        }
        for (unsigned i = 0; i < count && at < count; ++i) {
            float r[4], rr[2];
            if (i != at && element_rect(strip_items[i], r, rr) && x >= r[0] - 8.0f && x < r[2] + 8.0f) {
                log_rect(i < at ? "KBM TRACE: stats click hit Q icon " : "KBM TRACE: stats click hit E icon ",
                         strip_items[i], r);
                *delta = i < at ? -1 : 1;
                *why = i < at ? "Q icon click" : "E icon click";
                return 1;
            }
        }
        begin_line();
        text("KBM TRACE: stats click: beside the selector but on no icon (strip children ");
        number(count, 10);
        text(", selector at ");
        number(at, 10);
        text(")");
        flush_line();
        return 0;
    }
    unsigned visited = 0;
    unsigned char *host = find_descendant(selector, VSP_VTABLE, 0, &visited), *items[64];
    unsigned count = host ? visual_children(host, items, ARRAY_COUNT(items)) : 0;
    float left[64];
    int hit = -1;
    for (unsigned i = 0; i < count; ++i) {
        float r[4], rr[2];
        left[i] = 1e9f;
        if (element_rect(items[i], r, rr)) {
            left[i] = r[0];
            if (hit < 0 && x >= r[0] && x < r[2]) {
                hit = (int)i;
            }
        }
    }
    int item_count = -1;
    unsigned char *collection = NULL;
    void **collection_vtable = NULL;
    void *count_fn = NULL;
    if (memory_read(selector + SELECTOR_ITEMS, &collection, sizeof(collection)) && collection &&
        memory_read(collection, &collection_vtable, sizeof(collection_vtable)) &&
        (unsigned char *)collection_vtable >= image &&
        (unsigned char *)collection_vtable < image + GAME_IMAGE_SIZE &&
        memory_read((unsigned char *)collection_vtable + ITEMS_COUNT_SLOT, &count_fn, sizeof(count_fn)) &&
        (unsigned char *)count_fn >= image && (unsigned char *)count_fn < image + GAME_IMAGE_SIZE) {
        item_count = ((ChildCountFn)count_fn)(collection);
    }
    int rank = 0;
    for (unsigned i = 0; hit >= 0 && i < count; ++i) {
        rank += left[i] < left[hit];
    }
    int current = selected_index(selector);
    begin_line();
    text("KBM TRACE: stats click: items host ");
    number((uintptr_t)host, 16);
    text(host ? " (VirtualizingStackPanel)" : " (not found)");
    text(", containers ");
    number(count, 10);
    text(", items ");
    signed_number(item_count);
    text(", hit container ");
    signed_number(hit);
    text(" = tab ");
    signed_number(hit < 0 ? -1 : rank);
    text(", SelectedIndex ");
    signed_number(current);
    flush_line();
    if (hit < 0 || current < 0 || (item_count >= 0 && rank >= item_count)) {
        return 0;
    }
    *delta = rank - current;
    *why = "tab click";
    return 1;
}

static unsigned char *stats_click_page;
static int stats_click_delta;
static const char *stats_click_why;

/* ---------- car strips (garage1) ----------

   MOJE SAMOCHODY and the buy-car pages (CarSelect, vtable 0x2dcdf88; also
   MarketplaceCarSelect 0x2e00d98) show their cars in an ItemsSelector
   (vtable 0x2ecdfc0, the class the STATYSTYKI tabs use) styled
   A_Style_Carousel6ItemList: its items host is a VirtualizingScrollPanel
   (vtable 0x2ece780) with Padding 295, ItemWidth 295 (289 + margin) and
   FocusItemIndex 1, so the selected car sits in the second slot after the
   padding and the strip slides under it. Generic for any page: the first
   ItemsSelector under the top page whose items host is such a panel.
   - Click on a car: step the selection by its slot distance from the focus
     slot with the selector's own step (0x14092d0, as Left / Right do).
   - Click on the selected car, or a second click within 500 ms at the same
     place (double click): A, which opens the car.
   - Wheel: one step per notch, up = previous (left), down = next (right).
   - Right click is already B (back) while the pointer is on. */
#define SCROLL_PANEL_VTABLE 0x2ece780u
#define CAROUSEL_SLOT 295.0f
#define CAROUSEL_PADDING_SLOTS 1
#define CAROUSEL_FOCUS_SLOT 1
static int carousel_ok = -1;
static uint64_t carousel_last_click;
static float carousel_last_x, carousel_last_y;

static unsigned char *find_carousel(unsigned char *visual, unsigned depth, unsigned *visited,
                                    unsigned char **host_out) {
    unsigned char *children[64];
    if (!visual || depth > 40 || ++*visited > 4000) {
        return NULL;
    }
    if (checked(visual, ITEMS_SELECTOR_VTABLE)) {
        unsigned inner = 0;
        unsigned char *host = find_descendant(visual, SCROLL_PANEL_VTABLE, 0, &inner);
        if (host) {
            *host_out = host;
            return visual;
        }
    }
    unsigned count = visual_children(visual, children, ARRAY_COUNT(children));
    for (unsigned i = 0; i < count; ++i) {
        unsigned char *found = find_carousel(children[i], depth + 1, visited, host_out);
        if (found) {
            return found;
        }
    }
    return NULL;
}

/* The car strip under the top page, or NULL. */
static unsigned char *carousel_on(unsigned char *page, unsigned char **host) {
    if (carousel_ok < 0) {
        carousel_ok = memory_equal(image + SELECTOR_STEP, selector_step_bytes, sizeof(selector_step_bytes));
        log_append(carousel_ok ? "KBM: car strip pointer: ItemsSelector step 0x14092d0 verified "
                                 "(VirtualizingScrollPanel 0x2ece780 strips).\r\n"
                               : "KBM: car strip pointer: ItemsSelector step 0x14092d0 changed; car "
                                 "strips keep keys only.\r\n");
    }
    unsigned visited = 0;
    *host = NULL;
    return carousel_ok && page ? find_carousel(page, 0, &visited, host) : NULL;
}

static void carousel_step(unsigned char *selector, int delta, const char *why) {
    int before = selected_index(selector);
    unsigned char moved = ((SelectorStepFn)(void *)(image + SELECTOR_STEP))(selector, delta);
    int after = selected_index(selector);
    begin_line();
    text("KBM TRACE: car strip ");
    text(why);
    text(": step ");
    signed_number(delta);
    text(" via ItemsSelector 0x14092d0 on ");
    number((uintptr_t)selector, 16);
    text(", SelectedIndex ");
    signed_number(before);
    text(" -> ");
    signed_number(after);
    text(moved ? "." : " (not moved).");
    flush_line();
}

/* A press: 1 if it landed on a car strip (handled), else 0.
   The panel scrolls its children without moving their layout offsets
   (container k always sits at panel left + 295 * k), and it keeps the
   selected car drawn in the focus slot: the second tile, panel left + 295
   to + 590 (the highlight bar at 359-679 is that tile scaled 1.12). So the
   click's distance from the focus slot, in whole tiles, is the step; 0 is
   the selected car. */
static int carousel_click(unsigned char *page, float fx, float fy, uint64_t now) {
    unsigned char *host = NULL, *selector = carousel_on(page, &host);
    float strip[4], root[2];
    if (!selector || !element_rect(host, strip, root)) {
        return 0;
    }
    float x = fx * root[0], y = fy * root[1];
    if (x < strip[0] || x >= strip[2] || y < strip[1] - 40.0f || y >= strip[3] + 40.0f) {
        return 0;
    }
    float focus_left = strip[0] + CAROUSEL_SLOT * (float)CAROUSEL_FOCUS_SLOT;
    float slots = (x - focus_left) / CAROUSEL_SLOT;
    int delta = (int)(slots + 64.0f) - 64; /* floor */
    int current = selected_index(selector);
    begin_line();
    text("KBM TRACE: car strip click at ");
    number((uint64_t)x, 10);
    text(",");
    number((uint64_t)y, 10);
    text(": panel ");
    number((uint64_t)(strip[0] < 0 ? 0 : strip[0]), 10);
    text("-");
    number((uint64_t)(strip[2] < 0 ? 0 : strip[2]), 10);
    text(", focus tile ");
    number((uint64_t)(focus_left < 0 ? 0 : focus_left), 10);
    text("-");
    number((uint64_t)(focus_left + CAROUSEL_SLOT), 10);
    text(", tiles from it ");
    signed_number(delta);
    text(", SelectedIndex ");
    signed_number(current);
    flush_line();
    float dx = x - carousel_last_x, dy = y - carousel_last_y;
    int repeat = carousel_last_click && now - carousel_last_click < 500u && dx * dx + dy * dy < 400.0f;
    carousel_last_click = repeat ? 0 : now;
    carousel_last_x = x;
    carousel_last_y = y;
    if (delta == 0 || repeat) {
        log_append(delta == 0 ? "KBM TRACE: car strip click on the selected car: A (open).\r\n"
                              : "KBM TRACE: car strip double click: A (open).\r\n");
        InterlockedExchange(&pointer_confirm, 1);
        return 1;
    }
    if (current + delta < 0) {
        log_append("KBM TRACE: car strip click left of the first car; nothing.\r\n");
        return 1;
    }
    carousel_step(selector, delta, "click");
    return 1;
}

#define CAR_VIEW_TYPE 0x32fb0b8u /* .?AVCarViewBase@@ */
/* Pages with FH6-style keys (RTTI type descriptors). */
#define PHOTO_MODE_TYPE 0x33669e0u   /* .?AVPhotoModeScene@@ */
#define PAUSE_MENU_TYPE 0x33669b8u   /* .?AVPauseMenuTiled@@ */
#define CAR_SELECT_TYPE 0x32fb108u   /* .?AVCarSelect@@ (My Cars, buy car) */
#define MARKET_SELECT_TYPE 0x335a348u /* .?AVMarketplaceCarSelect@@ */
enum { PAGE_OTHER, PAGE_PHOTO, PAGE_PAUSE, PAGE_CARS };

static unsigned page_kind(unsigned char *page) {
    void **vtable = NULL;
    if (!page || !memory_read(page, &vtable, sizeof(vtable)) || !in_game_image(vtable, 8)) {
        return PAGE_OTHER;
    }
    return class_derives(vtable, PHOTO_MODE_TYPE)   ? PAGE_PHOTO
           : class_derives(vtable, PAUSE_MENU_TYPE) ? PAGE_PAUSE
           : class_derives(vtable, CAR_SELECT_TYPE) || class_derives(vtable, MARKET_SELECT_TYPE)
               ? PAGE_CARS
               : PAGE_OTHER;
}

/* FH6 page keys: P, R, L, Backspace (index order). */
static int page_key_target(unsigned page, unsigned key) {
    switch (page) {
        case PAGE_PHOTO:
            /* P Quickshot = RB (Skanuj auta), R = LB, Backspace Hide UI = View. */
            return key == 0 ? 0x14 : key == 1 ? 0x13 : key == 3 ? 0x0d : -1;
        case PAGE_CARS:
            /* P Toggle Detail = the D-pad toggle FH2 offers (D-pad up). */
            return key == 0 ? 0x52 : -1;
    }
    return -1;
}
static unsigned char *viewer_toggled_page, *viewer_pending_page;
static uint64_t viewer_pending_at;

/* A shown help-bar prompt for a stick click (Button value 10 or more, past
   A..RT) under `node`: the viewer offers "Zaawansowana kamera" only in some
   camera presets. Collapsed prompts have no size and do not count. */
static int has_stick_prompt(unsigned char *node, unsigned depth, unsigned *visited) {
    unsigned char *children[64];
    if (!node || depth > 40 || ++*visited > 3000) {
        return 0;
    }
    if (checked(node, PROMPT_VTABLE)) {
        void *dp = NULL, *value = NULL;
        int button = -1;
        float rect[4], root[2];
        if (memory_read(image + PROMPT_BUTTON_DP, &dp, sizeof(dp)) && dp) {
            ((GetValueFn)(void *)(image + GET_VALUE))(node, &value, dp);
            if (value) {
                memory_read((unsigned char *)value + 0x28, &button, sizeof(button));
            }
            release_reference(value);
        }
        return button >= 10 && element_rect(node, rect, root);
    }
    unsigned count = visual_children(node, children, ARRAY_COUNT(children));
    for (unsigned i = 0; i < count; ++i) {
        if (has_stick_prompt(children[i], depth + 1, visited)) {
            return 1;
        }
    }
    return 0;
}

/* Popups (defined with the message box code below). */
static unsigned char *popup_box(void);
static void popup_hover(unsigned char *box, float fx, float fy);
static int popup_click(unsigned char *box, float fx, float fy);
static int prompt_under_pointer(float fx, float fy);
static int popup_horizontal;
static unsigned char *settings_list_on(unsigned char *page);
static void settings_hover(unsigned char *list, float fx, float fy);
static int settings_click(unsigned char *list, float fx, float fy);
static int popup_press_taken; /* the release of a press a popup took */
static unsigned char *color_list_on(unsigned char *page);
static void color_hover(unsigned char *list, float fx, float fy);
static int color_click(unsigned char *list, float fx, float fy);
static void color_wheel(unsigned char *list, long wheel);
static volatile LONG color_on_top;

static int carousel_wheel(unsigned char *page, long wheel) {
    unsigned char *host = NULL, *selector = carousel_on(page, &host);
    if (!selector) {
        return 0;
    }
    int notches = (int)((wheel < 0 ? -wheel : wheel) + 60) / 120;
    notches = notches < 1 ? 1 : notches > 4 ? 4 : notches;
    carousel_step(selector, wheel > 0 ? -notches : notches, wheel > 0 ? "wheel up" : "wheel down");
    return 1;
}

/* UI thread, once per frame: windows whose only reference is ours are
   released and forgotten, and the cached UI root is kept only while it is
   one of the recorded live windows. */
static void prune_ui_windows(void) {
    AcquireSRWLockExclusive(&ui_windows_lock);
    LONG count = ui_window_count, kept = 0;
    int root_live = 0;
    for (LONG i = 0; i < count; ++i) {
        unsigned char *window = ui_windows[i];
        LONG references = 0;
        if (!window || !memory_read(window + 0x10, &references, sizeof(references)) || references <= 1 ||
            references > 0x100000) {
            if (window) {
                if (last_ui_root == window) {
                    last_ui_root = NULL;
                }
                begin_line();
                text("KBM: AVUI window released by the game; dropped from the pointer's list.");
                flush_line();
                if (references == 1) {
                    release_reference(window); /* ours was the last reference */
                }
            }
            continue;
        }
        root_live |= last_ui_root == window;
        ui_windows[kept++] = window;
    }
    for (LONG i = kept; i < count; ++i) {
        ui_windows[i] = NULL;
    }
    InterlockedExchange(&ui_window_count, kept);
    ReleaseSRWLockExclusive(&ui_windows_lock);
    if (last_ui_root && !root_live) {
        last_ui_root = NULL;
    }
}

static void pointer_pump(void *ui) {
    unsigned char *window;
    prune_ui_windows();
    void *provider = mouse_provider(ui, &window);
    static int provider_logged;
    static void *logged_provider;
    if (pointer_mode && !provider && !provider_logged) {
        provider_logged = 1;
        log_append("KBM: menu pointer: no AVUI window with a mouse provider in UserInterface.\r\n");
    }
    if (!provider || !pointer_mode) {
        if (pump_active) {
            if (provider) {
                report_mouse(provider, RAW_DEACTIVATE, 0);
            }
            kbm_win32_show_cursor(0);
            pump_active = 0;
            hub_last_hover = NULL;
            click_pending = 0;
            hub_rects_logged = 0;
        }
        InterlockedExchange(&pointer_tail, pointer_head);
        InterlockedExchange(&stats_on_top, 0);
        InterlockedExchange(&popup_on_top, 0);
        return;
    }
    float x, y, width, height, ui_size[2] = {0.0f, 0.0f};
    int inside = kbm_win32_cursor(&x, &y, &width, &height);
    memory_read(window + WINDOW_RENDER_SIZE, ui_size, sizeof(ui_size));
    if (!float_is_finite(ui_size[0]) || !float_is_finite(ui_size[1]) || ui_size[0] < 160.0f ||
        ui_size[1] < 90.0f || ui_size[0] > 16384.0f || ui_size[1] > 16384.0f) {
        ui_size[0] = 1920.0f; /* the FH2 UI layout size, used only if unreadable */
        ui_size[1] = 1080.0f;
    }
    /* Only the map page itself hides the cursor and clicks at the map
       cursor: a popup or page over it (FILTR MAPY, event cards) takes the
       pointer like any menu. */
    unsigned char *popup = popup_box(), *top = top_page();
    int on_map = map_shown() && !popup && (!top || checked(top, MAP_PAGE_VTABLE));
    {
        static unsigned char *map_cover_logged;
        unsigned char *cover = map_shown() && !on_map ? (popup ? popup : top) : NULL;
        if (cover && cover != map_cover_logged) {
            void *cover_vtable = NULL;
            memory_read_pointer(cover, &cover_vtable);
            begin_line();
            text("KBM TRACE: map covered by vtable ");
            number((uintptr_t)cover_vtable - (uintptr_t)image, 16);
            text("; the pointer drives it like a menu.");
            flush_line();
        }
        map_cover_logged = cover;
    }
    kbm_win32_show_cursor(!on_map && (!photo_active() || !mouse_right_held || popup));
    if (provider != logged_provider) {
        logged_provider = provider;
        void *window_vtable = NULL, *provider_vtable = NULL;
        memory_read_pointer(window, &window_vtable);
        memory_read_pointer(provider, &provider_vtable);
        begin_line();
        text("KBM: menu pointer active; AVUI window ");
        number((uintptr_t)window, 16);
        text(" (vtable ");
        number((uintptr_t)window_vtable - (uintptr_t)image, 16);
        text(") layout ");
        number((uint64_t)ui_size[0], 10);
        text("x");
        number((uint64_t)ui_size[1], 10);
        text(", mouse provider ");
        number((uintptr_t)provider, 16);
        text(" (vtable ");
        number((uintptr_t)provider_vtable - (uintptr_t)image, 16);
        text("), game client ");
        number((uint64_t)(inside ? width : 0.0f), 10);
        text("x");
        number((uint64_t)(inside ? height : 0.0f), 10);
        text(".");
        flush_line();
    }
    HubHit hit;
    int on_tile = 0;
    unsigned char *hub = current_hub();
    unsigned char *stats = stats_ok && checked(top, MYSTATS_VTABLE) ? top : NULL;
    if (hub && (!hub_on_top(hub, top) || stats)) {
        hub = NULL;
    }
    InterlockedExchange(&stats_on_top, stats != NULL);
    InterlockedExchange(&popup_on_top, popup != NULL);
    void **top_vtable = NULL;
    unsigned char *viewer_page = top && memory_read(top, &top_vtable, sizeof(top_vtable)) &&
                                         in_game_image(top_vtable, 8) &&
                                         class_derives(top_vtable, CAR_VIEW_TYPE)
                                     ? top
                                     : NULL;
    unsigned char *color = !popup && !stats ? color_list_on(top) : NULL;
    InterlockedExchange(&color_on_top, color != NULL);
    unsigned char *settings_list = !popup && !stats && !viewer_page && !color ? settings_list_on(top) : NULL;
    LONG page_keys = InterlockedExchange(&page_key_requests, 0);
    unsigned page = page_kind(top);
    if (InterlockedExchange(&right_stick_click_request, 0)) {
        if (page == PAGE_PAUSE) {
            /* FH6: Tab = Drive. The pause hub closes with Menu, as on a pad. */
            log_append("KBM TRACE: Tab on the pause hub: Menu (drive).\r\n");
            ui_key(0x07);
        } else {
            log_append("KBM TRACE: Camera key in a menu: right stick click (UI key 0x77).\r\n");
            ui_key(0x77);
        }
    }
    for (unsigned i = 0; page_keys && i < PAGE_KEY_COUNT; ++i) {
        int key = (page_keys >> i) & 1 ? page_key_target(page, i) : -1;
        if (key >= 0) {
            begin_line();
            text("KBM TRACE: page key ");
            text(page_key_names[i]);
            text(" -> UI key ");
            number((unsigned)key, 16);
            flush_line();
            ui_key(key);
        }
    }
    static unsigned char *popup_logged;
    if (popup != popup_logged) {
        popup_logged = popup;
        if (popup) {
            log_append("KBM TRACE: pointer drives the open popup (hover, click, wheel); the page "
                       "behind gets no clicks.\r\n");
        }
    }
    static unsigned char *stats_logged;
    if (stats != stats_logged) {
        stats_logged = stats;
        if (stats) {
            unsigned kind = 0;
            int state = -1;
            memory_read(stats + PAGE_STATE, &state, sizeof(state));
            unsigned char *info = stats_scroll_info(stats, &kind);
            begin_line();
            text("KBM TRACE: STATYSTYKI page ");
            number((uintptr_t)stats, 16);
            text(" on top (state ");
            signed_number(state);
            text("), category selector ");
            number((uintptr_t)stats_selector(stats), 16);
            text(" SelectedIndex ");
            signed_number(stats_selector(stats) ? selected_index(stats_selector(stats)) : -1);
            text(", list scroll info ");
            number((uintptr_t)info, 16);
            text(info ? " (" : "");
            text(info ? scroll_infos[kind].name : "");
            text(info ? ")" : "");
            text("; hub pointer off.");
            flush_line();
        } else {
            log_append("KBM TRACE: STATYSTYKI page no longer on top.\r\n");
        }
    }
    if (!pump_active && hub) {
        hub_log();
    }
    if (inside) {
        float ui_x = x * ui_size[0] / width, ui_y = y * ui_size[1] / height;
        /* Car viewer (CarViewBase pages, RTTI 0x32fb0b8): the first drag
           with a button held switches to "Zaawansowana kamera" (the right
           stick click, UI key 0x77), whose camera the drag then orbits. */
        if (viewer_page && pump_active && (ui_x != pointer_x || ui_y != pointer_y) &&
            (mouse_left_held || mouse_right_held) && viewer_toggled_page != viewer_page &&
            !viewer_active()) {
            viewer_toggled_page = viewer_page;
            unsigned visited = 0;
            if (has_stick_prompt(viewer_page, 0, &visited)) {
                log_append("KBM TRACE: car viewer: first drag switches to the advanced camera (right "
                           "stick click).\r\n");
                ui_key(0x77);
            } else {
                /* A cinematic preset: the advanced camera is offered after the
                   page's own X (Zmien kamere); X first, then the click. */
                log_append("KBM TRACE: car viewer: advanced camera not offered in this preset; X "
                           "(change camera) first.\r\n");
                ui_key(0x43);
                viewer_pending_page = viewer_page;
                viewer_pending_at = GetTickCount64();
            }
        }
        if (viewer_pending_page && (viewer_pending_page != viewer_page ||
                                    GetTickCount64() - viewer_pending_at > 450u)) {
            unsigned visited = 0;
            if (viewer_pending_page == viewer_page && has_stick_prompt(viewer_page, 0, &visited)) {
                log_append("KBM TRACE: car viewer: advanced camera offered now; right stick click.\r\n");
                ui_key(0x77);
            } else if (viewer_pending_page == viewer_page) {
                log_append("KBM TRACE: car viewer: advanced camera still not offered after X; left "
                           "as it is (Tab switches when it is).\r\n");
            }
            viewer_pending_page = NULL;
        }
        if (!pump_active || ui_x != pointer_x || ui_y != pointer_y) {
            /* AVUI's provider adds Activate to the first report after a Deactivate. */
            pointer_x = ui_x;
            pointer_y = ui_y;
            report_mouse(provider, RAW_ABSOLUTE_MOVE, 0);
        }
        on_tile = !popup && hub && hub_hit(hub, window, x / width, y / height, &hit);
        if (popup) {
            popup_hover(popup, x / width, y / height);
        } else if (color && !mouse_left_held && !mouse_right_held) {
            color_hover(color, x / width, y / height);
        } else if (settings_list) {
            settings_hover(settings_list, x / width, y / height);
        }
        /* Hover: focus follows the pointer from tile to tile. */
        if (on_tile && hit.element != hub_last_hover) {
            hub_focus(hub, &hit);
            if (config.trace) {
                begin_line();
                text("KBM TRACE: pointer over tile ");
                number((uint64_t)hit.column, 10);
                text(",");
                number((uint64_t)hit.row, 10);
                flush_line();
            }
        }
        hub_last_hover = on_tile ? hit.element : NULL;
    }
    pump_active = 1;
    LONG wheel = InterlockedExchange(&avui_wheel, 0);
    if (wheel && inside) {
        report_mouse(provider, RAW_WHEEL, (int)wheel);
        if (stats && !popup) {
            stats_wheel(stats, wheel);
        } else if (color && !popup) {
            color_wheel(color, wheel);
        } else if (!popup && !on_tile && carousel_wheel(top, wheel)) {
            /* a car strip: the selection moved left / right */
        } else if (!hub || popup) {
            /* (a popup's list moves its focus the same way) */
            /* Lists and scrolling pages: one Up/Down UI key per notch, the
               same key the arrows raise. */
            int notches = (int)((wheel < 0 ? -wheel : wheel) + 60) / 120;
            notches = notches < 1 ? 1 : notches > 4 ? 4 : notches;
            int sideways = popup && popup_horizontal;
            for (int i = 0; i < notches; ++i) {
                ui_key(sideways ? (wheel > 0 ? 0x17 : 0x19) : wheel > 0 ? 0x18 : 0x1a);
            }
        }
    }
    while (pointer_tail != pointer_head) {
        LONG tail = pointer_tail;
        unsigned code = pointer_ring[(ULONG)tail % ARRAY_COUNT(pointer_ring)].code;
        unsigned down = pointer_ring[(ULONG)tail % ARRAY_COUNT(pointer_ring)].down;
        InterlockedExchange(&pointer_tail, tail + 1);
        if (code != KBM_MOUSE_LEFT) {
            continue;
        }
        if (down && !popup && settings_list && inside && settings_click(settings_list, x / width, y / height)) {
            continue;
        }
        if (down && !popup && color && inside && color_click(color, x / width, y / height)) {
            click_pending = 0;
            click_tab = -1;
            stats_click_page = NULL;
            popup_press_taken = 1; /* its release is the colour page's too */
            continue;
        }
        if (down && !popup && viewer_page && !prompt_under_pointer(x / width, y / height)) {
            continue; /* the car viewer: a press only starts a drag (help-bar prompts still work) */
        }
        if (down && popup && inside && popup_click(popup, x / width, y / height)) {
            /* A list popup takes the press; nothing reaches the page behind. */
            click_pending = 0;
            click_tab = -1;
            stats_click_page = NULL;
            popup_press_taken = 1;
            continue;
        }
        if (!down && popup_press_taken) {
            popup_press_taken = 0;
            continue;
        }
        if (down) {
            /* Press: the tile under the pointer takes focus; nothing else. */
            click_pending = inside && (on_tile || on_map);
            click_tab = -1;
            stats_click_page = NULL;
            if (inside && stats && !click_pending) {
                int delta = 0;
                const char *why = NULL;
                if (stats_hit(stats, x / width, y / height, &delta, &why)) {
                    stats_click_page = stats;
                    stats_click_delta = delta;
                    stats_click_why = why;
                    continue;
                }
            }
            if (inside && !click_pending && !stats &&
                carousel_click(top, x / width, y / height, GetTickCount64())) {
                continue;
            }
            if (inside && !on_tile && !on_map && hub) {
                int current = 0, groups = 0;
                click_tab = tab_hit(hub, x / width, y / height, &current, &groups);
                click_tab_current = current;
                if (click_tab != -1) {
                    begin_line();
                    text("KBM TRACE: click hit tab ");
                    if (click_tab == -2) {
                        text("LB");
                    } else if (click_tab == -3) {
                        text("RB");
                    } else {
                        number((uint64_t)click_tab, 10);
                    }
                    text(" (current ");
                    number((uint64_t)(current < 0 ? 0 : current), 10);
                    text(")");
                    flush_line();
                    continue;
                }
            }
            if (click_pending && !on_map) {
                hub_focus(hub, &hit);
            }
            if (!click_pending && inside && window) {
                PromptSearch search = {x / width, y / height, 0, NULL, PROMPT_VTABLE};
                prompt_walk(last_ui_root ? last_ui_root : window, &search, 0);
                if (!search.found && last_ui_root && last_ui_root != window) {
                    search.visited = 0;
                    prompt_walk(window, &search, 0);
                }
                int button = search.found ? prompt_button(search.found) : -1;
                if (button < 0) {
                    PromptSearch tabs = {x / width, y / height, 0, NULL, ITEMS_SELECTOR_VTABLE};
                    prompt_walk(last_ui_root ? last_ui_root : window, &tabs, 0);
                    unsigned strip_visits = 0;
                    if (tabs.found && find_descendant(tabs.found, SCROLL_PANEL_VTABLE, 0, &strip_visits)) {
                        /* a car strip is not a tab strip (garage1 handles it) */
                        log_append("KBM TRACE: click on a car strip outside its band; ignored.\r\n");
                        continue;
                    }
                    if (tabs.found) {
                        PanelSearch item = {x / width, y / height, 0, -1, 0};
                        panel_walk(tabs.found, &item, 0);
                        int current = selected_index(tabs.found);
                        begin_line();
                        text("KBM TRACE: click hit tab ");
                        number((uint64_t)(item.index < 0 ? 0 : item.index), 10);
                        text(item.index < 0 ? " (no item)" : "");
                        text(" (current ");
                        number((uint64_t)(current < 0 ? 0 : current), 10);
                        text(")");
                        flush_line();
                        if (item.index >= 0 && item.index != current) {
                            selector_pending = tabs.found;
                            selector_index = item.index;
                        }
                        continue;
                    }
                }
                if (button >= 0) {
                    click_prompt = button;
                    begin_line();
                    text("KBM TRACE: click hit prompt ");
                    text(prompt_names[button]);
                    flush_line();
                    continue;
                }
            }
            begin_line();
            if (click_pending && on_map) {
                text("KBM TRACE: click on the map (A at the map cursor) at ");
            } else if (click_pending) {
                text("KBM TRACE: click hit cell ");
                number((uint64_t)hit.column, 10);
                text(",");
                number((uint64_t)hit.row, 10);
                text(" at ");
            } else {
                text("KBM TRACE: click hit nothing at ");
            }
            number((uint64_t)(pointer_x < 0.0f ? 0.0f : pointer_x), 10);
            text(",");
            number((uint64_t)(pointer_y < 0.0f ? 0.0f : pointer_y), 10);
            flush_line();
        } else if (stats_click_page) {
            /* Release: the step, if the same STATYSTYKI page is still on top. */
            unsigned char *page = stats_click_page;
            stats_click_page = NULL;
            if (page != stats) {
                log_append("KBM TRACE: stats click released after the page changed; ignored.\r\n");
            } else if (stats_click_delta) {
                stats_step(page, stats_click_delta, stats_click_why);
            } else {
                log_append("KBM TRACE: stats click on the selected tab; nothing to do.\r\n");
            }
        } else if (selector_pending) {
            ((ListSelectFn)(void *)(image + LIST_SELECT))(selector_pending, selector_index);
            selector_pending = NULL;
        } else if (click_prompt >= 0) {
            int button = click_prompt;
            click_prompt = -1;
            if (!ui_key(prompt_keys[button])) {
                log_append("KBM: prompt click: the UI key entry is not verified; ignored.\r\n");
            }
        } else if (click_tab != -1) {
            /* Release on a tab: the group is changed the way the nav manager
               changes it for PageUp/PageDown. */
            int tab = click_tab;
            click_tab = -1;
            if (!hub || !nav_groups_ok) {
                log_append("KBM: tab click: group functions not verified; ignored.\r\n");
            } else if (tab == -2) {
                ((NavGroupStepFn)(void *)(image + NAV_PREVIOUS_GROUP))(hub, 0, 0);
            } else if (tab == -3) {
                ((NavGroupStepFn)(void *)(image + NAV_NEXT_GROUP))(hub, 0, 0);
            } else if (tab != click_tab_current) {
                go_to_group(hub, tab);
            }
        } else if (click_pending) {
            /* Release: A on the tile the press focused, if still under the pointer. */
            click_pending = 0;
            if (inside && on_map) {
                if (!ui_key(0x12)) { /* the map's own select, as A reaches it */
                    InterlockedExchange(&pointer_confirm, 1);
                }
            } else if (inside && on_tile && hit.element == hub_last_hover) {
                click_element(hit.element);
            }
        }
    }
}

static void native_box_tick(void);
static void *volatile native_box;

static void world_prompts_tick(void);
static void ui_update_hook(void *ui, float dt) {
    kbm_menu_ui_tick(); /* in-game key rebinding (kbm_rebind.c) */
    native_box_tick();
    prompts_tick();
    world_prompts_tick();
    pointer_pump(ui);
    original_ui_update(ui, dt);
}

static void install_pointer(void) {
    void **slot = (void **)(image + UI_UPDATE_SLOT);
    DWORD protection, ignored;
    if (*slot != image + UI_UPDATE ||
        !memory_equal(image + MOUSE_REPORT, mouse_report_bytes, sizeof(mouse_report_bytes))) {
        log_append("KBM: UserInterface::Update slot or AVUI mouse report changed; menu pointer "
                   "off.\r\n");
        return;
    }
    hub_select_ok = memory_equal(image + SELECT_CELL, select_cell_bytes, sizeof(select_cell_bytes));
    ui_keys_ok = memory_equal(image + UI_KEY_TARGET, ui_key_target_bytes, sizeof(ui_key_target_bytes));
    frame_page_ok = memory_equal(image + FRAME_PAGE, frame_page_bytes, sizeof(frame_page_bytes));
    verify_stats();

    nav_groups_ok = memory_equal(image + NAV_PREVIOUS_GROUP, nav_step_bytes, sizeof(nav_step_bytes)) &&
                    memory_equal(image + NAV_NEXT_GROUP, nav_step_bytes, sizeof(nav_step_bytes)) &&
                    memory_equal(image + NAV_FOCUS_GROUP, nav_focus_group_bytes, sizeof(nav_focus_group_bytes));
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        log_append("KBM: UserInterface vtable not writable; menu pointer off.\r\n");
        return;
    }
    original_ui_update = (UpdateFn)(void *)(image + UI_UPDATE);
    InterlockedExchangePointer((void *volatile *)slot, (void *)ui_update_hook);
    VirtualProtect(slot, sizeof(void *), protection, &ignored);
    pointer_installed = config.menu_cursor;
    prompts_installed = 1;
    log_append("KBM: hook UserInterface::Update vtable slot 0x2ecfbf8 (0x142c5e0) installed: menu "
               "pointer via AVUI MouseInputProvider::ReportInput 0x149a2f0, button prompts.\r\n");
    log_append(hub_select_ok ? "KBM: menu pointer hovers tiles via the hub grid (focus 0x140cc50).\r\n"
                             : "KBM: hub focus function 0x140cc50 changed; pointer hover off.\r\n");
}


/* ---------- AVUI windows and message boxes ---------- */

/* Every AVUI::Window builds its PresentationSource (with the mouse provider)
   in 0x154b9e0(window); windows are recorded there for the menu pointer. */
static unsigned char *ui_windows[16];
static volatile LONG ui_window_count;

static void window_source_hook(void *window) {
    ((ObjectFn1)hooks[HOOK_WINDOW].original)(window);
    AcquireSRWLockExclusive(&ui_windows_lock);
    LONG count = ui_window_count;
    int added = window && count < (LONG)ARRAY_COUNT(ui_windows);
    if (added) {
        /* Held until the game lets the window go (prune_ui_windows), so a
           recorded pointer never outlives its object. */
        InterlockedIncrement((volatile LONG *)((unsigned char *)window + 0x10));
        ui_windows[count] = window;
        InterlockedExchange(&ui_window_count, count + 1);
    }
    ReleaseSRWLockExclusive(&ui_windows_lock);
    if (added) {
        void *vtable = NULL;
        memory_read_pointer(window, &vtable);
        begin_line();
        text("KBM: AVUI window created, vtable ");
        number((uintptr_t)vtable - (uintptr_t)image, 16);
        text(".");
        flush_line();
    }
}

typedef void (*ObjectFn2)(void *, void *);

/* Yes/no dialogs are PopupMessageBox pages (A_MessageBox.xaml: a vertical
   CommonListBox "Menu"; vtable 0x2e24148). Their B is not always bound
   (photo mode's "exit?" has only A). UserInterface hands each key to the
   page on top through the page's OnKey (vtable slot 0x578, called at
   0x1422e93; PopupMessageBox keeps the base 0xcec3d0(page, WPF key, down,
   extra), which returns whether it used the key; B is WPF key 0x2d). That
   slot is wrapped (a vtable entry, no code patch): a box that receives the B
   of a fresh Esc press and does not use it is open and on top, so its last
   item -- the "No"/cancel choice -- is selected with Down presses and
   confirmed. Boxes built ahead of time (photo mode keeps its exit box from
   the start) see no keys until shown, and the Esc that opens a box goes to
   the page below, so it stays a plain B. A highlighted "Yes" is never
   confirmed: the Downs come first and a list does not wrap. */
#define DIALOG_DOWN 0xf3u
#define DIALOG_CONFIRM 0xf4u
#define WHEEL_UP 0xf5u
#define TAB_PREVIOUS 0xf6u
#define TAB_NEXT 0xf7u
#define DIALOG_WAIT_MS 150u
#define ESCAPE_FRESH_MS 300u
#define MESSAGE_BOX_VTABLE 0x2e24148u
/* The PopupMessageBox family (PopupMessageBox 0x2e24148, PopupMenu 0x2e249c8,
   ...) shares the box A handler 0xdcc070 in vtable slot 0x590. */
static int is_message_box(void *box) {
    void **vtable = NULL;
    void *choose = NULL;
    return box && memory_read(box, &vtable, sizeof(vtable)) && (unsigned char *)vtable > image &&
           (unsigned char *)vtable < image + GAME_IMAGE_SIZE &&
           memory_read((unsigned char *)vtable + 0x590, &choose, sizeof(choose)) &&
           choose == image + 0xdcc070u;
}
#define MESSAGE_BOX_ON_KEY_SLOT 0x2e246c0u /* MESSAGE_BOX_VTABLE + 0x578 */
#define PAGE_ON_KEY 0xcec3d0u
#define WPF_KEY_B 0x2du

typedef unsigned char (*OnKeyFn)(void *, unsigned, unsigned char, unsigned char);
static OnKeyFn original_box_on_key;
static void *volatile box_request; /* UI thread -> input tick */
static volatile LONG64 escape_back_at, box_key_at;
static volatile LONG escape_back_used;

static struct {
    void *box;
    uint64_t at;
    unsigned step;
} dialog_cancel;

/* Message boxes shown over a page (photo mode's "exit?" and most yes/no
   questions) do not take keys through the page OnKey: UserInterface shows
   them with vtable slot 0x1d8 (0x1425060(ui, owner, box)), which links
   box+0x298 = owner and owner+0x2a0 = box; the owner's +0x2a0 is cleared
   when the box closes (0xd08883 tests it). That slot is wrapped to remember
   the last box; it counts as open while the link holds and the box has no
   box of its own on top. */
#define UI_SHOW_POPUP_SLOT 0x2ecfd58u /* 0x2ecfb80 + 0x1d8 */
#define UI_SHOW_POPUP 0x1425060u
#define POPUP_OWNER 0x298u
#define PAGE_POPUP 0x2a0u
typedef void (*ShowPopupFn)(void *, void *, void *);
static ShowPopupFn original_show_popup;
static void *volatile last_popup;
static volatile LONG64 last_popup_at;

static void show_popup_hook(void *ui, void *owner, void *box) {
    original_show_popup(ui, owner, box);
    cover_owner = owner;
    cover_box = box;
    void *vtable = NULL;
    memory_read_pointer(box, &vtable);
    if (is_message_box(box)) {
        last_popup = box;
        InterlockedExchange64(&last_popup_at, (LONG64)GetTickCount64());
    }
    static volatile LONG shown_logged;
    if (config.trace || InterlockedIncrement(&shown_logged) <= 40) {
        void *owner_vtable = NULL;
        memory_read_pointer(owner, &owner_vtable);
        begin_line();
        text("KBM: popup shown, vtable ");
        number((uintptr_t)vtable - (uintptr_t)image, 16);
        text(" over page vtable ");
        number((uintptr_t)owner_vtable - (uintptr_t)image, 16);
        flush_line();
    }
}

static int popup_open(void *box) {
    void *vtable = NULL, *owner = NULL, *link = NULL, *above = (void *)1;
    return box && is_message_box(box) && memory_read_pointer(box, &vtable) &&
           memory_read((unsigned char *)box + POPUP_OWNER, &owner, sizeof(owner)) && owner &&
           memory_read((unsigned char *)owner + PAGE_POPUP, &link, sizeof(link)) && link == box &&
           memory_read((unsigned char *)box + PAGE_POPUP, &above, sizeof(above)) && !above;
}

static void install_show_popup(void) {
    void **slot = (void **)(image + UI_SHOW_POPUP_SLOT);
    DWORD protection, ignored;
    if (*slot != image + UI_SHOW_POPUP ||
        !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        log_append("KBM: UserInterface show-popup slot changed; Esc stays B in popups.\r\n");
        return;
    }
    original_show_popup = (ShowPopupFn)(void *)(image + UI_SHOW_POPUP);
    InterlockedExchangePointer((void *volatile *)slot, (void *)show_popup_hook);
    VirtualProtect(slot, sizeof(void *), protection, &ignored);
    log_append("KBM: hook UserInterface show-popup vtable slot 0x2ecfd58 (0x1425060) installed "
               "(Esc = No in yes/no popups).\r\n");
}

/* Input thread: Esc pressed as B. A popup box open for a moment without a
   back target gets its last item chosen. */
static void dialog_escape_pressed(uint64_t now) {
    InterlockedExchange(&escape_back_used, 0);
    InterlockedExchange64(&escape_back_at, (LONG64)now);
    void *box = last_popup, *back = (void *)1;
    if (!dialog_cancel.box && popup_open(box) && now - (uint64_t)last_popup_at >= 250u &&
        memory_read((unsigned char *)box + 0x480, &back, sizeof(back)) && !back) {
        InterlockedExchange(&escape_back_used, 1); /* not again through OnKey */
        InterlockedExchangePointer(&native_box, box);
        log_append("KBM: Esc in a yes/no popup without a back target.\r\n");
    }
}

static unsigned char box_on_key_hook(void *box, unsigned key, unsigned char down,
                                     unsigned char extra) {
    unsigned char used = original_box_on_key(box, key, down, extra);
    uint64_t now = GetTickCount64();
    InterlockedExchange64(&box_key_at, (LONG64)now);
    if (config.trace) {
        begin_line();
        text("KBM TRACE: message box key ");
        number(key, 16);
        text(down ? " down" : " up");
        text(used ? ", used." : ", not used.");
        flush_line();
    }
    if (key != WPF_KEY_B || !down || used || !kbm_active ||
        now - (uint64_t)escape_back_at > ESCAPE_FRESH_MS ||
        InterlockedExchange(&escape_back_used, 1)) {
        return used;
    }
    void *back = NULL;
    memory_read((unsigned char *)box + 0x480, &back, sizeof(back));
    if (back) {
        log_append("KBM: Esc in a message box with its own back target: plain B.\r\n");
        return used;
    }
    InterlockedExchangePointer(&native_box, box);
    return used;
}

static void install_box_on_key(void) {
    void **slot = (void **)(image + MESSAGE_BOX_ON_KEY_SLOT);
    DWORD protection, ignored;
    if (*slot != image + PAGE_ON_KEY ||
        !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        log_append("KBM: PopupMessageBox OnKey slot changed; Esc stays B in dialogs.\r\n");
        return;
    }
    original_box_on_key = (OnKeyFn)(void *)(image + PAGE_ON_KEY);
    InterlockedExchangePointer((void *volatile *)slot, (void *)box_on_key_hook);
    VirtualProtect(slot, sizeof(void *), protection, &ignored);
    log_append("KBM: hook PopupMessageBox OnKey vtable slot 0x2e246c0 (0xcec3d0) installed (Esc "
               "= No in dialogs without B).\r\n");
}

/* Native "No": the box keeps its CommonListBox "Menu" at +0x430; the list's
   items at +0x1f8 report their count through vtable slot 0xb8; 0xbafe20(list,
   index) sets the list's SelectedIndex; the box's A handler (vtable slot
   0x590, 0xdcc070, reached from its key 0x12 handler 0xde7160) then runs the
   selected item's action. The last item is the No/cancel choice. Runs on the
   UI thread. */
#define BOX_LIST 0x430u
#define LIST_ITEMS 0x1f8u
#define LIST_SELECT 0xbafe20u
#define BOX_CHOOSE_SLOT 0x590u
#define BOX_CHOOSE 0xdcc070u
typedef void (*ListSelectFn)(void *, int);
typedef int (*CountFn)(void *);
static const unsigned char list_select_bytes[] = {0x89, 0x54, 0x24, 0x10, 0x53, 0x48, 0x83, 0xec, 0x30};

#define UI_BACK 0x1407bc0u
typedef void *(*UiBackFn)(void *, void *);
static const unsigned char ui_back_bytes[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x49, 0x68};
static int node_derives(unsigned char *node, uint32_t type);
static int native_no(void *box) {
    unsigned char *list = NULL, *items = NULL;
    int menu_list = 0;
    void **box_vtable = NULL, **items_vtable = NULL;
    void *choose = NULL;
    if (!memory_equal(image + LIST_SELECT, list_select_bytes, sizeof(list_select_bytes)) ||
        !memory_read(box, &box_vtable, sizeof(box_vtable)) ||
        !memory_read((unsigned char *)box_vtable + BOX_CHOOSE_SLOT, &choose, sizeof(choose)) ||
        choose != image + BOX_CHOOSE ||
        !memory_read((unsigned char *)box + BOX_LIST, &list, sizeof(list)) ||
        (!list && (!memory_read((unsigned char *)box + 0x4a0, &list, sizeof(list)) || !list ||
                   (menu_list = 1, 0))) ||
        !node_derives(list, 0x33d1c48u /* .?AVListBox@AVUI@@ */) ||
        !memory_read(list + LIST_ITEMS, &items, sizeof(items)) || !items ||
        !memory_read(items, &items_vtable, sizeof(items_vtable)) || !items_vtable) {
        return 0;
    }
    int count = ((CountFn)items_vtable[0xb8 / 8])(items);
    if (count < 1 || count > 16) {
        return 0;
    }
    if (menu_list) {
        /* A PopupMenu (its own list "Menu" at +0x4a0; the map's context
           menu) may hold actions such as fast travel or a route, so it is
           only closed: UserInterface's back for a popup (vtable slot 0x188,
           0x1407bc0(ui, page) -> 0x1418960(owner, popup)), which unlinks it
           without choosing an item. */
        void *ui = NULL;
        if (!memory_equal(image + UI_BACK, ui_back_bytes, sizeof(ui_back_bytes)) ||
            !memory_read(image + 0x3684b90u, &ui, sizeof(ui)) || !ui) {
            return 0;
        }
        ((UiBackFn)(void *)(image + UI_BACK))(ui, box);
        log_append("KBM: Esc closed the popup menu without choosing an item.\r\n");
        return 2;
    }
    ((ListSelectFn)(void *)(image + LIST_SELECT))(list, count - 1);
    ((ObjectFn1)choose)(box);
    return 1;
}

/* ---------- popups under the pointer (garage5) ----------

   While a PopupMessageBox-family popup (A handler 0xdcc070 in slot 0x590;
   PopupMessageBox, PopupMenu) is open over its page, the pointer drives that
   popup the way its keys do. Its list ("Menu" CommonListBox at +0x430, or
   +0x4a0 for a PopupMenu) is hit-tested from its item containers: every
   ListBoxItem (RTTI .?AVListBoxItem@AVUI@@ 0x33d8958) found by an
   RTTI-checked visual walk under the list, ordered top to bottom, so title
   and message rows are never counted. The focused item is the list's
   SelectedIndex (the item style selects the item that has keyboard focus);
   focus is moved with the UI's own Up / Down keys, and a click raises A
   (0x12) through the UI-key entry exactly as Enter does. Popups without
   such a list (the colour picker) are left to the page's normal pointer
   handling. Clicks outside the items of a list popup are swallowed so the
   page behind never gets them; a right click closes it as Esc does. */
#define LIST_BOX_ITEM_TYPE 0x33d8958u
#define UI_KEY_UP 0x18
#define UI_KEY_DOWN 0x1a
#define UI_KEY_A 0x12

static unsigned char *popup_box(void) {
    void *box = last_popup;
    return popup_open(box) ? (unsigned char *)box : NULL;
}

static unsigned char *find_derived(unsigned char *node, uint32_t type, unsigned depth, unsigned *visited);

static unsigned char *popup_list(unsigned char *box) {
    unsigned char *list = NULL;
    /* +0x430 / +0x4a0 are lists only in PopupMessageBox / PopupMenu; other
       popups of the family (PopupMapFilter) may hold anything there. */
    if (!memory_read(box + BOX_LIST, &list, sizeof(list)) ||
        (!list && (!memory_read(box + 0x4a0, &list, sizeof(list)) || !list)) ||
        !node_derives(list, 0x33d1c48u /* .?AVListBox@AVUI@@ */)) {
        /* Popups such as the colour picker keep their list elsewhere: the
           first CommonListBox under the popup (its swatches). */
        unsigned visited = 0;
        list = checked(box, MESSAGE_BOX_VTABLE) ? NULL : find_derived(box, 0x33d1cc0u, 0, &visited);
        if (!list) {
            return NULL;
        }
    }
    void *count_fn = NULL, *child_fn = NULL;
    return visual_node(list, NULL, &count_fn, &child_fn) ? list : NULL;
}

static int is_list_item(unsigned char *node) {
    void **vtable = NULL;
    return memory_read(node, &vtable, sizeof(vtable)) && in_game_image(vtable, 8) &&
           class_derives(vtable, LIST_BOX_ITEM_TYPE);
}

typedef struct {
    unsigned char *item;
    float rect[4];
} PopupRow;

static void collect_rows(unsigned char *node, PopupRow *rows, unsigned *count, unsigned max,
                         unsigned depth, unsigned *visited) {
    unsigned char *children[40];
    if (depth > 24 || ++*visited > 800 || *count >= max) {
        return;
    }
    unsigned found = visual_children(node, children, ARRAY_COUNT(children));
    for (unsigned i = 0; i < found && *count < max; ++i) {
        if (is_list_item(children[i])) {
            float root[2];
            if (element_rect(children[i], rows[*count].rect, root)) {
                rows[*count].item = children[i];
                ++*count;
            }
            continue;
        }
        collect_rows(children[i], rows, count, max, depth + 1, visited);
    }
}

/* The list's item rows in reading order (top to bottom, or left to right
   for a horizontal list, e.g. colour swatches); 0 if it has none. */
static unsigned popup_rows(unsigned char *list, PopupRow *rows, unsigned max) {
    unsigned count = 0, visited = 0;
    collect_rows(list, rows, &count, max, 0, &visited);
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (unsigned i = 0; i < count; ++i) {
        min_x = rows[i].rect[0] < min_x ? rows[i].rect[0] : min_x;
        max_x = rows[i].rect[0] > max_x ? rows[i].rect[0] : max_x;
        min_y = rows[i].rect[1] < min_y ? rows[i].rect[1] : min_y;
        max_y = rows[i].rect[1] > max_y ? rows[i].rect[1] : max_y;
    }
    int horizontal = count > 1 && max_x - min_x > max_y - min_y;
    popup_horizontal = horizontal;
    for (unsigned i = 1; i < count; ++i) {
        for (unsigned j = i; j > 0 && (horizontal ? rows[j].rect[0] < rows[j - 1].rect[0]
                                                  : rows[j].rect[1] < rows[j - 1].rect[1]); --j) {
            PopupRow swap = rows[j];
            rows[j] = rows[j - 1];
            rows[j - 1] = swap;
        }
    }
    return count;
}

/* 1 if the popup has a pointer-driven list; *index the item under the
   pointer or -1, *count the rows, *focused the focused row. */
static int popup_target(unsigned char *box, float fx, float fy, unsigned char **list_out, int *index,
                        int *count, int *focused) {
    PopupRow rows[32];
    unsigned char *list = popup_list(box);
    *index = -1;
    *count = 0;
    *focused = -1;
    *list_out = list;
    if (!list) {
        return 0;
    }
    unsigned found = popup_rows(list, rows, ARRAY_COUNT(rows));
    if (!found) {
        return 0;
    }
    *count = (int)found;
    *focused = selected_index(list);
    float root[2], probe[4];
    if (!element_rect(rows[0].item, probe, root)) {
        return 1;
    }
    float x = fx * root[0], y = fy * root[1];
    for (unsigned i = 0; i < found; ++i) {
        if (x >= rows[i].rect[0] && x < rows[i].rect[2] && y >= rows[i].rect[1] && y < rows[i].rect[3]) {
            *index = (int)i;
            break;
        }
    }
    return 1;
}

/* Moves keyboard focus from `from` to `to` with the UI's Up / Down keys. */
static void popup_focus(int from, int to) {
    if (from < 0 || to < 0 || from == to) {
        return;
    }
    int steps = to > from ? to - from : from - to;
    for (int i = 0; i < steps && i < 32; ++i) {
        ui_key(popup_horizontal ? (to > from ? 0x19 : 0x17) : (to > from ? UI_KEY_DOWN : UI_KEY_UP));
    }
}

static unsigned char *popup_hover_box;
static int popup_hover_index = -1;

static void popup_hover(unsigned char *box, float fx, float fy) {
    unsigned char *list = NULL;
    int index, count, focused;
    if (box != popup_hover_box) {
        popup_hover_box = box;
        popup_hover_index = -1;
    }
    if (!popup_target(box, fx, fy, &list, &index, &count, &focused) || index < 0 ||
        index == popup_hover_index) {
        return;
    }
    popup_hover_index = index;
    popup_focus(focused, index);
    if (config.trace) {
        begin_line();
        text("KBM TRACE: popup hover: item ");
        number((uint64_t)index, 10);
        text(" of ");
        number((uint64_t)count, 10);
        text(", focus ");
        signed_number(focused);
        text(" -> ");
        signed_number(selected_index(list));
        text(" (Up/Down keys).");
        flush_line();
    }
}

/* 1 if the press was the popup's (handled or swallowed). */
/* A shown help-bar prompt under the pointer (garage18): those clicks go
   on to the prompt handling even while a popup is open. */
static int prompt_under_pointer(float fx, float fy) {
    unsigned char *root = last_ui_root;
    if (!root) {
        return 0;
    }
    PromptSearch search = {fx, fy, 0, NULL, PROMPT_VTABLE};
    prompt_walk(root, &search, 0);
    return search.found && prompt_button(search.found) >= 0;
}

/* The shown help-bar prompts under `node`, one bit per button (A 0 .. RT 9). */
static unsigned prompt_mask(unsigned char *node, unsigned depth, unsigned *visited) {
    unsigned char *children[64];
    if (!node || depth > 40 || ++*visited > 3000) {
        return 0;
    }
    if (checked(node, PROMPT_VTABLE)) {
        float rect[4], root[2];
        int button = prompt_button(node);
        return button >= 0 && element_rect(node, rect, root) ? 1u << button : 0u;
    }
    unsigned mask = 0, count = visual_children(node, children, ARRAY_COUNT(children));
    for (unsigned i = 0; i < count; ++i) {
        mask |= prompt_mask(children[i], depth + 1, visited);
    }
    return mask;
}

/* Trace: one line per visual under `node` (depth, vtable, RTTI class, rect). */
static void trace_tree(unsigned char *node, unsigned depth, unsigned *visited) {
    unsigned char *children[64];
    void **vtable = NULL;
    uintptr_t locator = 0;
    uint32_t col[6];
    char name[64] = "?";
    float rect[4] = {0}, root[2];
    if (!node || depth > 40 || ++*visited > 600 || !memory_read(node, &vtable, sizeof(vtable))) {
        return;
    }
    if (memory_read((unsigned char *)vtable - 8, &locator, sizeof(locator)) &&
        in_game_image((void *)locator, sizeof(col)) && memory_read((void *)locator, col, sizeof(col)) &&
        in_game_image(image + col[3] + 0x10, sizeof(name)) &&
        memory_read(image + col[3] + 0x10, name, sizeof(name) - 1)) {
        name[sizeof(name) - 1] = 0;
    }
    element_rect(node, rect, root);
    begin_line();
    text("KBM TRACE: tree ");
    number(depth, 10);
    text(" ");
    number((uintptr_t)node, 16);
    text(" vt ");
    number((uintptr_t)vtable - (uintptr_t)image, 16);
    text(" ");
    text(name);
    text(" ");
    signed_number((int)rect[0]);
    text(",");
    signed_number((int)rect[1]);
    text(",");
    signed_number((int)rect[2]);
    text(",");
    signed_number((int)rect[3]);
    flush_line();
    unsigned count = visual_children(node, children, ARRAY_COUNT(children));
    void *count_fn = NULL, *child_fn = NULL;
    if (!count && visual_node(node, NULL, &count_fn, &child_fn)) {
        int raw = ((ChildCountFn)count_fn)(node);
        for (int i = 0; i < raw && i < 8; ++i) {
            unsigned char *child = (unsigned char *)((ChildAtFn)child_fn)(node, i), *parent = NULL;
            void *child_vtable = NULL;
            if (child) {
                memory_read(child + VISUAL_PARENT, &parent, sizeof(parent));
                memory_read(child, &child_vtable, sizeof(child_vtable));
            }
            begin_line();
            text("KBM TRACE: tree raw child ");
            number((uint64_t)i, 10);
            text(" ");
            number((uintptr_t)child, 16);
            text(" vt ");
            number((uintptr_t)child_vtable - (uintptr_t)image, 16);
            text(" parent ");
            number((uintptr_t)parent, 16);
            flush_line();
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        trace_tree(children[i], depth + 1, visited);
    }
}

static int popup_click(unsigned char *box, float fx, float fy) {
    unsigned char *list = NULL;
    int index, count, focused;
    static unsigned char *traced_box;
    if (prompt_under_pointer(fx, fy)) {
        log_append("KBM TRACE: popup click on a help-bar prompt; the prompt takes it.\r\n");
        return 0;
    }
    if (!popup_target(box, fx, fy, &list, &index, &count, &focused)) {
        if (checked(box, MESSAGE_BOX_VTABLE)) {
            /* A message box without an item list. Only a pure info box (its
               help bar offers A alone) is confirmed by a click anywhere; a
               box that also offers B (cancel) - delete, sell, spend - acts
               only on its help-bar prompts, so a stray click never confirms. */
            unsigned visited = 0, mask = prompt_mask(box, 0, &visited);
            if (!mask && last_ui_root) {
                visited = 0;
                mask = prompt_mask(last_ui_root, 0, &visited);
            }
            if (mask == 1u) {
                log_append("KBM TRACE: popup click on an info box (Ok only): A.\r\n");
                ui_key(0x12);
            } else {
                log_append("KBM TRACE: popup click on a box with Ok and Cancel: only its prompts act; "
                           "swallowed.\r\n");
            }
            return 1;
        }
        log_append("KBM TRACE: popup click: no item list in this popup; the click goes on as usual.\r\n");
        if (config.trace && box != traced_box) {
            unsigned visited = 0;
            traced_box = box;
            trace_tree(box, 0, &visited);
        }
        return 0;
    }
    begin_line();
    text("KBM TRACE: popup click: ");
    if (index < 0) {
        if (!checked(box, MESSAGE_BOX_VTABLE) && popup_horizontal) {
            text("outside its swatches; the click goes on (tabs).");
            flush_line();
            return 0;
        }
        text("outside its items; swallowed.");
        flush_line();
        return 1;
    }
    text("item ");
    number((uint64_t)index, 10);
    text(" of ");
    number((uint64_t)count, 10);
    text(", focus ");
    signed_number(focused);
    if (!checked(box, MESSAGE_BOX_VTABLE) && popup_horizontal && index != focused) {
        /* Swatches: the first click selects (focuses), a click on the
           selected one is Ok. */
        text(" -> item (select; click it again for Ok).");
        flush_line();
        popup_focus(focused, index);
        popup_hover_index = index;
        return 1;
    }
    text(" -> item, then A through the UI-key entry.");
    flush_line();
    popup_focus(focused, index);
    popup_hover_index = index;
    ui_key(UI_KEY_A);
    return 1;
}

/* ---------- settings option lists (garage18) ----------

   Video, SWIAT, KLAWIATURA I MYSZ, audio, HUD... pages list their options as
   SpinnerControl / Slider rows in a CommonListBox. The list keeps focus on
   one row (its item style selects the row with keyboard focus and draws its
   "Background" 2 px taller, Margin 0,-2); that taller background is how the
   focused row is found among the realized rows, top to bottom. Hover moves
   the focus there with the UI's Up / Down keys; a click on a row's value
   half sends Left / Right (left or right of its middle) after focusing it,
   and a click on the label of the focused row cycles forward (Right). */
#define COMMON_LIST_TYPE 0x33d1cc0u /* .?AVCommonListBox@@ */
#define SPINNER_TYPE 0x33d2ab0u     /* .?AVSpinnerControl@@ */
#define SLIDER_TYPE 0x341e9e8u      /* .?AVSlider@AVUI@@ */
#define UI_KEY_LEFT 0x17
#define UI_KEY_RIGHT 0x19
static unsigned char *settings_page, *settings_list_cached, *settings_hover_list;
static int settings_hover_row = -1;

static int node_derives(unsigned char *node, uint32_t type) {
    void **vtable = NULL;
    return node && memory_read(node, &vtable, sizeof(vtable)) && in_game_image(vtable, 8) &&
           class_derives(vtable, type);
}

static unsigned char *find_derived(unsigned char *node, uint32_t type, unsigned depth, unsigned *visited) {
    unsigned char *children[64];
    if (!node || depth > 40 || ++*visited > 4000) {
        return NULL;
    }
    if (node_derives(node, type)) {
        return node;
    }
    unsigned count = visual_children(node, children, ARRAY_COUNT(children));
    for (unsigned i = 0; i < count; ++i) {
        unsigned char *found = find_derived(children[i], type, depth + 1, visited);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static unsigned char *settings_list_on(unsigned char *page) {
    if (page != settings_page) {
        settings_page = page;
        settings_list_cached = NULL;
        settings_hover_row = -1;
        unsigned visited = 0;
        unsigned char *list = page ? find_derived(page, COMMON_LIST_TYPE, 0, &visited) : NULL;
        PopupRow rows[4];
        if (list && popup_rows(list, rows, ARRAY_COUNT(rows))) {
            unsigned a = 0, b = 0;
            if (find_derived(rows[0].item, SPINNER_TYPE, 0, &a) || find_derived(rows[0].item, SLIDER_TYPE, 0, &b)) {
                settings_list_cached = list;
                log_append("KBM TRACE: settings option list on this page: hover focuses, clicks change "
                           "values.\r\n");
            }
        }
    }
    return settings_list_cached && node_derives(settings_list_cached, COMMON_LIST_TYPE) ? settings_list_cached
                                                                                         : NULL;
}

/* The realized rows top to bottom inside the list's box; *focused the row
   drawn focused, or -1; *hit the row under the pointer, or -1. */
static unsigned settings_rows(unsigned char *list, PopupRow *rows, unsigned max, float fx, float fy,
                              int *focused, int *hit, float *x_out) {
    float box[4], root[2];
    unsigned found = popup_rows(list, rows, max), kept = 0;
    *focused = -1;
    *hit = -1;
    if (!element_rect(list, box, root)) {
        return 0;
    }
    float x = fx * root[0], y = fy * root[1];
    *x_out = x;
    for (unsigned i = 0; i < found; ++i) {
        float middle = (rows[i].rect[1] + rows[i].rect[3]) * 0.5f;
        if (middle < box[1] || middle > box[3]) {
            continue; /* scrolled out of the list's box */
        }
        rows[kept] = rows[i];
        unsigned char *grid[4], *parts[8];
        float r[4], rr[2];
        if (visual_children(rows[i].item, grid, 4) && visual_children(grid[0], parts, 8) &&
            element_rect(parts[0], r, rr) && (r[3] - r[1]) > (rows[i].rect[3] - rows[i].rect[1]) + 2.0f) {
            *focused = (int)kept;
        }
        if (x >= rows[i].rect[0] && x < rows[i].rect[2] && y >= rows[i].rect[1] && y < rows[i].rect[3]) {
            *hit = (int)kept;
        }
        ++kept;
    }
    return kept;
}

static void settings_focus(int from, int to) {
    if (from < 0 || to < 0 || from == to) {
        return;
    }
    int steps = to > from ? to - from : from - to;
    for (int i = 0; i < steps && i < 32; ++i) {
        ui_key(to > from ? UI_KEY_DOWN : UI_KEY_UP);
    }
}

static void settings_hover(unsigned char *list, float fx, float fy) {
    PopupRow rows[48];
    int focused, hit;
    float x;
    if (list != settings_hover_list) {
        settings_hover_list = list;
        settings_hover_row = -1;
    }
    settings_rows(list, rows, ARRAY_COUNT(rows), fx, fy, &focused, &hit, &x);
    if (hit < 0 || hit == settings_hover_row) {
        return;
    }
    settings_hover_row = hit;
    if (focused >= 0) {
        settings_focus(focused, hit);
    }
}

static int settings_click(unsigned char *list, float fx, float fy) {
    PopupRow rows[48];
    int focused, hit;
    float x;
    settings_rows(list, rows, ARRAY_COUNT(rows), fx, fy, &focused, &hit, &x);
    if (hit < 0) {
        return 0;
    }
    int was_focused = hit == focused;
    settings_focus(focused, hit);
    settings_hover_row = hit;
    float left = rows[hit].rect[0], right = rows[hit].rect[2], middle = (left + right) * 0.5f;
    int key = -1;
    if (x >= middle) {
        key = x < (middle + right) * 0.5f ? UI_KEY_LEFT : UI_KEY_RIGHT; /* the value half: arrows */
    } else if (was_focused) {
        key = UI_KEY_RIGHT; /* the focused row's label: cycle forward */
    }
    begin_line();
    text("KBM TRACE: settings click on row ");
    number((uint64_t)hit, 10);
    text(was_focused ? " (focused)" : "");
    text(key == UI_KEY_LEFT ? ": Left" : key == UI_KEY_RIGHT ? ": Right" : ": focus");
    flush_line();
    if (key >= 0) {
        ui_key(key);
    }
    return 1;
}

/* ---------- paint colour page (garage19) ----------

   LAKIEROW. / WYBOR KOLORU (A_ColorSelect.xaml; CarColorSelect and
   LiveryColorSelect pages) lists its colours in the CommonListBox
   PART_CommonListBox_PaintMenu: ZWYKLY is a WrapPanel (Orientation
   Vertical) of small swatches, a grid filled column by column; the other
   tabs are one horizontal StackPanel of large swatches in a ScrollViewer.
   Each realized ListBoxItem is placed by its layout rectangle into a column
   and a row (distinct x / y positions), so hover and clicks find the cell
   under the pointer and focus moves there the way the pad moves it: Left /
   Right from column to column, Up / Down within a column. The focused cell
   is the list's SelectedIndex (the item style selects the item that has
   keyboard focus). A cell's item index comes from its place, column by
   column (the containers' visual order is not the item order: the selected
   one is drawn last). The large preview left of the swatches is another
   focus group and has focus when the page opens (SelectedIndex -1); Right
   enters the swatches from it. SelectedIndex is never set directly: that
   frames a swatch while the preview keeps focus and its own frame. A click
   on a cell selects it; a second click on the selected cell is Ok (A). The colour type tabs
   (ItemsSelector PART_ItemsSelector_ColorType, not focusable) and the Q / E
   icons beside them step with LB / RB, the page's own tab keys. */
#define CAR_COLOR_SELECT_TYPE 0x3337648u    /* .?AVCarColorSelect@@ */
#define LIVERY_COLOR_SELECT_TYPE 0x32fb4e0u /* .?AVLiveryColorSelect@@ */
#define ITEMS_SELECTOR_TYPE 0x33d2480u      /* .?AVItemsSelector@@ */
#define SCROLL_VIEWER_TYPE 0x32fc0b0u       /* .?AVScrollViewer@AVUI@@ */
#define COLOR_CELLS 256u
#define UI_KEY_LB 0x13
#define UI_KEY_RB 0x14

typedef struct {
    unsigned char *item[COLOR_CELLS];
    float rect[COLOR_CELLS][4];
    int column[COLOR_CELLS], row[COLOR_CELLS];
    int index[COLOR_CELLS]; /* item index: column by column */
    unsigned count;
    int columns, rows;
    float view[4]; /* the ScrollViewer's box (hits are clipped to it) */
    int has_view;
} ColorGrid;

static ColorGrid color_grid_data; /* UI thread only */
static unsigned char *color_page_cached, *color_list_cached, *color_selector_cached;
static unsigned char *color_hover_list;
static int color_hover_cell = -1, color_click_cell = -1, color_hover_tab = -2;

static int is_color_page(unsigned char *page) {
    return page && (node_derives(page, CAR_COLOR_SELECT_TYPE) || node_derives(page, LIVERY_COLOR_SELECT_TYPE));
}

/* The colour list of the colour page on top, or NULL. */
static unsigned char *color_list_on(unsigned char *page) {
    if (!is_color_page(page)) {
        color_page_cached = NULL;
        return NULL;
    }
    if (page != color_page_cached || !node_derives(color_list_cached, COMMON_LIST_TYPE)) {
        unsigned visited = 0;
        color_page_cached = page;
        color_list_cached = find_derived(page, COMMON_LIST_TYPE, 0, &visited);
        visited = 0;
        color_selector_cached = find_derived(page, ITEMS_SELECTOR_TYPE, 0, &visited);
        color_hover_cell = color_click_cell = -1;
        color_hover_tab = -2;
        begin_line();
        text("KBM TRACE: paint colour page on top: colour list ");
        number((uintptr_t)color_list_cached, 16);
        text(", colour type tabs ");
        number((uintptr_t)color_selector_cached, 16);
        text(" (SelectedIndex ");
        signed_number(color_selector_cached ? selected_index(color_selector_cached) : -1);
        text(").");
        flush_line();
    }
    return color_list_cached;
}

/* Ranks `value` among the distinct positions in `seen` (within `tolerance`). */
static int color_slot(float *seen, int *count, float value, float tolerance) {
    for (int i = 0; i < *count; ++i) {
        float d = seen[i] - value;
        if (d < tolerance && d > -tolerance) {
            return i;
        }
    }
    if (*count >= (int)COLOR_CELLS) {
        return -1;
    }
    seen[*count] = value;
    return (*count)++;
}

static int color_rank(const float *seen, int count, int slot) {
    int rank = 0;
    for (int i = 0; i < count; ++i) {
        rank += seen[i] < seen[slot] ? 1 : 0;
    }
    return rank;
}

/* Reads the realized swatches of the list into a grid; 0 if it has none. */
static int color_grid(unsigned char *list, ColorGrid *grid) {
    static unsigned char *children[COLOR_CELLS];
    static float xs[COLOR_CELLS], ys[COLOR_CELLS];
    grid->count = 0;
    grid->columns = grid->rows = 0;
    grid->has_view = 0;
    unsigned visited = 0;
    unsigned char *first = find_derived(list, LIST_BOX_ITEM_TYPE, 0, &visited), *host = NULL;
    if (!first || !memory_read(first + VISUAL_PARENT, &host, sizeof(host)) || !host) {
        return 0;
    }
    unsigned found = visual_children(host, children, COLOR_CELLS);
    int xn = 0, yn = 0;
    for (unsigned i = 0; i < found; ++i) {
        float root[2];
        unsigned n = grid->count;
        if (!is_list_item(children[i]) || !element_rect(children[i], grid->rect[n], root)) {
            continue;
        }
        float w = grid->rect[n][2] - grid->rect[n][0], h = grid->rect[n][3] - grid->rect[n][1];
        int cx = color_slot(xs, &xn, (grid->rect[n][0] + grid->rect[n][2]) * 0.5f, w * 0.5f);
        int cy = color_slot(ys, &yn, (grid->rect[n][1] + grid->rect[n][3]) * 0.5f, h * 0.5f);
        if (cx < 0 || cy < 0) {
            continue;
        }
        grid->item[n] = children[i];
        grid->column[n] = cx;
        grid->row[n] = cy;
        ++grid->count;
    }
    for (unsigned i = 0; i < grid->count; ++i) {
        grid->column[i] = color_rank(xs, xn, grid->column[i]);
        grid->row[i] = color_rank(ys, yn, grid->row[i]);
        grid->index[i] = grid->column[i] * yn + grid->row[i];
    }
    grid->columns = xn;
    grid->rows = yn;
    visited = 0;
    unsigned char *viewer = find_derived(list, SCROLL_VIEWER_TYPE, 0, &visited);
    float root[2];
    grid->has_view = viewer && element_rect(viewer, grid->view, root);
    return grid->count != 0;
}

/* The cell under the pointer, or -1; *inside whether the pointer is over
   the swatch area at all. */
static int color_cell_at(ColorGrid *grid, float fx, float fy, int *inside) {
    float root[2], probe[4];
    *inside = 0;
    if (!grid->count || !element_rect(grid->item[0], probe, root)) {
        return -1;
    }
    float x = fx * root[0], y = fy * root[1];
    if (grid->has_view &&
        (x < grid->view[0] || x >= grid->view[2] || y < grid->view[1] || y >= grid->view[3])) {
        return -1;
    }
    float box[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    for (unsigned i = 0; i < grid->count; ++i) {
        float *r = grid->rect[i];
        box[0] = r[0] < box[0] ? r[0] : box[0];
        box[1] = r[1] < box[1] ? r[1] : box[1];
        box[2] = r[2] > box[2] ? r[2] : box[2];
        box[3] = r[3] > box[3] ? r[3] : box[3];
        if (x >= r[0] && x < r[2] && y >= r[1] && y < r[3]) {
            *inside = 1;
            return (int)i;
        }
    }
    *inside = x >= box[0] && x < box[2] && y >= box[1] && y < box[3];
    return -1;
}

static int color_has(const ColorGrid *grid, int column, int row) {
    for (unsigned i = 0; i < grid->count; ++i) {
        if (grid->column[i] == column && grid->row[i] == row) {
            return 1;
        }
    }
    return 0;
}

/* The cell of item `index`, or -1. */
static int color_find(const ColorGrid *grid, int index) {
    for (unsigned i = 0; i < grid->count; ++i) {
        if (grid->index[i] == index) {
            return (int)i;
        }
    }
    return -1;
}

/* Moves keyboard focus to cell `to` with arrows as on a pad, counted from
   the focused item (SelectedIndex). With no focused swatch (the preview
   has focus) Right enters the swatches first; the keys act at once, so
   each round starts from where the last one landed. */
static void color_move(unsigned char *list, ColorGrid *grid, int to, const char *why) {
    if (to < 0 || to >= (int)grid->count) {
        return;
    }
    int target = grid->index[to], start = selected_index(list), now = start, keys = 0;
    for (int round = 0; round < 3 && now != target; ++round) {
        int from = color_find(grid, now);
        if (from < 0) {
            ui_key(UI_KEY_RIGHT); /* from the preview into the swatches */
            ++keys;
        } else {
            int dc = grid->column[to] - grid->column[from], dr = grid->row[to] - grid->row[from];
            /* Up / Down first while the current column has the target row (a
               short last column is entered sideways only at a row it has). */
            int vertical_first = color_has(grid, grid->column[from], grid->row[to]);
            for (int pass = 0; pass < 2; ++pass) {
                int vertical = (pass == 0) == (vertical_first != 0);
                int steps = vertical ? dr : dc;
                int count = steps < 0 ? -steps : steps;
                for (int i = 0; i < count && keys < 64; ++i, ++keys) {
                    ui_key(vertical ? (steps > 0 ? UI_KEY_DOWN : UI_KEY_UP)
                                    : (steps > 0 ? UI_KEY_RIGHT : UI_KEY_LEFT));
                }
            }
        }
        int was = now;
        now = selected_index(list);
        if (now == was && from >= 0) {
            ui_key(UI_KEY_RIGHT); /* the keys moved nothing: focus is on the preview */
            ++keys;
            now = selected_index(list);
        }
    }
    if (config.trace || now != target) {
        begin_line();
        text("KBM TRACE: colour ");
        text(why);
        text(": item ");
        signed_number(target);
        text(" (column ");
        signed_number(grid->column[to]);
        text(", row ");
        signed_number(grid->row[to]);
        text(" of ");
        signed_number(grid->columns);
        text("x");
        signed_number(grid->rows);
        text("), focus ");
        signed_number(start);
        text(", ");
        signed_number(keys);
        text(" arrow keys -> ");
        signed_number(now);
        text(now != target ? " (not reached)." : ".");
        flush_line();
    }
}

static void color_hover(unsigned char *list, float fx, float fy) {
    ColorGrid *grid = &color_grid_data;
    int inside;
    if (list != color_hover_list) {
        color_hover_list = list;
        color_hover_cell = color_click_cell = -1;
    }
    if (!color_grid(list, grid)) {
        return;
    }
    int cell = color_cell_at(grid, fx, fy, &inside);
    if (cell < 0 || grid->index[cell] == color_hover_cell) {
        return;
    }
    color_hover_cell = grid->index[cell];
    if (selected_index(list) != grid->index[cell]) {
        color_move(list, grid, cell, "hover");
    }
}

/* The colour type tab (or Q / E icon) under the pointer: the LB / RB steps
   to make, in *delta. 1 on a hit. */
static int color_tab_hit(float fx, float fy, int *delta) {
    unsigned char *selector = color_selector_cached, *strip = NULL, *items[16];
    float sel[4], root[2];
    if (!selector || !node_derives(selector, ITEMS_SELECTOR_TYPE) || !element_rect(selector, sel, root)) {
        return 0;
    }
    float x = fx * root[0], y = fy * root[1];
    if (y < sel[1] - 4.0f || y >= sel[3] + 4.0f) {
        return 0;
    }
    int current = selected_index(selector);
    if (x >= sel[0] && x < sel[2]) {
        PanelSearch item = {fx, fy, 0, -1, 0};
        panel_walk(selector, &item, 0);
        *delta = item.index >= 0 && current >= 0 ? item.index - current : 0;
        begin_line();
        text("KBM TRACE: colour type tab ");
        signed_number(item.index);
        text(" clicked (current ");
        signed_number(current);
        text(").");
        flush_line();
        return 1;
    }
    memory_read(selector + VISUAL_PARENT, &strip, sizeof(strip));
    unsigned count = strip ? visual_children(strip, items, ARRAY_COUNT(items)) : 0, at = count;
    for (unsigned i = 0; i < count; ++i) {
        at = items[i] == selector ? i : at;
    }
    for (unsigned i = 0; i < count && at < count; ++i) {
        float r[4], rr[2];
        if (i != at && element_rect(items[i], r, rr) && x >= r[0] - 4.0f && x < r[2] + 4.0f) {
            *delta = i < at ? -1 : 1;
            log_append(i < at ? "KBM TRACE: colour type Q icon clicked (LB).\r\n"
                              : "KBM TRACE: colour type E icon clicked (RB).\r\n");
            return 1;
        }
    }
    return 0;
}

/* 1 if the press was the colour page's. */
static int color_click(unsigned char *list, float fx, float fy) {
    ColorGrid *grid = &color_grid_data;
    int delta = 0, inside = 0;
    if (prompt_under_pointer(fx, fy)) {
        return 0; /* help-bar prompts act as usual */
    }
    if (color_tab_hit(fx, fy, &delta)) {
        int steps = delta < 0 ? -delta : delta;
        for (int i = 0; i < steps && i < 8; ++i) {
            ui_key(delta < 0 ? UI_KEY_LB : UI_KEY_RB);
        }
        color_hover_cell = color_click_cell = -1;
        return 1;
    }
    if (!color_grid(list, grid)) {
        return 0;
    }
    int cell = color_cell_at(grid, fx, fy, &inside);
    if (cell < 0) {
        if (inside) {
            log_append("KBM TRACE: colour click between swatches; swallowed.\r\n");
        }
        return inside;
    }
    int focused = selected_index(list), index = grid->index[cell];
    if (index == focused && index == color_click_cell) {
        log_append("KBM TRACE: colour click on the selected swatch: Ok (A).\r\n");
        ui_key(UI_KEY_A);
        return 1;
    }
    if (index != focused) {
        color_move(list, grid, cell, "click");
    } else if (config.trace) {
        log_append("KBM TRACE: colour click selects the focused swatch; click it again for Ok.\r\n");
    }
    color_click_cell = index;
    color_hover_cell = index;
    return 1;
}

/* One notch: Up / Down in the grid, Left / Right in a single row. */
static void color_wheel(unsigned char *list, long wheel) {
    ColorGrid *grid = &color_grid_data;
    int sideways = color_grid(list, grid) && grid->rows <= 1;
    int notches = (int)((wheel < 0 ? -wheel : wheel) + 60) / 120;
    notches = notches < 1 ? 1 : notches > 4 ? 4 : notches;
    for (int i = 0; i < notches; ++i) {
        ui_key(sideways ? (wheel > 0 ? UI_KEY_LEFT : UI_KEY_RIGHT) : (wheel > 0 ? UI_KEY_UP : UI_KEY_DOWN));
    }
    color_click_cell = -1;
}

static void native_box_tick(void) {
    void *box = InterlockedExchangePointer(&native_box, NULL);
    if (!box) {
        return;
    }
    int done = native_no(box);
    if (done == 1) {
        log_append("KBM: Esc chose the box's last item (No) directly.\r\n");
    } else if (done == 2) {
        /* closed without a choice */
    } else {
        void *yes_no_list = NULL;
        if (memory_read((unsigned char *)box + BOX_LIST, &yes_no_list, sizeof(yes_no_list)) &&
            yes_no_list) {
            log_append("KBM: Esc: the box's list was not readable; using Down/Enter instead.\r\n");
            InterlockedExchangePointer(&box_request, box);
        } else {
            /* A menu without the yes/no list is never answered by keys. */
            log_append("KBM: Esc: popup menu could not be closed; left as it is.\r\n");
        }
    }
}

/* Input tick: Down, Down, Down, then Enter, each a short tap. Enter is sent
   only if the box took keys after the request (it is still the page on top). */
static void dialog_cancel_tick(uint64_t now) {
    void *request = InterlockedExchangePointer(&box_request, NULL);
    if (request && !dialog_cancel.box) {
        log_append("KBM: Esc in a message box that ignores B: choosing the last item (No).\r\n");
        dialog_cancel.box = request;
        dialog_cancel.at = now;
        dialog_cancel.step = 0;
    }
    if (!dialog_cancel.box) {
        return;
    }
    uint64_t elapsed = now - dialog_cancel.at;
    if (elapsed < DIALOG_WAIT_MS) {
        return;
    }
    unsigned step = (unsigned)((elapsed - DIALOG_WAIT_MS) / 120u);
    if (step == dialog_cancel.step) {
        return;
    }
    dialog_cancel.step = step;
    if (step == 6 && !popup_open(dialog_cancel.box) &&
        (uint64_t)box_key_at <= dialog_cancel.at + DIALOG_WAIT_MS) {
        key_held[DIALOG_DOWN] = 0;
        dialog_cancel.box = NULL;
        log_append("KBM: ... the box did not take the Down keys; nothing confirmed.\r\n");
        return;
    }
    if (step <= 6) {
        unsigned code = step < 6 ? DIALOG_DOWN : DIALOG_CONFIRM;
        int press = step % 2 == 1 || step == 6;
        key_held[DIALOG_DOWN] = 0;
        if (press) {
            key_held[code] = key_tapped[code] = 1;
            key_pressed_at[code] = now;
        }
        if (step == 6) {
            log_append("KBM: ... last item selected and confirmed.\r\n");
        }
    } else {
        key_held[DIALOG_CONFIRM] = 0;
        dialog_cancel.box = NULL;
    }
}

/* AVUI MouseDevice::ChangeMouseOver(device, element): logged for the first
   calls so the log shows whether AVUI hit-tests the pointer reports. */
static void mouse_over_hook(void *device, void *element) {
    static volatile LONG calls;
    LONG call = InterlockedIncrement(&calls);
    if (call <= 6) {
        void *vtable = NULL;
        memory_read_pointer(element, &vtable);
        begin_line();
        text("KBM: AVUI mouse-over change #");
        number((uint64_t)call, 10);
        text(element ? ": element vtable " : ": no element");
        if (element) {
            number((uintptr_t)vtable - (uintptr_t)image, 16);
        }
        text(".");
        flush_line();
    }
    ((ObjectFn2)hooks[HOOK_MOUSE_OVER].original)(device, element);
}

/* ---------- keyboard-only sign-in (no pad) ----------

   On the title, with no player yet, 0xea0d40 takes the controller at
   manager+0xab28, asks its pad for the signed-in user (0xdeb0c0 on the pad
   from 0x106ea00) and calls 0xe7ebc0(sign_in, 0, user, controller) at
   0xea0f66. 0xe7ebc0 accepts a signed-in user with a controller at once; with
   no user and no pad it fails ("WYMAGANE ZALOGOWANIE"). With keyboard & mouse
   on and a controller that has no pad (our idle controller object), that call
   is given the host's signed-in user instead: Windows.Xbox.System.User's
   Users[0] (0x56ede0 returns the Users view; GetAt is slot 0x30, Size 0x38).
   A pad's controller, or a pad that has a user, is never touched. The call is
   pointed at an absolute jump in the int3 gap at 0xead940. */
static void write_jump(unsigned char *, const void *);
#define SIGN_IN_CALL 0xea0f66u
#define SIGN_IN 0xe7ebc0u
#define SIGN_IN_CAVE 0xead940u
#define USERS_VIEW 0x56ede0u
typedef void (*SignInFn)(void *, int, void *, void *);
typedef void *(*UsersViewFn)(void);
typedef long (*ComGetAtFn)(void *, unsigned, void **);
typedef long (*ComSizeFn)(void *, unsigned *);
typedef unsigned long (*ComReleaseFn)(void *);
static const unsigned char sign_in_bytes[] = {0x48, 0x89, 0x4c, 0x24, 0x08, 0x53, 0x56, 0x57};
static const unsigned char users_view_bytes[] = {0x48, 0x8b, 0xc4, 0x53, 0x48, 0x83, 0xec, 0x50};

static void com_release(void *object) {
    void **vtable = NULL;
    if (object && memory_read(object, &vtable, sizeof(vtable)) && vtable) {
        ((ComReleaseFn)vtable[2])(object);
    }
}

static void *first_user(void) {
    void *view = ((UsersViewFn)(void *)(image + USERS_VIEW))();
    void *user = NULL;
    unsigned count = 0;
    void **vtable = NULL;
    if (view && memory_read(view, &vtable, sizeof(vtable)) && vtable &&
        ((ComSizeFn)vtable[7])(view, &count) >= 0 && count) {
        if (((ComGetAtFn)vtable[6])(view, 0, &user) < 0) {
            user = NULL;
        }
    }
    com_release(view);
    return user;
}

static void sign_in_hook(void *sign_in, int mode, void *user, void *controller) {
    void *pad = (void *)1;
    if (!user && controller && installed && config.enabled && config.mode != KBM_MODE_PAD &&
        memory_read((unsigned char *)controller + CONTROLLER_GAMEPAD, &pad, sizeof(pad)) && !pad) {
        void *player = first_user();
        if (player) {
            ((SignInFn)(void *)(image + SIGN_IN))(sign_in, mode, player, controller);
            com_release(player);
            log_append("KBM: signed Player 1 in on the keyboard (no pad).\r\n");
            return;
        }
        log_append("KBM: keyboard sign-in: the host listed no user; left to the game.\r\n");
    }
    ((SignInFn)(void *)(image + SIGN_IN))(sign_in, mode, user, controller);
}

static void install_sign_in(void) {
    unsigned char *call = image + SIGN_IN_CALL, *cave = image + SIGN_IN_CAVE;
    unsigned char expected[5] = {0xe8, 0, 0, 0, 0};
    int32_t relative = (int32_t)((image + SIGN_IN) - (call + 5));
    memory_copy(expected + 1, &relative, 4);
    for (unsigned i = 0; i < 16; ++i) {
        if (cave[i] != 0xcc) {
            log_append("KBM: sign-in cave 0xead940 is in use; keyboard-only sign-in off.\r\n");
            return;
        }
    }
    if (!memory_equal(call, expected, sizeof(expected)) ||
        !memory_equal(image + SIGN_IN, sign_in_bytes, sizeof(sign_in_bytes)) ||
        !memory_equal(image + USERS_VIEW, users_view_bytes, sizeof(users_view_bytes))) {
        log_append("KBM: title sign-in code not as expected; keyboard-only sign-in off.\r\n");
        return;
    }
    DWORD old_call, old_cave, ignored;
    if (!VirtualProtect(cave, 16, PAGE_EXECUTE_READWRITE, &old_cave)) {
        return;
    }
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old_call)) {
        VirtualProtect(cave, 16, old_cave, &ignored);
        return;
    }
    write_jump(cave, (void *)sign_in_hook);
    relative = (int32_t)(cave - (call + 5));
    memory_copy(call + 1, &relative, 4);
    VirtualProtect(call, 5, old_call, &ignored);
    VirtualProtect(cave, 16, old_cave, &ignored);
    FlushInstructionCache(GetCurrentProcess(), cave, 16);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    log_append("KBM: hook title sign-in call 0xea0f66 (0xe7ebc0) installed via 0xead940 "
               "(keyboard-only start).\r\n");
}

/* The disconnect page is raised by the per-frame player check (0x228b2c /
   0x228c04): when a signed-in player's controller (player->vt+0xb0 ->
   vt+0x58(0)) is missing, it calls 0xb620c0(pairing, 1, ...) at 0x228bd4
   (player 1) and 0x228c65 (player 2). A player signed in on the keyboard has
   no pad controller, so with keyboard & mouse on and no pad attached at all
   those raises are skipped, and a page already up is hidden the way the
   game hides it (0xb620c0(pairing, 0, 1)). Both calls are pointed at one
   absolute jump in the int3 gap at 0x292d80. */
#define PAIRING_SHOW 0xb620c0u
#define PAIRING_RAISE_1 0x228bd4u
#define PAIRING_RAISE_2 0x228c65u
#define PAIRING_CAVE 0x292d80u
typedef void (*PairingShowFn)(void *, unsigned char, unsigned char);
static const unsigned char pairing_show_bytes[] = {0x48, 0x8b, 0xc4, 0x55, 0x41, 0x56, 0x41, 0x57};

static int any_pad_attached(void) {
    unsigned char *owner = manager();
    for (unsigned index = 0; owner && index < CONTROLLER_SLOTS; ++index) {
        void *gamepad = NULL;
        if (memory_read(controller_at(owner, index) + CONTROLLER_GAMEPAD, &gamepad, sizeof(gamepad)) &&
            gamepad) {
            return 1;
        }
    }
    return 0;
}

static void pairing_raise_hook(void *pairing, unsigned char show, unsigned char flag) {
    static int logged;
    /* Only while keyboard & mouse is the active source: a pad user whose pad
       goes away gets the game's own popup (garage4). */
    if (show && installed && config.enabled && config.mode != KBM_MODE_PAD && source == SOURCE_KBM) {
        unsigned char shown = 0;
        if (memory_read((unsigned char *)pairing + 8, &shown, 1) && shown) {
            ((PairingShowFn)(void *)(image + PAIRING_SHOW))(pairing, 0, 1);
        }
        if (!logged) {
            logged = 1;
            log_append("KBM: controller-disconnected prompt suppressed (keyboard & mouse active, no pad).\r\n");
        }
        return;
    }
    ((PairingShowFn)(void *)(image + PAIRING_SHOW))(pairing, show, flag);
}

static void install_pairing_raise(void) {
    unsigned char *cave = image + PAIRING_CAVE;
    unsigned char *calls[2] = {image + PAIRING_RAISE_1, image + PAIRING_RAISE_2};
    for (unsigned i = 0; i < 16; ++i) {
        if (cave[i] != 0xcc) {
            log_append("KBM: pairing cave 0x292d80 is in use; the disconnect prompt is left alone.\r\n");
            return;
        }
    }
    if (!memory_equal(image + PAIRING_SHOW, pairing_show_bytes, sizeof(pairing_show_bytes))) {
        log_append("KBM: pairing show 0xb620c0 changed; the disconnect prompt is left alone.\r\n");
        return;
    }
    for (unsigned k = 0; k < 2; ++k) {
        unsigned char expected[5] = {0xe8, 0, 0, 0, 0};
        int32_t relative = (int32_t)((image + PAIRING_SHOW) - (calls[k] + 5));
        memory_copy(expected + 1, &relative, 4);
        if (!memory_equal(calls[k], expected, 5)) {
            log_append("KBM: pairing raise calls changed; the disconnect prompt is left alone.\r\n");
            return;
        }
    }
    DWORD old_cave, old_call, ignored;
    if (!VirtualProtect(cave, 16, PAGE_EXECUTE_READWRITE, &old_cave)) {
        return;
    }
    write_jump(cave, (void *)pairing_raise_hook);
    VirtualProtect(cave, 16, old_cave, &ignored);
    FlushInstructionCache(GetCurrentProcess(), cave, 16);
    for (unsigned k = 0; k < 2; ++k) {
        if (VirtualProtect(calls[k], 5, PAGE_EXECUTE_READWRITE, &old_call)) {
            int32_t relative = (int32_t)(cave - (calls[k] + 5));
            memory_copy(calls[k] + 1, &relative, 4);
            VirtualProtect(calls[k], 5, old_call, &ignored);
            FlushInstructionCache(GetCurrentProcess(), calls[k], 5);
        }
    }
    log_append("KBM: hook disconnect-page raises 0x228bd4/0x228c65 (0xb620c0) installed via 0x292d80 "
               "(no pad demanded on keyboard & mouse).\r\n");
}

/* The controller-missing pause (features/controller_pause.c): while keyboard &
   mouse is the active source and no pad is attached it is never fired. */
static int pause_suppressed(void) {
    return installed && config.enabled && config.mode != KBM_MODE_PAD && source == SOURCE_KBM &&
           !any_pad_attached();
}

static void install_pause_check(void) {
    controller_pause_set_suppressed(pause_suppressed);
    controller_pause_install(image);
}

/* The same per-frame check hands the UI its input controller: Player 1's
   controller, through UserInterface vtable slot 0xe8 (0x14237e0 stores it at
   UserInterface+0x88; called at 0x228cbd). A player signed in on the
   keyboard has none, so the UI's confirm/back/LB/RB queries went to no
   controller. With keyboard & mouse on and no pad attached, a missing
   controller is replaced by the local player's routed controller
   (manager+0xab28, our idle controller) or, without one, the any-controller
   object, which the keyboard answers. */
#define UI_SET_CONTROLLER_SLOT 0x2ecfc68u /* 0x2ecfb80 + 0xe8 */
#define UI_SET_CONTROLLER 0x14237e0u
typedef void (*SetControllerFn)(void *, void *);
static const unsigned char ui_set_controller_bytes[] = {0x48, 0x89, 0x91, 0x88, 0x00, 0x00, 0x00, 0xc3};

static void ui_set_controller_hook(void *ui, void *controller) {
    static int logged;
    /* Only while keyboard & mouse is the active source and no pad is attached:
       with a pad, Player 1's controller binding is the game's (garage4). */
    if (!controller && installed && config.enabled && config.mode != KBM_MODE_PAD &&
        source == SOURCE_KBM && !any_pad_attached()) {
        unsigned char *owner = manager();
        void *routed = NULL;
        if (owner) {
            memory_read(owner + MANAGER_SELECTED, &routed, sizeof(routed));
            controller = routed ? routed : (void *)controller_at(owner, CONTROLLER_SLOTS);
        }
        if (controller && !logged) {
            logged = 1;
            log_append("KBM: the UI reads the keyboard's controller (Player 1 has no pad).\r\n");
        }
    }
    ((SetControllerFn)(void *)(image + UI_SET_CONTROLLER))(ui, controller);
}

static void install_ui_controller(void) {
    void **slot = (void **)(image + UI_SET_CONTROLLER_SLOT);
    DWORD protection, ignored;
    if (*slot != image + UI_SET_CONTROLLER ||
        !memory_equal(image + UI_SET_CONTROLLER, ui_set_controller_bytes, sizeof(ui_set_controller_bytes)) ||
        !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        log_append("KBM: UserInterface set-controller slot changed; keyboard-only menus may not confirm.\r\n");
        return;
    }
    InterlockedExchangePointer((void *volatile *)slot, (void *)ui_set_controller_hook);
    VirtualProtect(slot, sizeof(void *), protection, &ignored);
    log_append("KBM: hook UserInterface set-controller vtable slot 0x2ecfc68 (0x14237e0) installed "
               "(keyboard-only menus).\r\n");
}

/* ---------- controller-disconnected page ---------- */

/* When the signed-in player's pad goes away the game shows ControllerPairing
   ("KONTROLER ODLACZONY ... press A", vtable 0x2de33d0). Its update (slot
   0x4c8, 0xcfaa10) waits for A from the controller at manager+0xab28 and,
   once the player has a controller again, calls 0xcf3ea0(page), which marks
   it done (+0x420) so the page closes itself a moment later. With keyboard &
   mouse on (Mode=KBM or Auto) the page is marked done the same way: the
   keyboard needs no pad and play goes on. */
#define PAIRING_UPDATE_SLOT 0x2de3898u /* 0x2de33d0 + 0x4c8 */
#define PAIRING_UPDATE 0xcfaa10u
#define PAIRING_DONE 0xcf3ea0u
static const unsigned char pairing_update_bytes[] = {0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x40};
static const unsigned char pairing_done_bytes[] = {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x48, 0xc7, 0x44, 0x24, 0x20};
static ObjectFn1 original_pairing_update;
typedef void *(*UserGetFn)(void *);
/* Last time the page was seen waiting for a gamepad user (UI thread). */
static volatile LONG64 pairing_waiting_at;
#define USER_MANAGER 0x34b3e18u /* read by 0x327270 */

/* The player the page is about (0x327270()->vt+0x50, as 0xcfab7d reads it):
   none means the title's sign-in, which is left to the game. */
static int pairing_player_signed_in(void) {
    unsigned char *manager = NULL, **vtable = NULL;
    void *get = NULL;
    if (!memory_read(image + USER_MANAGER, &manager, sizeof(manager)) || !manager ||
        !memory_read(manager, &vtable, sizeof(vtable)) || (unsigned char *)vtable < image ||
        (unsigned char *)vtable >= image + GAME_IMAGE_SIZE ||
        !memory_read((unsigned char *)vtable + 0x50, &get, sizeof(get)) ||
        (unsigned char *)get < image || (unsigned char *)get >= image + GAME_IMAGE_SIZE) {
        return 0;
    }
    return ((UserGetFn)get)(manager) != NULL;
}

static void pairing_update_hook(void *page) {
    unsigned char done = 1, busy = 1;
    static int title_logged;
    if (installed && config.enabled && config.mode != KBM_MODE_PAD &&
        memory_read((unsigned char *)page + 0x420, &done, 1) && !done &&
        !pairing_player_signed_in()) {
        if (!title_logged) {
            title_logged = 1;
            log_append("KBM: controller pairing page with no player signed in (title sign-in); "
                       "left to the game.\r\n");
        }
        original_pairing_update(page);
        return;
    }
    /* The page is closed for the keyboard only while keyboard & mouse is the
       active source: a pad user's page stays until a key switches the source
       (that key is held back as the switch press) or the pad pairs again. */
    static int waiting_logged;
    if (installed && config.enabled && config.mode != KBM_MODE_PAD &&
        memory_read((unsigned char *)page + 0x420, &done, 1) &&
        memory_read((unsigned char *)page + 0x421, &busy, 1) && !done && !busy) {
        if (source == SOURCE_KBM) {
            ((ObjectFn1)(void *)(image + PAIRING_DONE))(page);
            log_append(waiting_logged ? "KBM: controller-disconnected page closed: a key switched to "
                                        "keyboard & mouse.\r\n"
                                      : "KBM: controller-disconnected prompt suppressed (keyboard & "
                                        "mouse active).\r\n");
            waiting_logged = 0;
        } else {
            InterlockedExchange64(&pairing_waiting_at, (LONG64)GetTickCount64());
            if (!waiting_logged) {
                waiting_logged = 1;
                log_append("KBM: controller-disconnected page shown for the gamepad user; a key press "
                           "switches to keyboard & mouse and closes it.\r\n");
            }
        }
    }
    original_pairing_update(page);
}

static void install_pairing(void) {
    void **slot = (void **)(image + PAIRING_UPDATE_SLOT);
    DWORD protection, ignored;
    if (*slot != image + PAIRING_UPDATE ||
        !memory_equal(image + PAIRING_UPDATE, pairing_update_bytes, sizeof(pairing_update_bytes)) ||
        !memory_equal(image + PAIRING_DONE, pairing_done_bytes, sizeof(pairing_done_bytes)) ||
        !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        log_append("KBM: ControllerPairing page not as expected; the disconnect prompt is left alone.\r\n");
        return;
    }
    original_pairing_update = (ObjectFn1)(void *)(image + PAIRING_UPDATE);
    InterlockedExchangePointer((void *volatile *)slot, (void *)pairing_update_hook);
    VirtualProtect(slot, sizeof(void *), protection, &ignored);
    log_append("KBM: hook ControllerPairing update vtable slot 0x2de3898 (0xcfaa10) installed (no pad "
               "demanded while keyboard & mouse is on).\r\n");
}

/* ---------- keyboard button prompts ---------- */

/* Prompt icons are textures: XAML help bars use
   /ForzaUI;component/Textures/Data_Bound/ControllerIcons/Controller_A.png
   (Resources/Anthem/Global_Textures.xaml) and HUD prompts load
   game:\Media\UI\Textures\Data_Bound\ControllerIcons\Medium\*.swatchbin.
   Every UI bitmap is loaded by 0x1104e70(handle+0x50, ext, options, callback)
   with the path in the options' name string; the handle is an
   AVUITextureHandle (vtable 0x2f31430: references +0x10, pixel buffer
   +0x80). While the keyboard is the active source each ControllerIcons
   handle receives a keycap drawn with the bound key's name, submitted like
   the custom gamerpic (pixel buffer 0x165e600, RGBA8 upload 0x1105510,
   bitmap update 0x16e2dc0); when the pad is active again the original file
   is loaded back through 0x1104e70. */
#define TEXTURE_HANDLE_VTABLE 0x2f31430u
#define EMPTY_TEXT 0x2360baau
#define KEYCAP_SIZE 128u
#define PROMPT_SLOTS 192u

typedef void *(*TextureLoadFn)(void *, const char *, void *, void **);
typedef void **(*PixelBufferFn)(void **, int);
typedef void (*QueueTextureFn)(void *, const void *, int, unsigned char, const void *);
typedef void (*QueueUpdateFn)(void *, void **);
typedef void (*DestroyFn)(void *);

typedef struct {
    char name[16];
    uint64_t length, capacity;
    uint32_t priority, format, width, height;
    unsigned char no_cache, padding[7];
} TextureOptions;

typedef struct {
    unsigned char *handle;
    unsigned button;
    int shows_keys;
    TextureOptions options;
    char path[160];
    unsigned char *original; /* bytes of an icon uploaded from memory */
    int original_size;
    int path_in_extension; /* the loader took the path as its second argument */
    wchar_t label[16];     /* the keycap text last drawn into this handle */
} PromptIcon;

enum {
    BUTTON_NONE, BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_LB, BUTTON_RB, BUTTON_LT,
    BUTTON_RT, BUTTON_MENU, BUTTON_VIEW, BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT,
    BUTTON_UP_DOWN, BUTTON_LEFT_RIGHT, BUTTON_ARROWS, BUTTON_MOUSE
};

static PromptIcon prompt_icons[PROMPT_SLOTS];
static volatile LONG prompt_count;
static SRWLOCK prompt_lock = SRWLOCK_INIT;
static unsigned char keycap_pixels[KEYCAP_SIZE * KEYCAP_SIZE * 4];
static int prompts_logged;

static int text_contains(const char *text, size_t length, const char *needle) {
    size_t n = text_length(needle);
    for (size_t i = 0; i + n <= length; ++i) {
        size_t j = 0;
        while (j < n) {
            char a = text[i + j], b = needle[j];
            a = a >= 'A' && a <= 'Z' ? (char)(a + 32) : a;
            b = b >= 'A' && b <= 'Z' ? (char)(b + 32) : b;
            if (a != b) {
                break;
            }
            ++j;
        }
        if (j == n) {
            return 1;
        }
    }
    return 0;
}

/* Button of a ControllerIcons file name ("...\Controller_DPadUp.swatchbin"). */
static unsigned icon_button(const char *path, size_t length) {
    size_t start = length;
    while (start && path[start - 1] != '\\' && path[start - 1] != '/') {
        --start;
    }
    const char *stem = path + start;
    size_t size = length - start;
    static const struct {
        const char *name;
        unsigned button;
    } names[] = {
        {"controller_a.", BUTTON_A},          {"controller_b.", BUTTON_B},
        {"controller_x.", BUTTON_X},          {"controller_y.", BUTTON_Y},
        {"controller_lb.", BUTTON_LB},        {"controller_rb.", BUTTON_RB},
        {"controller_lt.", BUTTON_LT},        {"controller_rt.", BUTTON_RT},
        {"controller_menu.", BUTTON_MENU},    {"controller_view.", BUTTON_VIEW},
        {"dpadupdown.", BUTTON_UP_DOWN},      {"dpadleftright.", BUTTON_LEFT_RIGHT},
        {"dpadup.", BUTTON_UP},               {"dpaddown.", BUTTON_DOWN},
        {"dpadleft.", BUTTON_LEFT},           {"dpadright.", BUTTON_RIGHT},
        {"controller_dpad.", BUTTON_ARROWS},  {"leftthumb_updown.", BUTTON_UP_DOWN},
        {"leftthumb_leftright.", BUTTON_LEFT_RIGHT}, {"leftthumb_up.", BUTTON_UP},
        {"leftthumb_down.", BUTTON_DOWN},     {"leftthumb_left.", BUTTON_LEFT},
        {"leftthumb_right.", BUTTON_RIGHT},   {"controller_leftthumb.", BUTTON_ARROWS},
        {"rightthumb", BUTTON_MOUSE},         {"controller_r3.", BUTTON_MOUSE},
    };
    for (unsigned i = 0; i < ARRAY_COUNT(names); ++i) {
        if (text_contains(stem, size, names[i].name)) {
            return names[i].button;
        }
    }
    return BUTTON_NONE;
}

static void key_label(unsigned code, wchar_t *out) {
    static const struct {
        unsigned short code;
        const wchar_t *text;
    } short_names[] = {
        {0x0e, L"Bksp"}, {0x1c, L"Enter"}, {0x39, L"Space"}, {0x2a, L"Shift"}, {0x36, L"Shift"},
        {0x1d, L"Ctrl"}, {0x9d, L"Ctrl"}, {0xc9, L"PgUp"}, {0xd1, L"PgDn"}, {0xc8, L"\x2191"},
        {0xd0, L"\x2193"}, {0xcb, L"\x2190"}, {0xcd, L"\x2192"}, {0x1a, L"["}, {0x1b, L"]"},
        {0x0f, L"Tab"}, {0xc7, L"Home"}, {0xcf, L"End"}, {0xd2, L"Ins"}, {0xd3, L"Del"},
        {0x9c, L"Enter"},
    };
    for (unsigned i = 0; i < ARRAY_COUNT(short_names); ++i) {
        if (short_names[i].code == code) {
            lstrcpyW(out, short_names[i].text);
            return;
        }
    }
    const char *name = kbm_config_key_name(code);
    unsigned i = 0;
    if (code >= 0x47 && code <= 0x53 && name[0] == 'N') {
        out[i++] = L'N';
        out[i++] = L'u';
        out[i++] = L'm';
        name += 6; /* "Numpad" */
    }
    while (*name && i < 7) {
        out[i++] = (wchar_t)*name++;
    }
    out[i] = 0;
}

static void binding_label(unsigned binding, wchar_t *out) {
    unsigned code = config.keys[binding][0];
    if (!code) {
        lstrcpyW(out, L"?");
        return;
    }
    key_label(code, out);
}

/* World prompts name a pad button, but in the world that button stands for a
   racing action whose button depends on the control scheme (with the manual
   gearbox, "enter the garage" moves from X to View). The game resolves an
   action to its physical inputs through the controller's binding set:
   0x106f230(controller+0x20, context, action) gives the count and
   0x106e7f0(controller+0x20, context, action, i)+8 each input id (0x25 none),
   as the axis query 0x106f040 does. A button's input ids come from its fixed
   UI-context action (A 0x4a, B 0x4d, X 0x8d, Y 0x90, LB 0x87, RB 0x8a, LT 0x81,
   RT 0x84, Menu 0x54, View 0x50); in the world the prompt shows the keys of
   the racing action that uses the same input (Activate first). */
#define BIND_COUNT 0x106f230u
#define BIND_AT 0x106e7f0u
typedef unsigned (*BindCountFn)(void *, unsigned, unsigned);
typedef unsigned char *(*BindAtFn)(void *, unsigned, unsigned, unsigned);
static const unsigned char bind_count_bytes[] = {0x4c, 0x8b, 0x19, 0x48, 0x8b, 0x49, 0x08, 0x44, 0x8b, 0xd2};
static const unsigned char bind_at_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10};
static int in_world(void);
/* Prompt labels in use: 1 world (racing actions), 0 menus (prompts_tick). */
static volatile int prompt_world;
/* Page-specific labels: 0 none, else the page (prompts_tick). */
enum { PAGE_PHOTO_LABELS = 1, PAGE_CARS_LABELS = 3 };
static volatile int prompt_page;

static unsigned action_inputs(unsigned char *controller, unsigned context, unsigned action, int *ids,
                              unsigned max) {
    static int verified = -1;
    if (verified < 0) {
        verified = memory_equal(image + BIND_COUNT, bind_count_bytes, sizeof(bind_count_bytes)) &&
                   memory_equal(image + BIND_AT, bind_at_bytes, sizeof(bind_at_bytes));
        log_append(verified ? "KBM: world prompts follow the game's action bindings (0x106f230/0x106e7f0).\r\n"
                            : "KBM: action binding lookup changed; world prompts use fixed keys.\r\n");
    }
    if (!verified || !controller) {
        return 0;
    }
    unsigned count = ((BindCountFn)(void *)(image + BIND_COUNT))(controller + 0x20, context, action), found = 0;
    for (unsigned i = 0; i < count && i < 8 && found < max; ++i) {
        unsigned char *binding = ((BindAtFn)(void *)(image + BIND_AT))(controller + 0x20, context, action, i);
        int id = 0x25;
        if (binding && memory_read(binding + 8, &id, sizeof(id)) && id != 0x25) {
            ids[found++] = id;
        }
    }
    return found;
}

/* The binding whose racing action uses the button's input, or -1. */
static int world_prompt_binding(unsigned button) {
    static const struct {
        unsigned button, ui_action;
    } ui_actions[] = {{BUTTON_A, 0x4a}, {BUTTON_B, 0x4d}, {BUTTON_X, 0x8d}, {BUTTON_Y, 0x90},
                      {BUTTON_LB, 0x87}, {BUTTON_RB, 0x8a}, {BUTTON_LT, 0x81}, {BUTTON_RT, 0x84},
                      {BUTTON_MENU, 0x54}, {BUTTON_VIEW, 0x50}};
    unsigned char *controller = (unsigned char *)routed_controller;
    int button_ids[8], action_ids[8];
    unsigned ui_action = 0;
    for (unsigned i = 0; i < ARRAY_COUNT(ui_actions); ++i) {
        ui_action = ui_actions[i].button == button ? ui_actions[i].ui_action : ui_action;
    }
    if (!ui_action || !controller) {
        return -1;
    }
    unsigned button_count = action_inputs(controller, CONTEXT_UI, ui_action, button_ids, 8);
    /* Activate first: it is what world prompts mostly offer. */
    static const unsigned char racing[] = {KBM_ACTIVATE, KBM_CAMERA, KBM_REWIND, KBM_PHOTO_MODE,
                                           KBM_LOOK_BACK, KBM_HANDBRAKE, KBM_SHIFT_UP, KBM_SHIFT_DOWN,
                                           KBM_CLUTCH, KBM_RADIO_PREVIOUS, KBM_RADIO_NEXT};
    for (unsigned r = 0; r < ARRAY_COUNT(racing) && button_count; ++r) {
        for (unsigned f = 0; f < ARRAY_COUNT(families); ++f) {
            if (families[f].binding != racing[r] || families[f].context != CONTEXT_RACING) {
                continue;
            }
            unsigned count = action_inputs(controller, CONTEXT_RACING, families[f].actions[0], action_ids, 8);
            for (unsigned a = 0; a < count; ++a) {
                for (unsigned b = 0; b < button_count; ++b) {
                    if (action_ids[a] == button_ids[b]) {
                        return racing[r];
                    }
                }
            }
        }
    }
    return -1;
}

static void button_label(unsigned button, wchar_t *out) {
    if (prompt_page == PAGE_PHOTO_LABELS) {
        if (button == BUTTON_RB) { lstrcpyW(out, L"P"); return; }
        if (button == BUTTON_LB) { lstrcpyW(out, L"R"); return; }
        if (button == BUTTON_VIEW) { lstrcpyW(out, L"Backspace"); return; }
    } else if (prompt_page == PAGE_CARS_LABELS && button == BUTTON_UP_DOWN) {
        lstrcpyW(out, L"P");
        return;
    }
    int world = prompt_world ? world_prompt_binding(button) : -1;
    if (world >= 0) {
        binding_label((unsigned)world, out);
        return;
    }
    /* In the world the triggers drive ("RT to accelerate"): name the throttle
       and brake keys, not the menu triggers (PgUp/PgDn). Label only; the
       driving keys already act as the triggers there. */
    if (prompt_world && (button == BUTTON_RT || button == BUTTON_LT)) {
        binding_label(button == BUTTON_RT ? KBM_THROTTLE : KBM_BRAKE, out);
        return;
    }
    switch (button) {
        case BUTTON_A: binding_label(KBM_CONFIRM, out); return;
        case BUTTON_B:
            if (config.escape) {
                lstrcpyW(out, L"Esc");
            } else {
                binding_label(KBM_BACK, out);
            }
            return;
        case BUTTON_X: binding_label(KBM_MENU_X, out); return;
        case BUTTON_Y: binding_label(KBM_MENU_Y, out); return;
        case BUTTON_LB: binding_label(KBM_TAB_LEFT, out); return;
        case BUTTON_RB: binding_label(KBM_TAB_RIGHT, out); return;
        case BUTTON_LT: binding_label(KBM_MENU_LEFT_TRIGGER, out); return;
        case BUTTON_RT: binding_label(KBM_MENU_RIGHT_TRIGGER, out); return;
        case BUTTON_MENU:
            if (config.escape) {
                lstrcpyW(out, L"Esc");
            } else {
                binding_label(KBM_PAUSE, out);
            }
            return;
        case BUTTON_VIEW: binding_label(KBM_MAP, out); return;
        case BUTTON_UP: lstrcpyW(out, L"\x2191"); return;
        case BUTTON_DOWN: lstrcpyW(out, L"\x2193"); return;
        case BUTTON_LEFT: lstrcpyW(out, L"\x2190"); return;
        case BUTTON_RIGHT: lstrcpyW(out, L"\x2192"); return;
        case BUTTON_UP_DOWN: lstrcpyW(out, L"\x2191\x2193"); return;
        case BUTTON_LEFT_RIGHT: lstrcpyW(out, L"\x2190\x2192"); return;
        case BUTTON_ARROWS: lstrcpyW(out, L"\x2190\x2191\x2192"); return;
        case BUTTON_MOUSE: binding_label(KBM_CAMERA, out); return; /* right stick click: Tab */
    }
    lstrcpyW(out, L"?");
}

static void release_reference(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0) {
        ((DestroyFn)(void *)(image + GAME_RVA_DESTROY_UI_OBJECT))(object);
    }
}

/* Uploads RGBA pixels into a texture handle, as gamerpic.c does. */
static int submit_keycap(unsigned char *handle, unsigned button, wchar_t *drawn) {
    wchar_t label[16];
    button_label(button, label);
    if (drawn) {
        lstrcpyW(drawn, label);
    }
    if (!kbm_win32_render_keycap(label, KEYCAP_SIZE, keycap_pixels)) {
        return 0;
    }
    int size = (int)sizeof(keycap_pixels);
    void *buffer = NULL;
    ((PixelBufferFn)(void *)(image + GAME_RVA_CREATE_PIXEL_BUFFER))(&buffer, size);
    void *storage = buffer ? *(void **)((unsigned char *)buffer + 0x28) : NULL;
    if (!storage) {
        release_reference(buffer);
        return 0;
    }
    memory_copy(storage, keycap_pixels, (size_t)size);
    void **member = (void **)(handle + 0x80);
    release_reference(*member);
    *member = buffer;
    TextureOptions options;
    for (unsigned i = 0; i < sizeof(options); ++i) {
        ((volatile unsigned char *)&options)[i] = 0;
    }
    options.capacity = 15;
    options.priority = 0x60000;
    options.format = 3; /* RGBA8 */
    options.width = KEYCAP_SIZE;
    options.height = KEYCAP_SIZE;
    ((QueueTextureFn)hooks[HOOK_TEXTURE_BYTES].original)(handle + 0x50, storage, size, 0, &options);
    void *owned = handle;
    InterlockedIncrement((volatile LONG *)(handle + 0x10));
    void *manager = *(void **)(image + GAME_RVA_UI_MANAGER);
    ((QueueUpdateFn)(void *)(image + GAME_RVA_QUEUE_BITMAP_UPDATE))(manager, &owned);
    return 1;
}

static void reload_original(PromptIcon *icon) {
    if (icon->original) {
        TextureOptions copy = icon->options;
        if (copy.length >= 16) {
            char *text = icon->path;
            memory_copy(copy.name, &text, sizeof(text));
            copy.capacity = copy.length;
        }
        ((QueueTextureFn)hooks[HOOK_TEXTURE_BYTES].original)(icon->handle + 0x50, icon->original,
                                                             icon->original_size, 0, &copy);
        void *owned = icon->handle;
        InterlockedIncrement((volatile LONG *)(icon->handle + 0x10));
        ((QueueUpdateFn)(void *)(image + GAME_RVA_QUEUE_BITMAP_UPDATE))(
            *(void **)(image + GAME_RVA_UI_MANAGER), &owned);
        return;
    }
    void *callback = NULL;
    TextureOptions options = icon->options;
    if (icon->path_in_extension) {
        options.length = 0;
        options.capacity = 15;
        options.name[0] = 0;
        ((TextureLoadFn)hooks[HOOK_TEXTURE].original)(icon->handle + 0x50, icon->path, &options,
                                                      &callback);
        return;
    }
    if (options.length >= 16) {
        char *text = icon->path;
        memory_copy(options.name, &text, sizeof(text));
        options.capacity = options.length;
    }
    ((TextureLoadFn)hooks[HOOK_TEXTURE].original)(icon->handle + 0x50, (const char *)(image + EMPTY_TEXT),
                                                  &options, &callback);
}

/* The first calls of each texture hook are logged whatever Trace says, to
   show that the hooks run and what they are given. */
static volatile LONG texture_loads, texture_uploads;

static void texture_diagnostic(const char *loader, LONG call, void *context, const void *options_pointer,
                               const char *extension) {
    if (call > 16) {
        return;
    }
    void *vtable = NULL;
    TextureOptions options;
    char name[96];
    size_t n = 0;
    memory_read_pointer((unsigned char *)context - 0x50, &vtable);
    int has_options = options_pointer && memory_read(options_pointer, &options, sizeof(options));
    if (has_options && options.length < 0x10000) {
        const char *at = options.capacity >= 16 ? *(const char *const *)options_pointer
                                                : (const char *)options_pointer;
        n = options.length < sizeof(name) - 1 ? (size_t)options.length : sizeof(name) - 1;
        if (!memory_read(at, name, n)) {
            n = 0;
        }
    }
    name[n] = 0;
    begin_line();
    text("KBM: texture ");
    text(loader);
    text(" #");
    number((uint64_t)call, 10);
    text(": handle vtable ");
    number((uintptr_t)vtable - (uintptr_t)image, 16);
    text(has_options ? ", name length " : ", no options");
    if (has_options) {
        number(options.length, 10);
        text(" \"");
        text(name);
        text("\"");
    }
    if (extension) {
        char ext[48];
        unsigned k = 0;
        while (k < sizeof(ext) - 1 && memory_read(extension + k, &ext[k], 1) && ext[k]) {
            ++k;
        }
        ext[k] = 0;
        text(", second argument \"");
        text(ext);
        text("\"");
    }
    text(".");
    flush_line();
}

/* garage8 diagnostics: a prompt texture handle that now shows another
   button (a world prompt once showed View's key for X). */
static void note_prompt_change(unsigned char *handle, unsigned from, unsigned to, const char *path) {
    begin_line();
    text("KBM: prompt icon handle ");
    number((uintptr_t)handle & 0xffffffu, 16);
    text(" changes from button ");
    number(from, 10);
    text(" to ");
    number(to, 10);
    text(" (");
    text(path);
    text(").");
    flush_line();
}

static void *texture_load_hook(void *context, const char *extension, void *options_pointer,
                               void **callback) {
    /* 0x1104e70(handle + 0x50, path, options, callback): the file path is the
       second argument (game:\\media\\UI\\Textures\\...); older call sites
       carry it in the options' name instead. */
    static volatile LONG matches;
    InterlockedIncrement(&texture_loads);
    TextureOptions options;
    char path[160];
    size_t length = 0;
    int in_extension = 0;
    int have_options = options_pointer && memory_read(options_pointer, &options, sizeof(options));
    if (!prompts_installed || !context) {
        return ((TextureLoadFn)hooks[HOOK_TEXTURE].original)(context, extension, options_pointer,
                                                             callback);
    }
    while (extension && length < sizeof(path) - 1 && memory_read(extension + length, &path[length], 1) &&
           path[length]) {
        ++length;
    }
    in_extension = length > 0;
    if (!length && have_options && options.length > 0 && options.length < sizeof(path)) {
        const char *text = options.capacity >= 16 ? *(const char **)options_pointer
                                                  : (const char *)options_pointer;
        if (memory_read(text, path, (size_t)options.length)) {
            length = (size_t)options.length;
        }
    }
    path[length] = 0;
    if (!length || !text_contains(path, length, "ControllerIcons")) {
        return ((TextureLoadFn)hooks[HOOK_TEXTURE].original)(context, extension, options_pointer,
                                                             callback);
    }
    unsigned char *handle = (unsigned char *)context - 0x50;
    void *vtable = NULL;
    memory_read(handle, &vtable, sizeof(vtable));
    unsigned button = icon_button(path, length);
    if (InterlockedIncrement(&matches) <= 24) {
        begin_line();
        text("KBM: prompt icon texture ");
        text(path);
        text(" (button ");
        number(button, 10);
        text(vtable == image + TEXTURE_HANDLE_VTABLE ? ", AVUI handle)." : ", other handle vtable ");
        if (vtable != image + TEXTURE_HANDLE_VTABLE) {
            number((uintptr_t)vtable - (uintptr_t)image, 16);
            text(").");
        }
        flush_line();
    }
    if (button == BUTTON_NONE || vtable != image + TEXTURE_HANDLE_VTABLE) {
        return ((TextureLoadFn)hooks[HOOK_TEXTURE].original)(context, extension, options_pointer,
                                                             callback);
    }
    AcquireSRWLockExclusive(&prompt_lock);
    PromptIcon *icon = NULL;
    for (LONG i = 0; i < prompt_count; ++i) {
        if (prompt_icons[i].handle == handle) {
            icon = &prompt_icons[i];
        }
    }
    if (!icon && prompt_count < (LONG)PROMPT_SLOTS) {
        icon = &prompt_icons[prompt_count];
        icon->handle = handle;
        InterlockedIncrement((volatile LONG *)(handle + 0x10)); /* kept for later swaps */
        InterlockedIncrement(&prompt_count);
        if (prompt_count == 1) {
            log_append("KBM: first prompt icon texture seen; keyboard keys replace pad icons while "
                       "keyboard & mouse is active.\r\n");
        }
    }
    if (icon && icon->button && icon->button != button) {
        note_prompt_change(handle, icon->button, button, path);
    }
    if (icon) {
        icon->button = button;
        if (have_options) {
            icon->options = options;
        } else {
            for (unsigned i = 0; i < sizeof(icon->options); ++i) {
                ((volatile unsigned char *)&icon->options)[i] = 0;
            }
            icon->options.capacity = 15;
        }
        icon->path_in_extension = in_extension;
        memory_copy(icon->path, path, length + 1);
    }
    int keys = icon && kbm_active && (!callback || !*callback);
    if (icon) {
        icon->shows_keys = keys;
    }
    ReleaseSRWLockExclusive(&prompt_lock);
    if (keys && submit_keycap(handle, button, icon->label)) {
        return NULL; /* the file load is skipped while keys are shown */
    }
    if (icon) {
        icon->shows_keys = 0;
    }
    return ((TextureLoadFn)hooks[HOOK_TEXTURE].original)(context, extension, options_pointer,
                                                         callback);
}

/* Trace=1: the first texture paths each loader sees, to show their naming. */
static void trace_texture(const char *loader, const char *path, size_t length) {
    static volatile LONG traced_textures;
    if (!config.trace || InterlockedIncrement(&traced_textures) > 40) {
        return;
    }
    char copy[200];
    size_t n = length < sizeof(copy) - 1 ? length : sizeof(copy) - 1;
    memory_copy(copy, path, n);
    copy[n] = 0;
    begin_line();
    text("KBM TRACE: texture via ");
    text(loader);
    text(": ");
    text(copy);
    flush_line();
}

/* Icons whose file bytes are already in memory are uploaded directly. */
static void texture_bytes_hook(void *context, const void *bytes, int size, unsigned char own,
                               void *options_pointer) {
    texture_diagnostic("upload", InterlockedIncrement(&texture_uploads), context, options_pointer, NULL);
    TextureOptions options;
    unsigned char *handle = (unsigned char *)context - 0x50;
    void *vtable = NULL;
    char path[160];
    unsigned button = BUTTON_NONE;
    if (prompts_installed && context && bytes && size > 0 && size <= 1 << 20 && options_pointer &&
        memory_read(options_pointer, &options, sizeof(options)) && options.length >= 16 &&
        options.length < sizeof(path) && memory_read(handle, &vtable, sizeof(vtable)) &&
        vtable == image + TEXTURE_HANDLE_VTABLE) {
        const char *text = options.capacity >= 16 ? *(const char **)options_pointer
                                                  : (const char *)options_pointer;
        if (memory_read(text, path, (size_t)options.length)) {
            path[options.length] = 0;
            trace_texture("upload", path, (size_t)options.length);
            if (text_contains(path, (size_t)options.length, "ControllerIcons")) {
                button = icon_button(path, (size_t)options.length);
            }
        }
    }
    if (button == BUTTON_NONE) {
        ((QueueTextureFn)hooks[HOOK_TEXTURE_BYTES].original)(context, bytes, size, own,
                                                             options_pointer);
        return;
    }
    AcquireSRWLockExclusive(&prompt_lock);
    PromptIcon *icon = NULL;
    for (LONG i = 0; i < prompt_count; ++i) {
        if (prompt_icons[i].handle == handle) {
            icon = &prompt_icons[i];
        }
    }
    if (!icon && prompt_count < (LONG)PROMPT_SLOTS) {
        icon = &prompt_icons[prompt_count];
        icon->handle = handle;
        InterlockedIncrement((volatile LONG *)(handle + 0x10));
        InterlockedIncrement(&prompt_count);
        if (prompt_count == 1) {
            log_append("KBM: first prompt icon texture seen; keyboard keys replace pad icons while "
                       "keyboard & mouse is active.\r\n");
        }
    }
    if (icon && icon->button && icon->button != button) {
        note_prompt_change(handle, icon->button, button, path);
    }
    if (icon) {
        icon->button = button;
        icon->options = options;
        memory_copy(icon->path, path, (size_t)options.length);
        icon->path[options.length] = 0;
        if (!icon->original || icon->original_size != size) {
            icon->original = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size);
            icon->original_size = icon->original ? size : 0;
        }
        if (icon->original) {
            memory_copy(icon->original, bytes, (size_t)size);
        }
    }
    int keys = icon && kbm_active;
    ReleaseSRWLockExclusive(&prompt_lock);
    if (keys && !own && submit_keycap(handle, button, icon->label)) {
        icon->shows_keys = 1; /* the pad icon upload is skipped while keys are shown */
        return;
    }
    if (icon) {
        icon->shows_keys = 0;
    }
    ((QueueTextureFn)hooks[HOOK_TEXTURE_BYTES].original)(context, bytes, size, own, options_pointer);
}

/* UI thread: show keys while the keyboard is the source, pad icons otherwise. */
static int same_label(const wchar_t *a, const wchar_t *b) {
    unsigned i = 0;
    while (a[i] && a[i] == b[i]) {
        ++i;
    }
    return a[i] == b[i];
}

static void prompts_tick(void) {
    if (!prompts_installed) {
        return;
    }
    int keys = kbm_active;
    /* World or menus: prompt textures are shared between world prompts and
       menu help bars, so every keycap is redrawn with the other labels when
       the racing context goes live or stops (one frame after the change, so a
       single-frame flicker does not redraw). Only handles whose text differs
       are drawn again. */
    static int candidate = -1, stable;
    /* A world screen that takes the UI buttons (Car Meet and its dialogs)
       shows the menu labels: Enter selects, Esc goes back. */
    int world = keys && in_world() && !world_overlay;
    if (world != candidate) {
        candidate = world;
        stable = 0;
    } else if (stable < 2) {
        ++stable;
    }
    {
        /* The page on top picks its FH6 labels (photo mode, My Cars). */
        unsigned char *top = top_page();
        void **vtable = NULL;
        int page = 0;
        if (keys && !world && top && memory_read(top, &vtable, sizeof(vtable)) && in_game_image(vtable, 8)) {
            page = class_derives(vtable, 0x33669e0u)                                      ? PAGE_PHOTO_LABELS
                   : class_derives(vtable, 0x32fb108u) || class_derives(vtable, 0x335a348u) ? PAGE_CARS_LABELS
                                                                                            : 0;
        }
        prompt_page = page;
    }
    if (stable >= 1 && prompt_world != candidate) {
        prompt_world = candidate;
        if (config.trace) log_append(candidate ? "KBM: prompt keys follow the world's actions.\r\n"
                             : "KBM: prompt keys follow the menus.\r\n");
    }
    /* One label per button per frame: the world lookup asks the game. */
    static wchar_t labels[64][16];
    unsigned char have[64] = {0};
    AcquireSRWLockExclusive(&prompt_lock);
    for (LONG i = 0; i < prompt_count; ++i) {
        PromptIcon *icon = &prompt_icons[i];
        if (keys && icon->shows_keys && icon->button < 64) {
            if (!have[icon->button]) {
                button_label(icon->button, labels[icon->button]);
                have[icon->button] = 1;
            }
            if (same_label(labels[icon->button], icon->label)) {
                continue;
            }
            submit_keycap(icon->handle, icon->button, icon->label);
            continue;
        }
        if (icon->shows_keys == keys) {
            continue;
        }
        if (keys) {
            icon->shows_keys = submit_keycap(icon->handle, icon->button, icon->label);
        } else {
            reload_original(icon);
            icon->shows_keys = 0;
        }
        if (!prompts_logged) {
            prompts_logged = 1;
            log_append(keys ? "KBM: button prompts switched to keyboard keys.\r\n"
                            : "KBM: button prompts switched to gamepad icons.\r\n");
        }
    }
    ReleaseSRWLockExclusive(&prompt_lock);
}

/* ---------- world overlays (Car Meet) ----------

   The Car Meet screen and its car card keep the racing context live and the
   simulation running, so in_world() holds: Esc is pause (UI Menu 0x54, asked
   by the world at 0x78b761) and the help bar is labelled with racing keys
   (world_prompt_binding: B -> ShiftUp's E, A -> Handbrake's Space). The
   screen itself takes its keys from the UI translator (0x142cdfb, callers
   0x142ceae/0x142cee1), which asks the UI button actions (A 0x4a, B 0x4d,
   ...); those were answered only by the menu keys (E answered RB 0x8a).

   Signal (query-driven, input thread): a world screen takes the UI buttons
   when, in the world, UI A or B was asked within OVERLAY_WINDOW_MS while no
   driving input (racing throttle 0, brake 1, steering 2) was asked in that
   time. While driving the car's input is asked every frame, so the overlay
   stays off however often the UI translator runs. While it is on:
   - Esc takes the back role (UI B) instead of pause;
   - the racing keys a world prompt shows for a button (world_prompt_binding,
     cached on the UI thread) also answer that button's UI actions, with
     their own consumed flags so a racing query cannot eat the press, and
     they stop answering the other buttons' UI actions (E is B, not RB), as
     the translator raises only one key per frame.
   Diagnostics: the UI button polls seen in the world (once per action and
   caller) and every change of the signal are logged, at most
   OVERLAY_LOG_LINES lines per session. */
#define WORLD_PROMPT_BUTTONS 10u
#define OVERLAY_WINDOW_MS 250u
#define OVERLAY_LOG_LINES 60
static const unsigned char world_prompt_menu[WORLD_PROMPT_BUTTONS] = {
    KBM_CONFIRM, KBM_BACK, KBM_MENU_X, KBM_MENU_Y, KBM_PAUSE, KBM_MAP,
    KBM_TAB_LEFT, KBM_TAB_RIGHT, KBM_MENU_LEFT_TRIGGER, KBM_MENU_RIGHT_TRIGGER};
static const unsigned char world_prompt_icon[WORLD_PROMPT_BUTTONS] = {
    BUTTON_A, BUTTON_B, BUTTON_X, BUTTON_Y, BUTTON_MENU, BUTTON_VIEW,
    BUTTON_LB, BUTTON_RB, BUTTON_LT, BUTTON_RT};
/* The press action of each button's UI family. */
static const unsigned char world_prompt_press[WORLD_PROMPT_BUTTONS] = {
    0x4a, 0x4d, 0x8d, 0x90, 0x54, 0x50, 0x87, 0x8a, 0x81, 0x84};
static volatile signed char world_prompt_racing[WORLD_PROMPT_BUTTONS] = {-1, -1, -1, -1, -1,
                                                                         -1, -1, -1, -1, -1};
static volatile LONG64 ui_button_polled_at, driving_polled_at;
static volatile int world_now;
static volatile LONG overlay_log_lines;
static SRWLOCK poll_log_lock = SRWLOCK_INIT;

static int overlay_log_allowed(void) {
    return InterlockedIncrement(&overlay_log_lines) <= OVERLAY_LOG_LINES;
}

static void note_poll(unsigned context, unsigned action, const void *caller) {
    if (context == CONTEXT_RACING && action <= ACTION_STEERING) {
        InterlockedExchange64(&driving_polled_at, (LONG64)GetTickCount64());
        return;
    }
    if (context != CONTEXT_UI) {
        return;
    }
    unsigned button = WORLD_PROMPT_BUTTONS;
    for (unsigned i = 0; i < WORLD_PROMPT_BUTTONS; ++i) {
        button = world_prompt_press[i] == action ? i : button;
    }
    if (button == WORLD_PROMPT_BUTTONS) {
        return;
    }
    if (button <= 1) {
        InterlockedExchange64(&ui_button_polled_at, (LONG64)GetTickCount64());
    }
    if (!world_now || overlay_log_lines >= OVERLAY_LOG_LINES) {
        return;
    }
    /* Once per (action, caller) while in the world. */
    static struct {
        unsigned action;
        uintptr_t caller;
    } seen[32];
    static unsigned seen_count;
    uintptr_t at = (uintptr_t)caller - (uintptr_t)image;
    int fresh = 1;
    AcquireSRWLockExclusive(&poll_log_lock);
    for (unsigned i = 0; i < seen_count && fresh; ++i) {
        fresh = !(seen[i].action == action && seen[i].caller == at);
    }
    if (fresh && seen_count < ARRAY_COUNT(seen)) {
        seen[seen_count].action = action;
        seen[seen_count].caller = at;
        ++seen_count;
    } else {
        fresh = 0;
    }
    ReleaseSRWLockExclusive(&poll_log_lock);
    if (fresh && overlay_log_allowed()) {
        begin_line();
        text("KBM: world asks UI ");
        text(prompt_names[button]);
        text(" action ");
        number(action, 16);
        text(" (caller ");
        number(at, 16);
        text(").");
        flush_line();
    }
}

/* UI thread: the racing binding each button's world prompt names. */
static void world_prompts_tick(void) {
    static uint64_t last_scan;
    static signed char logged[WORLD_PROMPT_BUTTONS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    uint64_t now = GetTickCount64();
    /* An overlay shows menu labels, so the world keys stop standing in for
       its buttons: Enter and Esc work there as in any menu. */
    if (!prompt_world || !kbm_active) {
        for (unsigned i = 0; i < WORLD_PROMPT_BUTTONS; ++i) {
            world_prompt_racing[i] = -1;
        }
        return;
    }
    if (now - last_scan < 100u) {
        return;
    }
    last_scan = now;
    int changed = 0;
    for (unsigned i = 0; i < WORLD_PROMPT_BUTTONS; ++i) {
        int racing = world_prompt_binding(world_prompt_icon[i]);
        world_prompt_racing[i] = (signed char)(racing >= 0 && racing < (int)KBM_BINDING_COUNT ? racing : -1);
        changed |= world_prompt_racing[i] != logged[i];
    }
    if (changed && overlay_log_allowed()) {
        begin_line();
        text("KBM: world prompt keys:");
        for (unsigned i = 0; i < WORLD_PROMPT_BUTTONS; ++i) {
            logged[i] = world_prompt_racing[i];
            if (logged[i] >= 0) {
                text(" ");
                text(prompt_names[i]);
                text("=");
                text(kbm_config_binding_name((unsigned)logged[i]));
            }
        }
        flush_line();
    }
}

/* Input thread, start of each input tick (before Esc takes its role). */
static void world_overlay_tick(uint64_t now) {
    static int logged_state = -1;
    int world = in_world();
    world_now = world;
    int ui = now - (uint64_t)ui_button_polled_at < OVERLAY_WINDOW_MS;
    int driving = now - (uint64_t)driving_polled_at < OVERLAY_WINDOW_MS;
    /* In game (2026-10-22 log): plain driving never asks UI A/B, while Car
       Meet asks them through the UI key translator and still asks the
       driving input, so UI A/B alone marks the overlay; driving is only logged. */
    int overlay = world && ui;
    int state = world ? 1 | ui << 1 | driving << 2 : 0;
    if (state != logged_state && (world || logged_state > 0) && overlay_log_allowed()) {
        begin_line();
        if (!world) {
            text("KBM: world overlay: left the world.");
        } else {
            text("KBM: world overlay ");
            text(overlay ? "ON" : "off");
            text(": UI A/B asked ");
            text(ui ? "yes" : "no");
            text(", driving input asked ");
            text(driving ? "yes" : "no");
            text(overlay ? "; Esc is back, B=" : "; Esc is pause.");
            if (overlay) {
                text(world_prompt_racing[1] >= 0 ? kbm_config_binding_name((unsigned)world_prompt_racing[1])
                                                 : "-");
                text(" A=");
                text(world_prompt_racing[0] >= 0 ? kbm_config_binding_name((unsigned)world_prompt_racing[0])
                                                 : "-");
            }
        }
        flush_line();
    }
    logged_state = state;
    world_overlay = overlay;
}

static const unsigned short *world_prompt_keys(unsigned context, unsigned action) {
    if (!world_overlay || context != CONTEXT_UI || action >= ACTIONS) {
        return NULL;
    }
    unsigned binding = action_binding[CONTEXT_UI][action];
    /* Car Meet changes the station with D-pad left/right only: the arrows
       (stick directions in menus) answer them too, as the numpad keys do. */
    if (binding - 1 == KBM_DPAD_LEFT || binding - 1 == KBM_DPAD_RIGHT) {
        return binding_list[binding - 1 == KBM_DPAD_LEFT ? KBM_LEFT : KBM_RIGHT];
    }
    if (binding - 1 == KBM_LEFT || binding - 1 == KBM_RIGHT) {
        return binding_list[binding - 1 == KBM_LEFT ? KBM_DPAD_LEFT : KBM_DPAD_RIGHT];
    }
    for (unsigned i = 0; binding && i < WORLD_PROMPT_BUTTONS; ++i) {
        int racing = world_prompt_racing[i];
        if (world_prompt_menu[i] == binding - 1 && racing >= 0) {
            return binding_list[racing];
        }
    }
    return NULL;
}

/* A key another button's world prompt shows does not answer this button. */
static int world_prompt_steals(unsigned context, unsigned action, unsigned code) {
    if (!world_overlay || context != CONTEXT_UI || action >= ACTIONS) {
        return 0;
    }
    unsigned binding = action_binding[CONTEXT_UI][action], own = WORLD_PROMPT_BUTTONS;
    for (unsigned i = 0; binding && i < WORLD_PROMPT_BUTTONS; ++i) {
        own = world_prompt_menu[i] == binding - 1 ? i : own;
    }
    if (own == WORLD_PROMPT_BUTTONS) {
        return 0; /* directions and other families keep their keys */
    }
    for (unsigned i = 0; i < WORLD_PROMPT_BUTTONS; ++i) {
        int racing = world_prompt_racing[i];
        if (i == own || racing < 0) {
            continue;
        }
        for (unsigned k = 0; k < BINDING_KEYS; ++k) {
            if (binding_list[racing][k] == code) {
                return 1;
            }
        }
    }
    return 0;
}

/* ---------- the input tick, after the game's own pad update ---------- */

/* Pause loop (pad and keyboard alike). WaitForPause (0x964810) asks
   0x9635f0 every frame whether to pause, and that is true while the app
   object [0x34aba60] says the window is hidden (+0x19e == 0, 0x963661) or,
   outside the case at 0x963675, inactive (+0x19d == 0, 0x963693). Those bytes
   change only from the window's Visibility (0x223b10) and Activated
   (0x223b50: CodeActivated/PointerActivated -> 1, Deactivated -> 0) events.
   When an activation is missed the flag stays 0 with the game in the
   foreground: the pause menu opens by itself and every Back re-pauses at
   once, until a focus-out and focus-in delivers a new Activated (QA
   2026-10-23). Once the game window has been the foreground window for a full
   second with a flag still 0, the flag is set to 1, the very write the
   game's own Activated / Visible handlers make. Real deactivation (another
   window in front) is never touched. */
#define APP_WINDOW_STATE 0x34aba60u
#define APP_ACTIVATED 0x19du
#define APP_VISIBLE 0x19eu
#define APP_RESYNC_MS 1000u

static void window_state_resync(uint64_t now, int focused) {
    static uint64_t focused_since;
    unsigned char *app = NULL;
    unsigned char flags[2] = {1, 1};
    if (!focused) {
        focused_since = 0;
        return;
    }
    if (!focused_since) {
        focused_since = now;
    }
    if (now - focused_since < APP_RESYNC_MS || !memory_read(image + APP_WINDOW_STATE, &app, sizeof(app)) ||
        !app || !memory_read(app + APP_ACTIVATED, flags, sizeof(flags)) || (flags[0] && flags[1]) ||
        !memory_is_writable(app + APP_ACTIVATED, sizeof(flags))) {
        return;
    }
    *(volatile unsigned char *)(app + APP_ACTIVATED) = 1;
    *(volatile unsigned char *)(app + APP_VISIBLE) = 1;
    begin_line();
    text("KBM: the game window is in front but the game still had it as ");
    text(!flags[1] ? (!flags[0] ? "hidden and inactive" : "hidden") : "inactive");
    text("; restored (stops the pause menu reopening by itself).");
    flush_line();
}

/* The pause menu and the world map stop the simulation; the pointer is
   offered only then (not on the title, in the garage hub or while driving). */
static int simulation_paused(void) {
    void *game, *simulation;
    unsigned char paused;
    return memory_read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game) &&
           memory_read_pointer((unsigned char *)game + 0x48, &simulation) &&
           memory_read((unsigned char *)simulation + 0x28, &paused, 1) && paused;
}

static int in_world(void);

/* Pause menu or map: the PauseGame state was entered and driving has not
   resumed, or the simulation is flagged paused. */
static int pairing_player_signed_in(void);
static int pointer_gate(void) {
    /* Pause menu, map, garage/festival hubs and every other menu: whenever
       the racing context is not live. Not on the title (no player yet). */
    return !in_world() && (pause_entered || simulation_paused() || pairing_player_signed_in());
}

static int key_physical(unsigned code) {
    if (code == DIALOG_DOWN || code == DIALOG_CONFIRM || code == WHEEL_UP || code == TAB_PREVIOUS ||
        code == TAB_NEXT) {
        return -1;
    }
    return kbm_win32_key_down(code == ESCAPE_AS_PAUSE || code == ESCAPE_AS_BACK ? ESCAPE_SCAN
                              : code == RIGHT_AS_BACK                         ? KBM_MOUSE_RIGHT
                                                                              : code);
}

/* Driving in the world: the racing context is live and the simulation runs. */
static int in_world(void) {
    void *routed = routed_controller;
    return routed && context_allowed(routed, CONTEXT_RACING) && simulation_running();
}

/* Esc takes its role when pressed and keeps it until released. */
static unsigned map_right_button(int down) {
    if (down) {
        /* Not back: as in FH4/FH5 on PC, Esc and Backspace go back; the right
           button only drags the car viewer. */
        right_role = 0;
        if (right_role) {
            /* In a yes/no popup or popup menu a right click closes it as Esc does. */
            dialog_escape_pressed(GetTickCount64());
        }
    }
    return right_role;
}

static unsigned map_escape(unsigned code, int down) {
    if (code != ESCAPE_SCAN || !config.escape) {
        return code;
    }
    if (down) {
        /* A world screen taking the UI buttons (Car Meet) takes Esc as back. */
        escape_role = in_world() && !world_overlay ? ESCAPE_AS_PAUSE : ESCAPE_AS_BACK;
        if (escape_role == ESCAPE_AS_BACK) {
            dialog_escape_pressed(GetTickCount64());
        }
    }
    return escape_role ? escape_role : ESCAPE_AS_BACK;
}

static void neutral_reset(int block_held_keys) {
    for (unsigned code = 0; code < 256; ++code) {
        if (block_held_keys && (key_held[code] || key_physical(code) == 1)) {
            key_blocked[code] = 1;
        }
        key_held[code] = key_tapped[code] = 0;
        key_current[code] = key_previous[code] = key_consumed[code] = 0;
    }
    steering = 0.0f;
    mouse_yaw = 0.0f;
    mouse_window_counts = 0;
    long dx, dy;
    kbm_win32_take_mouse(&dx, &dy);
    for (unsigned context = 0; context < CONTEXTS; ++context) {
        for (unsigned action = 0; action < ACTIONS; ++action) {
            repeat_deadline[context][action] = 0;
        }
    }
}

static void log_source(const char *reason) {
    begin_line();
    text(source == SOURCE_KBM ? "KBM: input source -> keyboard & mouse ("
                              : "KBM: input source -> gamepad (");
    text(reason);
    text(").");
    flush_line();
}

static void select_source(Source next, const char *reason, unsigned trigger_key, uint64_t now) {
    if (source == next) {
        return;
    }
    source = next;
    last_switch = now;
    if (next == SOURCE_KBM) {
        /* Keys held before the hand-over stay ignored until pressed again. */
        for (unsigned code = 1; code < 256; ++code) {
            if (code != trigger_key && key_held[code] && !key_tapped[code]) {
                key_blocked[code] = 1;
            }
        }
        /* The press that ends a gamepad user's controller-disconnected page
           only closes it; it does nothing in the game (no throttle, no A). */
        if (trigger_key && now - (uint64_t)pairing_waiting_at < 500u) {
            key_blocked[trigger_key] = 1;
            key_tapped[trigger_key] = 0;
            log_append("KBM: the key that closed the controller-disconnected page is held back.\r\n");
        }
        steering = 0.0f;
        mouse_yaw = 0.0f;
    } else {
        neutral_reset(0);
        dialog_cancel.box = NULL;
    }
    log_source(reason);
}

/* A key that can do something now: bound in a context that the routed
   controller or the any-controller object currently allows. */
static int key_meaningful(unsigned char *owner, unsigned code) {
    unsigned contexts = key_contexts[code];
    void *routed = routed_controller;
    for (unsigned context = 0; contexts && context < CONTEXTS; ++context) {
        if (contexts >> context & 1 &&
            (context_allowed(controller_at(owner, CONTROLLER_SLOTS), context) ||
             context_allowed(routed, context))) {
            return 1;
        }
    }
    return 0;
}

static void route_controller(unsigned char *owner, int assign) {
    void *selected = NULL;
    memory_read(owner + MANAGER_SELECTED, &selected, sizeof(selected));
    if (selected || !assign) {
        routed_controller = selected;
        return;
    }
    /* No pad has been used yet. Point the game's "last active controller"
       at an idle controller object so the local player's actions (whose
       controller lookup falls back to manager+0xab28) reach the keyboard.
       The slot keeps its empty pad state; a real pad replaces it the moment
       the game sees that pad's input. */
    unsigned chosen = CONTROLLER_SLOTS;
    for (unsigned index = 0; index < CONTROLLER_SLOTS; ++index) {
        unsigned char *candidate = controller_at(owner, index);
        void *gamepad = NULL;
        memory_read(candidate + CONTROLLER_GAMEPAD, &gamepad, sizeof(gamepad));
        if (!gamepad && context_allowed(candidate, CONTEXT_UI)) {
            chosen = index;
            break;
        }
    }
    unsigned char *target = controller_at(owner, chosen);
    if (!memory_is_writable(owner + MANAGER_SELECTED, sizeof(void *))) {
        return;
    }
    *(void *volatile *)(owner + MANAGER_SELECTED) = target;
    routed_controller = target;
    if (routed_logged_slot != (int)chosen) {
        routed_logged_slot = (int)chosen;
        begin_line();
        text("KBM: no gamepad controller was active; keyboard & mouse actions use controller "
             "object ");
        text(chosen == CONTROLLER_SLOTS ? "any (+0x9810)" : "slot ");
        if (chosen != CONTROLLER_SLOTS) {
            number(chosen, 10);
        }
        text(".");
        flush_line();
    }
}

/* Keyboard-only driving (garage6). The car's input (0x2fd8xx) takes the
   player's controller object and, unless its connected byte (+4) is set
   (0x2fd9e3), feeds the car neutral input without a single action query;
   the pause flow (0x9648ed) and the per-frame player check (0x228bb1) read
   the same byte. With keyboard & mouse the active source and no pad attached
   at all, the idle controller object routed to the keyboard (manager+0xab28,
   no gamepad behind it) is marked connected after each input-manager update,
   and the mark is taken back as soon as a pad appears or the pad becomes the
   source. pad_attached() still needs a gamepad, so it never counts as a pad. */
static unsigned char *connected_forced;

static void keyboard_controller_connected(unsigned char *owner, int want) {
    unsigned char *target = NULL;
    void *gamepad = (void *)1;
    if (want && owner) {
        memory_read(owner + MANAGER_SELECTED, &target, sizeof(target));
        if (target && (!memory_read(target + CONTROLLER_GAMEPAD, &gamepad, sizeof(gamepad)) || gamepad)) {
            target = NULL; /* a real pad's object is the game's */
        }
    }
    if (connected_forced && connected_forced != target) {
        void *pad = (void *)1;
        if (memory_read(connected_forced + CONTROLLER_GAMEPAD, &pad, sizeof(pad)) && !pad &&
            memory_is_writable(connected_forced + CONTROLLER_CONNECTED, 1)) {
            *(volatile unsigned char *)(connected_forced + CONTROLLER_CONNECTED) = 0;
        }
        log_append("KBM: keyboard controller object no longer marked connected (pad or source change).\r\n");
        connected_forced = NULL;
    }
    if (!target || !memory_is_writable(target + CONTROLLER_CONNECTED, 1)) {
        return;
    }
    if (*(volatile unsigned char *)(target + CONTROLLER_CONNECTED) != 1) {
        *(volatile unsigned char *)(target + CONTROLLER_CONNECTED) = 1;
    }
    if (connected_forced != target) {
        connected_forced = target;
        log_append("KBM: keyboard controller object marked connected so the car takes keyboard input "
                   "(no pad attached).\r\n");
    }
}

/* Gamepad intent, observed from the game's own per-tick pad state. Only a
   0->1 edge seen on two consecutive focused ticks of an attached pad counts:
   bits that are set when a pad (or a virtual pad) appears, while the window
   was inactive (Azimuth then reports empty readings), or that never clear,
   never select the gamepad. */
static unsigned char pad_baseline[CONTROLLER_SLOTS], activity_baseline;
static char pad_reason[] = "gamepad slot 0 button 0x0000";

static void pad_intent(unsigned char *owner, int focused, int *button, int *axis) {
    unsigned char activity = 0;
    *button = *axis = 0;
    memory_read(owner + MANAGER_PAD_ACTIVITY, &activity, 1);
    for (unsigned index = 0; index < CONTROLLER_SLOTS; ++index) {
        unsigned char *controller = controller_at(owner, index);
        unsigned short buttons = 0;
        int attached = pad_attached(controller);
        if (attached) {
            memory_read(controller + CONTROLLER_BUTTONS, &buttons, sizeof(buttons));
        }
        if (!attached) {
            pad_attached_at[index] = 0;
        } else if (!pad_attached_at[index]) {
            pad_attached_at[index] = GetTickCount64();
        }
        /* The first two seconds after a pad appears are its own start-up. */
        int settled = attached && GetTickCount64() - pad_attached_at[index] >= 2000;
        if (pad_baseline[index] && settled && focused &&
            (buttons & ~previous_pad_buttons[index])) {
            *button = 1;
            static const char digits[] = "0123456789abcdef";
            unsigned rising = buttons & ~previous_pad_buttons[index];
            char *cursor = pad_reason + 24; /* after "gamepad slot N button 0x" */
            pad_reason[13] = (char)('0' + index);
            for (int shift = 12; shift >= 0; shift -= 4) {
                *cursor++ = digits[(rising >> shift) & 15];
            }
        }
        previous_pad_buttons[index] = buttons;
        pad_baseline[index] = (unsigned char)(attached && focused);
    }
    /* manager+0xab98 is set by the pad update when any pad is outside the
       game's deadzones (triggers > 30, sticks > 7849/8689); its rising edge
       is a new stick or trigger movement, a held stick is not. */
    if (activity_baseline && focused && activity && !previous_pad_activity) {
        *axis = 1;
        /* Name the pad: triggers +0xc2/+0xc3, sticks +0xc4..+0xca of the copy
           of its reading at +0xbc. */
        for (unsigned index = 0; index < CONTROLLER_SLOTS; ++index) {
            unsigned char *controller = controller_at(owner, index);
            unsigned char triggers[2] = {0, 0};
            short sticks[4] = {0, 0, 0, 0};
            if (!pad_attached(controller)) {
                continue;
            }
            memory_read(controller + 0xc2, triggers, sizeof(triggers));
            memory_read(controller + 0xc4, sticks, sizeof(sticks));
            begin_line();
            text("KBM: gamepad slot ");
            number(index, 10);
            text(" activity: LT ");
            number(triggers[0], 10);
            text(" RT ");
            number(triggers[1], 10);
            for (unsigned k = 0; k < 4; ++k) {
                static const char *const names[] = {" LX ", " LY ", " RX ", " RY "};
                text(names[k]);
                if (sticks[k] < 0) {
                    text("-");
                }
                number((uint64_t)(sticks[k] < 0 ? -sticks[k] : sticks[k]), 10);
            }
            flush_line();
        }
    }
    previous_pad_activity = activity;
    activity_baseline = (unsigned char)focused;
}

static void collect_keys(uint64_t now, int focused, unsigned *pressed_key) {
    KbmKeyEvent event;
    *pressed_key = 0;
    if (kbm_win32_state() == KBM_WIN32_POLLING) {
        for (unsigned code = 1; code < 256; ++code) {
            if (!key_contexts[code] || code == ESCAPE_AS_PAUSE || code == ESCAPE_AS_BACK) {
                continue;
            }
            int down = focused && key_physical(code) == 1;
            if (down && !key_held[code]) {
                key_held[code] = key_tapped[code] = 1;
                key_pressed_at[code] = now;
                *pressed_key = code;
            } else if (!down) {
                key_held[code] = 0;
            }
        }
        if (config.escape) {
            int down = focused && kbm_win32_key_down(ESCAPE_SCAN) == 1;
            unsigned role = escape_role;
            if (down && (!role || !key_held[role])) {
                role = map_escape(ESCAPE_SCAN, 1);
                key_held[role] = key_tapped[role] = 1;
                key_pressed_at[role] = now;
                *pressed_key = role;
            } else if (!down && role) {
                key_held[role] = 0;
            }
        }
        return;
    }
    while (kbm_win32_next_event(&event)) {
        unsigned code = map_escape(event.code & 0xffu, event.down);
        if (code == KBM_MOUSE_LEFT) {
            mouse_left_held = event.down; /* car viewer drag (garage11) */
        }
        if (code == KBM_MOUSE_RIGHT) {
            mouse_right_held = event.down; /* car viewer drag when right is not back */
        }
        if (code == KBM_MOUSE_LEFT || code == KBM_MOUSE_MIDDLE) {
            queue_pointer_button(code, event.down);
            continue;
        }
        if (code == KBM_MOUSE_RIGHT) {
            code = map_right_button(event.down);
            if (!code) {
                continue;
            }
        }
        if (event.down) {
            if (!key_held[code]) {
                key_held[code] = key_tapped[code] = 1;
                key_pressed_at[code] = now;
                if (key_contexts[code]) {
                    *pressed_key = code;
                }
            }
        } else {
            key_held[code] = 0;
        }
    }
    /* A release lost while the window was inactive must not leave a key
       held: trust the physical key state once the press is not brand new. */
    for (unsigned code = 1; code < 256; ++code) {
        if (key_held[code] && now - key_pressed_at[code] > STUCK_KEY_CHECK_MS &&
            key_physical(code) == 0) {
            key_held[code] = 0;
        }
    }
}

static void update_steering(float dt) {
    float target = (float)(binding_down(KBM_STEER_RIGHT) - binding_down(KBM_STEER_LEFT));
    if (config.invert_steering) {
        target = -target;
    }
    float scale = (float)config.steering_speed / 100.0f;
    if (target == 0.0f) {
        float step = STEER_RELEASE_RATE * scale * dt;
        steering = steering > step ? steering - step : steering < -step ? steering + step : 0.0f;
        return;
    }
    if (steering * target < 0.0f) {
        steering = 0.0f; /* reversing starts from the centre at once */
    }
    float step = STEER_PRESS_RATE * scale * dt;
    steering += target > 0.0f ? step : -step;
    if (steering > 1.0f) {
        steering = 1.0f;
    } else if (steering < -1.0f) {
        steering = -1.0f;
    }
}

static void update_mouse(uint64_t now, float dt, int focused, int *meaningful) {
    long dx, dy;
    kbm_win32_take_mouse(&dx, &dy);
    *meaningful = 0;
    void *routed = routed_controller;
    int world = routed && context_allowed(routed, CONTEXT_RACING) && simulation_running();
    int look = config.mouse_look && kbm_win32_raw_mouse() && focused && world;
    int pointer = pointer_installed && kbm_win32_raw_mouse() && focused && pointer_gate();
    {
        /* MapCursorSpeed 100 = 15 counts per tick for a full stick; the default
           400 is four times that. */
        float gain = (float)config.map_cursor_speed / 1500.0f;
        int live = pointer && source == SOURCE_KBM;
        float tx = live ? (float)dx * gain : 0.0f, ty = live ? -(float)dy * gain : 0.0f;
        /* Movement sets the stick at once; once the mouse stops it glides
           out (time constant about 0.12 s) instead of stopping dead. */
        float glide = dt > 0.0f && dt < 0.5f ? 1.0f - dt / 0.12f : 0.0f;
        glide = glide < 0.0f ? 0.0f : glide;
        map_mouse_x = dx || !live ? tx : map_mouse_x * glide;
        map_mouse_y = dy || !live ? ty : map_mouse_y * glide;
        map_mouse_live = live;
        /* Car viewer orbit: a left-button drag is the right stick (about 12
           counts per tick for a full stick), gliding out like the map's. */
        int drag = live && (mouse_right_held || (mouse_left_held && !color_on_top));
        /* "Drag the car": right turns it right, up tilts it up (the camera goes lower). */
        float ox = drag ? (float)dx / 12.0f : 0.0f, oy = drag ? (float)dy / 12.0f : 0.0f;
        if (config.invert_mouse_y) {
            oy = -oy; /* Invert mouse Y */
        }
        ox = ox > 1.0f ? 1.0f : ox < -1.0f ? -1.0f : ox;
        oy = oy > 1.0f ? 1.0f : oy < -1.0f ? -1.0f : oy;
        orbit_x = dx || !drag ? ox : orbit_x * glide;
        orbit_y = dy || !drag ? oy : orbit_y * glide;
        orbit_live = live;
    }
    {
        /* Photo mode: plain mouse motion is the right stick (MouseSensitivity
           80 = about 12 counts per tick for a full stick), gliding out. */
        /* Only while the right button is held, as in free roam; otherwise the
           pointer stays free for the help-bar prompts. */
        int photo = photo_active() && focused && source == SOURCE_KBM && kbm_win32_raw_mouse() &&
                    mouse_right_held;
        float scale = (float)config.mouse_sensitivity / (80.0f * 12.0f);
        float px = photo ? (float)dx * scale : 0.0f, py = photo ? -(float)dy * scale : 0.0f;
        if (config.invert_mouse_y) {
            py = -py; /* Invert mouse Y */
        }
        px = px > 1.0f ? 1.0f : px < -1.0f ? -1.0f : px;
        py = py > 1.0f ? 1.0f : py < -1.0f ? -1.0f : py;
        float glide = dt > 0.0f && dt < 0.5f ? 1.0f - dt / 0.12f : 0.0f;
        glide = glide < 0.0f ? 0.0f : glide;
        photo_x = dx || !photo ? px : photo_x * glide;
        photo_y = dy || !photo ? py : photo_y * glide;
        if (photo) {
            kbm_win32_set_capture(config.mouse_capture); /* the pointer is not needed there */
        }
    }
    long counts = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (now - mouse_window_start > MOUSE_INTENT_WINDOW_MS) {
        mouse_window_start = now;
        mouse_window_counts = 0;
    }
    /* garage20: while driving, mouse motion is keyboard & mouse intent only
       as a right-button look (FH6); a bumped or noisy mouse never takes the
       car from the pad. The menu pointer counts once the world has been gone
       for a moment (or the pause menu / map is open), so a brief racing
       context flicker while driving does not count as a menu. */
    static uint64_t world_seen_at;
    if (world) {
        world_seen_at = now;
    }
    int look_drag = look && mouse_right_held;
    int menu_pointer = pointer && !world &&
                       (pause_entered || simulation_paused() || now - world_seen_at > MOUSE_WORLD_GRACE_MS);
    mouse_window_counts += look_drag || menu_pointer ? counts : 0;
    if (mouse_window_counts >= MOUSE_INTENT_COUNTS) {
        *meaningful = 1;
        mouse_intent_why = look_drag ? "mouse: right-button look" : "mouse: menu pointer";
    }
    /* Mouse look pitch probe (kbm_pitch.c, MouseLookPitch=1). */
    if (config.trace) {
        kbm_pitch_debug_look_back(look && binding_down(KBM_LOOK_BACK));
    }
    /* FH6: the mouse looks around only while the right button is held; the
       view returns to centre after it is let go. */
    if (look && !mouse_right_held) {
        dx = 0;
        dy = 0;
    }
    kbm_pitch_update(&config, dy, look && (source == SOURCE_KBM || *meaningful) &&
                                      !binding_down(KBM_LOOK_BACK), now, dt);
    if (!look) {
        mouse_yaw = 0.0f;
        if (!photo_active() || !mouse_right_held) {
            kbm_win32_set_capture(0);
        }
        return;
    }
    if (source != SOURCE_KBM && !*meaningful) {
        kbm_win32_set_capture(0);
        return;
    }
    kbm_win32_set_capture(config.mouse_capture);
    if (binding_down(KBM_LOOK_BACK)) {
        mouse_yaw = 0.0f; /* the native rear view has priority */
        return;
    }
    float yaw = mouse_yaw;
    if (dx) {
        yaw += (float)dx * (float)config.mouse_sensitivity * (PI_F / 180000.0f);
        last_mouse_move = now;
    } else if (now - last_mouse_move >= (uint64_t)config.mouse_return_delay) {
        float step = (float)config.mouse_return_speed * (PI_F / 180.0f) * dt;
        yaw = yaw > step ? yaw - step : yaw < -step ? yaw + step : 0.0f;
    }
    float limit = look_limit > 0.0f ? look_limit : PI_F;
    mouse_yaw = yaw > limit ? limit : yaw < -limit ? -limit : yaw;
}

/* Menu pointer keys, on the input tick: a left click is a short A tap and
   each wheel notch an Up or Down tap (lists and tile hubs scroll by them). */
static uint64_t pointer_confirm_release;
static int wheel_steps;
static unsigned wheel_code;
static long wheel_rest;
static uint64_t wheel_until;

static int tab_steps;
static unsigned tab_code;
static uint64_t tab_until;

static void pointer_keys(uint64_t now) {
    /* Tab clicks: one LB/RB tap per group, 60 ms down, 160 ms between. */
    tab_steps += (int)InterlockedExchange(&tab_request, 0);
    tab_steps = tab_steps > 12 ? 12 : tab_steps < -12 ? -12 : tab_steps;
    if (tab_code && now >= tab_until) {
        key_held[tab_code] = 0;
        tab_code = 0;
        tab_until = now + 160u;
    } else if (!tab_code && tab_steps && now >= tab_until) {
        tab_code = tab_steps > 0 ? TAB_NEXT : TAB_PREVIOUS;
        tab_steps += tab_steps > 0 ? -1 : 1;
        key_held[tab_code] = key_tapped[tab_code] = 1;
        key_pressed_at[tab_code] = now;
        tab_until = now + 60u;
    }
    if (pointer_confirm_release && now >= pointer_confirm_release) {
        key_held[DIALOG_CONFIRM] = 0;
        pointer_confirm_release = 0;
    }
    if (InterlockedExchange(&pointer_confirm, 0) && !dialog_cancel.box) {
        key_held[DIALOG_CONFIRM] = key_tapped[DIALOG_CONFIRM] = 1;
        key_pressed_at[DIALOG_CONFIRM] = now;
        pointer_confirm_release = now + 60u;
    }
    long wheel = kbm_win32_take_wheel();
    if (wheel && viewer_active() && !photo_active() && !popup_on_top) {
        long notches = (wheel < 0 ? -wheel : wheel) / 120 + 1;
        viewer_zoom_in = wheel > 0;
        InterlockedExchange64(&viewer_zoom_until, (LONG64)(now + 120u * (uint64_t)notches));
        wheel = 0;
    }
    if (wheel && photo_active() && !popup_on_top) {
        /* Photo mode: the wheel drives the zoom axis 0x24 like the map's
           zoom: each notch adds 0.6 (clamped), easing out below. */
        float zoom = photo_zoom + (float)wheel / 120.0f * 0.6f;
        photo_zoom = zoom > 1.0f ? 1.0f : zoom < -1.0f ? -1.0f : zoom;
        wheel = 0;
    }
    int popup_wheel_wanted = popup_on_top != 0; /* a popup over the map takes the wheel */
    if (pointer_mode && wheel && (!map_shown() || popup_wheel_wanted)) {
        /* Lists and hubs: AVUI's own wheel (MouseInputProvider, sent by the
           pump) and the navigation vector the arrows also drive. */
        InterlockedExchangeAdd(&avui_wheel, wheel);
        /* Not while STATYSTYKI is on top: the pump scrolls its list, and the
           pause hub behind it still updates its navigation. */
        if (!stats_on_top && !popup_wheel_wanted && !color_on_top) {
            long notches = (wheel < 0 ? -wheel : wheel) / 120 + 1;
            nav_wheel_direction = wheel > 0 ? -1.0f : 1.0f;
            InterlockedExchange64(&nav_wheel_until, (LONG64)(now + 140u * (uint64_t)notches));
        }
        wheel = 0;
    }
    {
        /* Map zoom: each notch adds 0.6 to a zoom velocity (clamped to +-1)
           that decays exponentially (time constant about 0.3 s); axis 0x72
           answers that value, so the wheel eases like a trigger. */
        static uint64_t zoom_last;
        float elapsed = zoom_last ? (float)(now - zoom_last) / 1000.0f : 0.0f;
        zoom_last = now;
        float keep = elapsed < 0.3f ? 1.0f - elapsed / 0.3f : 0.0f;
        float zoom = map_zoom * (keep < 0.0f ? 0.0f : keep);
        if (pointer_mode && map_shown() && wheel) {
            zoom += (float)wheel / 120.0f * 0.6f;
        }
        zoom = zoom > 1.0f ? 1.0f : zoom < -1.0f ? -1.0f : zoom;
        map_zoom = zoom > -0.02f && zoom < 0.02f ? 0.0f : zoom;
        (void)map_zoom_until;
        /* The photo camera's zoom axis eases out the same way. */
        float photo = photo_zoom * (keep < 0.0f ? 0.0f : keep);
        photo_zoom = photo > -0.02f && photo < 0.02f ? 0.0f : photo;
    }
    if (pointer_mode && map_shown()) {
        wheel = 0;
    }
    if (!pointer_mode) {
        wheel_steps = 0;
        wheel_rest = 0;
        if (wheel_code) {
            key_held[wheel_code] = 0;
            wheel_code = 0;
        }
        return;
    }
    (void)wheel_rest;
    (void)wheel_steps;
    (void)wheel_code;
    (void)wheel_until;
}

static void input_tick(unsigned char *owner, float game_dt) {
    uint64_t now = GetTickCount64();
    float dt = game_dt;
    ++input_ticks;
    if (!float_is_finite(dt) || dt <= 0.0f || dt > 0.25f) {
        dt = last_tick && now > last_tick ? (float)(now - last_tick) * 0.001f : 0.016f;
        dt = dt > 0.25f ? 0.25f : dt;
    }
    last_tick = now;
    world_overlay_tick(now);
    KbmWin32State win32 = kbm_win32_state();
    if (win32 == KBM_WIN32_FAILED) {
        kbm_active = 0;
        return;
    }

    int prompt = kbm_win32_host_prompt();
    int focused = kbm_win32_focused();
    if (prompt != was_prompt && was_prompt != -1) {
        log_append(prompt ? "KBM: Azimuth prompt open; keyboard & mouse actions neutral.\r\n"
                          : "KBM: Azimuth prompt closed.\r\n");
    }
    was_prompt = prompt;
    if (focused != was_focused) {
        if (was_focused != -1 && !prompt) {
            log_append(focused ? "KBM: game window focused; held keys wait for a new press.\r\n"
                               : "KBM: game window lost focus; keyboard & mouse actions "
                                 "neutral, cursor released.\r\n");
        }
        if (!focused) {
            neutral_reset(1);
            kbm_win32_set_capture(0);
        }
        was_focused = focused;
    }
    window_state_resync(now, focused);
    if (kbm_win32_take_overflow()) {
        neutral_reset(1);
        log_append("KBM: input event queue overflowed; actions reset to neutral.\r\n");
    }

    unsigned pressed_key;
    collect_keys(now, focused, &pressed_key);
    dialog_cancel_tick(now);
    pointer_keys(now);
    for (unsigned code = 1; code < 256; ++code) {
        /* A blocked key needs a real release; polling cannot see one unless
           the physical key is up. */
        if (key_blocked[code] && !key_held[code] && !key_tapped[code] &&
            key_physical(code) != 1) {
            key_blocked[code] = 0;
        }
    }

    /* Only keyboard & mouse may give an unused controller object the role of
       the local player's controller; the gamepad path keeps the game's own. */
    route_controller(owner, source == SOURCE_KBM && win32 != KBM_WIN32_STARTING);
    keyboard_controller_connected(owner, source == SOURCE_KBM && win32 != KBM_WIN32_STARTING &&
                                             config.mode != KBM_MODE_PAD && !any_pad_attached());
    int pad_button, pad_axis, mouse_intent;
    pad_intent(owner, focused, &pad_button, &pad_axis);
    int key_intent = pressed_key && focused && key_meaningful(owner, pressed_key);
    if (InterlockedExchange(&pointer_intent, 0) && focused && !in_world()) {
        key_intent = 1; /* a menu click (never while driving) */
        mouse_intent_why = "mouse click in a menu";
    }
    if (key_intent && pressed_key >= KBM_MOUSE_LEFT && pressed_key <= KBM_MOUSE_MIDDLE && in_world()) {
        key_intent = 0; /* mouse buttons never take the car from the pad (garage20) */
    }
    update_mouse(now, dt, focused, &mouse_intent);
    {
        /* Menus: the Camera key (Tab) is the right stick click, e.g. the car
           viewer's "Zaawansowana kamera"; the pump raises it on the UI thread. */
        static int camera_was;
        int menus = focused && source == SOURCE_KBM && !in_world();
        int camera = menus && binding_down(KBM_CAMERA);
        if (camera && !camera_was) {
            InterlockedExchange(&right_stick_click_request, 1);
        }
        camera_was = camera;
        /* FH6 page keys (P, R, L, Backspace): presses raised for the page on
           top by the pump. */
        static unsigned char page_keys_was[PAGE_KEY_COUNT];
        for (unsigned i = 0; i < PAGE_KEY_COUNT; ++i) {
            int held = menus && key_current[page_key_codes[i]];
            if (held && !page_keys_was[i]) {
                InterlockedOr(&page_key_requests, (LONG)(1u << i));
            }
            page_keys_was[i] = (unsigned char)held;
        }
    }

    if (config.mode == KBM_MODE_PAD || !config.enabled) {
        select_source(SOURCE_PAD, config.enabled ? "Mode=PAD" : "keyboard & mouse off", 0, now);
    } else if (config.mode == KBM_MODE_KBM) {
        select_source(SOURCE_KBM, "Mode=KBM", 0, now);
    } else if (pad_button && !key_intent && !mouse_intent) {
        select_source(SOURCE_PAD, pad_reason, 0, now);
    } else if (pad_axis && !key_intent && !mouse_intent &&
               now - last_switch >= AXIS_SWITCH_DEBOUNCE_MS) {
        select_source(SOURCE_PAD, "gamepad stick or trigger", 0, now);
    } else if (key_intent && !pad_button && !pad_axis) {
        select_source(SOURCE_KBM,
                      pressed_key == ESCAPE_AS_PAUSE  ? "Esc as pause"
                      : pressed_key == ESCAPE_AS_BACK ? "Esc as back"
                      : pressed_key ? kbm_config_key_name(pressed_key)
                                    : mouse_intent_why,
                      pressed_key, now);
    } else if (mouse_intent && !pad_button && !pad_axis) {
        select_source(SOURCE_KBM, mouse_intent_why, 0, now);
    }

    int active = source == SOURCE_KBM;
    for (unsigned code = 1; code < 256; ++code) {
        key_previous[code] = key_current[code];
        key_current[code] = active && focused && !key_blocked[code] &&
                            (key_held[code] || key_tapped[code]);
        key_tapped[code] = 0;
        if (!key_current[code]) {
            key_consumed[code] = 0;
            prompt_consumed[code] = 0;
        }
    }
    if (active && focused) {
        update_steering(dt);
    } else {
        steering = 0.0f;
    }
    if (!active) {
        mouse_yaw = 0.0f;
    }
    if (pause_entered && in_world()) {
        InterlockedExchange(&pause_entered, 0); /* driving resumed */
    }
    int gate = pointer_gate();
    static int gate_was, gate_reports;
    if (gate && !gate_was && gate_reports < 3) {
        ++gate_reports;
        begin_line();
        text("KBM: pause menu / map opened; texture hook calls so far: load ");
        number((uint64_t)texture_loads, 10);
        text(", upload ");
        number((uint64_t)texture_uploads, 10);
        text("; prompt icons registered ");
        number((uint64_t)prompt_count, 10);
        text(".");
        flush_line();
    }
    gate_was = gate;
    if (config.trace && gate != pointer_gate_logged) {
        pointer_gate_logged = gate;
        log_append(gate ? "KBM TRACE: pause menu / map state on (pointer allowed).\r\n"
                        : "KBM TRACE: pause menu / map state off.\r\n");
    }
    pointer_mode = pointer_installed && active && focused && kbm_win32_raw_mouse() && gate;
    kbm_active = active;
}

static void update_hook(void *owner, float dt) {
    ((UpdateFn)hooks[HOOK_UPDATE].original)(owner, dt);
    unsigned char *checked = manager();
    if (checked && checked == owner) {
        input_tick(checked, dt);
    }
}

static int rumble_muted(void) {
    return kbm_active;
}

void kbm_menu_option(const wchar_t *key, const wchar_t *value) {
    char narrow_key[32], narrow_value[16];
    unsigned i = 0;
    for (; key[i] && i < sizeof(narrow_key) - 1; ++i) {
        narrow_key[i] = (char)key[i];
    }
    narrow_key[i] = 0;
    for (i = 0; value[i] && i < sizeof(narrow_value) - 1; ++i) {
        narrow_value[i] = (char)value[i];
    }
    narrow_value[i] = 0;
    KbmSettings updated;
    memory_copy(&updated, &config, sizeof(updated));
    if (!kbm_config_parse(&updated, narrow_key, narrow_value)) {
        return;
    }
    memory_copy(&config, &updated, sizeof(config));
    kbm_win32_set_cursor_size(config.cursor_size);
    pointer_installed = prompts_installed && config.menu_cursor;
    begin_line();
    text("KBM: setting changed in the menu: ");
    text(narrow_key);
    text("=");
    text(narrow_value);
    text(installed || !config.enabled ? "." : " (takes effect after a restart).");
    flush_line();
}

int kbm_driving_input_active(void) {
    return installed && kbm_active &&
           (binding_down(KBM_THROTTLE) || binding_down(KBM_BRAKE) ||
            binding_down(KBM_STEER_LEFT) || binding_down(KBM_STEER_RIGHT) ||
            binding_down(KBM_HANDBRAKE));
}

/* ---------- installation ---------- */

static void build_bindings(void) {
    for (unsigned i = 0; i < ARRAY_COUNT(families); ++i) {
        const ActionFamily *family = &families[i];
        unsigned binding = family->binding;
        /* MenuArrows=DPad: arrows raise the D-pad families, Numpad the stick. */
        if (config.menu_arrows_dpad && binding >= KBM_UP && binding <= KBM_RIGHT) {
            binding += KBM_DPAD_UP - KBM_UP;
        } else if (config.menu_arrows_dpad && binding >= KBM_DPAD_UP && binding <= KBM_DPAD_RIGHT) {
            binding -= KBM_DPAD_UP - KBM_UP;
        }
        for (unsigned j = 0; j < family->count; ++j) {
            action_binding[family->context][family->actions[j]] = (unsigned char)(binding + 1);
        }
        for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
            unsigned code = config.keys[binding][k];
            if (code) {
                key_contexts[code] |= (unsigned char)(1u << family->context);
            }
        }
    }
    for (unsigned binding = KBM_STEER_LEFT; binding <= KBM_STEER_RIGHT; ++binding) {
        for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
            unsigned code = config.keys[binding][k];
            if (code) {
                key_contexts[code] |= 1u << CONTEXT_RACING;
            }
        }
    }
    action_binding[CONTEXT_RACING][ACTION_STEERING] = KBM_STEER_LEFT + 1;
    for (unsigned binding = 0; binding < KBM_BINDING_COUNT; ++binding) {
        for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
            binding_list[binding][k] = config.keys[binding][k];
        }
    }
    binding_list[KBM_DOWN][KBM_KEYS_PER_BINDING] = DIALOG_DOWN;
    binding_list[KBM_UP][KBM_KEYS_PER_BINDING] = WHEEL_UP;
    binding_list[KBM_TAB_LEFT][KBM_KEYS_PER_BINDING] = TAB_PREVIOUS;
    binding_list[KBM_TAB_RIGHT][KBM_KEYS_PER_BINDING] = TAB_NEXT;
    binding_list[KBM_CONFIRM][KBM_KEYS_PER_BINDING] = DIALOG_CONFIRM;
    if (config.menu_cursor) {
        binding_list[KBM_BACK][KBM_KEYS_PER_BINDING + 1] = RIGHT_AS_BACK;
        key_contexts[RIGHT_AS_BACK] = 1u << CONTEXT_UI;
    }
    if (config.escape) {
        binding_list[KBM_PAUSE][KBM_KEYS_PER_BINDING] = ESCAPE_AS_PAUSE;
        binding_list[KBM_BACK][KBM_KEYS_PER_BINDING] = ESCAPE_AS_BACK;

        key_contexts[ESCAPE_AS_PAUSE] = 1u << CONTEXT_UI;
        key_contexts[ESCAPE_AS_BACK] = 1u << CONTEXT_UI;
    }
}

static void log_bindings(void) {
    begin_line();
    text("KBM: keys");
    for (unsigned binding = 0; binding < KBM_BINDING_COUNT; ++binding) {
        if (line_length > 400) {
            flush_line();
            begin_line();
            text("KBM: keys");
        }
        text(" ");
        text(kbm_config_binding_name(binding));
        text("=");
        for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
            unsigned code = config.keys[binding][k];
            if (k && code) {
                text(",");
            }
            if (code) {
                text(kbm_config_key_name(code));
            } else if (!k) {
                text("None");
            }
        }
    }
    flush_line();
}

static void write_jump(unsigned char *at, const void *target) {
    static const unsigned char jump[6] = {0xff, 0x25, 0, 0, 0, 0};
    uintptr_t address = (uintptr_t)target;
    memory_copy(at, jump, sizeof(jump));
    memory_copy(at + 6, &address, sizeof(address));
}

/* ---------- UI navigation vector and pause state ---------- */

/* Tile hubs (the pause hub's TileMenuNavigationManager grid) move focus by a
   navigation vector, not by the arrow key events: UserInterface's update
   (0x142bf7b) calls 0x1408be0(ui, Vector2 *out), which reads the UI
   controller's left stick, or its D-pad when pressed, into out = {x, -y}, and
   steps focus with its own repeat timing (0x140e250 maps it to a direction).
   While the keyboard is the active source this vector comes from the arrow
   keys (and the D-pad keys) instead. The call site 0x142bfaa is pointed at
   an absolute jump in the 16-byte int3 gap at 0x140b220. */
#define NAV_CALL 0x142bfaau
#define NAV_VECTOR 0x1408be0u
#define NAV_CAVE 0x140b220u
/* PauseGame state (vtable 0x2d16678): Enter in slot 0x20 (0x2d16698,
   0x8e98e0). Autodrive may already have wrapped that slot; both are chained. */
#define PAUSE_ENTER_SLOT 0x2d16698u
#define PAUSE_ENTER 0x8e98e0u

typedef void *(*NavVectorFn)(void *, float *);
typedef void (*StateFn2)(void *);
static StateFn2 next_pause_enter;
static int nav_installed;

static void *nav_vector_hook(void *ui, float *out) {
    void *result = ((NavVectorFn)(void *)(image + NAV_VECTOR))(ui, out);
    if (!hub_covered((unsigned char *)ui)) {
        nav_manager = (unsigned char *)ui;
        InterlockedExchange64(&nav_manager_at, (LONG64)GetTickCount64());
    }
    if (kbm_active && out) {
        float x = (float)((binding_down(KBM_RIGHT) || binding_down(KBM_DPAD_RIGHT)) -
                          (binding_down(KBM_LEFT) || binding_down(KBM_DPAD_LEFT)));
        float y = (float)((binding_down(KBM_DOWN) || binding_down(KBM_DPAD_DOWN)) -
                          (binding_down(KBM_UP) || binding_down(KBM_DPAD_UP)));
        if ((uint64_t)GetTickCount64() < (uint64_t)nav_wheel_until && y == 0.0f) {
            y = nav_wheel_direction;
        }
        out[0] = x;
        out[1] = y;
    }
    return result;
}

/* Probe (garage2): who pauses the game on a keyboard-only start. Each
   PauseGame entry logs the pad / source state and the game return addresses
   found on the stack (values just after a call instruction), first 24. */
static void log_pause_origin(void *state) {
    volatile unsigned char marker = 0;
    unsigned char *stack = (unsigned char *)&marker;
    unsigned found = 0;
    begin_line();
    text("KBM TRACE: PauseGame entered; pad attached ");
    number((uint64_t)any_pad_attached(), 10);
    text(", source ");
    text(source == SOURCE_KBM ? "keyboard & mouse" : "pad");
    unsigned char pause_flag = 0xff;
    void *pause_param = NULL;
    memory_read((unsigned char *)state + 0x60, &pause_flag, 1);
    memory_read((unsigned char *)state + 0x30, &pause_param, sizeof(pause_param));
    text(pause_param ? ", pause from a parameter" : pause_flag ? ", pause=true" : ", pause=false (unpause state)");
    {
        /* What 0x9635f0 reads for WaitForPause: the window's activated and
           visible flags (see window_state_resync). */
        unsigned char *app = NULL, flags[2] = {0xff, 0xff};
        if (memory_read(image + APP_WINDOW_STATE, &app, sizeof(app)) && app) {
            memory_read(app + APP_ACTIVATED, flags, sizeof(flags));
        }
        text(", window activated ");
        number(flags[0], 10);
        text(" visible ");
        number(flags[1], 10);
    }
    text(", stack:");
    /* The unwinder's view first (frames with unwind data), then the raw scan. */
    typedef unsigned short(WINAPI * CaptureFn)(unsigned long, unsigned long, void **, unsigned long *);
    static CaptureFn capture;
    if (!capture) {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        capture = ntdll ? (CaptureFn)(void *)GetProcAddress(ntdll, "RtlCaptureStackBackTrace") : NULL;
    }
    if (capture) {
        void *frames[32];
        unsigned short count = capture(0, 32, frames, NULL);
        text(" unwound");
        for (unsigned short i = 0; i < count; ++i) {
            uintptr_t address = (uintptr_t)frames[i];
            text(" ");
            if (address >= (uintptr_t)image && address < (uintptr_t)image + GAME_IMAGE_SIZE) {
                text("g");
                number(address - (uintptr_t)image, 16);
            } else {
                number(address & 0xffffffu, 16);
            }
        }
        text("; scanned");
    }
    unsigned in_image = 0;
    for (unsigned offset = 0; offset < 0x8000 && found < 24; offset += 8) {
        uintptr_t value = 0;
        unsigned char before[6];
        if (!memory_read(stack + offset, &value, sizeof(value)) || value < (uintptr_t)image + 0x1000 ||
            value >= (uintptr_t)image + 0x1d00000 || !memory_read((void *)(value - 6), before, 6)) {
            continue;
        }
        ++in_image;
        /* call rel32 (e8), call [mem] (ff 15 / ff 50 xx / ff 90 xxxxxxxx), call reg (ff d0..d7) */
        if (before[1] == 0xe8 || (before[0] == 0xff && (before[1] & 0x38) == 0x10) ||
            (before[3] == 0xff && (before[4] & 0x38) == 0x10) || (before[4] == 0xff && (before[5] & 0xf8) == 0xd0)) {
            text(" ");
            number(value - (uintptr_t)image, 16);
            ++found;
        }
    }
    text(" (code-range values ");
    number(in_image, 10);
    text(")");
    flush_line();
    (void)marker;
}

static void pause_enter_hook(void *state) {
    InterlockedExchange(&pause_entered, 1);
    if (config.trace) {
        log_pause_origin(state);
    }
    next_pause_enter(state);
}

static int install_navigation(void) {
    static const unsigned char call_bytes[5] = {0xe8, 0x31, 0xcc, 0xfd, 0xff};
    unsigned char *call = image + NAV_CALL, *cave = image + NAV_CAVE;
    for (unsigned i = 0; i < 16; ++i) {
        if (cave[i] != 0xcc) {
            log_append("KBM: navigation cave 0x140b220 is in use; menu arrows use keys only.\r\n");
            return 0;
        }
    }
    if (!memory_equal(call, call_bytes, sizeof(call_bytes)) || cave[-1] != 0xc3) {
        log_append("KBM: navigation call 0x142bfaa changed; menu arrows use keys only.\r\n");
        return 0;
    }
    DWORD old_call, old_cave, ignored;
    if (!VirtualProtect(cave, 16, PAGE_EXECUTE_READWRITE, &old_cave)) {
        return 0;
    }
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old_call)) {
        VirtualProtect(cave, 16, old_cave, &ignored);
        return 0;
    }
    write_jump(cave, (void *)nav_vector_hook);
    int32_t relative = (int32_t)(cave - (call + 5));
    memory_copy(call + 1, &relative, 4);
    VirtualProtect(call, 5, old_call, &ignored);
    VirtualProtect(cave, 16, old_cave, &ignored);
    FlushInstructionCache(GetCurrentProcess(), cave, 16);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    nav_installed = 1;
    return 1;
}

static void install_pause_gate(void) {
    void **slot = (void **)(image + PAUSE_ENTER_SLOT);
    void *current = *slot;
    DWORD protection, ignored;
    int in_game = (unsigned char *)current >= image && (unsigned char *)current < image + GAME_IMAGE_SIZE;
    if (!current || (in_game && current != image + PAUSE_ENTER)) {
        log_append("KBM: PauseGame enter slot changed; menu pointer limited to paused "
                   "simulation.\r\n");
        return;
    }
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
        return;
    }
    next_pause_enter = (StateFn2)current;
    InterlockedExchangePointer((void *volatile *)slot, (void *)pause_enter_hook);
    VirtualProtect(slot, sizeof(void *), protection, &ignored);
    log_append("KBM: hook PauseGame enter vtable slot 0x2d16698 installed (menu pointer gate).\r\n");
}

/* The first event's race-activation tutorial (tutorial_flow.xml
   race_activation, instance '.../race_activation_event_200_.../tutorial_flow')
   holds game control under GameControlWrapper until its last state,
   wait_for_activate_press: WaitForButtonPressUnsafe button=ACTIVATE_PRESS
   context=CONTEXT_RACING. Its update (vtable 0x2d2c6b8 slot 14, 0x957170)
   asks no action query: it looks up the button bound to (context, action) in
   0x106e8f0 and tests the controller's raw pad snapshot (0xf9dac0) in
   0x10728f0, so a keyboard press never ends it, race_trigger_activation's
   GameControlWrapper then fails to take control (2026-10-23 QA trace:
   "control held by .../tutorial_flow"), and only a pad press opens the event.
   With keyboard & mouse the source, a fresh press of a key bound to that
   (context, action) sets the behaviour's own done flag the way 0x9572ab does
   (0x1923a70(this+0x58, 0x1923a40()) = 1); the original update then takes
   its own onReady transition (QA 2026-10-23: keyboard X ends the tutorial,
   the event opens, Enter starts the race). The pad state is not touched. A
   key already held when the wait begins must be released first. */
#define WAIT_UNSAFE_SLOT 0x2d2c728u
#define WAIT_UNSAFE_UPDATE 0x957170u
#define FLOW_PARAMETER 0x8898c0u /* (parameter) -> int * */
#define FLOW_DATA_KEY 0x1923a40u
#define FLOW_DATA_AT 0x1923a70u
typedef uintptr_t (*BehaviorUpdateFn)(void *, void *, void *, void *);
typedef int *(*FlowParameterFn)(void *);
typedef void *(*FlowDataKeyFn)(void);
typedef unsigned char *(*FlowDataAtFn)(void *, void *);

#define WAIT_SAFE_SLOT 0x2d2c668u /* WaitForButtonPress, vtable 0x2d2c5f8 slot 14 */
#define WAIT_SAFE_UPDATE 0x957090u

/* Shared by both wait classes (same layout: button +0x70, context +0xb0,
   done flag +0x58, 0x957170 / 0x957090). */
static void wait_press_answer(unsigned char *self, const char *kind) {
    static void *seen;
    static unsigned char latched[256];
    int eligible = installed && config.enabled && config.mode != KBM_MODE_PAD && source == SOURCE_KBM && kbm_active;
    /* 0x9571db / 0x957101: the +0x70 parameter goes in r8d (the action) and
       the +0xb0 one in edx (the context) of 0x106e8f0 / 0x1072500, which index
       the context table with edx (0x106e910 shl 5). */
    int *action = ((FlowParameterFn)(void *)(image + FLOW_PARAMETER))(self + 0x70);
    int *context = ((FlowParameterFn)(void *)(image + FLOW_PARAMETER))(self + 0xb0);
    int context_id = context ? *context : -1, action_id = action ? *action : -1;
    const unsigned short *keys = context_id >= 0 && action_id >= 0 ? binding_keys((unsigned)context_id, (unsigned)action_id)
                                                                    : NULL;
    int first = self != seen, fired = 0;
    for (unsigned i = 0; keys && i < BINDING_KEYS; ++i) {
        unsigned code = keys[i];
        if (!code || code >= 256) {
            continue;
        }
        if (first) {
            latched[code] = key_current[code]; /* held at the start: wait for a release */
        } else if (!key_current[code]) {
            latched[code] = 0;
        } else if (!latched[code] && !fired) {
            latched[code] = 1;
            fired = 1;
        }
    }
    seen = self;
    if (!fired || !eligible) {
        return;
    }
    void *key = ((FlowDataKeyFn)(void *)(image + FLOW_DATA_KEY))();
    unsigned char *done = ((FlowDataAtFn)(void *)(image + FLOW_DATA_AT))(self + 0x58, key);
    if (done) {
        *done = 1;
    }
    begin_line();
    text("KBM: a key answered the wait for a button press (");
    text(kind);
    text(" context ");
    number((unsigned)context_id, 10);
    text(" action ");
    number((unsigned)action_id, 16);
    text(").");
    flush_line();
}

static uintptr_t wait_unsafe_hook(void *behavior, void *a, void *b, void *c) {
    wait_press_answer((unsigned char *)behavior, "WaitForButtonPressUnsafe");
    return ((BehaviorUpdateFn)(void *)(image + WAIT_UNSAFE_UPDATE))(behavior, a, b, c);
}

static uintptr_t wait_safe_hook(void *behavior, void *a, void *b, void *c) {
    wait_press_answer((unsigned char *)behavior, "WaitForButtonPress");
    return ((BehaviorUpdateFn)(void *)(image + WAIT_SAFE_UPDATE))(behavior, a, b, c);
}

static void install_wait_unsafe(void) {
    static const unsigned char unsafe_bytes[] = {0x40, 0x57, 0x48, 0x81, 0xec, 0xe0, 0x00, 0x00, 0x00};
    static const unsigned char safe_bytes[] = {0x40, 0x56, 0x48, 0x83, 0xec, 0x20};
    static const struct {
        unsigned slot, update;
        const unsigned char *bytes;
        unsigned size;
        void *hook;
        const char *done;
    } waits[2] = {{WAIT_UNSAFE_SLOT, WAIT_UNSAFE_UPDATE, unsafe_bytes, sizeof(unsafe_bytes), (void *)wait_unsafe_hook,
                   "KBM: hook WaitForButtonPressUnsafe update vtable slot 0x2d2c728 (0x957170) installed "
                   "(keys answer tutorial button waits).\r\n"},
                  {WAIT_SAFE_SLOT, WAIT_SAFE_UPDATE, safe_bytes, sizeof(safe_bytes), (void *)wait_safe_hook,
                   "KBM: hook WaitForButtonPress update vtable slot 0x2d2c668 (0x957090) installed "
                   "(keys answer button waits).\r\n"}};
    for (unsigned i = 0; i < 2; ++i) {
        void **slot = (void **)(image + waits[i].slot);
        DWORD protection, ignored;
        if (*slot != image + waits[i].update || !memory_equal(image + waits[i].update, waits[i].bytes, waits[i].size) ||
            !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
            log_append("KBM: a WaitForButtonPress update is not as expected; that wait needs a pad.\r\n");
            continue;
        }
        InterlockedExchangePointer((void *volatile *)slot, waits[i].hook);
        VirtualProtect(slot, sizeof(void *), protection, &ignored);
        log_append(waits[i].done);
    }
}

static int install_hooks(void) {
    for (unsigned i = 0; i < HOOKS; ++i) {
        if (!memory_equal(image + hooks[i].rva, hooks[i].expected, hooks[i].size)) {
            begin_line();
            text("KBM: unexpected bytes at ");
            text(hooks[i].name);
            text(" (RVA ");
            number(hooks[i].rva, 16);
            text("); keyboard & mouse not installed, nothing patched.");
            flush_line();
            return 0;
        }
    }
    if (!memory_equal(image + DIGITAL_BODY_RVA, digital_body, sizeof(digital_body))) {
        log_append("KBM: unexpected digital action query body; keyboard & mouse not installed.\r\n");
        return 0;
    }
    trampolines = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!trampolines) {
        log_append("KBM: trampoline allocation failed; keyboard & mouse not installed.\r\n");
        return 0;
    }
    unsigned char *cursor = trampolines;
    for (unsigned i = 0; i < HOOKS; ++i) {
        if (!hooks[i].trampoline) {
            hooks[i].original = image + DIGITAL_BODY_RVA;
            continue;
        }
        /* The displaced prologues are register moves, pushes and stack
           adjustments only; none is RIP-relative, so they run unchanged. */
        hooks[i].original = cursor;
        if (hooks[i].trampoline == 2) {
            /* The last five bytes are a call rel32: re-emit it as an absolute call. */
            int32_t relative;
            memory_copy(&relative, image + hooks[i].rva + hooks[i].size - 4, 4);
            uintptr_t target = (uintptr_t)(image + hooks[i].rva + hooks[i].size) + (intptr_t)relative;
            unsigned copied = hooks[i].size - 5;
            memory_copy(cursor, image + hooks[i].rva, copied);
            cursor[copied] = 0x48; /* mov rax, imm64 */
            cursor[copied + 1] = 0xb8;
            memory_copy(cursor + copied + 2, &target, 8);
            cursor[copied + 10] = 0xff; /* call rax */
            cursor[copied + 11] = 0xd0;
            write_jump(cursor + copied + 12, image + hooks[i].rva + hooks[i].size);
            cursor += (copied + 12 + 14 + 15) & ~15u;
            continue;
        }
        memory_copy(cursor, image + hooks[i].rva, hooks[i].size);
        write_jump(cursor + hooks[i].size, image + hooks[i].rva + hooks[i].size);
        cursor += (hooks[i].size + 14 + 15) & ~15u;
    }
    DWORD ignored;
    if (!VirtualProtect(trampolines, 4096, PAGE_EXECUTE_READ, &ignored)) {
        VirtualFree(trampolines, 0, MEM_RELEASE);
        log_append("KBM: trampoline protection failed; keyboard & mouse not installed.\r\n");
        return 0;
    }
    FlushInstructionCache(GetCurrentProcess(), trampolines, 4096);
    DWORD protections[HOOKS];
    for (unsigned i = 0; i < HOOKS; ++i) {
        if (!VirtualProtect(image + hooks[i].rva, hooks[i].size, PAGE_EXECUTE_READWRITE,
                            &protections[i])) {
            while (i--) {
                VirtualProtect(image + hooks[i].rva, hooks[i].size, protections[i], &ignored);
            }
            log_append("KBM: hook sites are not writable; keyboard & mouse not installed.\r\n");
            return 0;
        }
    }
    for (unsigned i = 0; i < HOOKS; ++i) {
        unsigned char patch[16];
        for (unsigned j = 0; j < sizeof(patch); ++j) {
            patch[j] = hooks[i].trampoline ? 0x90 : 0xcc;

        }
        write_jump(patch, hooks[i].hook);
        memory_copy(image + hooks[i].rva, patch, hooks[i].size);
    }
    for (unsigned i = 0; i < HOOKS; ++i) {
        VirtualProtect(image + hooks[i].rva, hooks[i].size, protections[i], &ignored);
        FlushInstructionCache(GetCurrentProcess(), image + hooks[i].rva, hooks[i].size);
    }
    for (unsigned i = 0; i < HOOKS; ++i) {
        unsigned char now[2] = {0, 0};
        memory_read(image + hooks[i].rva, now, sizeof(now));
        begin_line();
        text("KBM: hook ");
        text(hooks[i].name);
        text(" at ");
        number(hooks[i].rva, 16);
        text(now[0] == 0xff && now[1] == 0x25 ? " installed (" : " NOT in place (");
        number(hooks[i].size, 10);
        text(" bytes moved).");
        flush_line();
    }
    return 1;
}

void kbm_install(unsigned char *game_image, const KbmSettings *settings) {
    memory_copy(&config, settings, sizeof(config));
    image = game_image;
    if (!config.enabled) {
        log_append("KBM: keyboard & mouse is off ([KeyboardMouse] Enabled=0).\r\n");
        return;
    }
#ifndef FH2_AZIMUTH
    log_append("KBM: keyboard & mouse needs an inspected Azimuth host (1.0.2); not installed.\r\n");
    return;
#else
    if (config.mode == KBM_MODE_PAD) {
        log_append("KBM: Mode=PAD; the gamepad path is untouched and no input hooks are "
                   "installed.\r\n");
        return;
    }
    build_bindings();
    if (!install_hooks()) {
        return;
    }
    installed = 1;
    source = SOURCE_KBM;
    begin_line();
    text("KBM: native keyboard & mouse hooks installed (input update 0x1075490, action queries "
         "0x1072500/0x106f040/0x106e3e0/0x106e690/0x106d640, driving look 0x103f9d0); mode ");
    text(config.mode == KBM_MODE_KBM ? "KBM" : "Auto");
    text(config.mouse_look ? ", mouse look on." : ", mouse look off.");
    flush_line();
    log_bindings();
    log_append(config.mode == KBM_MODE_KBM
                   ? "KBM: keyboard & mouse is the input source.\r\n"
                   : "KBM: keyboard & mouse is the input source until a gamepad is used.\r\n");
    kbm_win32_gate_rumble(rumble_muted);
    kbm_win32_set_cursor_size(config.cursor_size);
    install_pointer();
    install_box_on_key();
    install_show_popup();
    install_sign_in();
    install_pairing_raise();
    install_pause_check();
    install_ui_controller();
    install_pairing();
    install_pause_gate();
    install_wait_unsafe();
    if (install_navigation()) {
        log_append("KBM: menu navigation vector follows the arrow keys (0x1408be0 via 0x142bfaa).\r\n");
    }
    if (!kbm_win32_start()) {
        log_append("KBM: input thread could not start; keys stay neutral.\r\n");
    }
#endif
}

#ifdef FH2_TWEAKS_TEST
/* Installs the hooks into a mapped copy of the game image (offline check). */
__declspec(dllexport) int Fh2TestKbmInstall(unsigned char *game_image) {
    KbmSettings settings;
    kbm_config_defaults(&settings);
    settings.enabled = 1;
    kbm_install(game_image, &settings);
    return installed;
}

__declspec(dllexport) void *Fh2TestKbmOriginal(unsigned index) {
    return index < HOOKS ? hooks[index].original : NULL;
}
#endif

/* ---------- key changes while running ---------- */

/* New keys (for an in-game binding page), on the UI thread: the binding
   tables are rebuilt in place, as at install. */
void kbm_set_keys(const unsigned short keys[][KBM_KEYS_PER_BINDING]) {
    for (unsigned binding = 0; binding < KBM_BINDING_COUNT; ++binding) {
        for (unsigned k = 0; k < KBM_KEYS_PER_BINDING; ++k) {
            config.keys[binding][k] = keys[binding][k];
        }
    }
    if (!installed) {
        log_append("KBM: key bindings saved; they apply when keyboard & mouse is on.\r\n");
        return;
    }
    for (unsigned code = 0; code < 256; ++code) {
        key_contexts[code] = 0;
    }
    build_bindings();
    log_bindings();
}

void *kbm_input_top_page(void) {
    return installed && prompts_installed ? top_page() : NULL;
}
