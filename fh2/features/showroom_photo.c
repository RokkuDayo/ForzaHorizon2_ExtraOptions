#include "features.h"
#include "support.h"
#include "autodrive_game.h"
#include "logging.h"
#include "bindings.h"
#include "driver_options.h"
#include "photo_environment.h"

typedef void (*Action)(void *);

typedef struct {
    char text[16];
    size_t length, capacity;
} GameString;

typedef struct {
    void *page, *controller;
    int pressed;
} PreviewInput;

typedef struct {
    void *proxy, **blocks;
    size_t capacity, start, count;
} InputContexts;

typedef struct {
    void *vtable;
    unsigned flags, reserved;
    GameString name;
    unsigned mode;
} SceneTransition;

static unsigned char *image;
static int flow_ready;
static PreviewInput inputs[4];
static int photo_requested;
static void *photo_page;
static void *meet_pause_page, *meet_pause_manager;
static unsigned char meet_was_paused, meet_stopped_simulation;

static unsigned char *game_manager(void) {
    void *owner=*(void **)(image+0x34aba60);
    return owner?((void *(*)(void *))(image+0x21dfc0))(owner):NULL;
}

static void restore_meet_pause(void *page) {
    if(!meet_pause_page || (page && page!=meet_pause_page))return;
    unsigned char *manager=game_manager();
    if(manager && manager==meet_pause_manager){
        // Keep simulation stopped until the native unpause has notified the
        // renderer, animations and game systems, then restore the meet policy.
        if(!meet_was_paused || !meet_stopped_simulation){
            ((void (*)(void *,int))(*(void ***)manager)[6])(manager,3);
            manager[0x29]=meet_stopped_simulation;
            if(meet_was_paused)
                ((void (*)(void *,int))(*(void ***)manager)[6])(manager,2);
        }
    }
    meet_pause_page=meet_pause_manager=NULL;
}

static void pause_meet(void *page) {
    unsigned char *manager=game_manager();
    if(!manager)return;
    meet_pause_page=page;meet_pause_manager=manager;
    meet_was_paused=manager[0x28];meet_stopped_simulation=manager[0x29];
    // Same policy as global.stop_simulation_when_paused, followed by the
    // PauseGame state's native manager request. Car Meet normally keeps this off.
    if(!meet_was_paused || !meet_stopped_simulation){
        // A meet can already be logically paused with simulation still enabled.
        // Re-enter native pause so its change detector also freezes rendering.
        if(meet_was_paused)
            ((void (*)(void *,int))(*(void ***)manager)[6])(manager,3);
        manager[0x29]=1;
        ((void (*)(void *,int))(*(void ***)manager)[6])(manager,2);
    }
}
static void *updating_photo_camera;
static volatile LONG photo_effects_active;
static SceneTransition photo_transitions[3];
static int transitions_registered;
static SRWLOCK transition_lock = SRWLOCK_INIT;

static int can_take_photo(unsigned char *page) {
    void *behavior = *(void **)(page + 0x310);
    return flow_ready && behavior && !*(void **)(page + 0x2a0) &&
           *(unsigned char *)((unsigned char *)behavior + 0x78);
}

static unsigned char enter_photo(void *page) {
    if (!can_take_photo(page)) {
        return 0;
    }
    photo_requested = *(void **)page==image+0x2dcc538 ? 2 : 1;
    const GameString event = {"_fh2_photo", 10, 15};
    ((void (*)(void *, const GameString *))(image + 0x1422f10))(page, &event);
    log_append("SHOWROOM: Photo Mode transition requested.\r\n");
    return 1;
}

static void release_ui_value(void *value) {
    if (value && InterlockedDecrement((volatile LONG *)((unsigned char *)value + 0x10)) == 0) {
        ((Action)(image + 0x257730))(value);
    }
}

static void *prompt_property(void *object, void *property) {
    void *value = NULL;
    if (property) {
        ((void **(*)(void *, void **, void *))(image + 0x15e0f10))(object, &value, property);
    }
    return value;
}

