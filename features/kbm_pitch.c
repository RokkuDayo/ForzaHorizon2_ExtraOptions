#include "kbm_pitch.h"
#include "logging.h"
#include "support.h"
#include <xmmintrin.h>

#define PI_F 3.14159265f
/* The game has no pitch clamp for these views; 35 degrees keeps the road in
   sight from every attached camera. */
#define PITCH_LIMIT (35.0f * PI_F / 180.0f)
#define DIRECTION_SLOT 0x260u

/* Look direction of the attached cameras: (camera, out) -> out, a 16-byte
   vector. 0x1033e70 builds sin(yaw) * axis 0 + cos(yaw) * axis 2 of the car
   at camera+8 (axes from car vtable +0x20(car, buffer, index), see
   0x1033ea3); 0x1033f10 (CCamHood) also rotates by its own pitch offset. */
#define DIRECTION_YAW 0x1033e70u
#define DIRECTION_HOOD 0x1033f10u
typedef float *(*DirectionFn)(void *, float *);
typedef float *(*AxisFn)(void *, float *, int);

static const unsigned char yaw_bytes[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec,
                                          0x60, 0xc5, 0xfa, 0x10, 0x81, 0xdc, 0x01, 0x00, 0x00};
/* mov rcx,[rbx+8]; lea rdx,[rsp+20h]; mov rax,[rcx]; xor r8d,r8d; call [rax+20h] */
static const unsigned char yaw_axis_bytes[] = {0x48, 0x8b, 0x4b, 0x08, 0x48, 0x8d, 0x54, 0x24, 0x20,
                                               0x48, 0x8b, 0x01, 0x45, 0x33, 0xc0, 0xff, 0x50, 0x20};
static const unsigned char hood_bytes[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x57,
                                           0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00};

typedef struct {
    const char *name;
    unsigned vtable, locator, function;
} CameraClass;

/* Vtable RVA, its RTTI complete-object-locator RVA (at vtable-8) and the
   function in slot +0x260. */
static const CameraClass classes[] = {
    {"CCamBumperHigh", 0x2e703b0u, 0x2620488u, DIRECTION_YAW},
    {"CCamRearView", 0x2e70628u, 0x2620578u, DIRECTION_YAW},
    {"CCamWheel", 0x2e708a0u, 0x2620608u, DIRECTION_YAW},
    {"CCamSide", 0x2e70b18u, 0x2620698u, DIRECTION_YAW},
    {"CCamCockpit", 0x2e77c30u, 0x262a338u, DIRECTION_YAW},
    {"CCamHood", 0x2e77ea8u, 0x262a360u, DIRECTION_HOOD},
    {"CCamDriver", 0x2e78120u, 0x262a3f0u, DIRECTION_YAW},
};

static unsigned char *image;
static DirectionFn original_yaw, original_hood;
static int install_tried, patched;
static volatile float pitch;
static float pitch_state;
static uint64_t last_move;
static volatile LONG used_logged[ARRAY_COUNT(classes)];
static int active_logged;

static void log_text(const char *a, const char *b, const char *c) {
    char text[256];
    unsigned length = 0;
    const char *parts[3] = {a, b, c};
    for (unsigned i = 0; i < 3; ++i) {
        for (const char *p = parts[i]; p && *p && length < sizeof(text) - 3; ++p) {
            text[length++] = *p;
        }
    }
    text[length++] = '\r';
    text[length++] = '\n';
    text[length] = 0;
    log_append(text);
}

static int trust_pointers; /* offline test only */

static int in_image(const void *address) {
    return trust_pointers || (image && (const unsigned char *)address >= image &&
           (const unsigned char *)address < image + GAME_IMAGE_SIZE);
}

static float sine(float x) {
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f)));
}

static float cosine(float x) {
    float x2 = x * x;
    return 1.0f - x2 / 2.0f * (1.0f - x2 / 12.0f * (1.0f - x2 / 30.0f));
}

static float root(float x) {
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));
}

