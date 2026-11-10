#include "fh2/camera_views.h"
#include "fm5/fm5_camera.h"
#include "fm5/fm5_settings.h"
#include "shared/support.h"

#include <xmmintrin.h>

#define HEADING_THUNK_RVA 0x63e3d1u
#define CHASE_THUNK_RVA 0x63e402u

enum {
    FOLLOW = 0x22bca50,
    FOLLOW_NEAR = 0x22bc4f0,
    FOLLOW_FAR = 0x22bc6a8,
    DRIVER = 0x22bc190,
    HOOD = 0x22bd268,
    BUMPER = 0x22b6f70
};

enum {
    WHEEL_SLIP_SLOT = 0x1a0,
    WHEEL_ROLLING_SPEED_SLOT = 0x478,
    WHEEL_CONTACT_SLOT = 0x480,
    NATIVE_WHEEL_SLIP = 0x7fda00,
    NATIVE_WHEEL_ROLLING_SPEED = 0x7fc670,
    NATIVE_WHEEL_CONTACT = 0x7fc690,
    WORLD_VELOCITY_SLOT = 0x248,
    NATIVE_WORLD_VELOCITY = 0x7fbcd0,
    CAMERA_SPRING_OFFSET = 0x110,
    CAMERA_HEADING_OFFSET = 0x140,
    CAMERA_VIEW_DIRTY_OFFSET = 0x192,
    NATIVE_ATAN2 = 0xe79e6a,
    NATIVE_HEADING_UPDATE = 0x63edb0,
    NATIVE_ATTACHED_UPDATE = 0x637150,
};

static const unsigned camera_vtables[] = {FOLLOW, FOLLOW_NEAR, FOLLOW_FAR, DRIVER, HOOD, BUMPER};

typedef float (*ArcTangent)(float, float);
typedef void *(*VectorGetter)(void *, float *);
typedef void *(*AxisGetter)(void *, float *, int);
typedef float (*WheelSlip)(void *, int);
typedef void *(*WheelContact)(void *, int);
typedef void (*CameraUpdate)(void *, float, unsigned char);


