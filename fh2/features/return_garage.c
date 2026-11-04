#include "features.h"
#include "support.h"
#include "logging.h"
#include "autodrive_game.h"
#include "video_options_text.h"
#include "bindings.h"

typedef void (*Action)(void *);
typedef void **(*MakeString)(void **, const wchar_t *);

typedef struct {
    void **vtable;
    void *page, *dialog;
} GarageConfirmation;

typedef struct {
    GarageConfirmation storage;
    GarageConfirmation *target;
} NativeFunction;

static unsigned char *image;
static int garage_flow_ready;
static void *confirmation_page;
static void *input_page, *input_controller;
static int garage_pressed;
static unsigned char request_garage_confirmation(void *, void *);

static void read_garage_binding(void *page, int initialize) {
    void *controller; unsigned short buttons;
    int available = autodrive_game_buttons(&controller, &buttons) == 0;
    int ready = available || binding_keyboard(BIND_RETURN_GARAGE);
    int pressed = !ready || !autodrive_game_window_focused() || binding_down(BIND_RETURN_GARAGE,buttons);
    if (!initialize && input_page == page && input_controller == controller && !garage_pressed &&
        pressed && ready && autodrive_game_window_focused() &&
        !*(void **)((unsigned char *)page + 0x2a0)) {
        void *delegate[2] = {NULL,page};
        request_garage_confirmation(delegate,NULL);
    }
    input_page=page;input_controller=controller;garage_pressed=pressed;
}


static struct {
    void *page, *behavior;
    uint64_t deadline;
} pending_return;

static int awaiting_garage;
static uint64_t garage_deadline;

typedef struct {
    union {
        char text[16];
        const char *pointer;
    } storage;

    size_t length, capacity;
} GameString;

static int is_target_flow(void *component, const char *expected) {
    GameString flow;
    if (!memory_read((unsigned char *)component + 0xb0, &flow, sizeof(flow)) ||
        flow.length != text_length(expected)) {
        return 0;
    }
    char text[32];
    const char *source =
        flow.capacity >= 16 ? flow.storage.pointer : (const char *)component + 0xb0;
    return flow.length < sizeof(text) && memory_read(source, text, flow.length) &&
           memory_equal(text, expected, flow.length);
}

static int is_island_garage(void *component) {
    static const char suffix[] = "island_destination_hub/activation_state_machine";
    GameString name = {0};
    ((void *(*)(void *, GameString *))(image + 0x1907b30))(component, &name);
    const char *text = name.capacity >= 16 ? name.storage.pointer : name.storage.text;
    size_t length = sizeof(suffix) - 1;
    int matches = name.length >= length &&
                  (name.length == length || text[name.length - length - 1] == '/') &&
                  memory_equal(text + name.length - length, suffix, length);
    if (name.capacity >= 16) {
        ((Action)(image + 0x31c7b0))((void *)name.storage.pointer);
    }
    return matches;
}

static SRWLOCK location_lock = SRWLOCK_INIT;
static GameShared location_reference;
static int location_island;
static const char *waiting_reason;

static void release_weak_location(GameShared value) {
    if (value.counter &&
        InterlockedDecrement((volatile LONG *)((unsigned char *)value.counter + 12)) == 0) {
        ((Action)(*(void ***)value.counter)[1])(value.counter);
    }
}

static void clear_location(void) {
    AcquireSRWLockExclusive(&location_lock);
    GameShared previous = location_reference;
    location_reference = (GameShared){0};
    ReleaseSRWLockExclusive(&location_lock);
    release_weak_location(previous);
}

static void activate_location(void *component) {
    ((Action)(image + 0x559260))(component);
    int island = 0;
    if (!is_target_flow(component, "garage.root_festival")) {
        if (!is_target_flow(component, "destination_hub.root") || !is_island_garage(component)) {
            return;
        }
        island = 1;
    }
    GameShared reference;
    if (!memory_read((unsigned char *)component + 0x30, &reference, sizeof(reference)) ||
        reference.object != (unsigned char *)component + 0x28 || !reference.counter) {
        return;
    }
    InterlockedIncrement((volatile LONG *)((unsigned char *)reference.counter + 12));
    AcquireSRWLockExclusive(&location_lock);
    GameShared previous = location_reference;
    location_reference = reference;
    location_island = island;
    ReleaseSRWLockExclusive(&location_lock);
    release_weak_location(previous);
    log_append("GARAGE: native festival activation component ready.\r\n");
}