/* Rotates `v` toward the car's up axis by `angle` (about the axis
   perpendicular to both), keeping its length. */
static void tilt(void *camera, float *v, float angle) {
    void *car = NULL, **vtable = NULL;
    AxisFn axis = NULL;
    _Alignas(16) float buffer[4];
    if (!memory_read((unsigned char *)camera + 8, &car, sizeof(car)) || !car ||
        !memory_read(car, &vtable, sizeof(vtable)) || !in_image(vtable) ||
        !memory_read((unsigned char *)vtable + 0x20, &axis, sizeof(axis)) || !in_image((void *)axis)) {
        return;
    }
    const float *up = axis(car, buffer, 1);
    if (!up) {
        return;
    }
    float vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    float vu = v[0] * up[0] + v[1] * up[1] + v[2] * up[2];
    float w[3] = {up[0] * vv - v[0] * vu, up[1] * vv - v[1] * vu, up[2] * vv - v[2] * vu};
    float ww = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
    if (!(vv > 1e-8f) || !(ww > 1e-12f) || !float_is_finite(ww)) {
        return;
    }
    float scale = root(vv) / root(ww), c = cosine(angle), s = sine(angle) * scale;
    for (unsigned i = 0; i < 3; ++i) {
        v[i] = v[i] * c + w[i] * s;
    }
}

static void note_use(void *camera, float angle) {
    void **vtable = NULL;
    if (!memory_read(camera, &vtable, sizeof(vtable))) {
        return;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(classes); ++i) {
        if ((unsigned char *)vtable == image + classes[i].vtable &&
            !InterlockedExchange(&used_logged[i], 1)) {
            char degrees[8];
            int value = (int)(angle * 180.0f / PI_F + (angle < 0 ? -0.5f : 0.5f));
            unsigned n = 0, magnitude = (unsigned)(value < 0 ? -value : value);
            if (value < 0) degrees[n++] = '-';
            if (magnitude >= 10) degrees[n++] = (char)('0' + magnitude / 10 % 10);
            degrees[n++] = (char)('0' + magnitude % 10);
            degrees[n] = 0;
            log_text("KBM PITCH: tilting the look direction of ", classes[i].name, "");
            log_text("KBM PITCH: first tilt (degrees) ", degrees, " - QA: up/down should follow the mouse.");
        }
    }
}

/* garage8 diagnostics: while LookBack is held (Trace=1), what the attached
   cameras' look direction gives: the smoothed look yaw at camera+0x1dc and
   the returned vector, a few lines per press. */
static volatile LONG look_back_lines = -1;
static void patch(void);

void kbm_pitch_debug_look_back(int held) {
    if (!held) {
        InterlockedExchange(&look_back_lines, -1);
        return;
    }
    if (look_back_lines < 0) {
        InterlockedExchange(&look_back_lines, 0);
        if (!install_tried) {
            patch();
        }
    }
}