static int prompt_visible(void *panel,int key) {
    void *button = ((void *(*)(void *, int))(image + 0xb2aae0))(panel, key);
    if (!button) {
        return 0;
    }
    // Native ClearHelpButtons retains XAML-backed items and clears their labels.
    // Finding the View item therefore does not mean its prompt is visible.
    void *attached = prompt_property(button, *(void **)(image + 0x34e2ce8));
    int from_xaml = attached && *((unsigned char *)attached + 0x28);
    release_ui_value(attached);
    void *property = from_xaml ? ((void *(*)(int))(image + 0xb2e040))(key)
                              : *(void **)(image + 0x34e2c18);
    void *label = prompt_property(from_xaml ? panel : button, property);
    const wchar_t *text = label ? *(const wchar_t **)((unsigned char *)label + 0x28) : NULL;
    int visible = text && *text;
    release_ui_value(label);
    return visible;
}

static int photo_prompt_key(void *page){
    if(*(void **)page!=image+0x2dcc538)return 5;
    switch(binding_xbox(BIND_CAR_MEET_PHOTO)){
        case 0x1000:return 0;case 0x2000:return 1;case 0x4000:return 2;
        case 0x8000:return 3;case 0x10:return 4;case 0x20:return 5;default:return -1;
    }
}
static void add_prompt(void *page) {
    int key=photo_prompt_key(page);if(key<0)return;
    void *panel = ((void *(*)(void *))(image + 0xced910))(page);
    if (!panel || prompt_visible(panel,key)) {
        return;
    }
    void *localized = NULL, *label = NULL;
    ((void **(*)(void **, const char *, unsigned))(image + 0x123a570))(&localized, "HelpButtons",
                                                                       35428);
    if (!localized) {
        return;
    }
    const wchar_t *text = ((const wchar_t *(*)(void *))(*(void ***)localized)[7])(localized);
    if (text && *text) {
        ((void **(*)(void **, const wchar_t *))(image + 0x13fe8e0))(&label, text);
    }
    ((Action)(*(void ***)localized)[3])(localized);
    if (label) {
        ((void (*)(void *, int, void **))(image + 0xb00010))(panel, key, &label);
    }
}

static void read_input(PreviewInput *input, void *page, int initialize) {
    GameAction action=input==&inputs[3]?BIND_CAR_MEET_PHOTO:BIND_SHOWROOM_PHOTO;
    void *controller;
    unsigned short buttons;
    int available = autodrive_game_buttons(&controller, &buttons) == 0;
    int ready = available || binding_keyboard(action);
    int pressed = !ready || !autodrive_game_window_focused() ||
                  binding_down(action, buttons);
    if (!initialize && input->page == page && input->controller == controller && !input->pressed &&
        pressed && ready && autodrive_game_window_focused()) {
        enter_photo(page);
    }
    *input = (PreviewInput){page, controller, pressed};
}

static void initialize(void *page, unsigned original, unsigned index) {
    photo_requested = 0;
    ((Action)(image + original))(page);
    read_input(&inputs[index], page, 1);
    if (flow_ready) {
        add_prompt(page);
    }
}

static void update(void *page, unsigned original, unsigned index) {
    ((Action)(image + original))(page);
    if (can_take_photo(page)) {
        add_prompt(page);
    }
    read_input(&inputs[index], page, 0);
}

static void initialize_view(void *page) {
    initialize(page, 0xcce280, 0);
}

static void initialize_pause_view(void *page) {
    initialize(page, 0xcf8d80, 1);
}

static void initialize_color(void *page) {
    initialize(page, 0xca99a0, 2);
}

static void update_view(void *page) {
    update(page, 0xcd1690, 0);
}

static void update_pause_view(void *page) {
    update(page, 0xcd1690, 1);
}

static void update_color(void *page) {
    update(page, 0xcaddb0, 2);
}

static void initialize_meet(void *page){initialize(page,0xcaa1f0,3);}
static void update_meet(void *page){update(page,0xcae140,3);}