typedef struct {
    void *vtable, *target;
    unsigned char unused[0x100];
    float *spring;
    unsigned char unused_after_spring[0x28];
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
static void (*log_message)(const char *);
static unsigned reported;
static void report_once(unsigned bit,const char *message){
    if(!(reported&bit) && log_message)log_message(message);
    reported|=bit;
}
static ArcTangent native_atan2;
static CameraUpdate native_heading_update, native_attached_update;
/* Drift rotation is bounded to +/-45 degrees. This polynomial stays within
   0.000001 of the native sine/cosine in that interval, without CRT imports. */
static void sin_cos(float a, float *sine, float *cosine) {
    float x=a*a;
    *sine=a*(1+x*(-1.0f/6+x*(1.0f/120+x*(-1.0f/5040+x/362880))));
    *cosine=1+x*(-.5f+x*(1.0f/24+x*(-1.0f/720+x/40320)));
}
static DriftOffset driver_offset;
static void *chase_cameras[3];
static float chase_slide_blend[3];

/* Independent from FH2 settings. HUD edits take effect immediately, like FH2. */
static const int defaults[FM5_CAMERA_SETTING_COUNT]={1,35,70,80,70,55,65,96,96,96,96,96,1};
#define values (fm5_settings_values())
static int valid(unsigned index,int value) {
    if(index==FM5_COCKPIT_BLUR)return value==0 || value==1;
    return index<FM5_CAMERA_SETTING_COUNT && value>= (index>=FM5_FOV_CHASE ? 40 : 0) &&
        value<= (index==FM5_DRIFT_MODE ? 2 : index>=FM5_FOV_CHASE ? 240 : 100);
}
static void save(void){fm5_settings_save();}
static void load(const wchar_t *directory){fm5_settings_load(directory);}
int fm5_camera_get(unsigned index) { return index<FM5_CAMERA_SETTING_COUNT ? values[index] : 0; }
int fm5_camera_set(unsigned index,int value) {
    if(!valid(index,value) || values[index]==value) return 0;
    values[index]=value; save(); return 1;
}
void fm5_camera_reset(void) { memory_copy(values,defaults,sizeof(defaults)); save(); }

static int drift_view_enabled(unsigned view) {
    int mode = values[FM5_DRIFT_MODE];
    return (mode == 0 && view >= CAMERA_CHASE && view <= CAMERA_DRIVER) ||
           (mode == 1 && view == CAMERA_DRIVER);
}

static float drift_look_speed(unsigned view) {
    int speed = values[view >= CAMERA_DRIVER ? FM5_DRIVER_SPEED : FM5_CHASE_SPEED];
    if (view >= CAMERA_DRIVER) {
        return 2 + speed * 0.1f;
    }
    return speed <= 50 ? 0.25f + speed * 0.015f : 1.0f + (speed - 50) * 0.06f;
}

static float drift_sensitivity(unsigned view) {
    int sensitivity =
        values[view >= CAMERA_DRIVER ? FM5_DRIVER_SENSITIVITY : FM5_CHASE_SENSITIVITY];
    return view >= CAMERA_DRIVER ? sensitivity * sensitivity / 10000.0f : sensitivity / 50.0f;
}

static float clamp_drift_angle(unsigned view, float angle) {
    unsigned setting = view >= CAMERA_DRIVER ? FM5_DRIVER_RANGE : FM5_CHASE_RANGE;
    float range = values[setting] *
                  (view >= CAMERA_DRIVER ? 0.7853981633974483f : 2.09439510239f) / 100;
    return angle < -range ? -range : angle > range ? range : angle;
}

static float drift_angle(unsigned view, float angle) {
    angle *= drift_sensitivity(view);
    if (view >= CAMERA_DRIVER) {
        float range = clamp_drift_angle(view, 1);
        float magnitude = angle < 0 ? -angle : angle;
        float start = range * 0.75f, shoulder = range * 0.25f;
        if (shoulder > 0 && magnitude > start) {
            float excess = magnitude - start;
            magnitude = start + shoulder * excess / (shoulder + excess);
            return angle < 0 ? -magnitude : magnitude;
        }
    }
    return clamp_drift_angle(view, angle);
}


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
    void *target,*vtable,*getter,*body,*body_vtable;
    if(!memory_read_pointer((unsigned char *)camera+8,&target) || !target ||
       !memory_read_pointer(target,&vtable) ||
       !memory_read_pointer((unsigned char *)vtable+0x48,&getter))return NULL;
    /* These are the same camera-target interface calls made by the native
       heading update. Race/player targets may wrap the car; do not assume its
       concrete vtable or a private car+physics pointer offset. */
    body=((void *(*)(void *))getter)(target);
    if(!body || !memory_read_pointer(body,&body_vtable))return NULL;
    /* Slot 260 is scalar speed in FM5. Slot 248 returns the world velocity
       vector, as confirmed by the paired native velocity setter at 250. */
    if(!memory_read_pointer((unsigned char *)body_vtable+WORLD_VELOCITY_SLOT,&getter) ||
       getter!=image+NATIVE_WORLD_VELOCITY)return NULL;
    ((VectorGetter)getter)(body,velocity);
    if(!float_is_finite(velocity[0]) || !float_is_finite(velocity[1]) || !float_is_finite(velocity[2]))return NULL;
    if(axis){
        if(!memory_read_pointer((unsigned char *)vtable+0x18,&getter))return NULL;
        ((AxisGetter)getter)(target,axis,2);
        if(!float_is_finite(axis[0]) || !float_is_finite(axis[1]) || !float_is_finite(axis[2]))return NULL;
    }
    return body;
}

