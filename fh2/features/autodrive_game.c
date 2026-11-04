#include "autodrive_game.h"
#include "game_addresses.h"
#include "logging.h"
#include "support.h"
#include "bindings.h"

static unsigned char *image;
typedef void *(*SharedGetter)(void *, GameShared *);
typedef void (*ObjectAction)(void *);
typedef void *(*PlayerGetter)(void *, int);
typedef void *(*CarGetter)(void *, void **);
typedef void (*DriverSetter)(void *, unsigned char);
typedef void (*GroupSetter)(void *, int);
typedef void (*ReplaySelector)(void *, unsigned char);
typedef int *(*GroupGetter)(void *);
typedef void *(*CameraGetter)(void *);
typedef void (*ViewFlash)(void *, int, float);

static void *game_function(unsigned rva) {
    return image + rva;
}

static uintptr_t method(void *object, unsigned slot) {
    void *vtable;
    uintptr_t address;
    if (!memory_read_pointer(object, &vtable) ||
        !memory_read((unsigned char *)vtable + slot, &address, 8) ||
        address < (uintptr_t)image + 0x1000 || address >= (uintptr_t)image + 0x1dcc000) {
        return 0;
    }
    return address;
}

static int has_vtable(void *object, unsigned rva) {
    void *vtable;
    return memory_read_pointer(object, &vtable) && vtable == image + rva;
}

void autodrive_game_release_shared(GameShared *value) {
    unsigned char *counter = value->counter;
    if (counter && InterlockedDecrement((volatile LONG *)(counter + 8)) == 0) {
        ((ObjectAction)((uintptr_t *)*(void **)counter)[0])(counter);
        if (InterlockedDecrement((volatile LONG *)(counter + 12)) == 0) {
            ((ObjectAction)((uintptr_t *)*(void **)counter)[1])(counter);
        }
    }
    *value = (GameShared){0};
}

void autodrive_game_bind(unsigned char *base) {
    image = base;
}

static int controller_status(int status) {
    static int previous = -2;
    if (status != previous) {
        log_append(status < 0 ? "AUTODRIVE: active controller is unavailable.\r\n"
                              : "AUTODRIVE: native controller button reader ready.\r\n");
        previous = status;
    }
    return status;
}

int autodrive_game_buttons(void **controller, unsigned short *buttons) {
    void *manager;
    unsigned char connected;
    *buttons = 0;
    *controller = 0;
    if (!memory_read_pointer(image + 0x365d7c8, &manager) || !has_vtable(manager, 0x2e7a2e8) ||
        !memory_read_pointer((unsigned char *)manager + 0xab28, controller)) {
        return -1;
    }
    uintptr_t first = (uintptr_t)manager + 0x10;
    uintptr_t selected = (uintptr_t)*controller;
    if (selected < first || selected >= first + 8 * 0x1300 || (selected - first) % 0x1300 ||
        !memory_read((unsigned char *)*controller + 4, &connected, 1) || connected != 1 ||
        !memory_read((unsigned char *)*controller + 0xc0, buttons, sizeof(*buttons))) {
        *controller = 0;
        return -1;
    }
    return 0;
}

int autodrive_game_dpad_down(void **controller) {
    unsigned short buttons;
    int status = controller_status(autodrive_game_buttons(controller, &buttons));
    return status < 0 ? status : binding_pad_down(BIND_AUTODRIVE, buttons);
}

int autodrive_game_driving_input(void *controller) {
    unsigned char triggers[2];
    float steering;
    if (!controller ||
        !memory_read((unsigned char *)controller + 0x70, triggers, sizeof(triggers)) ||
        !memory_read((unsigned char *)controller + 0xa0, &steering, sizeof(steering))) {
        return 0;
    }
    // These values have already passed the native trigger and stick deadzones.
    return triggers[0] || triggers[1] || steering < -0.05f || steering > 0.05f;
}

static int simulation_is_running(void) {
    void *game, *simulation;
    unsigned char paused;
    return memory_read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game) &&
           memory_read_pointer((unsigned char *)game + 0x48, &simulation) &&
           memory_read((unsigned char *)simulation + 0x28, &paused, 1) && !paused;
}

