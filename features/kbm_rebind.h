#ifndef FH2_KBM_REBIND_H
#define FH2_KBM_REBIND_H
#include "extra_options.h"
#include "kbm_config.h"
#include "video_options_text.h"

/* In-game key rebinding on the KEYBOARD & MOUSE page (kbm_menu.c). Each
   bindable action is a spinner row whose choices are fixed labels: the row's
   keys when the list was built, its default keys, None, every single key
   name and the short messages. The row's value picks the label, so a change
   needs only a value notification. Changing the value (Left/Right), or Enter
   on the focused row, arms a capture in kbm_win32.c; the next key press
   becomes the binding. Items: 0..KBM_REBIND_KEY_ITEMS-1 the actions, then
   Reset, then the hidden list-selection tracker (OptionsList SelectedIndex). */
enum { KBM_REBIND_KEY_ITEMS = 32, KBM_REBIND_RESET_ITEM = 32, KBM_REBIND_LIST_ITEM = 33,
       KBM_REBIND_ITEMS = 34 };

void kbm_rebind_set_directory(const wchar_t *directory);
int kbm_rebind_get(unsigned item);
/* 1 when the page should read the value again. */
int kbm_rebind_set(unsigned item, int value);
unsigned kbm_rebind_choice_count(unsigned item);
/* Valid until the next call. */
const wchar_t *kbm_rebind_choice(unsigned item, unsigned index, const VideoOptionsText *text);
const char *kbm_rebind_name(unsigned item, const VideoOptionsText *text);
const char *kbm_rebind_description(unsigned item, const VideoOptionsText *text);
/* The item's choice collection no longer holds its current label. */
int kbm_rebind_labels_stale(unsigned item);
/* 1 once after the item's displayed value changed outside its setter. */
int kbm_rebind_take_dirty(unsigned item);
/* A new KEYBOARD & MOUSE page was built. */
void kbm_rebind_page_built(void);
/* UI thread, once per UserInterface::Update (kbm_input.c). */
void kbm_rebind_ui_tick(void);
#endif
