#ifndef FH2_KBM_WIN32_H
#define FH2_KBM_WIN32_H
#include "extra_options.h"

/* Windows side of keyboard & mouse: the game window, focus, Azimuth's own
   prompt state, Raw Input collection on a private message-only window, and
   cursor confinement. No game addresses live here; the Azimuth host RVAs are
   byte-verified before use. */

typedef struct {
    unsigned short code; /* make code, 0x80 = E0 prefix */
    unsigned char down;
    unsigned char unused;
    uint32_t time;       /* GetTickCount64 low bits at arrival */
} KbmKeyEvent;

typedef enum {
    KBM_WIN32_STARTING,
    KBM_WIN32_RAW_INPUT,  /* keyboard and mouse through Raw Input */
    KBM_WIN32_POLLING,    /* keyboard polled per tick; no mouse look */
    KBM_WIN32_FAILED
} KbmWin32State;

int kbm_win32_start(void);
KbmWin32State kbm_win32_state(void);
int kbm_win32_raw_mouse(void);
int kbm_win32_next_event(KbmKeyEvent *event);
int kbm_win32_take_overflow(void);
void kbm_win32_take_mouse(long *dx, long *dy);
long kbm_win32_take_wheel(void);
/* Mouse buttons arrive as key events with these codes. */
enum { KBM_MOUSE_LEFT = 0xe1, KBM_MOUSE_RIGHT = 0xe2, KBM_MOUSE_MIDDLE = 0xe3 };
/* Cursor in client pixels; returns 1 when it is inside the game window. */
int kbm_win32_cursor(float *x, float *y, float *width, float *height);
/* Shows the system pointer over the fullscreen game (Azimuth hides it). */
void kbm_win32_show_cursor(int show);
/* Pointer size in percent of a 32-pixel arrow at 2160 lines. */
void kbm_win32_set_cursor_size(int percent);
/* Draws a keycap with `label` into size*size RGBA8 pixels. */
int kbm_win32_render_keycap(const wchar_t *label, unsigned size, unsigned char *rgba);
/* The real game window is the foreground window and Azimuth shows no prompt. */
int kbm_win32_focused(void);
int kbm_win32_host_prompt(void);
/* Physical state for polling and stuck-key repair: 1 down, 0 up, -1 unknown. */
int kbm_win32_key_down(unsigned code);
void kbm_win32_set_capture(int capture);
/* In-game rebinding: arm makes the next key or mouse button press (and its
   release) skip the game; take returns 1 with its code, 2 for a mouse button,
   0 while nothing came. Alt, Win and presses within 150 ms of arming are not
   taken; ignored returns such a code once (0 if none). Arm fails (0) unless
   Raw Input is running. */
int kbm_win32_capture_arm(void);
void kbm_win32_capture_disarm(void);
int kbm_win32_capture_take(unsigned *code);
unsigned kbm_win32_capture_ignored(void);
/* While set (count > 0), a press of one of these keys and its release are
   held back from the game; take_activation returns its code once. */
void kbm_win32_set_activation_keys(const unsigned short *codes, unsigned count);
unsigned kbm_win32_take_activation(void);
/* Zeroes Azimuth gamepad vibration (motors and impulse triggers) while
   `muted` returns nonzero. Patches one verified host vtable slot. */
int kbm_win32_gate_rumble(int (*muted)(void));
#endif