int autodrive_game_event_context(void) {
    void *hud_manager, *services;
    if (!memory_read_pointer(image + 0x34c0f40, &hud_manager) ||
        !memory_read_pointer((unsigned char *)hud_manager + 8, &services)) {
        return -1;
    }
    GameShared context = {0};
    ((SharedGetter)game_function(0x2129a0))(hud_manager, &context);
    int event_context = -1;
    memory_read((unsigned char *)context.object + 0x60, &event_context, 4);
    autodrive_game_release_shared(&context);
    return event_context;
}

int autodrive_game_is_freeroam(void) {
    return simulation_is_running() && autodrive_game_event_context() == 2;
}

int autodrive_game_has_gameplay_input(void *controller) {
    struct {
        void *proxy, **blocks;
        size_t capacity, start, count;
    } contexts;

    if (!controller || !simulation_is_running() ||
        !memory_read((unsigned char *)controller + 0x38, &contexts, sizeof(contexts))) {
        return 0;
    }
    if (!contexts.count || !contexts.blocks || !contexts.capacity || contexts.capacity > 4096 ||
        (contexts.capacity & (contexts.capacity - 1)) || contexts.count > contexts.capacity * 2 ||
        contexts.start > SIZE_MAX - contexts.count) {
        return 0;
    }
    void *block;
    unsigned permissions;
    size_t slot = (contexts.start / 2) & (contexts.capacity - 1);
    return memory_read_pointer(contexts.blocks + slot, &block) &&
           memory_read((unsigned char *)block + (contexts.start % 2) * 8 + 4, &permissions,
                       sizeof(permissions)) &&
           (permissions & 1);
}

int autodrive_game_can_start(void *controller) {
    return autodrive_game_is_freeroam() && autodrive_game_has_gameplay_input(controller);
}

int autodrive_game_can_drive(void *controller) {
    if (!autodrive_game_has_gameplay_input(controller)) {
        return 0;
    }
    switch (autodrive_game_presentation_mode()) {
        case 3:
        case 4:
        case 6:
        case 9:
        case 12:
            return 1;
        default:
            return 0;
    }
}

void *autodrive_game_event_route(void *car) {
    void *bindings, *property, *route;
    unsigned char valid;
    // CarRouteTrackingComponent's Event (3) binding. Read the route property;
    // the game owns its tracker, checkpoints, laps and lifetime throughout.
    if (!has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE) ||
        !memory_read_pointer((unsigned char *)car + 0x4f70, &bindings) ||
        !has_vtable(bindings, 0x2f5d850)) {
        return NULL;
    }
    // Event first. Road trips can instead use GroupSatNav or PersonalSatNav.
    // Personal (the free-roam random route) is deliberately never a candidate.
    static const unsigned offsets[] = {0x10, 0x88, 0x60};
    unsigned count = autodrive_game_is_freeroam() ? 1 : ARRAY_COUNT(offsets);
    for (unsigned i = 0; i < count; ++i) {
        if (memory_read_pointer((unsigned char *)bindings + offsets[i], &property) &&
            memory_read_pointer(property, &route) &&
            memory_read((unsigned char *)route + 8, &valid, 1) && valid == 1) {
            return route;
        }
    }
    return NULL;
}

int autodrive_game_presentation_mode(void) {
    void *services, *manager, *head;
    if (!memory_read_pointer(image + 0x354f4f8, &services) ||
        !memory_read_pointer((unsigned char *)services + 0x50, &manager) ||
        !memory_read_pointer((unsigned char *)manager + 8, &head)) {
        return -1;
    }
    GameShared presentation = {0};
    ((SharedGetter)game_function(0xf9ae70))(manager, &presentation);
    int mode = -1;
    memory_read((unsigned char *)presentation.object + 0x3cc, &mode, sizeof(mode));
    autodrive_game_release_shared(&presentation);
    return mode;
}

int autodrive_game_is_carmeet(void) {
    return autodrive_game_presentation_mode() == 11;
}

