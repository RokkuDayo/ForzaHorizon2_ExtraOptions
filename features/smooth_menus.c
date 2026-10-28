#include "smooth_menus.h"
#include "logging.h"
#include "support.h"

/* Inspected FH2 release: media\renderscenarios\*.xml (DynamicRenderSettings)
   are read into one object per setting; the value lives at +8 and each
   setting class reads it through vtable slot 6 (+0x30). The FrameRate class
   (vtable 0x2eb98d0, slot 2 GetName 0x13073b0 -> "FrameRate") reads it with
   the int reader 0x130ab00, which other int settings share. The FrontEnd,
   Photo and Replay scenarios set FrameRate to 30, so menus, replays and photo
   mode run at 30 on the console. Only slot 6 of this vtable is swapped: after
   the game's own read, a value below 60 becomes 60. The class default (slot
   4, 0x130bb50) already stores 60, so it is checked but left alone. The XML
   files are not changed. */
#define FRAME_RATE_VTABLE 0x2eb98d0u
#define FRAME_RATE_NAME 0x13073b0u
#define FRAME_RATE_NAME_TEXT 0x22372a8u
#define FRAME_RATE_DEFAULT 0x130bb50u
#define INT_SERIALIZE 0x130ab00u
#define SLOT_NAME 2
#define SLOT_DEFAULT 4
#define SLOT_SERIALIZE 6
#define SMOOTH_FRAME_RATE 60

typedef void (*SerializeFn)(void *self, void *reader);

static unsigned char *game;
static volatile LONG raised;

/* lea rax, [rip+"FrameRate"]; ret */
static const unsigned char name_code[] = {0x48, 0x8d, 0x05, 0xf1, 0xfe, 0xf2, 0x00, 0xc3};
static const char name_text[] = "FrameRate";
/* mov dword [rcx+8], 60; ret */
static const unsigned char default_code[] = {0xc7, 0x41, 0x08, 0x3c, 0x00, 0x00, 0x00, 0xc3};
/* The whole int reader: reads [rcx+8] through the reader's slot 0x188 and
   stores the result back to [rcx+8]. */
static const unsigned char serialize_code[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x41, 0x08, 0x4c, 0x8b, 0xca, 0x48, 0x8b,
    0xd9, 0x89, 0x44, 0x24, 0x30, 0x48, 0x8b, 0x02, 0x4c, 0x8d, 0x05, 0x83, 0x7c, 0xb4,
    0x00, 0x48, 0x8d, 0x54, 0x24, 0x30, 0x49, 0x8b, 0xc9, 0xff, 0x90, 0x88, 0x01, 0x00,
    0x00, 0x8b, 0x10, 0x89, 0x53, 0x08, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3};

static int32_t smooth_frame_rate(int32_t frame_rate) {
    return frame_rate < SMOOTH_FRAME_RATE ? SMOOTH_FRAME_RATE : frame_rate;
}

static void serialize_hook(unsigned char *self, void *reader) {
    ((SerializeFn)(game + INT_SERIALIZE))(self, reader);
    int32_t frame_rate, smooth;
    memory_copy(&frame_rate, self + 8, sizeof(frame_rate));
    smooth = smooth_frame_rate(frame_rate);
    if (smooth == frame_rate) {
        return;
    }
    memory_copy(self + 8, &smooth, sizeof(smooth));
    if (InterlockedIncrement(&raised) == 1) {
        log_append_value("SMOOTH MENUS: raised a render scenario frame rate to 60 from ",
                         frame_rate);
    }
}

static int bytes_are(const unsigned char *image, uint32_t rva, const void *bytes, size_t size) {
    unsigned char actual[sizeof(serialize_code)];
    return size <= sizeof(actual) && memory_read(image + rva, actual, size) &&
           memory_equal(actual, bytes, size);
}

int smooth_menus_install(unsigned char *game_image) {
    void **vtable = (void **)(game_image + FRAME_RATE_VTABLE);
    void *slots[SLOT_SERIALIZE + 1];
    if (!memory_read(vtable, slots, sizeof(slots)) ||
        slots[SLOT_NAME] != game_image + FRAME_RATE_NAME ||
        slots[SLOT_DEFAULT] != game_image + FRAME_RATE_DEFAULT ||
        slots[SLOT_SERIALIZE] != game_image + INT_SERIALIZE ||
        !bytes_are(game_image, FRAME_RATE_NAME, name_code, sizeof(name_code)) ||
        !bytes_are(game_image, FRAME_RATE_NAME_TEXT, name_text, sizeof(name_text)) ||
        !bytes_are(game_image, FRAME_RATE_DEFAULT, default_code, sizeof(default_code)) ||
        !bytes_are(game_image, INT_SERIALIZE, serialize_code, sizeof(serialize_code))) {
        log_append("SMOOTH MENUS: FrameRate render scenario class not as expected; not "
                   "installed.\r\n");
        return 0;
    }
    void **slot = vtable + SLOT_SERIALIZE;
    DWORD previous, ignored;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) {
        log_append("SMOOTH MENUS: FrameRate slot not writable; not installed.\r\n");
        return 0;
    }
    game = game_image;
    InterlockedExchangePointer(slot, (void *)(uintptr_t)serialize_hook);
    VirtualProtect(slot, sizeof(*slot), previous, &ignored);
    log_append("SMOOTH MENUS: installed; menus, replays and photo mode run at up to 60 "
               "FPS.\r\n");
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestSmoothFrameRate(int frame_rate) {
    return smooth_frame_rate(frame_rate);
}

__declspec(dllexport) int Fh2TestSmoothMenusInstall(unsigned char *image) {
    return smooth_menus_install(image);
}

__declspec(dllexport) void *Fh2TestSmoothMenusHook(void) {
    return (void *)(uintptr_t)serialize_hook;
}
#endif
