typedef struct {
    unsigned kind;
    const char *state, *node, *replacement;
} OfflineCareerFallback;

static const OfflineCareerFallback offline_career_fallbacks[] = {
    {6, "<state id=\"set_rich_presence\">",
     "<node id=\"switch_on_career_race_mode\" state=\"career.switch_on_career_race_mode\">",
     "<node id=\"switch_on_career_race_mode\" state=\"career.is_career_event\"><exit id=\"yes\" target=\"_fh2_career_switch_on_career_race_mode\"/><exit id=\"no\" target=\"set_rich_presence\"/></node>\n    <node id=\"_fh2_career_switch_on_career_race_mode\" state=\"career.switch_on_career_race_mode\">"},
    {6, "<state id=\"change_player_car_if_required\">",
     "<node id=\"_switch_on_career_race_mode\" state=\"career.switch_on_career_race_mode\">",
     "<node id=\"_switch_on_career_race_mode\" state=\"career.is_career_event\"><exit id=\"yes\" target=\"_fh2_career__switch_on_career_race_mode\"/><exit id=\"no\" target=\".exit\"/></node>\n    <node id=\"_fh2_career__switch_on_career_race_mode\" state=\"career.switch_on_career_race_mode\">"},
    {6, "<state id=\"post_race\">",
     "<node id=\"switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">",
     "<node id=\"switch_on_career_event_type\" state=\"career.is_career_event\"><exit id=\"yes\" target=\"_fh2_career_switch_on_career_event_type\"/><exit id=\"no\" target=\".continue\"/></node>\n    <node id=\"_fh2_career_switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">"},
    {6, "<state id=\"event_finished_flow\">",
     "<node id=\"_switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">",
     "<node id=\"_switch_on_career_event_type\" state=\"career.is_career_event\"><exit id=\"yes\" target=\"_fh2_career__switch_on_career_event_type\"/><exit id=\"no\" target=\"exit_and_continue\"/></node>\n    <node id=\"_fh2_career__switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">"},
    {7, "<state id=\"start_flow_offline\">",
     "<node id=\"_switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">",
     "<node id=\"_switch_on_career_event_type\" state=\"career.is_career_event\"><exit id=\"yes\" target=\"_fh2_career__switch_on_career_event_type\"/><exit id=\"no\" target=\"_flow_common\"/></node>\n    <node id=\"_fh2_career__switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">"},
    {7, "<state id=\"wait_for_skippable_section\">",
     "<node id=\"_switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">",
     "<node id=\"_switch_on_career_event_type\" state=\"career.is_career_event\"><exit id=\"yes\" target=\"_fh2_career__switch_on_career_event_type\"/><exit id=\"no\" target=\"_wait_until_skip_available\"/></node>\n    <node id=\"_fh2_career__switch_on_career_event_type\" state=\"career.switch_on_career_event_type\">"},
};
