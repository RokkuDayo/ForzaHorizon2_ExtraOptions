#include "features.h"
#include "logging.h"
#include "support.h"
#include "offline_events.h"
#include "custom_showcases.h"

/* Loaded CXML4Resource text only; no archive or EXE is written. */
#define XML_PARSE_SLOT_RVA 0x2ec4fe0u
#define XML_PARSE_RVA 0x13de7e0u
#define GAME_ALLOC_RVA 0x3229c0u
#define GAME_FREE_RVA 0x31c7b0u
#define GARAGE_GATE_RVA 0xccf660u
#define XML_MAX_SIZE (8u * 1024u * 1024u)

typedef unsigned char (*XmlParseFn)(void *resource);
typedef void *(*GameAllocFn)(size_t size);
typedef void (*GameFreeFn)(void *buffer);

typedef enum {
    FRONTEND_RESOURCE_NONE = 0,
    FRONTEND_RESOURCE_UI_SCREENS = 1,
    FRONTEND_RESOURCE_PAUSE_FLOW = 2,
    FRONTEND_RESOURCE_RACE_ACTIVATION = 3,
    FRONTEND_RESOURCE_FREE_ROAM_START = 4,
    FRONTEND_RESOURCE_GARAGE = 5,
    FRONTEND_RESOURCE_RACE_FLOW = 6,
    FRONTEND_RESOURCE_POST_RACE_FLOW = 7
} FrontendResourceKind;

typedef struct {
    uint32_t begin, end;
    const char *text;
} XmlEdit;

typedef struct {
    XmlEdit edits[16];
    uint32_t count, length;
} XmlPlan;

static void clear_xml_plan(XmlPlan *plan) {
    volatile unsigned char *p=(volatile unsigned char *)plan;
    for(size_t i=0;i<sizeof(*plan);++i)p[i]=0;
}

static XmlParseFn original_xml_parse;
static GameAllocFn game_alloc;
static GameFreeFn game_free;
static unsigned char *garage_game_image;
static int xml_hook_failed;
static int garage_enabled;
static int return_shortcut_enabled;
static int skip_ineligible_car_dialogue;
static int race_dialogue_failed;
static int spawn_at_festival;
static int festival_spawn_failed;

#include "festival_flow.h"
#include "garage_flow.h"
#include "return_garage_flow.h"
#include "showroom_photo_flow.h"
#include "offline_events_fallbacks.h"

/* Input disables are deque entries, not a boolean: release our entry before
   the outer pause flow releases its own. Consume the pause snapshot reference
   once before loading; a dedicated outer exit avoids re-enabling it on the way
   to the HUD or consuming an unrelated owner's reference. Native car loading
   and play_cutscene retain their own readiness checks. */

static uint32_t find_unique_text(const char *source, uint32_t length, const char *needle) {
    const char *found = text_find_unique(source, length, needle);
    return found ? (uint32_t)(found - source) : length;
}

static uint32_t find_first_text(const char *source, uint32_t length, const char *needle) {
    const char *found = *needle ? text_find(source, length, needle) : NULL;
    return found ? (uint32_t)(found - source) : length;
}

static int garage_plan_screen_exits(const char *block, uint32_t block_length, uint32_t start,
                                    XmlPlan *plan) {
    plan->count = 2;
    const char *back = "<exit id=\"back\" />";
    const char *transition = "<transition id=\"car_selected\" target=\".back\" />";
    uint32_t back_exit = find_unique_text(block, block_length, back),
             selected_exit = find_unique_text(block, block_length, transition);
    if (back_exit == block_length || selected_exit == block_length ||
        back_exit + text_length(back) > selected_exit ||
        find_unique_text(block, block_length,
                         "<behavior id=\"UIPageCarSelectGaragePauseMenu\" isBack=\"{IsEntry "
                         "enterback}\">") == block_length) {
        return 0;
    }
    plan->edits[0] = (XmlEdit){start + back_exit + (uint32_t)text_length(back),
                               start + back_exit + (uint32_t)text_length(back),
                               "\n    <exit id=\"car_selected\" />"};
    plan->edits[1] =
        (XmlEdit){start + selected_exit, start + selected_exit + (uint32_t)text_length(transition),
                  "<transition id=\"car_selected\" target=\".car_selected\" />"};
    return 1;
}

