#include "controller_pause.h"
#include "logging.h"
#include "support.h"

/* The pause flow's WaitForPause behaviour (vtable 0x2d2f6f8, per-frame slot
   14, 0x964810) fires onPause (0x1925260(behavior, behavior+0x28) at 0x9648fa)
   whenever the signed-in user's controller (user->vt+0x120(0)) reports itself
   not connected ([controller+4] == 0): the console's "controller lost: pause"
   rule. With no pad paired to the signed-in user (playing without a pad, or a
   pad the game never paired) that rule fires again on every close of the
   pause hub, so the game pauses over and over. With the option on, the fire is
   kept only for a real unplug: a pad that was attached and is now gone pauses
   once. Esc, Menu and every other pause request are untouched. The call is
   pointed at an absolute jump in the int3 gap at 0x9ae750. */
#define PAUSE_CHECK 0x9648edu
#define PAUSE_FIRE_CALL 0x9648fau
#define PAUSE_FIRE 0x1925260u
#define PAUSE_CAVE 0x9ae750u

/* CControllerManager: eight pad slots; a slot's gamepad is set while a pad is
   attached to it. */
#define MANAGER_RVA 0x365d7c8u
#define MANAGER_VTABLE_RVA 0x2e7a2e8u
#define MANAGER_FIRST_CONTROLLER 0x10u
#define CONTROLLER_STRIDE 0x1300u
#define CONTROLLER_SLOTS 8u
#define CONTROLLER_GAMEPAD 0x60u

typedef void (*TransitionFireFn)(void *, void *);

/* cmp byte [rbx+4],0; jne +0xc; lea rdx,[rdi+0x28]; mov rcx,rdi */
static const unsigned char pause_check_bytes[] = {0x80, 0x7b, 0x04, 0x00, 0x75, 0x0c, 0x48,
                                                  0x8d, 0x57, 0x28, 0x48, 0x8b, 0xcf};
static unsigned char *image;
static int (*suppressed)(void);
static volatile LONG installed;

static int any_pad_attached(void) {
    unsigned char *owner;
    void *vtable;
    if (!memory_read_pointer(image + MANAGER_RVA, (void **)&owner) ||
        !memory_read_pointer(owner, &vtable) || vtable != image + MANAGER_VTABLE_RVA) {
        return 0;
    }
    for (unsigned index = 0; index < CONTROLLER_SLOTS; ++index) {
        void *gamepad = NULL;
        if (memory_read(owner + MANAGER_FIRST_CONTROLLER + (size_t)index * CONTROLLER_STRIDE +
                            CONTROLLER_GAMEPAD,
                        &gamepad, sizeof(gamepad)) &&
            gamepad) {
            return 1;
        }
    }
    return 0;
}

static void pause_fire_hook(void *behavior, void *transition) {
    /* Keyboard & mouse active with no pad: never a controller-missing pause. */
    if (suppressed && suppressed()) {
        static int logged;
        if (!logged) {
            logged = 1;
            log_append("CONTROLLER PAUSE: controller-missing pause suppressed (keyboard & mouse, "
                       "no pad).\r\n");
        }
        return;
    }
    static int pad_was_attached = -1;
    int attached = any_pad_attached();
    int unplugged = pad_was_attached == 1 && !attached;
    pad_was_attached = attached;
    if (!unplugged) {
        static int logged;
        if (!logged) {
            logged = 1;
            log_append("CONTROLLER PAUSE: controller-missing pause skipped (no pad was unplugged).\r\n");
        }
        return;
    }
    ((TransitionFireFn)(void *)(image + PAUSE_FIRE))(behavior, transition);
}

static void write_jump(unsigned char *at, const void *target) {
    static const unsigned char jump[6] = {0xff, 0x25, 0, 0, 0, 0};
    uintptr_t address = (uintptr_t)target;
    memory_copy(at, jump, sizeof(jump));
    memory_copy(at + 6, &address, sizeof(address));
}

void controller_pause_set_suppressed(int (*callback)(void)) {
    suppressed = callback;
}

void controller_pause_install(unsigned char *game_image) {
    if (InterlockedCompareExchange(&installed, 1, 0)) {
        return;
    }
    image = game_image;
    unsigned char *cave = image + PAUSE_CAVE, *call = image + PAUSE_FIRE_CALL;
    unsigned char expected[5] = {0xe8, 0, 0, 0, 0};
    int32_t relative = (int32_t)((image + PAUSE_FIRE) - (call + 5));
    memory_copy(expected + 1, &relative, 4);
    for (unsigned i = 0; i < 16; ++i) {
        if (cave[i] != 0xcc) {
            log_append("CONTROLLER PAUSE: code gap 0x9ae750 is in use; the controller-missing pause "
                       "is left alone.\r\n");
            return;
        }
    }
    if (cave[-1] != 0xc3 ||
        !memory_equal(image + PAUSE_CHECK, pause_check_bytes, sizeof(pause_check_bytes)) ||
        !memory_equal(call, expected, sizeof(expected))) {
        log_append("CONTROLLER PAUSE: WaitForPause controller check 0x9648ed changed; the "
                   "controller-missing pause is left alone.\r\n");
        return;
    }
    DWORD old_cave, old_call, ignored;
    if (!VirtualProtect(cave, 16, PAGE_EXECUTE_READWRITE, &old_cave)) {
        return;
    }
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old_call)) {
        VirtualProtect(cave, 16, old_cave, &ignored);
        return;
    }
    write_jump(cave, (void *)pause_fire_hook);
    relative = (int32_t)(cave - (call + 5));
    memory_copy(call + 1, &relative, 4);
    VirtualProtect(call, 5, old_call, &ignored);
    VirtualProtect(cave, 16, old_cave, &ignored);
    FlushInstructionCache(GetCurrentProcess(), cave, 16);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    log_append("CONTROLLER PAUSE: the controller-missing pause now needs a real unplug.\r\n");
}
