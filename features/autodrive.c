#include "autodrive_game.h"
#include "game_input.h"
#include "autodrive_notice.h"
#include "autodrive_ui.h"
#include "features.h"
#include "logging.h"
#include "support.h"
#include "vehicle_controls.h"

#define CINEMATIC_DELAY 3.0f

typedef void (*UpdateFn)(void *, float);
typedef void (*ActionFn)(void *);
typedef void *(*DestroyFn)(void *, unsigned);
static unsigned char *game_image;
static int cinematic_enabled, autodrive_enabled, races_enabled;
static int f2_was_down, pad_was_down, pad_blocked = 1;

void autodrive_set_cinematic(int enabled) {
    cinematic_enabled = enabled != 0;
}

static void *previous_controller;
static void *available_tracker;

typedef struct {
    void *satnav, *tracker, *car, *route;
    float elapsed;
    int cinematic, tried_camera, context, route_owned;
    AutodriveCamera camera;
} AutodriveState;

static AutodriveState drive;

static void stop_driving(int flash) {
    if (!drive.car) {
        return;
    }
    void *camera = drive.camera.camera;
    autodrive_notice_clear();
    autodrive_ui_set_hidden(0);
    int restored = autodrive_game_restore_camera(&drive.camera);
    autodrive_game_set_ai(drive.car, 0);
    if (drive.route_owned) {
        autodrive_game_end_route(drive.tracker);
    }
    autodrive_game_release_car(drive.car);
    if (flash && restored) {
        autodrive_game_flash(camera);
    }
    drive = (AutodriveState){0};
}

static void start_driving(void *satnav, void *controller) {
    int context = autodrive_game_context();
    int race = context == AUTODRIVE_CONTEXT_EVENT && races_enabled;
    if ((!race && context != AUTODRIVE_CONTEXT_FREEROAM) ||
        !autodrive_game_has_gameplay_input(controller)) {
        return;
    }
    void *route = 0;
    if (!race) {
        int arrived;
        route = autodrive_game_route(satnav, &arrived);
        if (!route || arrived || !autodrive_game_tracker_matches(available_tracker, satnav)) {
            return;
        }
    }
    void *car = autodrive_game_get_car();
    if (!car) {
        return;
    }
    if (autodrive_game_driver_mode(car) != 1) {
        autodrive_game_release_car(car);
        return;
    }
    if (!race && !autodrive_game_begin_route(available_tracker)) {
        autodrive_game_release_car(car);
        log_append("AUTODRIVE: GPS event-route tracking could not start, or is "
                   "already owned.\r\n");
        return;
    }
    if (!autodrive_game_set_ai(car, 1)) {
        if (autodrive_game_driver_mode(car) == 0) {
            autodrive_game_set_ai(car, 0);
        }
        if (!race) {
            autodrive_game_end_route(available_tracker);
        }
        autodrive_game_release_car(car);
        log_append("AUTODRIVE: native AI driver handoff failed.\r\n");
        return;
    }
    drive.satnav = satnav;
    drive.tracker = race ? 0 : available_tracker;
    drive.car = car;
    drive.route = route;
    drive.context = context;
    drive.route_owned = !race;
    if (!race) {
        autodrive_notice_show();
    } else {
        log_append("AUTODRIVE: native AI handoff started in event context without a GPS route.\r\n");
    }
}

static void enter_cinematic(void) {
    drive.tried_camera = 1;
    if (!autodrive_game_slow_spin(&drive.camera)) {
        return;
    }
    drive.cinematic = 1;
    autodrive_ui_set_hidden(1);
    autodrive_game_flash(drive.camera.camera);
}

static void autodrive_tick(void *satnav, float dt, int f2, int escape, int focused) {
    int pressed = f2 && !f2_was_down;
    f2_was_down = f2;
    void *controller;
    int down = autodrive_game_dpad_down(&controller);
    if (down < 0 || controller != previous_controller || !focused) {
        pad_blocked = 1;
    } else if (!down) {
        pad_blocked = 0;
    }
    pressed |= down > 0 && !pad_was_down && !pad_blocked;
    pad_was_down = down > 0;
    previous_controller = controller;
    if (drive.car) {
        if (satnav != drive.satnav) {
            return;
        }
        if (pressed || escape || !focused) {
            stop_driving(pressed && focused && !escape &&
                         drive.context == AUTODRIVE_CONTEXT_FREEROAM &&
                         autodrive_game_is_freeroam());
            return;
        }
        if (autodrive_game_context() != drive.context) {
            stop_driving(0);
            return;
        }
        if (autodrive_game_driving_input(controller)) {
            stop_driving(1);
            return;
        }
        void *current_car = autodrive_game_get_car();
        int same_car = current_car == drive.car;
        autodrive_game_release_car(current_car);
        if (!same_car || autodrive_game_driver_mode(drive.car) != 0) {
            stop_driving(0);
            return;
        }
        if (drive.context == AUTODRIVE_CONTEXT_FREEROAM) {
            int arrived;
            void *route = autodrive_game_route(satnav, &arrived);
            if (arrived || route != drive.route) {
                stop_driving(1);
                return;
            }
        }
        if (dt > 0.0f && dt <= 1.0f) {
            drive.elapsed += dt;
        }
        autodrive_notice_update(drive.elapsed);
        if (cinematic_enabled && drive.context == AUTODRIVE_CONTEXT_FREEROAM &&
            !drive.tried_camera && drive.elapsed >= CINEMATIC_DELAY) {
            enter_cinematic();
        } else if (drive.cinematic) {
            autodrive_game_cycle_camera(&drive.camera);
        }
    } else if (focused && pressed && !escape) {
        start_driving(satnav, controller);
    }
}

