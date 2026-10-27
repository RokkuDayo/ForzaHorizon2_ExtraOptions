#ifndef FH2_EXIT_GAME_H
#define FH2_EXIT_GAME_H
#include "extra_options.h"
#include <stddef.h>
/* [Frontend] ExitGame (default 0): an Exit Game tile on the pause menu's Home
   tab, beside Online Road Trip (made square), that closes the game. */
void exit_game_install(unsigned char *game_image);
/* The pause page init has run (return_garage.c): binds the Exit Game tile. */
void exit_game_page_ready(void *page);
/* 1 for the pause page XAML while Exit Game is installed. */
int exit_game_is_xaml(const char *bytes, size_t length);
/* Adds the tile to the pause page XAML in place (capacity bytes available);
   returns the new length, or the old one when the layout does not match. */
size_t exit_game_extend_xaml(char *bytes, size_t length, size_t capacity);
#endif
