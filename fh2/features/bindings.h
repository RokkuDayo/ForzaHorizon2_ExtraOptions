#ifndef FH2_BINDINGS_H
#define FH2_BINDINGS_H

typedef enum {
    BIND_AUTODRIVE,
    BIND_CANCEL_AUTODRIVE,
    BIND_HEADLIGHTS,
    BIND_ROOF,
    BIND_SIGNAL_LEFT,
    BIND_SIGNAL_RIGHT,
    BIND_HAZARDS,
    BIND_PHOTO,
    BIND_SHOWROOM_PHOTO,
    BIND_RETURN_GARAGE,
    BIND_MANUFACTURER_JUMP,
    BIND_TOGGLE_UI,
    BIND_RIM_MANUFACTURER_JUMP,
    BIND_CAR_MEET_PHOTO,
    BIND_COUNT
} GameAction;

void bindings_reset(void);
int bindings_parse(const char *key, const char *value);
unsigned binding_xbox(GameAction action);
unsigned binding_keyboard(GameAction action);
int binding_pad_down(GameAction action, unsigned buttons);
int binding_key_down(GameAction action);
int binding_down(GameAction action, unsigned buttons);
#endif