static int garage_plan_pause_transition(const char *source, uint32_t length, const char *block,
                                        uint32_t block_length, uint32_t start, uint32_t end,
                                        const char *closing, XmlPlan *plan) {
    uint32_t parent = find_unique_text(source, length, "<state id=\"pause_menu_flow\">");
    if (parent == length) {
        return 0;
    }
    uint32_t parent_end = find_first_text(source + parent, length - parent, "</state>") + parent;
    if (start <= parent || end >= parent_end || parent_end == length) {
        return 0;
    }
    if (find_unique_text(block, block_length,
                         "<exit id=\"back\" target=\"pause_menu.enterback\" />") == block_length ||
        find_unique_text(block, block_length, "<exit id=\"car_view\" target=\"view_car\" />") ==
            block_length ||
        find_unique_text(source, length,
                         "<node id=\"unpause_and_exit_to_hud\" "
                         "state=\"global.dummy_state\">") == length) {
        return 0;
    }
    plan->edits[0] =
        (XmlEdit){end, end, "<exit id=\"car_selected\" target=\"_fh2_car_store\" />\n    "};
    plan->edits[1] = (XmlEdit){end + (uint32_t)text_length(closing),
                               end + (uint32_t)text_length(closing), car_change_flow_xml};
    /* Add a dedicated exit to both levels of the pause flow. Every original
       route still uses its original pause-snapshot cleanup. The resume
       nodes sit after a node whose exits are explicit, not after the final
       wait node where implicit fall-through would affect other routes. */
    uint32_t outer = find_unique_text(source, length, "<state id=\"pause_menu_tiled_flow\">");
    uint32_t outer_end = outer == length
                             ? length
                             : find_first_text(source + outer, length - outer, "</state>") + outer;
    const char *outer_open = "<node id=\"_pause_menu_flow\" state=\"pause_menu_flow\">";
    uint32_t outer_node = find_unique_text(source, length, outer_open);
    uint32_t outer_node_end =
        outer_node == length
            ? length
            : find_first_text(source + outer_node, length - outer_node, "</node>") + outer_node;
    const char *inner_exit = "<exit id=\"exit_to_hud\" />";
    uint32_t exit_at = find_unique_text(source + parent, parent_end - parent, inner_exit) + parent;
    if (outer == length || outer_end == length || outer_node <= outer ||
        outer_node_end >= outer_end || exit_at == parent_end || exit_at >= start) {
        return 0;
    }
    const char *outer_block = source + outer_node;
    uint32_t outer_length = outer_node_end - outer_node;
    if (find_first_text(outer_block + (uint32_t)text_length(outer_open),
                        outer_length - (uint32_t)text_length(outer_open),
                        "<node ") != outer_length - text_length(outer_open) ||
        find_unique_text(outer_block, outer_length,
                         "<exit id=\"exit_to_hud\" target=\"_show_hud_and_continue\" />") ==
            outer_length ||
        find_unique_text(outer_block, outer_length,
                         "<exit id=\"exit_behind_loading_screen\" "
                         "target=\"_disable_mixer_snapshot\" />") == outer_length ||
        find_unique_text(outer_block, outer_length,
                         "<exit id=\"exit_to_mie\" "
                         "target=\"_show_empty_scene_and_continue\" />") == outer_length) {
        return 0;
    }
    exit_at += (uint32_t)text_length(inner_exit);
    plan->edits[2] = (XmlEdit){exit_at, exit_at, "\n    <exit id=\"_fh2_car_resumed\" />"};
    plan->edits[3] = (XmlEdit){outer_node_end, outer_node_end,
                               "<exit id=\"_fh2_car_resumed\" target=\"_fh2_car_hud\" />\n    "};
    outer_node_end += (uint32_t)text_length("</node>");
    plan->edits[4] = (XmlEdit){outer_node_end, outer_node_end, car_change_resume_xml};
    plan->edits[5] =
        (XmlEdit){parent_end + (uint32_t)text_length("</state>"),
                  parent_end + (uint32_t)text_length("</state>"),
                  "\n  <state id=\"_fh2_car_store_road\"><behavior id=\"StoreCarPosition\" "
                  "entity_id=\"_fh2_car_road\" locator_name=\"\" "
                  "restore_facing_next_event=\"false\" /></state>\n"};
    plan->count = 6;
    return 1;
}

