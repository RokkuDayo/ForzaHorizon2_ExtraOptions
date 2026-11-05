#ifndef FH2_FESTIVAL_FLOW_H
#define FH2_FESTIVAL_FLOW_H

static const char festival_boot_placement_xml[] =
    "<node id=\"_fh2_festival_track\" "
    "state=\"track_swap.switch_on_active_track\">\n"
    "  <exit id=\"mainland\" target=\"_fh2_festival_intro\" />\n"
    "  <exit id=\"island\" target=\"_fh2_festival_restore\" />\n"
    "</node>\n"
    "<node id=\"_fh2_festival_intro\" state=\"global.is_ie_complete\">\n"
    "  <exit id=\"yes\" target=\"_fh2_festival_place\" />\n"
    "  <exit id=\"no\" target=\"_fh2_festival_restore\" />\n"
    "</node>\n"
    "<node id=\"_fh2_festival_place\" "
    "state=\"global.place_player_car_at_route_locator\" "
    "route_locator_name=\"festival_locator_garage_exit\" "
    "entity_id=\"track_route/route\" "
    "is_sibling=\"false\">\n"
    "  <exit target=\"_fh2_festival_done\" />\n"
    "</node>\n"
    "<node id=\"_fh2_festival_restore\" "
    "state=\"global.place_car_at_restore_point\">\n"
    "  <exit target=\"_fh2_festival_done\" />\n"
    "</node>\n"
    "<node id=\"_fh2_festival_done\" state=\"global.dummy_state\" />";

#endif
