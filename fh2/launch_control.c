// Launch control: hold the LaunchControl binding (default LT+RT on a controller) with the car
// stopped, and the throttle is worked so the engine sits at [LaunchControl] RPM percent of its
// redline. Let go of the binding (the brake) with the throttle still down to launch.
//
// At a standstill on the brake or handbrake the game already lets the engine rev freely, so
// only the throttle is changed. The player car's controls are built from the game's actions
// (game+0x2fd740), and its throttle is the analog value of action 0 (Gas), read through
// game+0x106e3e0 at the call at game+0x2fdb85; that call is pointed at launch_gas, through a
// 12-byte thunk in padding at game+0x2fead4 (as engine_swaps does), which answers with the held
// throttle while holding and with the game's own value otherwise. (The physics input frame,
// slots 0x150/0x90 of the physics vtable, only mirrors it: setting it doesn't change the revs.)
// The revs come from the physics object: +0x148 engine speed, +0x1C8 redline, +0x1CC limiter,
// all rad/s (the HUD rev counter's getters, slots 0x1A0, 0xF0 and 0x338, turn them into rpm). A
// PI controller with a 0.1 s look-ahead holds them: an engine free-revving at a standstill loses
// revs slowly, so it eases off before the target instead of overshooting it.
#include "fh2/launch_control.h"
#include "fh2/bindings.h"
#include "fh2/game_input.h"
#include "fh2/autodrive_game.h"
#include "shared/logging.h"
#include "shared/support.h"


#define CAR_PHYSICS 0x4c08
#define CAR_TARGET 0x20
#define PHYSICS_VTABLE 0x2eb5e18
#define TARGET_VTABLE 0x2c5ce28
#define VELOCITY_GETTER 0xf646b0
#define ENGINE_SPEED 0x148
#define ENGINE_REDLINE 0x1c8
#define ENGINE_LIMIT 0x1cc
#define RAD_TO_RPM 9.549296f
#define MOVING_SQUARED 0.25f  /* 0.5 m/s: arms below this */
#define TOO_FAST_SQUARED 9.0f /* 3 m/s: lets go above this */
#define ACTION_VALUE 0x106e3e0 /* float (manager, player, action) */
#define GAS_CALL 0x2fdb85      /* call ACTION_VALUE for action 0, in the car's controls builder */
#define GAS_THUNK 0x2fead4     /* 12 bytes of int3 padding after game+0x2fea40 */

typedef float (*ActionValue)(void *, int, int);
typedef void *(*VelocityGetter)(void *, float *);

static unsigned char *game_image;
static int enabled;
static float share = 0.60f;

static struct {
    int held, logged_hold, logged_launch;
    float integral, last_rpm, rate;
} state;

static float clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

static volatile int holding;
static volatile float held_gas;

// The car's throttle read (action 0 at GAS_CALL): the held throttle while holding the revs.
static float launch_gas(void *manager, int player, int action) {
    float value = ((ActionValue)(game_image + ACTION_VALUE))(manager, player, action);
    return holding && action == 0 ? held_gas : value;
}

static unsigned char thunk[12], call[5];
static const unsigned char padding[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                          0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char original_call[5] = {0xe8, 0x56, 0x08, 0xd7, 0x00};
static const unsigned char *const accepted_thunk[] = {padding, thunk};
static const unsigned char *const accepted_call[] = {original_call, call};
PatchDefinition launch_control_hook_patches[2] = {
    {GAS_THUNK, 0, 12, GROUP_LAUNCH_CONTROL, "launch_control_gas_thunk", padding, thunk, thunk, accepted_thunk, 2},
    {GAS_CALL, 0, 5, GROUP_LAUNCH_CONTROL, "launch_control_gas_read", original_call, call, call, accepted_call, 2},
};

void launch_control_bind(unsigned char *image, const ModOptions *options) {
    game_image = image;
    enabled = options->launch_control;
    share = clamp(options->launch_control_rpm * 0.01f, 0.2f, 0.95f);
    state.held = 0;
    holding = 0;
    const unsigned char jump[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t callback = (uintptr_t)launch_gas;
    memory_copy(thunk, jump, 12);
    memory_copy(thunk + 2, &callback, 8);
    int32_t displacement = GAS_THUNK - GAS_CALL - 5;
    call[0] = 0xe8;
    memory_copy(call + 1, &displacement, 4);
}

static void *car_physics(void *car) {
    void *physics, *vtable;
    if (!memory_read_pointer((unsigned char *)car + CAR_PHYSICS, &physics) || !physics ||
        !memory_read_pointer(physics, &vtable) || vtable != game_image + PHYSICS_VTABLE) {
        return NULL;
    }
    return physics;
}

// The car's speed squared (m/s), or -1 if it can't be read.
static float speed_squared(void *car) {
    unsigned char *target = (unsigned char *)car + CAR_TARGET;
    void *vtable;
    uintptr_t getter;
    if (!memory_read_pointer(target, &vtable) || vtable != game_image + TARGET_VTABLE ||
        !memory_read((unsigned char *)vtable + 0x10, &getter, sizeof(getter)) ||
        getter != (uintptr_t)game_image + VELOCITY_GETTER) {
        return -1.0f;
    }
    _Alignas(16) float velocity[4] = {0};
    ((VelocityGetter)(game_image + VELOCITY_GETTER))(target, velocity);
    float sum = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!float_is_finite(velocity[i])) {
            return -1.0f;
        }
        sum += velocity[i] * velocity[i];
    }
    return sum;
}