static void note_look_back(void *camera, const float *v) {
    if (look_back_lines < 0 || InterlockedIncrement(&look_back_lines) > 6 || !v) {
        return;
    }
    void **vtable = NULL;
    float smoothed = 0.0f;
    const char *name = "?";
    memory_read(camera, &vtable, sizeof(vtable));
    memory_read((unsigned char *)camera + 0x1dc, &smoothed, 4);
    for (unsigned i = 0; i < ARRAY_COUNT(classes); ++i) {
        if ((unsigned char *)vtable == image + classes[i].vtable) {
            name = classes[i].name;
        }
    }
    char line[200];
    unsigned n = 0;
    const char *parts[] = {"KBM TRACE: look back: ", name, " look yaw "};
    for (unsigned i = 0; i < 3; ++i) {
        for (const char *p = parts[i]; *p && n < 150; ++p) line[n++] = *p;
    }
    /* The direction against the car's own axis 2 (the one 0x1033e70 scales
       by cos(yaw)): +100 along it, -100 against it. */
    float along = 0.0f;
    void *car = NULL, **car_vtable = NULL;
    AxisFn axis = NULL;
    _Alignas(16) float buffer[4];
    if (memory_read((unsigned char *)camera + 8, &car, sizeof(car)) && car &&
        memory_read(car, &car_vtable, sizeof(car_vtable)) && in_image(car_vtable) &&
        memory_read((unsigned char *)car_vtable + 0x20, &axis, sizeof(axis)) && in_image((void *)axis)) {
        const float *forward = axis(car, buffer, 2);
        if (forward) {
            along = v[0] * forward[0] + v[1] * forward[1] + v[2] * forward[2];
        }
    }
    int values[5] = {(int)(smoothed * 57.2958f), (int)(v[0] * 100.0f), (int)(v[1] * 100.0f), (int)(v[2] * 100.0f),
                     (int)(along * 100.0f)};
    for (unsigned i = 0; i < 5; ++i) {
        int value = values[i];
        if (i == 1) { const char *t = " deg, direction x100 "; while (*t) line[n++] = *t++; }
        else if (i == 4) { const char *t = ", along car axis 2 x100 "; while (*t) line[n++] = *t++; }
        else if (i) line[n++] = ',';
        if (value < 0) { line[n++] = '-'; value = -value; }
        char digits[12]; unsigned d = 0;
        do { digits[d++] = (char)('0' + value % 10); value /= 10; } while (value && d < 11);
        while (d) line[n++] = digits[--d];
    }
    line[n++] = 13;
    line[n++] = 10;
    line[n] = 0;
    log_append(line);
}

static float *direction_yaw(void *camera, float *out) {
    float *result = original_yaw(camera, out);
    note_look_back(camera, result);
    float angle = pitch;
    if (result && angle != 0.0f) {
        tilt(camera, result, angle);
        note_use(camera, angle);
    }
    return result;
}

static float *direction_hood(void *camera, float *out) {
    float *result = original_hood(camera, out);
    note_look_back(camera, result);
    float angle = pitch;
    if (result && angle != 0.0f) {
        tilt(camera, result, angle);
        note_use(camera, angle);
    }
    return result;
}

void kbm_pitch_install(unsigned char *game_image) {
    image = game_image;
}

static int verify_class(const CameraClass *c) {
    uintptr_t locator = 0, slot = 0;
    return memory_read(image + c->vtable - 8, &locator, sizeof(locator)) &&
           locator == (uintptr_t)(image + c->locator) &&
           memory_read(image + c->vtable + DIRECTION_SLOT, &slot, sizeof(slot)) &&
           slot == (uintptr_t)(image + c->function);
}

static void patch(void) {
    install_tried = 1;
    unsigned char code[sizeof(yaw_bytes)];
    if (!image || !memory_read(image + DIRECTION_YAW, code, sizeof(yaw_bytes)) ||
        !memory_equal(code, yaw_bytes, sizeof(yaw_bytes)) ||
        !memory_read(image + DIRECTION_YAW + 0x33, code, sizeof(yaw_axis_bytes)) ||
        !memory_equal(code, yaw_axis_bytes, sizeof(yaw_axis_bytes)) ||
        !memory_read(image + DIRECTION_HOOD, code, sizeof(hood_bytes)) ||
        !memory_equal(code, hood_bytes, sizeof(hood_bytes))) {
        log_append("KBM PITCH: look direction functions 0x1033e70/0x1033f10 not as expected; "
                   "mouse pitch off.\r\n");
        return;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(classes); ++i) {
        if (!verify_class(&classes[i])) {
            log_text("KBM PITCH: camera vtable not as expected (", classes[i].name,
                     "); mouse pitch off.");
            return;
        }
    }
    original_yaw = (DirectionFn)(void *)(image + DIRECTION_YAW);
    original_hood = (DirectionFn)(void *)(image + DIRECTION_HOOD);
    for (unsigned i = 0; i < ARRAY_COUNT(classes); ++i) {
        void **slot = (void **)(image + classes[i].vtable + DIRECTION_SLOT);
        DWORD protection, ignored;
        if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &protection)) {
            continue;
        }
        InterlockedExchangePointer((void *volatile *)slot,
                                   classes[i].function == DIRECTION_HOOD ? (void *)direction_hood
                                                                         : (void *)direction_yaw);
        VirtualProtect(slot, sizeof(void *), protection, &ignored);
        ++patched;
    }
    log_append_value("KBM PITCH: look direction slot +0x260 patched (0x1033e70, CCamHood 0x1033f10); "
                     "attached camera classes: ",
                     patched);
    log_append("KBM PITCH: the chase cameras (CCamFollow*) have no look direction virtual; mouse "
               "pitch reaches bumper, hood, cockpit, driver, wheel and side views only.\r\n");
}

