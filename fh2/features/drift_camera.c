#include "camera_views.h"
#include "features.h"
#include "support.h"
#include "video_settings.h"
#include <xmmintrin.h>

#define HEADING_THUNK_RVA 0x103b261u
#define CHASE_THUNK_RVA 0x103b281u

enum {
    FOLLOW = 0x2e783a0,
    FOLLOW_NEAR = 0x2e78610,
    FOLLOW_FAR = 0x2e78880,
    DRIVER = 0x2e78120,
    HOOD = 0x2e77ea8,
    BUMPER = 0x2e703b0
};

enum {
    TARGET_PREDICTED_VTABLE = 0x2e79540,
    TARGET_CAR_VTABLE = 0x2c5ce28,
    TARGET_PLAYER_CAR_VTABLE = 0x2e6b348,
    CAR_BODY_OFFSET = 0x4be8,
    NATIVE_CAR_VELOCITY = 0xf646b0,
    NATIVE_CAR_AXIS = 0xf62220,
    WHEEL_SLIP_SLOT = 0x1d8,
    WHEEL_CONTACT_SLOT = 0x4e0,
    NATIVE_WHEEL_SLIP = 0x12c5cb0,
    NATIVE_WHEEL_CONTACT = 0x12e8040,
    CAMERA_SPRING_OFFSET = 0x180,
    CAMERA_HEADING_OFFSET = 0x1c0,
    CAMERA_VIEW_DIRTY_OFFSET = 0x21a,
    NATIVE_ATAN2 = 0xf17d52,
    NATIVE_HEADING_UPDATE = 0x1036920,
    NATIVE_ATTACHED_UPDATE = 0x103c590,
    NATIVE_SIN_COS = 0x1251ce0,
};

static const unsigned camera_vtables[] = {FOLLOW, FOLLOW_NEAR, FOLLOW_FAR, DRIVER, HOOD, BUMPER};

typedef float (*ArcTangent)(float, float);
typedef void *(*VectorGetter)(void *, float *);
typedef void *(*AxisGetter)(void *, float *, int);
typedef float (*WheelSlip)(void *, int);
typedef void *(*WheelContact)(void *, int);
typedef void (*CameraUpdate)(void *, float, unsigned char);
typedef void (*SinCos)(float, float *, float *);

typedef struct {
    void *vtable, *target;
    unsigned char unused[0x170];
    float *spring;
    unsigned char unused_after_spring[0x38];
    float yaw, yaw_speed;
} HeadingFrame;

typedef struct {
    void *camera, *target;
    float angle, angular_speed, body_heading, slide;
} DriftOffset;

typedef struct {
    float stiffness, damping;
} DriftSpring;

_Static_assert(offsetof(HeadingFrame, spring) == CAMERA_SPRING_OFFSET, "Native spring offset");
_Static_assert(offsetof(HeadingFrame, yaw) == CAMERA_HEADING_OFFSET, "Native yaw offset");

static unsigned char *image;
static ArcTangent native_atan2;
static CameraUpdate native_heading_update, native_attached_update;
static SinCos sin_cos;
static DriftOffset mounted_offsets[3];
static void *chase_cameras[3];
static float chase_slide_blend[3];
static unsigned char heading_thunk[15];
static const unsigned char unused_thunk[15] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_thunk[] = {unused_thunk, heading_thunk};
static unsigned char chase_thunk[12], original_update[8], attached_callback[8];
static const unsigned char unused_chase_thunk[12] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
                                                     0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_chase_thunk[] = {unused_chase_thunk, chase_thunk};
static const unsigned char *const accepted_update[] = {original_update, attached_callback};

static CameraView camera_type(void *camera) {
    void *vtable;
    if (!memory_read_pointer(camera, &vtable)) {
        return 0;
    }
    for (unsigned index = 0; index < ARRAY_COUNT(camera_vtables); ++index) {
        if (vtable == image + camera_vtables[index]) {
            return (CameraView)(index + 1);
        }
    }
    return 0;
}