static void initialize_photo(void *page) {
    restore_meet_pause(NULL);
    photo_environment_begin(page);
    driver_photo_reset();
    int requested = photo_requested;
    photo_requested = 0;
    if (photo_page == page) {
        InterlockedExchange(&photo_effects_active, 0);
        photo_page = NULL;
    }
    ((Action)(image + 0xdc9920))(page);
    video_options_photo_refresh(page);
    if (requested) {
        unsigned char *manager = *(void **)(image + 0x34e4588);
        unsigned char *camera = manager ? *(void **)(manager + 8) : NULL;
        if (camera && *(void **)camera == image + 0x2d50700) {
            *((unsigned char *)page + 0x11c9) = 1;
            photo_page = page;
            if(requested==2)pause_meet(page);
            InterlockedExchange(&photo_effects_active, 1);
            log_append("SHOWROOM: Photo Mode selected the native UI camera.\r\n");
        }
    }
}

static void close_photo(void *page) {
    restore_meet_pause(page);
    photo_environment_end(page);
    driver_photo_reset();
    if (photo_page == page) {
        InterlockedExchange(&photo_effects_active, 0);
        photo_page = NULL;
    }
    ((Action)(image + 0xdcde80))(page);
}

static void release_effect_string(GameString *text) {
    if (text->capacity >= 16) {
        ((Action)(image + 0x31c7b0))(*(void **)text->text);
    }
    text->capacity = 15;
    text->length = 0;
    text->text[0] = 0;
}

static void apply_camera_effects(void *renderer, unsigned char *effects, unsigned view) {
    if (view == 0 && InterlockedCompareExchange(&photo_effects_active, 0, 0)) {
        // This native by-value argument owns two strings even when its effects are skipped.
        release_effect_string((void *)(effects + 0x78));
        release_effect_string((void *)(effects + 0x58));
        return;
    }
    ((void (*)(void *, void *, unsigned))(image + 0xf90e60))(renderer, effects, view);
}

static void apply_dynamic_effects(void *renderer, void *effects, unsigned view) {
    if (view == 0 && InterlockedCompareExchange(&photo_effects_active, 0, 0)) {
        ((Action)(image + 0x2c5d10))(effects);
        return;
    }
    ((void (*)(void *, void *, unsigned))(image + 0xf910c0))(renderer, effects, view);
}

static void apply_track_effects(void *renderer, void **effects, unsigned view) {
    if (view == 0 && InterlockedCompareExchange(&photo_effects_active, 0, 0)) {
        if (*effects) {
            ((Action)(*(void ***)*effects)[3])(*effects);
        }
        return;
    }
    ((void (*)(void *, void *, unsigned))(image + 0xfb49b0))(renderer, effects, view);
}

static void read_photo_camera_input(unsigned char *inner) {
    void *controller;
    unsigned short buttons;
    if (!inner || autodrive_game_buttons(&controller, &buttons) < 0 ||
        *(void **)(inner + 0x10) != controller) {
        return;
    }
    // Keep the menu's input permissions intact while reading native camera mappings.
    __declspec(align(16)) unsigned char snapshot[0xd0];
    InputContexts *contexts = (void *)(snapshot + 0x38);
    unsigned permissions[2] = {0, 1u << (inner[0x22d] ? 2 : 1)};
    void *block = permissions, *owner = contexts;
    memory_copy(snapshot, controller, sizeof(snapshot));
    contexts->proxy = &owner;
    contexts->blocks = &block;
    contexts->capacity = 1;
    contexts->start = 0;
    contexts->count = 1;
    *(void **)(inner + 0x10) = snapshot;
    ((Action)(image + 0x103f660))(inner);
    *(void **)(inner + 0x10) = controller;
    inner[0x112] = 1;
}

