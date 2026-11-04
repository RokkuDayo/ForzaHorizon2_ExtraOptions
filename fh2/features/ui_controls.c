#include "ui_controls.h"
#include "manufacturer_jump.h"
#include "autodrive_game.h"
#include "autodrive_ui.h"
#include "bindings.h"
#include "support.h"

static unsigned char *image;
static int toggle_down, toggle_blocked = 1;
static unsigned turbo_interval = 100;
typedef unsigned char (*MenuKey)(void *, unsigned char, unsigned, unsigned, unsigned char, unsigned char);
static struct {
    void *window, *root, *controller;
    unsigned scan;
    unsigned char extended, system;
    unsigned key;
    uint64_t next;
} repeat;

static uint64_t clock_now(void) {
#ifdef FH2_TWEAKS_TEST
    extern uint64_t ui_controls_test_clock;
    return ui_controls_test_clock;
#else
    return GetTickCount64();
#endif
}

static void release_ui(void *object) {
    if (object && InterlockedDecrement((volatile LONG *)((unsigned char *)object + 0x10)) == 0)
        ((void (*)(void *))(image + 0x257730))(object);
}

static void clear_repeat(void) {
    void *window = repeat.window, *root = repeat.root;
    repeat.window = repeat.root = repeat.controller = NULL;
    repeat.key = 0;
    release_ui(root);
    release_ui(window);
}

void ui_controls_restore(void) {
    autodrive_ui_restore_manual();
    toggle_blocked = 1;
    clear_repeat();
}

static void *current_root(void *window) {
    void *owner = *(void **)((unsigned char *)window + 0x68), *root = NULL;
    if (owner)
        ((void **(*)(void *, void **))(image + 0xb70d30))(owner, &root);
    return root; // Native getter returns one owned AVUI reference.
}

static unsigned shoulder(void **controller) {
    unsigned short buttons;
    if (!autodrive_game_window_focused() ||
        autodrive_game_buttons(controller, &buttons) != 0 ||
        autodrive_game_has_gameplay_input(*controller))
        return 0;
    unsigned held = buttons & 0x300;
    return held == 0x100 ? 0x21 : held == 0x200 ? 0x22 : 0;
}

// Native WindowLayerManager input first offers the key to the game's Scene
// handler, then falls back to AVUI. Most carousels consume shoulders in Scene,
// so replaying an ItemsSelector handler bypasses their actual navigation.
static unsigned char menu_key(void *window, unsigned char up, unsigned key, unsigned scan,
                               unsigned char extended, unsigned char system) {
    void *active=current_root(window);
    int consumed=manufacturer_jump_menu_key(active,up);
    release_ui(active);
    if(consumed){clear_repeat();return 1;}
    unsigned char result = ((MenuKey)(image + 0x1422de0))(
        window, up, key, scan, extended, system);
    if (up || (key != 0x21 && key != 0x22) || !turbo_interval) {
        clear_repeat();
        return result;
    }
    void *controller = NULL;
    if (shoulder(&controller) != key) {
        clear_repeat();
        return result;
    }
    void *root = current_root(window);
    if (!root) {
        clear_repeat();
        return result;
    }
    if (repeat.window == window && repeat.root == root &&
        repeat.controller == controller && repeat.key == key) {
        uint64_t next = clock_now() + turbo_interval;
        if (repeat.next < next)
            repeat.next = next;
        release_ui(root);
        return result;
    }
    clear_repeat();
    InterlockedIncrement((volatile LONG *)((unsigned char *)window + 0x10));
    repeat.window = window;
    repeat.root = root;
    repeat.controller = controller;
    repeat.key = key;
    repeat.scan = scan;
    repeat.extended = extended;
    repeat.system = system;
    repeat.next = clock_now() + 500;
    return result;
}

static void repeat_tick(void) {
    if (!repeat.window)
        return;
    void *controller = NULL;
    void *root = current_root(repeat.window);
    int same = root && root == repeat.root;
    release_ui(root);
    if (!same || shoulder(&controller) != repeat.key || controller != repeat.controller) {
        clear_repeat();
        return;
    }
    uint64_t now = clock_now();
    if (now < repeat.next)
        return;
    repeat.next = now + turbo_interval;
    void *window = repeat.window;
    unsigned key = repeat.key, scan = repeat.scan;
    unsigned char extended = repeat.extended, system = repeat.system;
    InterlockedIncrement((volatile LONG *)((unsigned char *)window + 0x10));
    // Deliver an ordinary release/press through both native menu input paths.
    // Never modify the real controller, gameplay input, or a carousel's state.
    ((MenuKey)(image + 0x1422de0))(window, 1, key, scan, extended, system);
    if (repeat.window == window && repeat.key == key) {
        root = current_root(window);
        same = root && root == repeat.root;
        release_ui(root);
        if (same)
            ((MenuKey)(image + 0x1422de0))(window, 0, key, scan, extended, system);
        else
            clear_repeat();
    }
    release_ui(window);
}

static void toggle_tick(void) {
    unsigned short buttons = 0;
    void *controller = NULL;
    int focused = autodrive_game_window_focused();
    if (focused && binding_xbox(BIND_TOGGLE_UI))
        autodrive_game_buttons(&controller, &buttons);
    int down = focused && binding_down(BIND_TOGGLE_UI, buttons);
    if (!focused)
        toggle_blocked = 1;
    else if (!down)
        toggle_blocked = 0;
    if (down && !toggle_down && !toggle_blocked)
        autodrive_ui_toggle_manual();
    toggle_down = down;
}

static void application_update(void *application) {
    ((void (*)(void *))(image + 0x15912e0))(application);
    toggle_tick();
    repeat_tick();
}

PatchDefinition ui_controls_hook_patches[3];
static unsigned char originals[3][8], replacements[3][8];
static const unsigned char *accepted[3][2];
void ui_controls_prepare_hooks(unsigned char *base, const ModOptions *options) {
    if ((image && image != base) || !options->enabled)
        ui_controls_restore();
    image = base;
    turbo_interval = (unsigned)options->menu_turbo_interval_ms;
    if (!turbo_interval) clear_repeat();
    static const unsigned slots[] = {0x2ecfa98, 0x2f09078, 0x2ecf9f8};
    static const unsigned native[] = {0x15912e0, 0x15912e0, 0x1422de0};
    const uintptr_t callbacks[] = {(uintptr_t)application_update, (uintptr_t)application_update,
                                   (uintptr_t)menu_key};
    for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
        uintptr_t original = (uintptr_t)(base + native[i]);
        memory_copy(originals[i], &original, 8);
        memory_copy(replacements[i], &callbacks[i], 8);
        accepted[i][0] = originals[i]; accepted[i][1] = replacements[i];
        ui_controls_hook_patches[i] = (PatchDefinition){slots[i], 0, 8, GROUP_VIDEO_OPTIONS,
            "ui_controls", originals[i], replacements[i], replacements[i], accepted[i], 2};
    }
}

#ifdef FH2_TWEAKS_TEST
uint64_t ui_controls_test_clock;
__declspec(dllexport) void Fh2TestUiControlsTime(uint64_t now) { ui_controls_test_clock = now; }
__declspec(dllexport) void Fh2TestUiControlsCinematic(int hidden) { autodrive_ui_set_hidden(hidden); }
#endif