void *autodrive_game_get_car(void) {
    void *game, *car = 0;
    if (!memory_read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game)) {
        return 0;
    }
    void *player = ((PlayerGetter)game_function(GAME_RVA_GET_LOCAL_PLAYER))(game, 0);
    if (!player) {
        return 0;
    }
    ((CarGetter)game_function(GAME_RVA_GET_PLAYER_CAR))(player, &car);
    if (car && !has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE)) {
        autodrive_game_release_car(car);
        return 0;
    }
    return car;
}

void autodrive_game_release_car(void *car) {
    uintptr_t release = method(car, 0x18);
    if (release) {
        ((ObjectAction)release)(car);
    }
}

int autodrive_game_driver_mode(void *car) {
    void *holder, *driver;
    int mode = -1;
    if (!has_vtable(car, GAME_RVA_PLAYER_CAR_VTABLE) ||
        !memory_read_pointer((unsigned char *)car + 0x4d10, &holder) ||
        !memory_read_pointer(holder, &driver) || !method(driver, 0x48) ||
        !memory_read((unsigned char *)car + 0x4d30, &mode, 4)) {
        return -1;
    }
    return mode;
}

int autodrive_game_set_ai(void *car, int enabled) {
    int mode = autodrive_game_driver_mode(car);
    if (mode != (enabled ? 1 : 0)) {
        return 0;
    }
    ((DriverSetter)game_function(0xf79f10))(car, (unsigned char)enabled);
    return autodrive_game_driver_mode(car) == (enabled ? 0 : 1);
}

void autodrive_game_clear_rewind(void) {
    void *history, *recorder;
    if (!memory_read_pointer(image + 0x34d5710, &history) || !memory_is_writable(history, 0x128) ||
        !memory_read_pointer(image + 0x34d5700, &recorder) || !method(recorder, 0x30) ||
        !method(recorder, 0x68)) {
        return;
    }
    ((void (*)(void *, unsigned char, unsigned char))game_function(0x99c4c0))(history, 1, 1);
}

void *autodrive_game_route(void *satnav, int *arrived) {
    void *control, *route_control, *route;
    unsigned char active, reached, valid;
    *arrived = 0;
    if (!has_vtable(satnav, 0x2cc3188) ||
        !memory_read((unsigned char *)satnav + 0x160, &active, 1) || !active ||
        !memory_read_pointer((unsigned char *)satnav + 0x150, &control) ||
        !memory_read_pointer(control, &route_control) ||
        !memory_read((unsigned char *)route_control + 0x8a, &reached, 1) ||
        !memory_read_pointer((unsigned char *)route_control + 0x118, &route) ||
        !memory_read((unsigned char *)route + 8, &valid, 1) || !valid) {
        return 0;
    }
    *arrived = reached != 0;
    return route;
}

static int lock_tracker_reference(void *tracker, unsigned offset, GameShared *reference) {
    GameShared weak;
    LONG owners;
    unsigned char *wrapper = (unsigned char *)tracker + offset;
    if (!memory_read(wrapper + 0x38, &weak, sizeof(weak)) || !weak.object || !weak.counter ||
        !memory_read((unsigned char *)weak.counter + 8, &owners, sizeof(owners)) || owners <= 0) {
        return 0;
    }
    ((SharedGetter)game_function(0x3eb620))(wrapper, reference);
    return reference->object != 0;
}

int autodrive_game_tracker_matches(void *tracker, void *satnav) {
    if (!has_vtable(tracker, 0x2cc3278)) {
        return 0;
    }
    GameShared component = {0}, owner = {0};
    void *route_property, *owner_routing;
    int matches = lock_tracker_reference(tracker, 0x98, &component) && component.object == satnav &&
                  has_vtable(component.object, 0x2cc3188) &&
                  lock_tracker_reference(tracker, 0xe0, &owner) &&
                  memory_read_pointer((unsigned char *)owner.object + 0x128, &owner_routing) &&
                  memory_read_pointer((unsigned char *)tracker + 0x130, &route_property);
    autodrive_game_release_shared(&owner);
    autodrive_game_release_shared(&component);
    return matches;
}

