#define SHOWROOM_PHOTO_FLOW(name, exits, routes)                                                   \
    "<state id=\"" name "\">"                                                                      \
    "<entry target=\"_preview\" />" exits                                                          \
    "<parameter id=\"free_look_on\" type=\"state\" value=\"global.dummy_state\" />"                \
    "<parameter id=\"free_look_off\" type=\"state\" value=\"global.dummy_state\" />"               \
    "<node id=\"_preview\" state=\"_fh2_" name "\" "                                               \
    "free_look_on=\"{free_look_on}\" free_look_off=\"{free_look_off}\">" routes                    \
    "<exit target=\".exit\" /><exit id=\"_fh2_photo\" target=\"_orbit\" /></node>"                 \
    "<node id=\"_orbit\" state=\"{free_look_on}\" />"                                              \
    "<node state=\"global.wait_for_next_frame\" />"                                                \
    "<node id=\"_photo\" state=\"ui_screens.show_photo_mode\">"                                    \
    "<exit id=\"back\" target=\"_restore_camera\" /></node>"                                       \
    "<node id=\"_restore_camera\" state=\"{free_look_off}\">"                                      \
    "<exit target=\"_preview\" /></node></state>"

static const char showroom_photo_flows[] = SHOWROOM_PHOTO_FLOW(
    "show_view_car", "<exit id=\"get_in_car\" /><exit id=\"back\" />",
    "<exit id=\"get_in_car\" target=\".get_in_car\" />"
    "<exit id=\"back\" target=\".back\" />") SHOWROOM_PHOTO_FLOW("show_view_car_from_pause", "", "")
    SHOWROOM_PHOTO_FLOW("car_color_select",
                        "<exit id=\"done\" /><exit id=\"done_but_browse\" /><exit id=\"back\" />",
                        "<exit id=\"done\" target=\".done\" />"
                        "<exit id=\"done_but_browse\" target=\".done_but_browse\" />"
                        "<exit id=\"back\" target=\".back\" />")
    SHOWROOM_PHOTO_FLOW("show_car_meet_car_details", "<exit id=\"back\" /><exit id=\"buy_car\" />",
                        "<exit id=\"back\" target=\".back\" /><exit id=\"buy_car\" target=\".buy_car\" />");

#undef SHOWROOM_PHOTO_FLOW