static int lock_location(GameShared *result, int *island) {
    *result = (GameShared){0};
    AcquireSRWLockShared(&location_lock);
    GameShared reference = location_reference;
    if (reference.counter) {
        volatile LONG *uses = (volatile LONG *)((unsigned char *)reference.counter + 8);
        LONG count = InterlockedCompareExchange(uses, 0, 0);
        while (count > 0 && count < 0x7fffffff) {
            LONG observed = InterlockedCompareExchange(uses, count + 1, count);
            if (observed == count) {
                result->object = (unsigned char *)reference.object - 0x28;
                result->counter = reference.counter;
                *island = location_island;
                break;
            }
            count = observed;
        }
    }
    ReleaseSRWLockShared(&location_lock);
    return result->object != NULL;
}

void return_garage_tick(void) {
    if (!awaiting_garage) {
        return;
    }
    if (GetTickCount64() > garage_deadline) {
        awaiting_garage = 0;
        log_append("GARAGE: return timed out: ");
        log_append(waiting_reason);
        log_append(".\r\n");
        return;
    }
    if (!autodrive_game_is_freeroam() || autodrive_game_is_carmeet()) {
        waiting_reason = "free-roam simulation is not ready";
        return;
    }
    void *manager, *controller;
    if (!memory_read_pointer(image + 0x365d7c8, &manager) ||
        !memory_read_pointer((unsigned char *)manager + 0xab28, &controller) ||
        !autodrive_game_has_gameplay_input(controller)) {
        waiting_reason = "gameplay input is not ready";
        return;
    }
    if (((unsigned char (*)(void))(image + 0x99ecd0))()) {
        waiting_reason = "native location activation is blocked";
        return;
    }
    GameShared location;
    int island;
    if (!lock_location(&location, &island)) {
        waiting_reason = "festival activation component is unavailable";
        return;
    }
    awaiting_garage = 0;
    void *component = location.object;
    if (((unsigned char (*)(void *))(image + 0x559860))(component)) {
        log_append("GARAGE: return cancelled; location flow already active.\r\n");
    } else {
        unsigned char started = island
                                    ? ((unsigned char (*)(void *, const char *))(image + 0x5641b0))(
                                          component, "garage._fh2_return_island")
                                    : ((unsigned char (*)(void *))(image + 0x564320))(component);
        log_append(started ? "GARAGE: native location activation accepted.\r\n"
                           : "GARAGE: native location activation declined.\r\n");
    }
    autodrive_game_release_shared(&location);
}

static unsigned char original_slots[4][8], callback_slots[4][8];
static const unsigned char *accepted_slots[4][2];
PatchDefinition return_garage_hook_patches[4];

static void release_object(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0) {
        ((Action)(image + 0x257730))(object);
    }
}

static void release_localized(void *object) {
    if (object) {
        ((Action)(*(void ***)object)[3])(object);
    }
}

static int garage_is_available(void *page) {
    void *manager;
    GameShared context = {0};
    if (!garage_flow_ready || !page || !*(void **)((unsigned char *)page + 0x310) ||
        !memory_read_pointer(image + 0x34c0f40, &manager)) {
        return 0;
    }
    ((void *(*)(void *, GameShared *))(image + 0x2129a0))(manager, &context);
    int mode = -1;
    memory_read((unsigned char *)context.object + 0x60, &mode, sizeof(mode));
    autodrive_game_release_shared(&context);
    return mode == 2 && !autodrive_game_is_carmeet();
}