static int garage_finalize_edit_plan(XmlPlan *plan, uint32_t length) {
    for (unsigned i = 1; i < plan->count; ++i) {
        XmlEdit edit = plan->edits[i];
        unsigned j = i;
        while (j && plan->edits[j - 1].begin > edit.begin) {
            plan->edits[j] = plan->edits[j - 1];
            --j;
        }
        plan->edits[j] = edit;
    }
    uint32_t total = length;
    for (unsigned i = 0; i < plan->count; ++i) {
        if (i && plan->edits[i - 1].end > plan->edits[i].begin) {
            return 0;
        }
        total += (uint32_t)text_length(plan->edits[i].text) -
                 (plan->edits[i].end - plan->edits[i].begin);
    }
    if (total > XML_MAX_SIZE) {
        return 0;
    }
    plan->length = total;
    return 1;
}

static int plan_ineligible_car_dialogue(const char *source, uint32_t length, XmlPlan *plan) {
    const char *opening = "<node id=\"play_car_ineligible_vo\" state=\"global.trigger_vo\" "
                          "target=\"IneligibleCar\" blockui=\"true\">";
    uint32_t parent = find_unique_text(source, length, "<state id=\"offline_race_entry_flow\">");
    uint32_t parent_end =
        parent == length ? length
                         : find_first_text(source + parent, length - parent, "</state>") + parent;
    uint32_t start = find_unique_text(source, length, opening);
    uint32_t end = start == length
                       ? length
                       : find_first_text(source + start, length - start, "</node>") + start;
    if (parent == length || parent_end == length || start <= parent || end >= parent_end ||
        find_unique_text(source + start, end - start,
                         "<exit target=\"prepare_for_car_screen\" />") == end - start ||
        find_first_text(source + start + text_length(opening), end - start - text_length(opening),
                        "<node ") != end - start - text_length(opening) ||
        find_unique_text(source + parent, parent_end - parent,
                         "<exit id=\"no\" target=\"play_car_ineligible_vo\" />") ==
            parent_end - parent ||
        find_unique_text(source + parent, parent_end - parent,
                         "<node id=\"prepare_for_car_screen\" "
                         "state=\"prepare_for_car_screen\" />") == parent_end - parent) {
        return 0;
    }
    plan->count = 1;
    plan->edits[0] = (XmlEdit){start, start + (uint32_t)text_length(opening),
                               "<node id=\"play_car_ineligible_vo\" state=\"global.dummy_state\">"};
    return garage_finalize_edit_plan(plan, length);
}

static int plan_festival_spawn(const char *source, uint32_t length, XmlPlan *plan) {
    const char *placement = "<node state=\"global.place_car_at_restore_point\" />";
    uint32_t start = find_unique_text(source, length, "<state id=\"cold_boot\">");
    if (start == length || find_first_text(source, length, "_fh2_festival_") != length) {
        return 0;
    }
    uint32_t end = find_first_text(source + start, length - start, "</state>") + start;
    if (end == length) {
        return 0;
    }
    const char *block = source + start;
    uint32_t size = end - start;
    uint32_t setup = find_unique_text(block, size, "<node state=\"setup_free_roam_offline\" />");
    uint32_t place = find_unique_text(block, size, placement);
    uint32_t stream = find_unique_text(
        block, size, "<node state=\"global.wait_for_streaming_around_local_human_car\" />");
    if (place == size || stream == size || setup >= place || place >= stream ||
        find_first_text(block, size, "<state ") != 0 ||
        find_first_text(block + 1, size - 1, "<state ") != size - 1) {
        return 0;
    }
    plan->count = 1;
    plan->edits[0] = (XmlEdit){start + place, start + place + (uint32_t)text_length(placement),
                               festival_boot_placement_xml};
    return garage_finalize_edit_plan(plan, length);
}

