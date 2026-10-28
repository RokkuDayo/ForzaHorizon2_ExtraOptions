#ifndef FH2_KBM_INPUT_H
#define FH2_KBM_INPUT_H
#include "extra_options.h"
#include "kbm_config.h"

/* Native keyboard & mouse: keys answer the game's own action queries
   (EngineInputCmd per EngineInputContexts) for the local player's
   controller object; the mouse turns the native driving look angle. No
   virtual pad, no XInput/WGI readings and no controller state writes. */
void kbm_install(unsigned char *game_image, const KbmSettings *settings);
/* Keyboard driving keys are held while keyboard & mouse is the active source. */
int kbm_driving_input_active(void);
/* A [KeyboardMouse] key changed in the in-game settings page; applies live. */
void kbm_menu_option(const wchar_t *key, const wchar_t *value);
/* New key bindings, applied live (UI thread). */
void kbm_set_keys(const unsigned short keys[][KBM_KEYS_PER_BINDING]);
/* The open menu page while the pointer prompts are installed, else NULL. */
void *kbm_input_top_page(void);
#endif