int autodrive_game_begin_route(void *tracker) {
    void *task = 0;
    if (!memory_read((unsigned char *)tracker + 0x128, &task, 8) || task) {
        return 0;
    }
    ((ObjectAction)game_function(0x6bec10))(tracker);
    if (memory_read_pointer((unsigned char *)tracker + 0x128, &task)) {
        return 1;
    }
    autodrive_game_end_route(tracker);
    return 0;
}

void autodrive_game_end_route(void *tracker) {
    if (has_vtable(tracker, 0x2cc3278)) {
        ((ObjectAction)game_function(0x6a5fa0))(tracker);
    }
}

static void *primary_view(void) {
    void *game, *simulation, *manager, *views, *view;
    if (!memory_read_pointer(image + GAME_RVA_PLAYER_MANAGER, &game) ||
        !memory_read_pointer((unsigned char *)game + 0x48, &simulation) ||
        !memory_read_pointer((unsigned char *)simulation + 0x98, &manager) ||
        !memory_read_pointer((unsigned char *)manager + 0x2d0, &views) ||
        !memory_read_pointer(views, &view)) {
        return 0;
    }
    return view;
}

void *autodrive_game_camera(void) {
    void *view = primary_view();
    uintptr_t get_camera = method(view, 0x38);
    if (!get_camera) {
        return 0;
    }
    void *camera = ((CameraGetter)get_camera)(view);
    return has_vtable(camera, 0x2c6c840) ? camera : 0;
}

int autodrive_game_flash(void *camera) {
    void *view = primary_view(), *view_camera;
    unsigned char busy;
    if ((!has_vtable(view, 0x2e6e890) && !has_vtable(view, 0x2c66188)) || !camera ||
        autodrive_game_camera() != camera) {
        return 0;
    }
    if (!memory_read_pointer((unsigned char *)view + 0x40, &view_camera) ||
        !method(view_camera, 0x178) || !memory_read((unsigned char *)view + 0xb1, &busy, 1) ||
        !memory_is_writable((unsigned char *)view + 0xb1, 15)) {
        return 0;
    }
    if (busy) {
        return 0;
    }
    ((ViewFlash)game_function(0xfddae0))(view, 3, 0.6f);
    return 1;
}

int autodrive_game_camera_group(void *camera) {
    int value = -1;
    void *head;
    size_t count;
    if (!has_vtable(camera, 0x2c6c840) ||
        !memory_read_pointer((unsigned char *)camera + 0x1b0, &head) ||
        !memory_read((unsigned char *)camera + 0x1d0, &count, 8) || !count) {
        return -1;
    }
    int *current = ((GroupGetter)game_function(0x399ff0))((unsigned char *)camera + 0x1b0);
    return memory_read(current, &value, 4) ? value : -1;
}

int autodrive_game_set_camera_group(void *camera, int group) {
    unsigned char *groups, *end;
    void *target;
    if (group < 0 || group > 31 || !has_vtable(camera, 0x2c6c840) ||
        !memory_read_pointer((unsigned char *)camera + 0x188, (void **)&groups) ||
        !memory_read_pointer((unsigned char *)camera + 0x190, (void **)&end) || end < groups ||
        (size_t)(end - groups) / 64 <= (unsigned)group ||
        !memory_read_pointer(groups + (size_t)group * 64 + 0x30, &target)) {
        return 0;
    }
    ((GroupSetter)game_function(0xfe5780))(camera, group);
    return autodrive_game_camera_group(camera) == group;
}

static int relative_camera_data(void *camera, void **relative, unsigned char **begin,
                                size_t *count) {
    void *data;
    unsigned char *end;
    if (!has_vtable(camera, 0x2c6c840) ||
        !memory_read_pointer((unsigned char *)camera + 0x2f0, relative) ||
        !memory_read_pointer((unsigned char *)*relative + 0x188, &data) ||
        !memory_read_pointer((unsigned char *)data + 8, (void **)begin) ||
        !memory_read_pointer((unsigned char *)data + 0x10, (void **)&end) || end <= *begin ||
        (size_t)(end - *begin) % 0x4d0 || (size_t)(end - *begin) / 0x4d0 > 512) {
        return 0;
    }
    *count = (size_t)(end - *begin) / 0x4d0;
    return 1;
}