void kbm_pitch_update(const KbmSettings *settings, long dy, int active, uint64_t now, float dt) {
    if (!settings->mouse_look_pitch) {
        pitch = pitch_state = 0.0f;
        return;
    }
    if (!install_tried) {
        patch();
    }
    if (!patched || !active) {
        pitch = pitch_state = 0.0f;
        return;
    }
    if (!active_logged) {
        active_logged = 1;
        log_append(settings->invert_mouse_y ? "KBM PITCH: mouse Y tilts the view (inverted).\r\n"
                                            : "KBM PITCH: mouse Y tilts the view.\r\n");
    }
    float angle = pitch_state;
    if (dy) {
        /* Mouse forward (negative counts) looks up. */
        float step = (float)dy * (float)settings->mouse_sensitivity * (PI_F / 180000.0f);
        angle += settings->invert_mouse_y ? step : -step;
        last_move = now;
    } else if (now - last_move >= (uint64_t)settings->mouse_return_delay) {
        float step = (float)settings->mouse_return_speed * (PI_F / 180.0f) * dt;
        angle = angle > step ? angle - step : angle < -step ? angle + step : 0.0f;
    }
    angle = angle > PITCH_LIMIT ? PITCH_LIMIT : angle < -PITCH_LIMIT ? -PITCH_LIMIT : angle;
    pitch_state = angle;
    pitch = angle;
}

#ifdef FH2_TWEAKS_TEST
/* Offline: verification and patching on a mapped, relocated image copy. */
__declspec(dllexport) int Fh2TestPitchPatch(unsigned char *game_image) {
    image = game_image;
    install_tried = 0;
    patched = 0;
    patch();
    return patched;
}

__declspec(dllexport) void *Fh2TestPitchHooks(unsigned which) {
    return which ? (void *)direction_hood : (void *)direction_yaw;
}

__declspec(dllexport) float Fh2TestPitchStep(int enabled, int invert, long dy, int active,
                                             unsigned elapsed) {
    static KbmSettings settings;
    static uint64_t clock = 100000;
    kbm_config_defaults(&settings);
    settings.mouse_look_pitch = enabled;
    settings.invert_mouse_y = invert;
    clock += elapsed;
    patched = 1;
    install_tried = 1;
    kbm_pitch_update(&settings, dy, active, clock, (float)elapsed * 0.001f);
    return pitch;
}

/* The tilt on a vector, with a fake car whose up axis is (0, 1, 0). */
static float *fake_axis(void *car, float *buffer, int index) {
    (void)car;
    buffer[0] = 0.0f;
    buffer[1] = index == 1 ? 1.0f : 0.0f;
    buffer[2] = 0.0f;
    buffer[3] = 0.0f;
    return buffer;
}

__declspec(dllexport) void Fh2TestPitchTilt(float *vector, float angle, unsigned char *fake_image) {
    static void *vtable[8];
    static void *car[1];
    static void *camera[2];
    (void)fake_image;
    trust_pointers = 1;
    vtable[4] = (void *)fake_axis;
    car[0] = vtable;
    camera[1] = car;
    tilt(camera, vector, angle);
    trust_pointers = 0;
}
#endif
