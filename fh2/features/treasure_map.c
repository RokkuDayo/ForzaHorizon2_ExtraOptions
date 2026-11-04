#include "features.h"
#include "support.h"

#define BOARD_SETUP_RVA 0x50b7c0u
#define BOARD_DISCOVER_RVA 0x4fe910u
#define BOARD_CALLBACK_SLOT_RVA 0x4028f8u
#define BOARD_STATE_OFFSET 0x100u
#define BOARD_POSITION_OFFSET 0x128u

typedef void (*BoardSetupFn)(void *board);
typedef void (*BoardDiscoverFn)(void *board, int state, const void *position);

static BoardSetupFn original_board_setup;
static BoardDiscoverFn discover_board;
static unsigned char callback_pointer[8];
static const unsigned char unused_slot[8] = {0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc};
static const unsigned char *const accepted_slot[] = {unused_slot, callback_pointer};

static void treasure_map_setup_board(void *board) {
    original_board_setup(board);
    unsigned char *object = (unsigned char *)board;
    if (*(int *)(object + BOARD_STATE_OFFSET) == 0) {
        discover_board(board, 1, object + BOARD_POSITION_OFFSET);
    }
}

PatchDefinition treasure_map_hook_patch = {BOARD_CALLBACK_SLOT_RVA,
                                           0,
                                           8,
                                           GROUP_TREASURE_MAP,
                                           "treasure_map_board_setup_callback",
                                           unused_slot,
                                           callback_pointer,
                                           callback_pointer,
                                           accepted_slot,
                                           2};

void treasure_map_prepare_hook(unsigned char *game_image) {
    original_board_setup = (BoardSetupFn)(game_image + BOARD_SETUP_RVA);
    discover_board = (BoardDiscoverFn)(game_image + BOARD_DISCOVER_RVA);
    uintptr_t callback = (uintptr_t)treasure_map_setup_board;
    memory_copy(callback_pointer, &callback, sizeof(callback));
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestTreasureMapHookAddress(void) {
    return (void *)treasure_map_setup_board;
}
#endif