static int frontend_plan_xml_edits(const char *source, uint32_t length, unsigned resource_kind,
                                   XmlPlan *plan) {
    if (!source || !length || length > XML_MAX_SIZE ||
        (resource_kind != FRONTEND_RESOURCE_UI_SCREENS &&
         resource_kind != FRONTEND_RESOURCE_PAUSE_FLOW &&
         resource_kind != FRONTEND_RESOURCE_RACE_ACTIVATION &&
         resource_kind != FRONTEND_RESOURCE_FREE_ROAM_START)) {
        return 0;
    }
    for (uint32_t i = 0; i < length; ++i) {
        if (!source[i]) {
            return 0;
        }
    }
    if (resource_kind == FRONTEND_RESOURCE_RACE_ACTIVATION) {
        return plan_ineligible_car_dialogue(source, length, plan);
    }
    if (resource_kind == FRONTEND_RESOURCE_FREE_ROAM_START) {
        return plan_festival_spawn(source, length, plan);
    }
    if (find_first_text(source, length, "_fh2_car_") != length) {
        return 0;
    }
    const char *opening = resource_kind == FRONTEND_RESOURCE_UI_SCREENS
                              ? "<state id=\"car_select_garage_pause_menu\">"
                              : "<node id=\"_car_select_garage_pause_menu\" "
                                "state=\"ui_screens.car_select_garage_pause_menu\">";
    const char *closing = resource_kind == FRONTEND_RESOURCE_UI_SCREENS ? "</state>" : "</node>";
    uint32_t start = find_unique_text(source, length, opening);
    if (start == length) {
        return 0;
    }
    uint32_t end = find_first_text(source + start, length - start, closing) + start;
    if (end == length) {
        return 0;
    }
    uint32_t block_length = end - start;
    const char *block = source + start;
    /* Only the known leaf state/node is eligible. Nested elements of the same
       type, existing car_selected exits, or changed handlers are refused. */
    uint32_t inner = (uint32_t)text_length(opening);
    if (find_first_text(block + inner, block_length - inner,
                        resource_kind == FRONTEND_RESOURCE_UI_SCREENS ? "<state " : "<node ") !=
            block_length - inner ||
        find_first_text(block, block_length, "<exit id=\"car_selected\"") != block_length) {
        return 0;
    }

    int planned = resource_kind == FRONTEND_RESOURCE_UI_SCREENS
                      ? garage_plan_screen_exits(block, block_length, start, plan)
                      : garage_plan_pause_transition(source, length, block, block_length, start,
                                                     end, closing, plan);
    return planned && garage_finalize_edit_plan(plan, length);
}

static void frontend_apply_xml_edits(const char *source, uint32_t length, const XmlPlan *plan,
                                     char *destination) {
    uint32_t input = 0, output = 0;
    for (unsigned i = 0; i < plan->count; ++i) {
        const XmlEdit *edit = &plan->edits[i];
        uint32_t size = edit->begin - input;
        memory_copy(destination + output, source + input, size);
        output += size;
        size = (uint32_t)text_length(edit->text);
        memory_copy(destination + output, edit->text, size);
        output += size;
        input = edit->end;
    }
    memory_copy(destination + output, source + input, length - input);
    destination[plan->length] = 0;
}

static void garage_disable_action_on_failure(const char *message) {
    __atomic_store_n(&xml_hook_failed, 1, __ATOMIC_SEQ_CST);
    log_append(message);
    /* A partial/unsupported resource change must not expose the new action. */
    DWORD old, ignored;
    if (garage_game_image && memory_equal(garage_game_image + GARAGE_GATE_RVA, "\x90\x90", 2)) {
        if (VirtualProtect(garage_game_image + GARAGE_GATE_RVA, 2, PAGE_EXECUTE_READWRITE, &old)) {
            *(volatile uint16_t *)(garage_game_image + GARAGE_GATE_RVA) = 0x7474;
            FlushInstructionCache(GetCurrentProcess(), garage_game_image + GARAGE_GATE_RVA, 2);
            VirtualProtect(garage_game_image + GARAGE_GATE_RVA, 2, old, &ignored);
            log_append("GARAGE: Get In Car disabled for this session.\r\n");
        } else {
            log_append("GARAGE: failed to restore the Get In Car menu restriction.\r\n");
        }
    }
}

static unsigned frontend_identify_resource(const unsigned char *resource) {
    const char *name = (const char *)(resource + 0xb8);
    size_t length = *(const size_t *)(resource + 0xc8),
           capacity = *(const size_t *)(resource + 0xd0);
    if (!length || length > 4096 || capacity < length) {
        return 0;
    }
    if (capacity >= 16) {
        name = *(const char *const *)(resource + 0xb8);
    }
    if (!name) {
        return 0;
    }
    size_t start = 0;
    for (size_t i = 0; i < length; ++i) {
        if (name[i] == '/' || name[i] == '\\') {
            start = i + 1;
        }
    }
    const char *names[] = {"ui_screens.xml", "pause.xml", "race_trigger_activation.xml",
                           "free_roam_start.xml", "garage.xml", "race_flow.xml", "post_race_flow.xml"};
    for (unsigned kind = 1; kind <= ARRAY_COUNT(names); ++kind) {
        const char *expected = names[kind - 1];
        size_t n = text_length(expected);
        if (length - start != n) {
            continue;
        }
        size_t i;
        for (i = 0; i < n; ++i) {
            char ch = name[start + i];
            if (ch >= 'A' && ch <= 'Z') {
                ch += 'a' - 'A';
            }
            if (ch != expected[i]) {
                break;
            }
        }
        if (i == n) {
            return kind;
        }
    }
    return 0;
}

