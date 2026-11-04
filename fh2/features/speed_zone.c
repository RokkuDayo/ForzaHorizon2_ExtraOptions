#include "speed_zone.h"
#include "support.h"
#include "logging.h"
#include "autodrive_game.h"

static unsigned char *image;
static SRWLOCK zone_lock = SRWLOCK_INIT;
static GameShared zone;
typedef void (*Action)(void *);

static void release_weak(GameShared value) {
    if (value.counter && InterlockedDecrement(
            (volatile LONG *)((unsigned char *)value.counter + 12)) == 0) {
        ((Action)(*(void ***)value.counter)[1])(value.counter);
    }
}

/* Keep only a weak reference: streaming can destroy a zone at any time. */
static void update_zone(void *self, float dt) {
    ((void (*)(void *, float))(image + 0x6ba2f0))(self, dt);
    if (!*((unsigned char *)self + 0x3b9)) return;
    GameShared reference;
    if (!memory_read((unsigned char *)self + 0x30, &reference, sizeof(reference)) ||
        reference.object != (unsigned char *)self + 0x28 || !reference.counter) return;
    AcquireSRWLockExclusive(&zone_lock);
    GameShared previous = zone;
    if (previous.counter == reference.counter && previous.object == reference.object) {
        ReleaseSRWLockExclusive(&zone_lock);
        return;
    }
    InterlockedIncrement((volatile LONG *)((unsigned char *)reference.counter + 12));
    zone = reference;
    ReleaseSRWLockExclusive(&zone_lock);
    release_weak(previous);
}

void speed_zone_car_selection(void *page) {
    unsigned page_id = 0;
    if (!image || !page || !memory_read((unsigned char *)page + 0x228, &page_id, 4) ||
        page_id != 0x1c) return; // Only the pause menu's Change Car page.
    GameShared strong = {0};
    AcquireSRWLockExclusive(&zone_lock);
    GameShared previous = zone;
    zone = (GameShared){0};
    if (previous.counter) {
        volatile LONG *uses = (volatile LONG *)((unsigned char *)previous.counter + 8);
        LONG count = InterlockedCompareExchange(uses, 0, 0);
        while (count > 0 && count < 0x7fffffff) {
            LONG observed = InterlockedCompareExchange(uses, count + 1, count);
            if (observed == count) {
                strong = (GameShared){(unsigned char *)previous.object - 0x28, previous.counter};
                break;
            }
            count = observed;
        }
    }
    ReleaseSRWLockExclusive(&zone_lock);
    release_weak(previous);
    if (strong.object && *(void **)strong.object == image + 0x2cc38a8 &&
        *((unsigned char *)strong.object + 0x3b9)) {
        /* Native failure broadcasts SpeedZoneAborted and calls 6a80b0 to
           clear IsActive and release its car/activation reference. Merely
           sending the HUD event would leave that reference behind. */
        ((Action)(image + 0x6a2ae0))(strong.object);
        log_append("GARAGE: active speed zone failed before opening Change Car.\r\n");
    }
    autodrive_game_release_shared(&strong);
}

static unsigned char original[8], replacement[8];
static const unsigned char *accepted[] = {original, replacement};
PatchDefinition speed_zone_car_change_patch = {
    0x2cc38c0, 0, 8, GROUP_VIDEO_OPTIONS, "speed_zone_car_change",
    original, replacement, replacement, accepted, 2
};

void speed_zone_prepare(unsigned char *base) {
    GameShared previous = zone;
    zone = (GameShared){0};
    release_weak(previous);
    image = base;
    uintptr_t native = (uintptr_t)(base + 0x6ba2f0), hook = (uintptr_t)update_zone;
    memory_copy(original, &native, 8);
    memory_copy(replacement, &hook, 8);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestSpeedZoneCarSelection(void *page) {
    speed_zone_car_selection(page);
}
#endif
