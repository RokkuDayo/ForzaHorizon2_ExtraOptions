#ifndef FH2_GAME_INPUT_H
#define FH2_GAME_INPUT_H

typedef struct {
    int toggle, cancel, focused;
} KeyboardInput;

int game_key_down(unsigned key);
int game_triggers_down(unsigned triggers);
KeyboardInput game_input_read(void);
#endif