static void report_xml_failure(unsigned resource_kind, const char *garage_message,
                               const char *dialogue_message, const char *festival_message) {
    if (resource_kind == FRONTEND_RESOURCE_FREE_ROAM_START) {
        __atomic_store_n(&festival_spawn_failed, 1, __ATOMIC_SEQ_CST);
        log_append(festival_message);
    } else if (resource_kind == FRONTEND_RESOURCE_RACE_ACTIVATION) {
        __atomic_store_n(&race_dialogue_failed, 1, __ATOMIC_SEQ_CST);
        log_append(dialogue_message);
    } else {
        garage_disable_action_on_failure(garage_message);
    }
}

static int return_garage_plan(const char *source, uint32_t length, unsigned kind, XmlPlan *plan) {
    if (!source || !length || length > XML_MAX_SIZE || kind != FRONTEND_RESOURCE_GARAGE ||
        find_first_text(source, length, "_fh2_return_") != length) {
        return 0;
    }
    clear_xml_plan(plan);
    uint32_t at = find_unique_text(source, length, "</stategroup>");
    if (at == length ||
        find_unique_text(source, length, "<state id=\"root_festival\">") == length) {
        return 0;
    }
    // The native entry has already faded to black. Its looping camera establishes
    // the frontend streaming focus; don't reveal the car until those zones load.
    uint32_t pre = find_unique_text(source,length,"<state id=\"pre_functional_location\">");
    if(pre==length)return 0;
    uint32_t pre_end=pre+find_first_text(source+pre,length-pre,"</state>");
    const char *camera="<node state=\"load_and_start_looping_cutscene\" />";
    uint32_t camera_at=pre+find_unique_text(source+pre,pre_end-pre,camera);
    if(pre_end>=length || camera_at>=pre_end)return 0;
    uint32_t ready_at=camera_at+(uint32_t)text_length(camera);
    plan->edits[plan->count++] = (XmlEdit){ready_at,ready_at,
        "<node state=\"global.wait_one_frame\" />"
        "<node state=\"global.wait_for_streaming\" />"};
    plan->edits[plan->count++] = (XmlEdit){at, at, return_garage_flow_xml};
    return garage_finalize_edit_plan(plan, length);
}

static int return_garage_transform(void *game_resource, unsigned kind) {
    if (!return_shortcut_enabled || kind != FRONTEND_RESOURCE_GARAGE) {
        return 0;
    }
    unsigned char *resource = game_resource;
    char **buffer = (char **)(resource + 0xe0);
    uint32_t *length = (uint32_t *)(resource + 0xe8);
    XmlPlan plan;
    if (!return_garage_plan(*buffer, *length, kind, &plan)) {
        return 0;
    }
    char *replacement = game_alloc((size_t)plan.length + 1);
    if (!replacement) {
        return 0;
    }
    frontend_apply_xml_edits(*buffer, *length, &plan, replacement);
    game_free(*buffer);
    *buffer = replacement;
    *length = plan.length;
    return 1;
}

