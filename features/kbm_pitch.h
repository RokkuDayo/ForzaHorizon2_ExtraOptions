#ifndef FH2_KBM_PITCH_H
#define FH2_KBM_PITCH_H
#include "extra_options.h"
#include "kbm_config.h"

/* Mouse look pitch, a probe (MouseLookPitch=1, off by default). The game's
   driving look is yaw only. The attached cameras (CCamBumperHigh, CCamHood,
   CCamCockpit, CCamDriver, CCamWheel, CCamSide, CCamRearView) ask their
   look direction through one virtual (vtable +0x260: 0x1033e70, CCamHood
   0x1033f10); its result is tilted toward the car's up axis by the mouse
   pitch. The chase cameras (CCamFollow*) have no such virtual and are not
   reached. Vtable slots are patched only after their class, slot and
   function bytes are verified. */
void kbm_pitch_install(unsigned char *game_image);
/* Input tick (kbm_input.c update_mouse): mouse Y counts and whether mouse
   look is active for keyboard & mouse now. */
void kbm_pitch_update(const KbmSettings *settings, long dy, int active, uint64_t now, float dt);
/* Trace=1 diagnostics: LookBack held or not (input tick). */
void kbm_pitch_debug_look_back(int held);
#endif
