#ifndef FH2_KBM_CONFIG_H
#define FH2_KBM_CONFIG_H
#include "extra_options.h"

/* Keyboard & mouse settings from [KeyboardMouse] in FH2_ExtraOptions.ini.
   They live outside ModOptions (settings.c keeps them). Key codes are physical scan codes: the make code, plus 0x80 for
   E0-prefixed keys, so bindings do not move with the keyboard layout. */

typedef enum { KBM_MODE_AUTO, KBM_MODE_KBM, KBM_MODE_PAD } KbmMode;

typedef enum {
    /* Driving: the game's CONTEXT_RACING (0). */
    KBM_THROTTLE,
    KBM_BRAKE,
    KBM_STEER_LEFT,
    KBM_STEER_RIGHT,
    KBM_HANDBRAKE,
    KBM_SHIFT_UP,
    KBM_SHIFT_DOWN,
    KBM_CLUTCH,
    KBM_CAMERA,
    KBM_REWIND,
    KBM_LOOK_BACK,
    KBM_ACTIVATE,
    KBM_PHOTO_MODE,
    KBM_RADIO_PREVIOUS,
    KBM_RADIO_NEXT,
    /* Menus and HUD: the game's CONTEXT_UI (7). */
    KBM_MAP,
    KBM_PAUSE,
    KBM_CONFIRM,
    KBM_BACK,
    KBM_UP,
    KBM_DOWN,
    KBM_LEFT,
    KBM_RIGHT,
    KBM_TAB_LEFT,
    KBM_TAB_RIGHT,
    KBM_MENU_X,
    KBM_MENU_Y,
    KBM_MENU_LEFT_TRIGGER,
    KBM_MENU_RIGHT_TRIGGER,
    /* The D-pad families are separate from the arrows: the UI turns the
       first answered action into one key per frame, D-pad (NumPad keys)
       before the stick directions (arrow keys) that the menus navigate by. */
    KBM_DPAD_UP,
    KBM_DPAD_DOWN,
    KBM_DPAD_LEFT,
    KBM_DPAD_RIGHT,
    KBM_BINDING_COUNT,
    KBM_FIRST_MENU_BINDING = KBM_MAP
} KbmBinding;

enum { KBM_KEYS_PER_BINDING = 3 };

typedef struct {
    int enabled;
    int mode;
    int mouse_look;
    int mouse_capture;
    int mouse_sensitivity;   /* thousandths of a degree per mouse count */
    int mouse_return_delay;  /* milliseconds without movement before the view recenters */
    int mouse_return_speed;  /* degrees per second while recentering */
    int steering_speed;      /* percent of the built-in keyboard steering rates */
    int invert_steering;
    int trace;
    int escape;              /* Esc: pause in the world, back elsewhere */
    int menu_cursor;         /* pointer in the pause menu and map */
    int cursor_size;         /* percent of a 32-pixel arrow at 2160 lines */
    int menu_arrows_dpad;    /* arrows raise the D-pad actions instead of the stick ones */
    int map_cursor_speed;    /* percent; 100 = the 4g map cursor speed halved */
    int mouse_look_pitch;    /* mouse Y tilts the in-car look direction (kbm_pitch.c probe) */
    int invert_mouse_y;
    unsigned short keys[KBM_BINDING_COUNT][KBM_KEYS_PER_BINDING];
} KbmSettings;

void kbm_config_defaults(KbmSettings *settings);
/* 1 = accepted, 0 = unknown key or invalid value. */
int kbm_config_parse(KbmSettings *settings, const char *key, const char *value);
/* 1 when no key is bound twice inside the driving or the menu group. */
int kbm_config_validate(const KbmSettings *settings);
const char *kbm_config_binding_name(unsigned binding);
const char *kbm_config_key_name(unsigned code);
/* The bindable keys in table order (in-game rebinding, kbm_rebind.c). */
unsigned kbm_config_key_count(void);
unsigned kbm_config_key_code(unsigned index);
/* "W", "Down,B" or "None": the [KeyboardMouse] value for a binding's keys. */
void kbm_config_format_keys(const unsigned short keys[KBM_KEYS_PER_BINDING], char *out, size_t capacity);
#endif