static int showroom_photo_transform(unsigned char *resource, unsigned kind) {
    if (kind != FRONTEND_RESOURCE_UI_SCREENS) {
        return 0;
    }
    const char *source = *(char **)(resource + 0xe0);
    uint32_t length = *(uint32_t *)(resource + 0xe8);
    if (!source || !length || length > XML_MAX_SIZE || text_find(source, length, "_fh2_photo")) {
        return 0;
    }
    const char *states[] = {"<state id=\"show_view_car\">",
                            "<state id=\"show_view_car_from_pause\">",
                            "<state id=\"car_color_select\">",
                            "<state id=\"show_car_meet_car_details\">"};
    const char *pages[] = {"<state id=\"_fh2_show_view_car\"><entry target=\".\" />"
                           "<exit id=\"_fh2_photo\" />",
                           "<state id=\"_fh2_show_view_car_from_pause\"><entry target=\".\" />"
                           "<exit id=\"_fh2_photo\" />",
                           "<state id=\"_fh2_car_color_select\"><entry target=\".\" />"
                           "<exit id=\"_fh2_photo\" />",
                            "<state id=\"_fh2_show_car_meet_car_details\"><entry target=\".\" /><exit id=\"_fh2_photo\" />"};
    const char entry[] = "<entry target=\".\" />";
    const char *behaviors[] = {"<behavior id=\"UIPageCarView\">",
                               "<behavior id=\"UIPageCarViewPauseMenu\">",
                               "<behavior id=\"UIPageCarColorSelect\">",
                               "<behavior id=\"UIPageCarMeetCarDetails\">"};
    const char *reload[] = {"<behavior id=\"UIPageCarView\" force_reload=\"true\">",
                            "<behavior id=\"UIPageCarViewPauseMenu\" force_reload=\"true\">",
                            "<behavior id=\"UIPageCarColorSelect\" force_reload=\"true\">",
                            "<behavior id=\"UIPageCarMeetCarDetails\" force_reload=\"true\">"};
    const char transition[] = "<transition id=\"_fh2_photo\" target=\"._fh2_photo\" />";
    XmlPlan plan; clear_xml_plan(&plan);
    for (unsigned i = 0; i < ARRAY_COUNT(states); ++i) {
        uint32_t start = find_unique_text(source, length, states[i]);
        if (start == length) {
            return 0;
        }
        uint32_t end = start + find_first_text(source + start, length - start, "</state>");
        if (end == length) {
            return 0;
        }
        uint32_t opening = start + find_unique_text(source + start, end - start, behaviors[i]);
        if (opening == end) {
            return 0;
        }
        uint32_t behavior = opening + find_first_text(source + opening, end - opening, "</behavior>");
        if (behavior == end) {
            return 0;
        }
        uint32_t entry_at = start + find_unique_text(source + start, opening - start, entry);
        if (entry_at == opening) {
            return 0;
        }
        plan.edits[plan.count++] =
            (XmlEdit){start, entry_at + (uint32_t)text_length(entry), pages[i]};
        plan.edits[plan.count++] =
            (XmlEdit){opening, opening + (uint32_t)text_length(behaviors[i]), reload[i]};
        plan.edits[plan.count++] = (XmlEdit){behavior, behavior, transition};
    }
    uint32_t closing = find_unique_text(source, length, "</stategroup>");
    if (closing == length) {
        return 0;
    }
    plan.edits[plan.count++] = (XmlEdit){closing, closing, showroom_photo_flows};
    if (!garage_finalize_edit_plan(&plan, length)) {
        return 0;
    }
    char *replacement = game_alloc((size_t)plan.length + 1);
    if (!replacement) {
        return 0;
    }
    frontend_apply_xml_edits(source, length, &plan, replacement);
    game_free((void *)source);
    *(char **)(resource + 0xe0) = replacement;
    *(uint32_t *)(resource + 0xe8) = plan.length;
    return 1;
}

static int offline_career_transform(unsigned char *resource, unsigned kind) {
    if (kind != FRONTEND_RESOURCE_RACE_FLOW && kind != FRONTEND_RESOURCE_POST_RACE_FLOW) {
        return 0;
    }
    const char *source = *(char **)(resource + 0xe0);
    uint32_t length = *(uint32_t *)(resource + 0xe8);
    if (!source || !length || length > XML_MAX_SIZE || text_find(source, length, "_fh2_career_")) {
        return 0;
    }
    XmlPlan plan; clear_xml_plan(&plan);
    for (unsigned i = 0; i < ARRAY_COUNT(offline_career_fallbacks); ++i) {
        const OfflineCareerFallback *rule = &offline_career_fallbacks[i];
        if (kind != rule->kind) continue;
        uint32_t start = find_unique_text(source, length, rule->state);
        if (start == length) return 0;
        uint32_t end = start + find_first_text(source + start, length - start, "</state>");
        if (end == length) return 0;
        uint32_t node = start + find_unique_text(source + start, end - start, rule->node);
        if (node == end) return 0;
        plan.edits[plan.count++] = (XmlEdit){node, node + (uint32_t)text_length(rule->node), rule->replacement};
    }
    if (!garage_finalize_edit_plan(&plan, length)) return 0;
    char *replacement = game_alloc((size_t)plan.length + 1);
    if (!replacement) return 0;
    frontend_apply_xml_edits(source, length, &plan, replacement);
    game_free((void *)source);
    *(char **)(resource + 0xe0) = replacement;
    *(uint32_t *)(resource + 0xe8) = plan.length;
    return 1;
}