static void integrate_photo_camera(unsigned char *inner, float elapsed, unsigned char mode,
                                   unsigned char flags) {
    unsigned char *manager = *(void **)(image + 0x34e4588);
    unsigned char *camera = manager ? *(void **)(manager + 8) : NULL;
    if (camera && *(void **)camera == image + 0x2d50900 && *(void **)(camera + 0x60) == inner &&
        InterlockedCompareExchange(&photo_effects_active, 0, 0)) {
        // The shared preview camera can also be updated outside the Photo Mode tick.
        if (updating_photo_camera != camera || !camera[0x88]) {
            return;
        }
        *(float *)(inner + 0x230) = *(float *)(image + 0x1dfc288);
        read_photo_camera_input(inner);
    }
    ((void (*)(void *, float, unsigned char, unsigned char))(image + 0x103cda0))(inner, elapsed,
                                                                                 mode, flags);
}

static void set_preview_photo_pitch(void *inner,float pitch){
    unsigned char *camera=updating_photo_camera;
    if(camera && InterlockedCompareExchange(&photo_effects_active,0,0) &&
       *(void **)(camera+0x78)==inner){
        // The native photo integrator already limits pitch to +/-85 degrees.
        // a993f0's later orbit clamp must not overwrite that integrated angle.
        return;
    }
    ((void (*)(void *,float))(image+0x397700))(inner,pitch);
}

static void update_photo_camera(unsigned char *camera, float elapsed) {
    unsigned char *manager = *(void **)(image + 0x34e4588);
    void *previous = updating_photo_camera;
    if (manager && *(void **)(manager + 8) == camera) {
        camera[0x89] = camera[0x88];
        updating_photo_camera = camera;
    }
    ((void (*)(void *, float))(image + 0xaa26d0))(camera, elapsed);
    updating_photo_camera = previous;
}

static void *get_photo_source_camera(void *unused, void **output) {
    (void)unused;
    unsigned char *manager = *(void **)(image + 0x34e4588);
    unsigned char *camera = manager ? *(void **)(manager + 8) : NULL;
    if (camera && *(void **)camera == image + 0x2d50700) {
        log_append("SHOWROOM: Orbit camera retained for native Photo Mode entry.\r\n");
        return ((void *(*)(void *, void **))(image + 0x2b4900))(manager, output);
    }
    return ((void *(*)(void *, void **))(image + 0xda1500))(manager, output);
}

void showroom_photo_resource_parsed(unsigned char success) {
    if (success) {
        AcquireSRWLockExclusive(&transition_lock);
        if (!transitions_registered) {
            ((void *(*)(void *, const char *))(image + 0xcbf8c0))(&photo_transitions[0],
                                                                  "_fh2_photo");
            ((void *(*)(void *, const char *))(image + 0xc98cd0))(&photo_transitions[1],
                                                                  "_fh2_photo");
            ((void *(*)(void *,const char *))(image+0xc99010))(&photo_transitions[2],"_fh2_photo");
            transitions_registered = 1;
        }
        ReleaseSRWLockExclusive(&transition_lock);
    }
    flow_ready = success != 0;
}

PatchDefinition showroom_photo_hook_patches[23];
static unsigned char originals[23][12], replacements[23][12];
static const unsigned char *accepted[23][2];