static void initialize_pause(void *page) {
    if (confirmation_page != page) {
        confirmation_page = NULL;
    }
    if (pending_return.page != page) {
        pending_return.page = NULL;
    }
    ((Action)(image + 0xdc6e30))(page);
    read_garage_binding(page,1);
    if (!garage_is_available(page)) {
        return;
    }
    void *panel = ((void *(*)(void *))(image + 0xced910))(page), *label = NULL;
    ((MakeString)(image + 0x13fe8e0))(&label, video_options_text(image)->garage_label);
    if (panel && label) {
        ((void (*)(void *, int, void **))(image + 0xb00010))(panel, 2, &label);
        label = NULL;
    }
    release_object(label);
}

static void *clone_confirmation(GarageConfirmation *source, void *destination) {
    if (!destination) {
        destination = ((void *(*)(size_t))(image + 0x3229c0))(sizeof(*source));
    }
    if (destination) {
        memory_copy(destination, source, sizeof(*source));
    }
    return destination;
}

static void destroy_confirmation(GarageConfirmation *self, unsigned char deallocate) {
    if (deallocate) {
        ((Action)(image + 0x31c7b0))(self);
    }
}

static void *confirmation_type(void *self) {
    (void)self;
    return NULL;
}

static void accept_garage(GarageConfirmation *self) {
    if (confirmation_page != self->page) {
        return;
    }
    confirmation_page = NULL;
    void *button = ((void *(*)(void *))(image + 0xe01a60))(self->dialog);
    if (!button || *(int *)((unsigned char *)button + 0x68) != 0) {
        return;
    }
    pending_return.page = self->page;
    pending_return.behavior = *(void **)((unsigned char *)self->page + 0x310);
    pending_return.deadline = GetTickCount64() + 5000;
    log_append("GARAGE: return confirmed; waiting for the dialog to close.\r\n");
}

static void update_pause(void *page) {
    ((Action)(image + 0xdce660))(page);
    if (garage_is_available(page)) read_garage_binding(page,0);
    if (pending_return.page != page) {
        return;
    }
    void *behavior = *(void **)((unsigned char *)page + 0x310);
    if (!behavior || behavior != pending_return.behavior ||
        GetTickCount64() > pending_return.deadline) {
        pending_return.page = NULL;
        log_append("GARAGE: return cancelled; pause page changed or timed out.\r\n");
        return;
    }
    if (*(void **)((unsigned char *)page + 0x2a0) ||
        !*(unsigned char *)((unsigned char *)behavior + 0x78)) {
        return;
    }
    pending_return.page = NULL;
    if (!garage_is_available(page)) {
        log_append("GARAGE: return cancelled; free-roam context unavailable.\r\n");
        return;
    }
    waiting_reason = "pause cleanup has not finished";
    awaiting_garage = 1;
    garage_deadline = GetTickCount64() + 10000;
    ((Action)(image + 0xdb49e0))(page);
    log_append(
        "GARAGE: normal pause-menu exit requested; waiting for native location activation.\r\n");
}

static void *confirmation_vtable[] = {(void *)clone_confirmation, (void *)clone_confirmation,
                                      (void *)accept_garage, (void *)confirmation_type,
                                      (void *)destroy_confirmation};

static int add_button(void *buttons, int value, unsigned localized_key) {
    void *label = NULL, *button = NULL;
    ((void **(*)(void **, const char *, unsigned))(image + 0x123a570))(&label, "HelpButtons",
                                                                       localized_key);
    if (!label) {
        return 0;
    }
    ((void **(*)(void **, const int *, void **))(image + 0xc65ab0))(&button, &value, &label);
    release_localized(label);
    if (!button) {
        return 0;
    }
    void *items = *(void **)((unsigned char *)buttons + 0x50);
    int count = *(int *)((unsigned char *)items + 0x30);
    ((void (*)(void *, int, void *))(*(void ***)buttons)[8])(buttons, count, button);
    release_object(button);
    return 1;
}