int autodrive_game_slow_spin(AutodriveCamera *saved) {
    void *camera = autodrive_game_camera(), *relative;
    unsigned char *entries, *groups, *groups_end;
    size_t count;
    double previous_selection;
    int previous = autodrive_game_camera_group(camera), found = 0;
    if ((previous != 0 && previous != 1) ||
        !relative_camera_data(camera, &relative, &entries, &count) ||
        !memory_read((unsigned char *)relative + 0x748, &previous_selection, 8) ||
        !(previous_selection >= 0 && previous_selection < (double)count) ||
        !memory_is_writable((unsigned char *)relative + 0x748, 8) ||
        !memory_read_pointer((unsigned char *)camera + 0x188, (void **)&groups) ||
        !memory_read_pointer((unsigned char *)camera + 0x190, (void **)&groups_end) ||
        groups_end <= groups || (size_t)(groups_end - groups) % 64 ||
        (size_t)(groups_end - groups) / 64 < 19 || (size_t)(groups_end - groups) / 64 > 32) {
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        unsigned char label;
        if (!memory_read(entries + i * 0x4d0 + 0x20, &label, 1)) {
            return 0;
        }
        found |= label == 17;
    }
    void *target;
    if (!found || !memory_read(groups + 13 * 64 + 0x30, &target, 8) || target != relative) {
        return 0;
    }
    *saved = (AutodriveCamera){camera, relative, previous, previous_selection, 0, 17, 1};
    ((ReplaySelector)game_function(0xfe5e30))(camera, 17);
    double selection;
    unsigned char label = 0;
    int selected = memory_read((unsigned char *)relative + 0x748, &selection, 8) &&
                   selection >= 0 && selection < (double)count &&
                   memory_read(entries + (size_t)selection * 0x4d0 + 0x20, &label, 1) &&
                   label == 17 && autodrive_game_camera_group(camera) == 13;
    if (!selected) {
        autodrive_game_set_camera_group(camera, previous);
        *(double *)((unsigned char *)relative + 0x748) = previous_selection;
        *saved = (AutodriveCamera){0};
        return 0;
    }
    saved->selection = selection;
    return 1;
}

static int owns_relative_camera(const AutodriveCamera *saved) {
    void *relative;
    double selection;
    return saved->camera && autodrive_game_camera() == saved->camera &&
           autodrive_game_camera_group(saved->camera) == 13 &&
           memory_read_pointer((unsigned char *)saved->camera + 0x2f0, &relative) &&
           relative == saved->relative &&
           memory_read((unsigned char *)relative + 0x748, &selection, 8) &&
           selection == saved->selection;
}

static int relative_animation_finished(void *relative, unsigned char *entry, size_t selection) {
    unsigned char *player = (unsigned char *)relative + 0x190, *keys, *end;
    void *config;
    unsigned type, active;
    float elapsed, first, last;
    if (!has_vtable(relative, 0x2c6cb70) || !has_vtable(player, 0x2e779d8) ||
        !memory_read(entry + 0x24, &type, 4) || type != 0 ||
        !memory_read((unsigned char *)relative + 0x740, &active, 4) || active != selection ||
        !memory_read_pointer(player + 0x188, &config) || config != entry + 0x80 ||
        !memory_read_pointer(player + 0x190, (void **)&keys) ||
        !memory_read_pointer(player + 0x198, (void **)&end) || end <= keys ||
        (size_t)(end - keys) % 64 || (size_t)(end - keys) / 64 > 4096 ||
        !memory_read(keys, &first, 4) || !memory_read(end - 64, &last, 4) ||
        !memory_read(player + 0x288, &elapsed, 4) || !(first >= 0 && last > first && last <= 600) ||
        !(elapsed >= first && elapsed <= last + 1)) {
        return 0;
    }
    return elapsed >= last - 0.001f;
}