static void *car_body(void *camera, float velocity[4], float axis[4]) {
    void *target, *vtable, *body, *getter;
    if (!memory_read_pointer((unsigned char *)camera + 8, &target)) {
        return 0;
    }
    // Predicted targets wrap the same car; use its actual world velocity.
    for (unsigned depth = 0; depth < 3; ++depth) {
        if (!memory_read_pointer(target, &vtable)) {
            return 0;
        }
        if (vtable == image + TARGET_PREDICTED_VTABLE) {
            if (!memory_read_pointer((unsigned char *)target + 8, &target)) {
                return 0;
            }
            continue;
        }
        if ((vtable != image + TARGET_CAR_VTABLE && vtable != image + TARGET_PLAYER_CAR_VTABLE) ||
            !memory_read_pointer((unsigned char *)target + CAR_BODY_OFFSET, &body) || !body ||
            !memory_read_pointer((unsigned char *)vtable + 0x10, &getter) ||
            getter != image + NATIVE_CAR_VELOCITY) {
            return 0;
        }
        ((VectorGetter)getter)(target, velocity);
        if (!float_is_finite(velocity[0]) || !float_is_finite(velocity[1]) ||
            !float_is_finite(velocity[2])) {
            return NULL;
        }
        if (axis) {
            if (!memory_read_pointer((unsigned char *)vtable + 0x20, &getter) ||
                getter != image + NATIVE_CAR_AXIS) {
                return NULL;
            }
            ((AxisGetter)getter)(target, axis, 2);
            if (!float_is_finite(axis[0]) || !float_is_finite(axis[1]) ||
                !float_is_finite(axis[2])) {
                return NULL;
            }
        }
        return body;
    }
    return 0;
}

static float slide_weight(void *body) {
    void *vtable, *slip_getter, *contact_getter;
    if (!memory_read_pointer(body, &vtable) ||
        !memory_read_pointer((unsigned char *)vtable + WHEEL_SLIP_SLOT, &slip_getter) ||
        !memory_read_pointer((unsigned char *)vtable + WHEEL_CONTACT_SLOT, &contact_getter) ||
        slip_getter != image + NATIVE_WHEEL_SLIP ||
        contact_getter != image + NATIVE_WHEEL_CONTACT) {
        return 0;
    }
    float slip = 0;
    for (int axle = 0; axle < 2; ++axle) {
        float total = 0;
        unsigned grounded = 0;
        for (int wheel = axle * 2; wheel < axle * 2 + 2; ++wheel) {
            if (!((WheelContact)contact_getter)(body, wheel)) {
                continue;
            }
            float lateral = ((WheelSlip)slip_getter)(body, wheel);
            if (!float_is_finite(lateral)) {
                return 0;
            }
            total += lateral < 0 ? -lateral : lateral;
            ++grounded;
        }
        if (grounded && total / grounded > slip) {
            slip = total / grounded;
        }
    }
    // Normalized lateral slip reaches the tire's grip limit at one.
    if (slip <= 1) {
        return 0;
    }
    if (slip >= 1.35f) {
        return 1;
    }
    float blend = (slip - 1) / 0.35f;
    return blend * blend * (3 - 2 * blend);
}

static float heading_difference(float heading, float reference) {
    float difference = heading - reference;
    if (difference > 3.141592653589793f) {
        difference -= 6.283185307179586f;
    } else if (difference < -3.141592653589793f) {
        difference += 6.283185307179586f;
    }
    return difference;
}

static float momentum_weight(float body_z, float body_x, const float velocity[4],
                             float direction[2]) {
    float speed_squared = velocity[0] * velocity[0] + velocity[2] * velocity[2];
    float body_squared = body_x * body_x + body_z * body_z;
    if (!(speed_squared > 4 && speed_squared < 1000000 && body_squared > 0.5f &&
          body_squared < 1.5f)) {
        return 0;
    }
    float inverse_speed = 1 / _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(speed_squared)));
    float x = -velocity[0] * inverse_speed;
    float z = -velocity[2] * inverse_speed;
    float alignment = x * body_x + z * body_z;
    if (alignment <= -0.5f) {
        return 0;
    }
    float blend = speed_squared >= 16 ? 1 : (speed_squared - 4) / 12;
    blend = blend * blend * (3 - 2 * blend);
    if (alignment < 0) {
        blend *= (alignment + 0.5f) * 2;
    }
    direction[0] = x;
    direction[1] = z;
    return blend;
}

static float momentum_heading(float body_z, float body_x, void *camera) {
    _Alignas(16) float velocity[4] = {0};
    void *body;
    CameraView view = camera_type(camera);
    float body_heading = native_atan2(body_z, body_x);
    float fallback = view >= CAMERA_DRIVER ? 0 : body_heading;
    if (!float_is_finite(body_x) || !float_is_finite(body_z) || !view ||
        !drift_view_enabled(view) || !(body = car_body(camera, velocity, NULL))) {
        return fallback;
    }
    float direction[2];
    float blend = slide_weight(body) * momentum_weight(body_z, body_x, velocity, direction);
    if (!blend) {
        return fallback;
    }
    float heading = native_atan2(body_z + (direction[1] - body_z) * blend,
                                 body_x + (direction[0] - body_x) * blend);
    float offset = drift_angle(view, heading_difference(heading, body_heading));
    return view >= CAMERA_DRIVER ? offset : body_heading + offset;
}

