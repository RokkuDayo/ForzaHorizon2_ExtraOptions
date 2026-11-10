#include "fh2/autodrive_ui.h"
#include "shared/support.h"

typedef void (*DrawScreenFn)(void *, void *, void *);
typedef void (*PresentFn)(void *, void *);
typedef void (*ReleaseFn)(void *);

static unsigned char *game_image;
static volatile LONG rendering_disabled;

void autodrive_ui_prepare(unsigned char *image) {
    game_image = image;
}

void autodrive_ui_set_hidden(int hidden) {
    if (hidden) {
        InterlockedOr(&rendering_disabled, 1);
    } else {
        InterlockedAnd(&rendering_disabled, ~1L);
    }
}

void autodrive_ui_toggle_manual(void) {
    InterlockedXor(&rendering_disabled, 2);
}

void autodrive_ui_restore_manual(void) {
    InterlockedAnd(&rendering_disabled, ~2L);
}

void autodrive_ui_present(void *application, void *transform) {
    if (!InterlockedCompareExchange(&rendering_disabled, 0, 0)) {
        ((PresentFn)(game_image + 0x15b5110))(application, transform);
    }
}

__declspec(noinline) void autodrive_ui_draw(void *application, void *screen, void *transform) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0) - (uintptr_t)game_image;
    unsigned char *root = *(unsigned char **)screen;
    // Keep UserInterface's render-to-texture passes for in-world displays.
    if (!InterlockedCompareExchange(&rendering_disabled, 0, 0) ||
        caller == 0x14216af || caller == 0x1421bc8 || caller == 0x1422bdf ||
        (root && *(void **)root == game_image + 0x2de8380)) {
        ((DrawScreenFn)(game_image + 0x15b51a0))(application, screen, transform);
        return;
    }
    // Application::Render consumes the passed screen even when no draw is
    // submitted.
    if (root && InterlockedDecrement((volatile LONG *)(root + 0x10)) == 0) {
        ((ReleaseFn)(game_image + 0x257730))(root);
    }
}

void autodrive_ui_wait_render(void *renderer, void *surface, void *parameters) {
    if (!InterlockedCompareExchange(&rendering_disabled, 0, 0)) {
        ((DrawScreenFn)(game_image + 0x1140ed0))(renderer, surface, parameters);
        return;
    }
    // The graphics wrapper uses Release at +0x18, rather than the COM +0x10 slot.
    void *owned = *(void **)surface;
    if (owned) {
        ((ReleaseFn)(*(void ***)owned)[0x18 / 8])(owned);
    }
}