static unsigned char frontend_parse_resource_hook(void *game_resource) {
    unsigned char *resource = game_resource;
    unsigned resource_kind = frontend_identify_resource(resource);
    int race_dialogue = resource_kind == FRONTEND_RESOURCE_RACE_ACTIVATION;
    int festival = resource_kind == FRONTEND_RESOURCE_FREE_ROAM_START;
    int enabled = festival                                    ? spawn_at_festival
                  : race_dialogue                             ? skip_ineligible_car_dialogue
                  : resource_kind >= FRONTEND_RESOURCE_GARAGE ? 0
                                                              : garage_enabled;
    int failed = __atomic_load_n(festival        ? &festival_spawn_failed
                                 : race_dialogue ? &race_dialogue_failed
                                                 : &xml_hook_failed,
                                 __ATOMIC_SEQ_CST);
    if (resource_kind && enabled && !failed) {
        char **buffer = (char **)(resource + 0xe0);
        uint32_t *length = (uint32_t *)(resource + 0xe8);
        XmlPlan plan;
        if (!frontend_plan_xml_edits(*buffer, *length, resource_kind, &plan)) {
            report_xml_failure(resource_kind,
                               "GARAGE: unsupported menu-flow text; car reload unavailable.\r\n",
                               "FRONTEND: unsupported race flow; ineligible-car dialogue "
                               "unchanged.\r\n",
                               "STARTUP: unsupported cold-boot flow; festival override "
                               "disabled.\r\n");
        } else {
            char *replacement = game_alloc((size_t)plan.length + 1);
            if (!replacement) {
                report_xml_failure(
                    resource_kind,
                    "GARAGE: menu-flow allocation failed; car reload unavailable.\r\n",
                    "FRONTEND: race-flow allocation failed; ineligible-car dialogue "
                    "unchanged.\r\n",
                    "STARTUP: cold-boot allocation failed; festival override "
                    "disabled.\r\n");
            } else {
                frontend_apply_xml_edits(*buffer, *length, &plan, replacement);
                char *original_buffer = *buffer;
                *buffer = replacement;
                *length = plan.length;
                game_free(original_buffer);
                log_append(festival        ? "STARTUP: mainland festival placement added to "
                                             "cold-boot flow.\r\n"
                           : race_dialogue ? "FRONTEND: blocking IneligibleCar "
                                             "dialogue skipped in memory.\r\n"
                           : resource_kind == FRONTEND_RESOURCE_UI_SCREENS
                               ? "GARAGE: ui_screens.xml car_selected exit patched in "
                                 "memory.\r\n"
                               : "GARAGE: pause.xml car reload/resume sequence patched "
                                 "in memory.\r\n");
                if (resource_kind == FRONTEND_RESOURCE_PAUSE_FLOW) {
                    log_append("GARAGE: loading screen and fast-travel arrival animation "
                               "enabled.\r\n");
                }
                if (resource_kind == FRONTEND_RESOURCE_PAUSE_FLOW) {
                    log_append("GARAGE: transition input locks balanced; loading radio "
                               "resumed.\r\n");
                }
                if (resource_kind == FRONTEND_RESOURCE_PAUSE_FLOW) {
                    log_append("GARAGE: direct audio handoff; world-streaming and LOD0 waits "
                               "removed.\r\n");
                }
            }
        }
    }
    int return_garage_ready = return_garage_transform(resource, resource_kind);
    int photo_ready = showroom_photo_transform(resource, resource_kind);
    int events_ready = resource_kind == FRONTEND_RESOURCE_RACE_ACTIVATION ? offline_events_transform(resource) : 0;
    int showcases_ready = custom_showcases_transform(resource, resource_kind);
    int career_ready = offline_career_transform(resource, resource_kind);
    unsigned char result = original_xml_parse(resource);
    if (resource_kind == FRONTEND_RESOURCE_RACE_FLOW) {
        custom_showcases_resource_parsed(resource_kind, result && showcases_ready);
    }
    if (resource_kind >= FRONTEND_RESOURCE_RACE_FLOW) {
        if (!result || !career_ready) offline_events_resource_parsed(0);
        log_append(result && career_ready ? "EVENTS: standalone race career fallbacks ready.\r\n" : "EVENTS: unsupported race lifecycle; offline event points disabled.\r\n");
    }
    if (resource_kind == FRONTEND_RESOURCE_RACE_ACTIVATION) {
        offline_events_resource_parsed(result && events_ready);
        custom_showcases_resource_parsed(resource_kind, result && showcases_ready);
        log_append(result && events_ready ? "EVENTS: offline race setup flow ready.\r\n" : "EVENTS: unsupported race flow; offline event points disabled.\r\n");
    }
    if (resource_kind == FRONTEND_RESOURCE_UI_SCREENS) {
        showroom_photo_resource_parsed(result && photo_ready);
        log_append(result && photo_ready
                       ? "SHOWROOM: Photo Mode preview transitions ready.\r\n"
                       : "SHOWROOM: Photo Mode shortcut disabled; unsupported flow.\r\n");
    }
    if (resource_kind == FRONTEND_RESOURCE_GARAGE) {
        return_garage_resource_parsed(resource_kind, result && return_garage_ready);
        if (!return_garage_ready) {
            log_append("GARAGE: return shortcut disabled; unsupported garage flow.\r\n");
        }
    }
    if (resource_kind && enabled && !result) {
        report_xml_failure(resource_kind,
                           "GARAGE: original XML parser failed; car reload unavailable.\r\n",
                           "FRONTEND: original race-flow XML parser failed.\r\n",
                           "STARTUP: original cold-boot XML parser failed; override "
                           "disabled.\r\n");
    }
    return result;
}