static float wheel_weight(void *body, unsigned view) {
    /* Preserve the cockpit's established rolling-speed gate. Only chase
       views use normalized lateral slip, matching FH2's grip threshold. */
    unsigned slot=view==CAMERA_DRIVER ? WHEEL_ROLLING_SPEED_SLOT : WHEEL_SLIP_SLOT;
    unsigned native=view==CAMERA_DRIVER ? NATIVE_WHEEL_ROLLING_SPEED : NATIVE_WHEEL_SLIP;
    void *vtable, *slip_getter, *contact_getter;
    if (!memory_read_pointer(body, &vtable) ||
        !memory_read_pointer((unsigned char *)vtable + slot, &slip_getter) ||
        !memory_read_pointer((unsigned char *)vtable + WHEEL_CONTACT_SLOT, &contact_getter) ||
        slip_getter != image + native ||
        contact_getter != image + NATIVE_WHEEL_CONTACT) {
        return 0;
    }
    float slip = 0;
    unsigned contacts = 0;
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
            ++contacts;
        }
        if (grounded && total / grounded > slip) {
            slip = total / grounded;
        }
    }
    /* Airborne tires have no grip: let momentum continue driving the camera. */
    if (!contacts) {
        return 1;
    }
    /* The chase input is the native per-tire lateral slip at body+1360,
       getter 7FDA00 / slot 1A0. Slot 478 is radius * angular velocity,
       not normalized slip. Keep its old thresholds for cockpit behavior. */
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
    float blend = wheel_weight(body, view) * momentum_weight(body_z, body_x, velocity, direction);
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
    report_once(128,"CAMERA: native chase heading callback reached.\r\n");
    CameraView view = camera_type(camera);
    HeadingFrame frame;
    float spring[12];
    _Alignas(16) float velocity[4]={0};
    float slide = drift_view_enabled(view) ? wheel_weight(car_body(camera, velocity, NULL), view) : 0;
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
    report_once(1,"CAMERA: mounted driver update callback reached.\r\n");
    native_attached_update(camera, dt, reset);
    CameraView view = camera_type(camera);
    _Alignas(16) float forward[4], origin[4], velocity[4]={0}, axis[4]={0};
    void *target;
    if (view != CAMERA_DRIVER) {
        return;
    }
    DriftOffset *state = &driver_offset;
    if (!drift_view_enabled(view) && !state->camera) return;
    void *body = car_body(camera, velocity, axis);
    float direction[2];
    float slide =
        body && drift_view_enabled(view)
            ? wheel_weight(body, view) * momentum_weight(-axis[2], -axis[0], velocity, direction)
            : 0;
    if (!float_is_finite(dt) || dt < 0 || dt >= 0.25f || !body ||
        !memory_read((unsigned char *)camera + CAMERA_HEADING_OFFSET, forward, sizeof(forward)) ||
        !memory_read((unsigned char *)camera + 0x130, origin, sizeof(origin)) ||
        !memory_read_pointer((unsigned char *)camera + 8, &target)) {
        *state = (DriftOffset){0};
        return;
    }
    if (!float_is_finite(forward[0]) || !float_is_finite(forward[1]) ||
        !float_is_finite(forward[2]) || !float_is_finite(origin[0]) || !float_is_finite(origin[2])) {
        return;
    }
    if (!slide &&
        (!state->camera || (state->angle > -0.00001f && state->angle < 0.00001f &&
                            state->angular_speed > -0.0001f && state->angular_speed < 0.0001f))) {
        *state = (DriftOffset){0};
        return;
    }
    int initialize = reset || state->camera != camera || state->target != target;
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
    /* FM5 stores an absolute look-at point, not a direction vector. Rotate
       about the native eye position so cars anywhere on the track behave alike. */
    float x = forward[0] - origin[0], z = forward[2] - origin[2];
    forward[0] = origin[0] + cosine * x + sine * z;
    forward[2] = origin[2] + cosine * z - sine * x;
    memory_copy((unsigned char *)camera + CAMERA_HEADING_OFFSET, forward, sizeof(forward));
    *((unsigned char *)camera + CAMERA_VIEW_DIRTY_OFFSET) = 1;
}


typedef float (*GetFov)(void *);
static GetFov native_chase, native_mounted;
static float get_fov(void *camera) {
    report_once(64,"CAMERA: native FOV callback reached.\r\n");
    void *vtable;
    if (!memory_read_pointer(camera, &vtable)) {
        return 0;
    }
    unsigned view = CAMERA_UNKNOWN;
    for (unsigned index = 0; index < ARRAY_COUNT(camera_vtables); ++index) {
        if (vtable == image + camera_vtables[index]) {
            view = index + CAMERA_CHASE;
            break;
        }
    }
    if (!view) {
        return 0;
    }
    int chase = view <= CAMERA_CHASE_FAR;
    float result = (chase ? native_chase : native_mounted)(camera);
    unsigned setting = view <= CAMERA_CHASE_NEAR ? FM5_FOV_CHASE : FM5_FOV_FAR + view - CAMERA_CHASE_FAR;
    float degrees = values[setting] * .5f;
    if (degrees == 48 || !float_is_finite(result)) {
        return result;
    }
    float base;
    if (chase) {
        void *settings;
        if (!memory_read_pointer((unsigned char *)camera + 0x110, &settings) ||
            !memory_read((unsigned char *)settings + 0x1c, &base, sizeof(base))) {
            return result;
        }
        if (!float_is_finite(base) || base <= 0) {
            return result;
        }
        return result * (degrees / base);
    }
    if (!memory_read((unsigned char *)camera + 0x150, &base, sizeof(base)) ||
        !float_is_finite(base) || base <= 0) {
        return result;
    }
    return result * (degrees * 0.017453292519943295f / base);
}


