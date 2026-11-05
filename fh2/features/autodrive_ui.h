#ifndef FH2_AUTODRIVE_UI_H
#define FH2_AUTODRIVE_UI_H
#include "extra_options.h"

void autodrive_ui_prepare(unsigned char *image);
void autodrive_ui_set_hidden(int hidden);
void autodrive_ui_toggle_manual(void);
void autodrive_ui_restore_manual(void);
void autodrive_ui_draw(void *application, void *screen, void *transform);
void autodrive_ui_present(void *application, void *transform);
void autodrive_ui_wait_render(void *renderer, void *surface, void *parameters);
#endif