/* Unlike the fixed EXE ranges, this pointer is relocated by the game loader
   and its replacement points into this DLL. It is never normalized on disk. */
static unsigned char hook_original_bytes[8], hook_target_bytes[8];
static const unsigned char *const hook_accepted[] = {hook_original_bytes, hook_target_bytes};
PatchDefinition garage_hook_patch = {XML_PARSE_SLOT_RVA,
                                     0,
                                     8,
                                     GROUP_FRONTEND_XML,
                                     "garage_xml_parse_hook",
                                     hook_original_bytes,
                                     hook_target_bytes,
                                     hook_target_bytes,
                                     hook_accepted,
                                     2};

void garage_prepare_hook(unsigned char *game_image) {
    uintptr_t original_callback = (uintptr_t)(game_image + XML_PARSE_RVA),
              replacement_callback = (uintptr_t)frontend_parse_resource_hook;
    memory_copy(hook_original_bytes, &original_callback, 8);
    memory_copy(hook_target_bytes, &replacement_callback, 8);
    original_xml_parse = (XmlParseFn)original_callback;
    game_alloc = (GameAllocFn)(game_image + GAME_ALLOC_RVA);
    game_free = (GameFreeFn)(game_image + GAME_FREE_RVA);
    garage_game_image = game_image;
    __atomic_store_n(&xml_hook_failed, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&race_dialogue_failed, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&festival_spawn_failed, 0, __ATOMIC_SEQ_CST);
}

void garage_commit_options(const ModOptions *options) {
    garage_enabled = options->garage_from_pause;
    return_shortcut_enabled = options->enabled;
    skip_ineligible_car_dialogue = options->skip_ineligible_car_dialogue;
    spawn_at_festival = options->spawn_at_festival && options->skip_car_meet_on_startup;
}

void *garage_callback_address(void) {
    return (void *)frontend_parse_resource_hook;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) uint32_t Fh2TestReturnGarageXml(const char *source, uint32_t length,
                                                      unsigned kind, char *output,
                                                      uint32_t capacity) {
    XmlPlan plan;
    if (!return_garage_plan(source, length, kind, &plan) || !output || capacity <= plan.length) {
        return 0;
    }
    frontend_apply_xml_edits(source, length, &plan, output);
    return plan.length;
}

__declspec(dllexport) void *Fh2TestXmlHookAddress(void) {
    return (void *)frontend_parse_resource_hook;
}

__declspec(dllexport) uint32_t Fh2TestXmlTransform(const char *input, uint32_t length,
                                                   unsigned kind, char *output, uint32_t capacity) {
    XmlPlan plan;
    if (!frontend_plan_xml_edits(input, length, kind, &plan) || !output ||
        capacity <= plan.length) {
        return 0;
    }
    frontend_apply_xml_edits(input, length, &plan, output);
    return plan.length;
}
#endif