/* Render-scene setup copies IsInCockpit into the global blur-pass veto.
   Override only that one consumer: lighting, cockpit shadows, exposure and
   the presentation camera's real IsInCockpit value remain native. */
static unsigned char cockpit_blur(void *camera) {
    unsigned char cockpit=((unsigned char (*)(void *))(*(void ***)camera)[0xd8/8])(camera);
    return cockpit && !values[FM5_COCKPIT_BLUR];
}

static void pointer_site(Fm5Site *site,unsigned rva,void *original,void *replacement) {
    *site=(Fm5Site){.rva=rva,.size=8};
    memory_copy(site->original,&original,8);memory_copy(site->replacement,&replacement,8);
}
static void call_site(Fm5Site *site,unsigned rva,unsigned original,unsigned replacement) {
    *site=(Fm5Site){.rva=rva,.size=5,.original={0xe8},.replacement={0xe8}};
    int32_t a=(int32_t)(original-rva-5),b=(int32_t)(replacement-rva-5);
    memory_copy(site->original+1,&a,4);memory_copy(site->replacement+1,&b,4);
}
static void thunk_site(Fm5Site *site,unsigned rva,void *callback,int camera_argument) {
    *site=(Fm5Site){.rva=rva,.size=camera_argument ? 15 : 12};
    for(unsigned i=0;i<site->size;++i)site->original[i]=0xcc;
    unsigned shift=camera_argument ? 3 : 0;
    if(shift){site->replacement[0]=0x49;site->replacement[1]=0x89;site->replacement[2]=0xf0;}
    site->replacement[shift]=0x48;site->replacement[shift+1]=0xb8;
    memory_copy(site->replacement+shift+2,&callback,8);
    site->replacement[shift+10]=0xff;site->replacement[shift+11]=0xe0;
}
unsigned fm5_camera_prepare(unsigned char *game,Fm5Site *sites,const wchar_t *directory,int drift,int fov,void (*log)(const char *)) {
    image=game;log_message=log;reported=0; load(directory); unsigned count=0;
    thunk_site(sites+count++,0x84fae1,(void *)cockpit_blur,0);
    Fm5Site *blur_site=sites+count++;
    *blur_site=(Fm5Site){.rva=0x84fb73,.size=6,
        .original={0xff,0x90,0xd8,0,0,0},.replacement={0xe8,0,0,0,0,0x90}};
    int32_t blur_jump=(int32_t)(0x84fae1-0x84fb73-5);
    memory_copy(blur_site->replacement+1,&blur_jump,4);
    native_atan2=(ArcTangent)(image+NATIVE_ATAN2);
    native_heading_update=(CameraUpdate)(image+NATIVE_HEADING_UPDATE);
    native_attached_update=(CameraUpdate)(image+NATIVE_ATTACHED_UPDATE);
    driver_offset=(DriftOffset){0};
    for(unsigned i=0;i<3;++i){chase_cameras[i]=NULL;chase_slide_blend[i]=0;}
    if(drift) {
        thunk_site(sites+count++,HEADING_THUNK_RVA,(void *)momentum_heading,1);
        call_site(sites+count++,0x63ee55,NATIVE_ATAN2,HEADING_THUNK_RVA);
        thunk_site(sites+count++,CHASE_THUNK_RVA,(void *)update_chase_heading,0);
        call_site(sites+count++,0x63e6de,NATIVE_HEADING_UPDATE,CHASE_THUNK_RVA);
        pointer_site(sites+count++,DRIVER+0x18,(void *)native_attached_update,(void *)update_mounted_camera);
    }
    native_chase=(GetFov)(image+0x63ec40);native_mounted=(GetFov)(image+0x637490);
    if(fov) for(unsigned i=0;i<ARRAY_COUNT(camera_vtables);++i)
        pointer_site(sites+count++,camera_vtables[i]+0x58,(void *)(i<3 ? native_chase : native_mounted),(void *)get_fov);
    return count;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fm5TestCameraCallback(unsigned index) {
    void *callbacks[]={(void *)momentum_heading,(void *)update_chase_heading,(void *)update_mounted_camera,(void *)get_fov,(void *)cockpit_blur};
    return index<ARRAY_COUNT(callbacks)?callbacks[index]:NULL;
}
__declspec(dllexport) int Fm5TestCameraGet(unsigned index){return fm5_camera_get(index);}
__declspec(dllexport) int Fm5TestCameraSet(unsigned index,int value){return fm5_camera_set(index,value);}
__declspec(dllexport) void Fm5TestCameraReset(void){fm5_camera_reset();}
#endif
