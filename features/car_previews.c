#include "car_previews.h"
#include "logging.h"
#include "support.h"

/* Garage thumbnails (GarageThumbnail_<car> in the save: bigThumb.png and
   smallThumb.png) are rendered by CUI4PresentationRenderer (vtable slot 20)
   into an 816x459 target, or 384x216 in its second mode, and stored at the
   sizes of a two-rectangle table in .rdata: 816x459 and 192x108. Both are
   multiplied by the scale (2x is 1:1 in the My Cars panel at 2160p; the car
   strip tile is about 634x365 there), and the second mode's target is the
   small picture's size. The renderer's size block is rewritten in place:

       mov esi, big width / mov r13d, big height / lea r15, [rdi+0x50]
       cmp r12d, 1 / jne end
       mov esi, small width / mov r13d, small height
       add r15, 0x30                                  ; was lea r15, [rdi+0x80]
       nop / nop
   end:

   The original derives the second width from the first height (lea esi,
   [r13-0x4b]), so it is set directly. */
#define RENDER_SIZE_RVA 0xaa6a81u
#define STORED_SIZE_RVA 0x1dfde70u

static const unsigned char render_original[38] = {
    0xbe, 0x30, 0x03, 0x00, 0x00, 0x41, 0xbd, 0xcb, 0x01, 0x00, 0x00, 0x4c, 0x8d,
    0x7f, 0x50, 0x41, 0x83, 0xfc, 0x01, 0x75, 0x11, 0x41, 0x8d, 0x75, 0xb5, 0x41,
    0xbd, 0xd8, 0x00, 0x00, 0x00, 0x4c, 0x8d, 0xbf, 0x80, 0x00, 0x00, 0x00};
static const unsigned char render_patched[38] = {
    0xbe, 0x60, 0x06, 0x00, 0x00, 0x41, 0xbd, 0x96, 0x03, 0x00, 0x00, 0x4c, 0x8d,
    0x7f, 0x50, 0x41, 0x83, 0xfc, 0x01, 0x75, 0x0f, 0xbe, 0x00, 0x03, 0x00, 0x00,
    0x41, 0xbd, 0xb0, 0x01, 0x00, 0x00, 0x49, 0x83, 0xc7, 0x30, 0x90, 0x90};
/* Offsets of the four immediates in render_patched. */
enum { BIG_WIDTH = 1, BIG_HEIGHT = 7, SMALL_WIDTH = 22, SMALL_HEIGHT = 28 };
static const int32_t stored_original[8] = {0, 0, 816, 459, 0, 0, 192, 108};

static int write_code(unsigned char *target, const void *bytes, size_t size) {
    DWORD previous, ignored;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &previous)) {
        return 0;
    }
    memory_copy(target, bytes, size);
    VirtualProtect(target, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    return 1;
}

int car_previews_install(unsigned char *image, unsigned scale) {
    unsigned char render[sizeof(render_original)];
    int32_t stored[8];
    if (scale < 2 || scale > 4) {
        return 0;
    }
    if (!memory_read(image + RENDER_SIZE_RVA, render, sizeof(render)) ||
        !memory_read(image + STORED_SIZE_RVA, stored, sizeof(stored)) ||
        !memory_equal(render, render_original, sizeof(render)) ||
        !memory_equal(stored, stored_original, sizeof(stored))) {
        log_append("CAR PREVIEWS: thumbnail sizes not as expected; not installed.\r\n");
        return 0;
    }
    const int32_t sizes[4] = {816 * (int32_t)scale, 459 * (int32_t)scale, 192 * (int32_t)scale,
                              108 * (int32_t)scale};
    const int32_t stored_patched[8] = {0, 0, sizes[0], sizes[1], 0, 0, sizes[2], sizes[3]};
    memory_copy(render, render_patched, sizeof(render));
    memory_copy(render + BIG_WIDTH, &sizes[0], 4);
    memory_copy(render + BIG_HEIGHT, &sizes[1], 4);
    memory_copy(render + SMALL_WIDTH, &sizes[2], 4);
    memory_copy(render + SMALL_HEIGHT, &sizes[3], 4);
    if (!write_code(image + RENDER_SIZE_RVA, render, sizeof(render)) ||
        !write_code(image + STORED_SIZE_RVA, stored_patched, sizeof(stored_patched))) {
        log_append("CAR PREVIEWS: write failed.\r\n");
        return 0;
    }
    log_append_value("CAR PREVIEWS: new garage thumbnails at scale ", (int)scale);
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestCarPreviews(unsigned char *image, unsigned scale) {
    return car_previews_install(image, scale);
}
#endif