static unsigned char request_garage_confirmation(void *delegate, void *args) {
    (void)args;
    void *page = *(void **)((unsigned char *)delegate + 8);
    if (!garage_is_available(page)) {
        return ((unsigned char (*)(void *, void *))(image + 0xde7490))(delegate, args);
    }
    if (confirmation_page || pending_return.page || awaiting_garage) {
        return 1;
    }
    void *buttons = NULL;
    ((void **(*)(void **))(image + 0xc65230))(&buttons);
    if (!buttons) {
        return 1;
    }
    int complete = add_button(buttons, 0, *(unsigned *)(image + 0x354a598)) &&
                   add_button(buttons, 1, *(unsigned *)(image + 0x3549c08));
    void *dialog = complete ? ((void *(*)(void *))(image + 0xce2ca0))(page) : NULL;
    if (dialog) {
        const VideoOptionsText *text = video_options_text(image);
        void **title = (void **)((unsigned char *)dialog + 0x460);
        void **body = (void **)((unsigned char *)dialog + 0x468);
        release_object(*title);
        release_object(*body);
        *title = *body = NULL;
        ((MakeString)(image + 0x13fe8e0))(title, text->garage_label);
        ((MakeString)(image + 0x13fe8e0))(body, text->garage_question);
        ((void (*)(void *, void *))(image + 0xe1b5a0))(dialog, (unsigned char *)buttons + 0x38);
        ((void (*)(void *, int))(image + 0xdd8820))(dialog, 5);
        NativeFunction function = {{confirmation_vtable, page, dialog}, NULL};
        function.target = &function.storage;
        confirmation_page = page;
        ((void (*)(void *, int, NativeFunction *, unsigned char))(image + 0xdd8340))(dialog, 0,
                                                                                     &function, 1);
    }
    release_object(buttons);
    return 1;
}

static unsigned char show_garage_confirmation(void *delegate, void *args) {
    if (binding_xbox(BIND_RETURN_GARAGE) != 0x4000) return 0;
    return request_garage_confirmation(delegate,args);
}

void return_garage_resource_parsed(unsigned kind, unsigned char success) {
    if (kind == 5) {
        garage_flow_ready = success != 0;
    }
}

void return_garage_prepare_hooks(unsigned char *base) {
    clear_location();
    image = base;
    garage_flow_ready = 0;
    confirmation_page = NULL;
    input_page = input_controller = NULL;
    garage_pressed = 1;
    pending_return.page = NULL;
    awaiting_garage = 0;
    const unsigned slots[] = {0x2e14310, 0x2e20dd0, 0x2e14350, 0x2c98d10};
    const unsigned functions[] = {0xdc6e30, 0xde7490, 0xdce660, 0x559260};
    const uintptr_t callbacks[] = {(uintptr_t)initialize_pause, (uintptr_t)show_garage_confirmation,
                                   (uintptr_t)update_pause, (uintptr_t)activate_location};
    const char *names[] = {"pause_garage_footer", "pause_garage_help_action", "pause_garage_update",
                           "garage_location_activation"};
    for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
        uintptr_t original = (uintptr_t)(base + functions[i]);
        memory_copy(original_slots[i], &original, 8);
        memory_copy(callback_slots[i], &callbacks[i], 8);
        accepted_slots[i][0] = original_slots[i];
        accepted_slots[i][1] = callback_slots[i];
        return_garage_hook_patches[i] = (PatchDefinition){slots[i],
                                                          0,
                                                          8,
                                                          GROUP_VIDEO_OPTIONS,
                                                          names[i],
                                                          original_slots[i],
                                                          callback_slots[i],
                                                          callback_slots[i],
                                                          accepted_slots[i],
                                                          2};
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestReturnGarageReady(unsigned kind, unsigned char success) {
    return_garage_resource_parsed(kind, success);
}

__declspec(dllexport) void *Fh2TestGarageConfirmation(void) {
    return show_garage_confirmation;
}

__declspec(dllexport) void *Fh2TestGaragePauseInitialize(void) {
    return initialize_pause;
}

__declspec(dllexport) void *Fh2TestGaragePauseUpdate(void) {
    return update_pause;
}

__declspec(dllexport) void *Fh2TestGarageLocationActivate(void) {
    return activate_location;
}

__declspec(dllexport) void Fh2TestGarageTick(void) {
    return_garage_tick();
}

__declspec(dllexport) void Fh2TestGarageClearLocation(void) {
    clear_location();
}
#endif