static DriftSpring drift_spring_tuning(CameraView view) {
    float look_speed = drift_look_speed(view);
    if (view >= CAMERA_DRIVER) {
        return (DriftSpring){look_speed * look_speed / 4.5f, 2 * look_speed / 4.4f};
    }
    float damping_scale = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(look_speed)));
    return (DriftSpring){0.55f * look_speed, 0.5f * damping_scale};
}

static void tune_drift_spring(float spring[12], float slide, const DriftSpring *tuning) {
    spring[8] *= 1 + (tuning->stiffness - 1) * slide;
    spring[9] *= 1 + (tuning->damping - 1) * slide;
    spring[10] *= 1 - slide;
}

static float recover_slide(float previous, float slide, float dt) {
    if (slide < previous && dt > 0 && dt < 0.25f) {
        float decay = dt * 4 / (1 + dt * 4);
        slide = previous + (slide - previous) * decay;
        if (slide < 0.0001f) {
            slide = 0;
        }
    }
    return slide;
}

static void update_chase_heading(void *camera, float dt, unsigned char reset) {
    CameraView view = camera_type(camera);
    HeadingFrame frame;
    float spring[12];
    _Alignas(16) float velocity[4];
    float slide = drift_view_enabled(view) ? slide_weight(car_body(camera, velocity, NULL)) : 0;
    if (view && view <= CAMERA_CHASE_FAR && float_is_finite(dt) && dt > 0 && dt < 0.25f) {
        unsigned index = view - CAMERA_CHASE;
        if (chase_cameras[index] != camera || reset) {
            chase_cameras[index] = camera;
            chase_slide_blend[index] = slide;
        }
        slide = recover_slide(chase_slide_blend[index], slide, dt);
        chase_slide_blend[index] = slide;
    }
    void *settings;
    if (!view || view > CAMERA_CHASE_FAR || !slide || !float_is_finite(dt) ||
        !memory_read_pointer((unsigned char *)camera + CAMERA_SPRING_OFFSET, &settings) ||
        !memory_read(settings, spring, sizeof(spring)) ||
        !memory_read(camera, &frame.vtable, 2 * sizeof(void *)) ||
        !memory_read((unsigned char *)camera + CAMERA_HEADING_OFFSET, &frame.yaw,
                     2 * sizeof(float)) ||
        !float_is_finite(spring[8]) || !float_is_finite(spring[9]) ||
        !float_is_finite(spring[11])) {
        native_heading_update(camera, dt, reset);
        return;
    }
    DriftSpring tuning = drift_spring_tuning(view);
    tune_drift_spring(spring, slide, &tuning);
    frame.spring = spring;
    native_heading_update(&frame, dt, reset);
    memory_copy((unsigned char *)camera + CAMERA_HEADING_OFFSET, &frame.yaw, 2 * sizeof(float));
}

static void update_mounted_camera(void *camera, float dt, unsigned char reset) {
    native_attached_update(camera, dt, reset);
    CameraView view = camera_type(camera);
    _Alignas(16) float forward[4], velocity[4], axis[4];
    void *target;
    if (view != CAMERA_DRIVER) {
        return;
    }
    DriftOffset *state = &mounted_offsets[view - CAMERA_DRIVER];
    void *body = car_body(camera, velocity, axis);
    float direction[2];
    float slide =
        body && drift_view_enabled(view)
            ? slide_weight(body) * momentum_weight(-axis[2], -axis[0], velocity, direction)
            : 0;
    if (!float_is_finite(dt) || dt < 0 || dt >= 0.25f || !body ||
        !memory_read((unsigned char *)camera + CAMERA_HEADING_OFFSET, forward, sizeof(forward)) ||
        !memory_read_pointer((unsigned char *)camera + 8, &target)) {
        *state = (DriftOffset){0};
        return;
    }
    if (!float_is_finite(forward[0]) || !float_is_finite(forward[1]) ||
        !float_is_finite(forward[2])) {
        return;
    }
    if (!slide &&
        (!state->camera || (state->angle > -0.00001f && state->angle < 0.00001f &&
                            state->angular_speed > -0.0001f && state->angular_speed < 0.0001f))) {
        *state = (DriftOffset){0};
        return;
    }
    int initialize = state->camera != camera || state->target != target;
    float body_heading = native_atan2(-axis[2], -axis[0]);
    if (initialize) {
        *state = (DriftOffset){
            .camera = camera, .target = target, .body_heading = body_heading, .slide = slide};
        return;
    }
    state->slide = recover_slide(state->slide, slide, dt);
    if (drift_view_enabled(view) && clamp_drift_angle(view, 1) > 0) {
        float body_turn = heading_difference(body_heading, state->body_heading);
        float correction = body_turn * state->slide * drift_sensitivity(view);
        state->angle = heading_difference(state->angle - correction, 0);
    }
    state->body_heading = body_heading;
    float spring[12] = {0, 0, 0, 0, 0, 0, 0, 0, 4.5f, 4.4f, 0.67f, 2};
    DriftSpring tuning = drift_spring_tuning(view);
    tune_drift_spring(spring, 1, &tuning);
    HeadingFrame frame;
    frame.vtable = image + camera_vtables[view - CAMERA_CHASE];
    frame.target = target;
    frame.spring = spring;
    frame.yaw = state->angle;
    frame.yaw_speed = state->angular_speed;
    // Rebase the offset as the body turns so the spring smooths world heading.
    native_heading_update(&frame, dt, 0);
    if (!float_is_finite(frame.yaw) || !float_is_finite(frame.yaw_speed)) {
        *state = (DriftOffset){0};
        return;
    }
    state->angle = clamp_drift_angle(view, frame.yaw);
    if ((frame.yaw - state->angle) * frame.yaw_speed > 0) {
        frame.yaw_speed = 0;
    }
    state->angular_speed = frame.yaw_speed;
    float sine, cosine;
    sin_cos(-state->angle, &sine, &cosine);
    float x = forward[0], z = forward[2];
    forward[0] = cosine * x + sine * z;
    forward[2] = cosine * z - sine * x;
    memory_copy((unsigned char *)camera + CAMERA_HEADING_OFFSET, forward, sizeof(forward));
    *((unsigned char *)camera + CAMERA_VIEW_DIRTY_OFFSET) = 1;
}

