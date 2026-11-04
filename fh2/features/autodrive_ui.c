#include "autodrive_ui.h"
#include "support.h"

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

void autodrive_ui_draw(void *application, void *screen, void *transform) {
    if (!InterlockedCompareExchange(&rendering_disabled, 0, 0)) {
        ((DrawScreenFn)(game_image + 0x15b51a0))(application, screen, transform);
        return;
    }
    // Application::Render consumes the passed screen even when no draw is
    // submitted.
    unsigned char *root = *(unsigned char **)screen;
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
