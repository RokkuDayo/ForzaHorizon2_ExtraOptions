#ifndef FH2_EXIT_GAME_H
#define FH2_EXIT_GAME_H
#include "extra_options.h"
/* [Frontend] ExitGame (default 0): the pause menu's Car Packs tile (Marketplace
   tab, a store page that never opens offline) closes the game instead, with
   an Exit Game caption and the Quit icon. */
void exit_game_install(unsigned char *game_image);
/* Rewrites the Car Packs tile's caption in the pause page XAML, in place
   (capacity bytes available). Returns the new length (unchanged if not the
   pause page or not installed). */
int exit_game_relabel(char *bytes, int length, int capacity);
#endif