static void satnav_update(void *self, float dt) {
    ((UpdateFn)(game_image + 0x6b4d30))(self, dt);
    if (autodrive_enabled) {
        KeyboardInput input = game_input_read();
        autodrive_tick(self, dt, input.toggle, input.cancel, input.focused);
    }
}

static void satnav_deactivate(void *self) {
    if (self == drive.satnav) {
        stop_driving(0);
    }
    ((ActionFn)(game_image + 0x6c0c70))(self);
}

static void tracker_update(void *self, float dt) {
    available_tracker = self;
    ((UpdateFn)(game_image + 0x6b4e40))(self, dt);
}

static void *tracker_destroy(void *self, unsigned flags) {
    if (self == drive.tracker) {
        stop_driving(0);
    }
    if (self == available_tracker) {
        available_tracker = 0;
    }
    return ((DestroyFn)(game_image + 0x6a26e0))(self, flags);
}

static void pause_enter(void *self) {
    stop_driving(0);
    ((ActionFn)(game_image + 0x8e98e0))(self);
}

static void event_context_enter(void *self) {
    stop_driving(0);
    ((ActionFn)(game_image + 0x912860))(self);
}

static unsigned char original_pointers[12][8], hook_pointers[12][8];
static const unsigned char *accepted_pointers[12][2];
PatchDefinition autodrive_hook_patches[12];

void autodrive_prepare_hooks(unsigned char *image, const ModOptions *options) {
    static const unsigned slots[] = {0x2cc3198, 0x2cc31b0, 0x2cc3288, 0x2cc3278,
                                     0x2d16698, 0x2d1ea50, 0x2e7f040, 0x2ecfaa8,
                                     0x2ecfaa0, 0x2e832a8, 0x2f09088, 0x2f09080};
    static const unsigned originals[] = {0x6b4d30,  0x6c0c70,  0x6b4e40,  0x6a26e0,
                                         0x8e98e0,  0x912860,  0x1140ed0, 0x15b51a0,
                                         0x15b5110, 0x1140ed0, 0x15b51a0, 0x15b5110};
    static const char *names[] = {"autodrive_gps_update",     "autodrive_gps_deactivation",
                                  "autodrive_tracker_update", "autodrive_tracker_destruction",
                                  "autodrive_pause_entry",    "autodrive_event_context_entry",
                                  "autodrive_ui_wait_render", "autodrive_ui_draw",
                                  "autodrive_ui_present",     "autodrive_ui_wait_render_plain",
                                  "autodrive_ui_draw_base",   "autodrive_ui_present_base"};
    uintptr_t hooks[] = {(uintptr_t)satnav_update,
                         (uintptr_t)satnav_deactivate,
                         (uintptr_t)tracker_update,
                         (uintptr_t)tracker_destroy,
                         (uintptr_t)pause_enter,
                         (uintptr_t)event_context_enter,
                         (uintptr_t)autodrive_ui_wait_render,
                         (uintptr_t)autodrive_ui_draw,
                         (uintptr_t)autodrive_ui_present,
                         (uintptr_t)autodrive_ui_wait_render,
                         (uintptr_t)autodrive_ui_draw,
                         (uintptr_t)autodrive_ui_present};
    game_image = image;
    autodrive_game_bind(image);
    vehicle_controls_bind(image);
    autodrive_enabled = options->autodrive;
    races_enabled = options->autodrive_races;
    autodrive_notice_prepare(image);
    autodrive_ui_prepare(image);
    cinematic_enabled = options->autodrive_cinematic;
    for (unsigned i = 0; i < 12; ++i) {
        uintptr_t original = (uintptr_t)(image + originals[i]);
        memory_copy(original_pointers[i], &original, 8);
        memory_copy(hook_pointers[i], &hooks[i], 8);
        accepted_pointers[i][0] = original_pointers[i];
        accepted_pointers[i][1] = hook_pointers[i];
        autodrive_hook_patches[i] = (PatchDefinition){slots[i],
                                                      0,
                                                      8,
                                                      GROUP_AUTODRIVE,
                                                      names[i],
                                                      original_pointers[i],
                                                      hook_pointers[i],
                                                      hook_pointers[i],
                                                      accepted_pointers[i],
                                                      2};
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestAutodriveState(void) {
    return drive.car ? (drive.cinematic ? 2 : 1) : 0;
}

__declspec(dllexport) void *Fh2TestAutodriveHookAddress(unsigned index) {
    uintptr_t address = 0;
    if (index < 12) {
        memory_copy(&address, hook_pointers[index], 8);
    }
    return (void *)address;
}
#endif