// The throttle (RT) on the controller the game reads, 0..255.
static int throttle_trigger(void) {
    void *controller;
    unsigned short buttons;
    unsigned char values[2];
    if (autodrive_game_buttons(&controller, &buttons) != 0 ||
        !memory_read((unsigned char *)controller + 0x70, values, sizeof(values))) {
        return 0;
    }
    return values[1];
}

static int binding_held(void) {
    void *controller;
    unsigned short buttons;
    int held = binding_key_down(BIND_LAUNCH_CONTROL);
    if (autodrive_game_buttons(&controller, &buttons) == 0) {
        held |= binding_pad_down(BIND_LAUNCH_CONTROL, buttons);
    }
    return held;
}

static void let_go(int launched, float rpm) {
    holding = 0;
    if (state.held && launched && !state.logged_launch) {
        state.logged_launch = 1;
        log_append_value("LAUNCH CONTROL: launch at rpm ", (int)rpm);
    }
    state.held = 0;
}

void launch_control_tick(void *car, float dt, int focused) {
    if (!enabled || !game_image || !car) {
        return;
    }
    void *physics = car_physics(car);
    float speed = physics ? speed_squared(car) : -1.0f;
    float spin, redline, limit;
    if (!physics || speed < 0 ||
        !memory_read((unsigned char *)physics + ENGINE_SPEED, &spin, sizeof(spin)) ||
        !memory_read((unsigned char *)physics + ENGINE_REDLINE, &redline, sizeof(redline)) ||
        !memory_read((unsigned char *)physics + ENGINE_LIMIT, &limit, sizeof(limit)) ||
        !float_is_finite(spin) || !(redline > 0 && redline < 3000 && limit >= redline)) {
        let_go(0, 0);
        return;
    }
    float rpm = clamp(spin, 0, limit) * RAD_TO_RPM;
    float red_rpm = redline * RAD_TO_RPM;
    int throttle = focused && throttle_trigger() >= 128;
    int binding = focused && binding_held();
    if (!throttle || !binding || speed > TOO_FAST_SQUARED || (!state.held && speed > MOVING_SQUARED)) {
        let_go(throttle && !binding, rpm);
        return;
    }
    if (!state.held) {
        state.held = 1;
        state.integral = 0.05f;
        state.last_rpm = rpm;
        state.rate = 0;
        if (!state.logged_hold) {
            state.logged_hold = 1;
            log_append_value("LAUNCH CONTROL: holding rpm ", (int)(share * red_rpm));
        }
    }
    if (dt > 0) {
        state.rate += ((rpm - state.last_rpm) / dt - state.rate) * clamp(dt / 0.05f, 0, 1);
    }
    state.last_rpm = rpm;
    float target = share * red_rpm;
    float error = (target - rpm) / red_rpm;
    float ahead = (target - (rpm + state.rate * 0.1f)) / red_rpm;
    state.integral = clamp(state.integral + error * 2.0f * dt, 0, 1);
    float gas = clamp(state.integral + ahead * 6.0f, 0, 1);

    held_gas = gas;
    holding = 1;
}