PatchDefinition drift_camera_hook_patch = {HEADING_THUNK_RVA,
                                           0,
                                           sizeof(heading_thunk),
                                           GROUP_DRIFT_CAMERA,
                                           "drift_camera_heading_thunk",
                                           unused_thunk,
                                           heading_thunk,
                                           heading_thunk,
                                           accepted_thunk,
                                           2};

#define ATTACHED_HOOK(table, name)                                                                 \
    {table + 0x20,                                                                                 \
     0,                                                                                            \
     8,                                                                                            \
     GROUP_DRIFT_CAMERA,                                                                           \
     name,                                                                                         \
     original_update,                                                                              \
     attached_callback,                                                                            \
     attached_callback,                                                                            \
     accepted_update,                                                                              \
     2}

PatchDefinition drift_camera_view_hook_patches[4] = {
    {CHASE_THUNK_RVA, 0, sizeof(chase_thunk), GROUP_DRIFT_CAMERA, "drift_camera_chase_spring_thunk",
     unused_chase_thunk, chase_thunk, chase_thunk, accepted_chase_thunk, 2},
    ATTACHED_HOOK(DRIVER, "drift_camera_driver_update"),
    ATTACHED_HOOK(HOOD, "drift_camera_hood_update"),
    ATTACHED_HOOK(BUMPER, "drift_camera_bumper_update")};

void drift_camera_prepare_hook(unsigned char *game_image) {
    image = game_image;
    native_atan2 = (ArcTangent)(image + NATIVE_ATAN2);
    native_heading_update = (CameraUpdate)(image + NATIVE_HEADING_UPDATE);
    native_attached_update = (CameraUpdate)(image + NATIVE_ATTACHED_UPDATE);
    sin_cos = (SinCos)(image + NATIVE_SIN_COS);
    // Forward the native heading call's RSI camera as its third argument.
    static const unsigned char code[15] = {0x49, 0x89, 0xf0, 0x48, 0xb8, 0,    0,   0,
                                           0,    0,    0,    0,    0,    0xff, 0xe0};
    memory_copy(heading_thunk, code, sizeof(code));
    uintptr_t callback = (uintptr_t)momentum_heading;
    memory_copy(heading_thunk + 5, &callback, sizeof(callback));
    static const unsigned char chase_code[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    memory_copy(chase_thunk, chase_code, sizeof(chase_code));
    callback = (uintptr_t)update_chase_heading;
    memory_copy(chase_thunk + 2, &callback, sizeof(callback));
    callback = (uintptr_t)native_attached_update;
    memory_copy(original_update, &callback, sizeof(callback));
    callback = (uintptr_t)update_mounted_camera;
    memory_copy(attached_callback, &callback, sizeof(callback));
}

void drift_camera_commit_options(const ModOptions *options) {
    (void)options;
    for (unsigned view = 0; view < ARRAY_COUNT(mounted_offsets); ++view) {
        mounted_offsets[view] = (DriftOffset){0};
        chase_cameras[view] = NULL;
        chase_slide_blend[view] = 0;
    }
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestDriftCameraHookAddress(void) {
    return (void *)momentum_heading;
}

__declspec(dllexport) void *Fh2TestDriftViewHookAddress(unsigned index) {
    return index ? (void *)update_mounted_camera : (void *)update_chase_heading;
}
#endif