void autodrive_game_cycle_camera(AutodriveCamera *saved) {
    void *relative, *active_camera;
    unsigned char *entries;
    size_t count;
    if (!saved->cycling) {
        return;
    }
    if (!owns_relative_camera(saved)) {
        saved->cycling = 0;
        return;
    }
    if (!relative_camera_data(saved->camera, &relative, &entries, &count) ||
        !(saved->selection >= 0 && saved->selection < (double)count) ||
        saved->selection != (double)(size_t)saved->selection) {
        saved->cycling = 0;
        return;
    }
    unsigned char *entry = entries + (size_t)saved->selection * 0x4d0;
    unsigned char label;
    if (!memory_read(entry + 0x20, &label, 1) || label != saved->label) {
        saved->cycling = 0;
        return;
    }
    if (!relative_animation_finished(relative, entry, (size_t)saved->selection)) {
        return;
    }
    unsigned char next_label = saved->label == 17 ? 19 : 17;
    int found = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!memory_read(entries + i * 0x4d0 + 0x20, &label, 1)) {
            saved->cycling = 0;
            return;
        }
        found |= label == next_label;
    }
    if (!found || !memory_read_pointer((unsigned char *)saved->camera + 0x208, &active_camera) ||
        active_camera != relative || method(relative, 0x20) != (uintptr_t)game_function(0x3979a0)) {
        saved->cycling = 0;
        log_append("AUTODRIVE: next cinematic camera unavailable; cycling stopped.\r\n");
        return;
    }
    ((ReplaySelector)game_function(0xfe5e30))(saved->camera, next_label);
    double selection;
    if (!memory_read((unsigned char *)relative + 0x748, &selection, 8) ||
        !(selection >= 0 && selection < (double)count) ||
        !memory_read(entries + (size_t)selection * 0x4d0 + 0x20, &label, 1) ||
        label != next_label || autodrive_game_camera_group(saved->camera) != 13) {
        saved->cycling = 0;
        log_append("AUTODRIVE: native cinematic camera switch failed; cycling "
                   "stopped.\r\n");
        return;
    }
    saved->selection = selection;
    saved->label = label;
}

int autodrive_game_restore_camera(AutodriveCamera *saved) {
    int restored = 0;
    if (owns_relative_camera(saved) &&
        memory_is_writable((unsigned char *)saved->relative + 0x748, 8) &&
        autodrive_game_set_camera_group(saved->camera, saved->previous_group)) {
        *(double *)((unsigned char *)saved->relative + 0x748) = saved->previous_selection;
        restored = 1;
    }
    *saved = (AutodriveCamera){0};
    return restored;
}

static int lock_hud(GameShared wrapper, GameShared *hud) {
    GameShared weak;
    LONG owners;
    if (!wrapper.object ||
        !memory_read((unsigned char *)wrapper.object + 0x68, &weak, sizeof(weak)) || !weak.object ||
        !weak.counter || !memory_read((unsigned char *)weak.counter + 8, &owners, sizeof(owners)) ||
        owners <= 0) {
        return 0;
    }
    ((SharedGetter)game_function(0x3eb640))((unsigned char *)wrapper.object + 0x50, hud);
    return hud->object != 0;
}

int autodrive_game_get_hud(AutodriveHud *saved) {
    void *manager, *services;
    GameShared wrapper = {0}, hud = {0};
    unsigned char suspended = 1;
    if (memory_read_pointer(image + 0x34c0f40, &manager) &&
        memory_read_pointer((unsigned char *)manager + 8, &services)) {
        ((SharedGetter)game_function(0x91a4a0))(manager, &wrapper);
        if (lock_hud(wrapper, &hud) &&
            memory_read((unsigned char *)hud.object + 0xa8, &suspended, 1) && !suspended) {
            *saved = (AutodriveHud){wrapper, hud};
            return 1;
        }
    }
    autodrive_game_release_shared(&hud);
    autodrive_game_release_shared(&wrapper);
    return 0;
}