void showroom_photo_prepare_hooks(unsigned char *base) {
    image = base;
    flow_ready = 0;
    photo_requested = 0;
    photo_page = NULL;
    updating_photo_camera = NULL;
    InterlockedExchange(&photo_effects_active, 0);
    for (unsigned i = 0; i < ARRAY_COUNT(inputs); ++i) {
        inputs[i] = (PreviewInput){0};
    }
    static const unsigned slots[] = {0x2dd3710, 0x2d63cf8, 0x2dc6c20, 0x2dd3750, 0x2d63d38,
                                     0x2dc6c60, 0x2e172a8, 0x2d50908, 0x2e17318, 0x2c650f8,
                                     0x2e6c880, 0x2e6ced0, 0x2c65100, 0x2e6c888, 0x2e6ced8,
                                     0x2d50380, 0x2c65108, 0x2e6c890, 0x2e6cee0, 0x2dcc9c0, 0x2dcca00, 0x2d505e0, 0xdaf3eb};
    static const unsigned functions[] = {0xcce280,  0xcf8d80, 0xca99a0, 0xcd1690, 0xcd1690,
                                         0xcaddb0,  0xdc9920, 0xaa26d0, 0xdcde80, 0xf90e60,
                                         0xf90e60,  0xf90e60, 0xf910c0, 0xf910c0, 0xf910c0,
                                         0x103cda0, 0xfb49b0, 0xfb49b0, 0xfb49b0, 0xcaa1f0, 0xcae140, 0x397700, 0xda1500};
    static const uintptr_t callbacks[] = {
        (uintptr_t)initialize_view,       (uintptr_t)initialize_pause_view,
        (uintptr_t)initialize_color,      (uintptr_t)update_view,
        (uintptr_t)update_pause_view,     (uintptr_t)update_color,
        (uintptr_t)initialize_photo,      (uintptr_t)update_photo_camera,
        (uintptr_t)close_photo,           (uintptr_t)apply_camera_effects,
        (uintptr_t)apply_camera_effects,  (uintptr_t)apply_camera_effects,
        (uintptr_t)apply_dynamic_effects, (uintptr_t)apply_dynamic_effects,
        (uintptr_t)apply_dynamic_effects, (uintptr_t)integrate_photo_camera,
        (uintptr_t)apply_track_effects,   (uintptr_t)apply_track_effects,
        (uintptr_t)apply_track_effects,   (uintptr_t)initialize_meet,
        (uintptr_t)update_meet, (uintptr_t)set_preview_photo_pitch, (uintptr_t)get_photo_source_camera};
    static const char *const names[] = {"showroom_photo_view",
                                        "showroom_photo_pause_view",
                                        "showroom_photo_autoshow",
                                        "showroom_photo_view_input",
                                        "showroom_photo_pause_input",
                                        "showroom_photo_autoshow_input",
                                        "showroom_photo_ui_camera",
                                        "showroom_photo_camera_update",
                                        "showroom_photo_close",
                                        "showroom_photo_render_effects",
                                        "showroom_photo_base_effects",
                                        "showroom_photo_dx11_effects",
                                        "showroom_photo_dynamic_effects",
                                        "showroom_photo_base_dynamic",
                                        "showroom_photo_dx11_dynamic",
                                        "showroom_photo_motion",
                                        "showroom_photo_track_effects",
                                        "showroom_photo_base_track",
                                        "showroom_photo_dx11_track",
                                        "car_meet_photo_entry", "car_meet_photo_input", "showroom_photo_pitch",
                                        "showroom_photo_source_camera"};
    for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
        uintptr_t native = (uintptr_t)(base + functions[i]);
        unsigned length = 8;
        if (i == ARRAY_COUNT(slots) - 1) {
            static const unsigned char original[] = {0x48, 0x8b, 0x0d, 0x96, 0x51, 0x73,
                                                     0x02, 0xe8, 0x09, 0x21, 0xff, 0xff};
            length = sizeof(original);
            memory_copy(originals[i], original, length);
            replacements[i][0] = 0x48;
            replacements[i][1] = 0xb8;
            memory_copy(replacements[i] + 2, &callbacks[i], 8);
            replacements[i][10] = 0xff;
            replacements[i][11] = 0xd0;
        } else {
            memory_copy(originals[i], &native, 8);
            memory_copy(replacements[i], &callbacks[i], 8);
        }
        accepted[i][0] = originals[i];
        accepted[i][1] = replacements[i];
        showroom_photo_hook_patches[i] = (PatchDefinition){slots[i],        0,
                                                           length,          GROUP_VIDEO_OPTIONS,
                                                           names[i],        originals[i],
                                                           replacements[i], replacements[i],
                                                           accepted[i],     2};
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned char Fh2TestShowroomPhotoAction(void *page) {
    return enter_photo(page);
}

__declspec(dllexport) void Fh2TestShowroomPhotoReady(unsigned char ready) {
    showroom_photo_resource_parsed(ready);
}
#endif
