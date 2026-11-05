#include "custom_showcases.h"
#include "logging.h"
#include "support.h"
#include "video_settings.h"

typedef void (*Action)(void *);

static unsigned char *image;
static unsigned flow_ready;
static int selecting, requested_event, selected_event, selected_garage_id;

typedef void (*SelectCar)(void *, void *, int);

static int enabled(void) {
    return flow_ready == 3 && video_setting_get(CUSTOM_SHOWCASES);
}

static int current_showcase_event(void) {
    void *career;
    if (!memory_read_pointer(image + 0x34b6640, &career) || !career) {
        return 0;
    }
    unsigned char *info = (unsigned char *)career + 0x20;
    // Match the native IsCareerEvent and SwitchOnCareerEventType fields.
    return *(int *)(info + 0x38) > 0 && *(int *)(info + 0x48) == 2 && *(int *)(info + 0x40) > 0
               ? *(int *)(info + 0x40)
               : 0;
}

int custom_showcases_selecting(void) {
    return selecting && enabled() && requested_event == current_showcase_event();
}

static void select_car(void *choices, void *garage, int garage_id) {
    ((SelectCar)(*(void ***)choices)[0x1a8 / 8])(choices, garage, garage_id);
    if (garage_id > 0 && custom_showcases_selecting()) {
        selected_event = requested_event;
        selected_garage_id = garage_id;
        selecting = 0;
        log_append_value("SHOWCASES: selected owned garage car", garage_id);
    }
}

static int restore_selection(void) {
    if (!enabled() || selected_garage_id <= 0 || !selected_event ||
        selected_event != current_showcase_event()) {
        return 0;
    }
    void *garage = ((void *(*)(void))(image + 0xced510))();
    void *choices = ((void *(*)(void))(image + 0xced1e0))();
    if (!garage || !choices) {
        log_append("SHOWCASES: selected-car restore unavailable; native car retained.\r\n");
        return 0;
    }
    // Rebuild from the owned car's saved parts, after PrepareRaceOptions has
    // assigned the showcase preset and immediately before the final car load.
    ((SelectCar)(*(void ***)choices)[0x1a8 / 8])(choices, garage, selected_garage_id);
    *((unsigned char *)choices + 0x26c) = 0;
    *(float *)((unsigned char *)choices + 0x270) = 0;
    log_append_value("SHOWCASES: restored selected garage car before showcase load",
                     selected_garage_id);
    return 1;
}

/* Private arguments select the settings gate and late car-restore action.
   All ordinary IsShowcaseEvent instances keep their native implementation. */
static void check_enabled(unsigned char *behavior) {
    int argument = *(int *)(behavior + 0xc0);
    if ((argument != -141 && argument != -143) || *(void **)(behavior + 0x90)) {
        ((Action)(image + 0x8b1fd0))(behavior);
        return;
    }
    ((Action)(image + 0x8d4300))(behavior);
    int result;
    if (argument == -143) {
        result = restore_selection();
    } else {
        requested_event = current_showcase_event();
        selected_event = selected_garage_id = 0;
        result = selecting = enabled() && requested_event > 0;
        if (result) {
            log_append("SHOWCASES: entering unrestricted race garage picker.\r\n");
        }
    }
    ((void (*)(void *, void *))(image + 0x1925260))(behavior, behavior + (result ? 0x28 : 0x58));
}

static const char gate[] =
    "<state id=\"_fh2_custom_showcases_enabled\">"
    "<entry target=\".\"/><exit id=\"yes\"/><exit id=\"no\"/>"
    "<behavior id=\"IsShowcaseEvent\" event=\"-141\">"
    "<transition id=\"yes\" target=\".yes\"/><transition id=\"no\" target=\".no\"/>"
    "</behavior></state>\n";

static const char selection[] =
    "<node id=\"_fh2_showcase_gate\" state=\"_fh2_custom_showcases_enabled\">"
    "<exit id=\"no\" target=\".enter_showcase\"/><exit id=\"yes\" "
    "target=\"_fh2_showcase_prepare\"/></node>"
    "<node id=\"_fh2_showcase_prepare\" state=\"prepare_for_car_screen\">"
    "<exit target=\"_fh2_showcase_car\"/></node>"
    "<node id=\"_fh2_showcase_car\" state=\"ui_screens.race_sign_up_change_car\">"
    "<exit id=\"change_car\" target=\"_fh2_showcase_loading\"/>"
    "<exit id=\"selected_current_car\" target=\"_fh2_showcase_loading\"/>"
    "<exit id=\"buy_car\" target=\"_fh2_showcase_car\"/>"
    "<exit id=\"back\" target=\".exit_flow\"/></node>"
    // Keep the loading audio/cutscene handoff, but let showcase preparation load
    // the selected car once instead of loading it here and immediately reloading it.
    "<node id=\"_fh2_showcase_loading\" state=\"global.enable_mixer_snapshot\" "
    "snapshot=\"BASE_Loading\"/>"
    "<node id=\"_fh2_showcase_stop_cutscene\" state=\"global.stop_cutscene\" "
    "cutscene_component_id=\"FreeroamActivate\">"
    "<exit target=\".enter_showcase\"/></node>\n";

static const char load_flow[] =
    "<state id=\"_fh2_showcase_player_car\">"
    "<node state=\"_fh2_showcase_restore_choice\"/>"
    "<node state=\"route_switch.post_route_switch_prepare_for_race\"/></state>"
    "<state id=\"_fh2_showcase_restore_choice\">"
    "<entry target=\".\"/>"
    "<behavior id=\"IsShowcaseEvent\" event=\"-143\">"
    "<transition id=\"yes\"/><transition id=\"no\"/></behavior></state>\n";

static int transform_load_flow(unsigned char *resource) {
    const char *source = *(char **)(resource + 0xe0);
    uint32_t length = *(uint32_t *)(resource + 0xe8);
    if (!source || !length || length > 8 * 1024 * 1024 ||
        text_find(source, length, "_fh2_showcase_restore_choice")) {
        return 0;
    }
    const char *state = text_find_unique(source, length, "<state id=\"showcase_prerace_prepare\">");
    const char *end =
        state ? text_find(state, length - (size_t)(state - source), "</state>") : NULL;
    static const char old_node[] =
        "<node state=\"route_switch.post_route_switch_prepare_for_race\" />";
    static const char new_node[] = "<node state=\"_fh2_showcase_player_car\" />";
    const char *node = end ? text_find_unique(state, (size_t)(end - state), old_node) : NULL;
    const char *close = text_find(source, length, "</stategroup>");
    if (!node || !close || end >= close) {
        return 0;
    }
    size_t size = length - (sizeof(old_node) - 1) + sizeof(new_node) - 1 + sizeof(load_flow) - 1;
    char *replacement = ((void *(*)(size_t))(image + 0x3229c0))(size + 1);
    if (!replacement) {
        return 0;
    }
    const char *parts[] = {source, new_node, node + sizeof(old_node) - 1, load_flow, close};
    size_t sizes[] = {(size_t)(node - source), sizeof(new_node) - 1,
                      (size_t)(close - node) - sizeof(old_node) + 1, sizeof(load_flow) - 1,
                      length - (size_t)(close - source)};
    char *out = replacement;
    for (unsigned i = 0; i < ARRAY_COUNT(parts); ++i) {
        memory_copy(out, parts[i], sizes[i]);
        out += sizes[i];
    }
    *out = 0;
    ((Action)(image + 0x31c7b0))((void *)source);
    *(char **)(resource + 0xe0) = replacement;
    *(uint32_t *)(resource + 0xe8) = (uint32_t)size;
    return 1;
}

int custom_showcases_transform(void *game_resource, unsigned kind) {
    if (kind == 6) {
        return transform_load_flow(game_resource);
    }
    if (kind != 3) {
        return 0;
    }
    unsigned char *resource = game_resource;
    const char *source = *(char **)(resource + 0xe0);
    uint32_t length = *(uint32_t *)(resource + 0xe8);
    if (!source || !length || length > 8 * 1024 * 1024 ||
        text_find(source, length, "_fh2_custom_showcases_enabled")) {
        return 0;
    }
    const char *state = text_find(source, length, "<state id=\"offline_race_entry_flow\">");
    const char *end =
        state ? text_find(state, length - (size_t)(state - source), "</state>") : NULL;
    static const char old_exit[] = "<exit id=\"showcase\" target=\".enter_showcase\" />";
    static const char new_exit[] = "<exit id=\"showcase\" target=\"_fh2_showcase_gate\" />";
    const char *exit = end ? text_find(state, (size_t)(end - state), old_exit) : NULL;
    const char *close = text_find(source, length, "</stategroup>");
    if (!exit || !close || end >= close ||
        text_find(exit + sizeof(old_exit) - 1, (size_t)(end - exit) - sizeof(old_exit) + 1,
                  old_exit)) {
        return 0;
    }
    size_t added = sizeof(new_exit) - sizeof(old_exit) + sizeof(selection) - 1 + sizeof(gate) - 1;
    char *replacement = ((void *(*)(size_t))(image + 0x3229c0))(length + added + 1);
    if (!replacement) {
        return 0;
    }
    char *output = replacement;
    const char *parts[] = {source, new_exit, exit + sizeof(old_exit) - 1, selection, end,
                           gate,   close};
    size_t sizes[] = {(size_t)(exit - source),
                      sizeof(new_exit) - 1,
                      (size_t)(end - exit) - sizeof(old_exit) + 1,
                      sizeof(selection) - 1,
                      (size_t)(close - end),
                      sizeof(gate) - 1,
                      length - (size_t)(close - source)};
    for (unsigned i = 0; i < ARRAY_COUNT(parts); ++i) {
        memory_copy(output, parts[i], sizes[i]);
        output += sizes[i];
    }
    *output = 0;
    ((Action)(image + 0x31c7b0))((void *)source);
    *(char **)(resource + 0xe0) = replacement;
    *(uint32_t *)(resource + 0xe8) = (uint32_t)(output - replacement);
    return 1;
}

void custom_showcases_resource_parsed(unsigned kind, int success) {
    unsigned bit = kind == 3 ? 1 : kind == 6 ? 2 : 0;
    if (success) {
        flow_ready |= bit;
    } else {
        flow_ready &= ~bit;
    }
    if (bit) {
        log_append(success ? "SHOWCASES: race selection/load flow ready.\r\n"
                           : "SHOWCASES: unsupported flow; native car retained.\r\n");
    }
}

PatchDefinition custom_showcases_hook_patches[3];
static unsigned char originals[3][12], targets[3][12];
static const unsigned char *accepted[3][2];

void custom_showcases_prepare_hooks(unsigned char *base) {
    if (image != base) {
        flow_ready = 0;
        selecting = requested_event = selected_event = selected_garage_id = 0;
    }
    image = base;
    static const unsigned sites[] = {0x2d091d0, 0xccf5f4, 0xccf575};
    static const char *const names[] = {"custom_showcases_condition",
                                        "custom_showcases_selection_thunk",
                                        "custom_showcases_selection"};
    for (unsigned i = 0; i < ARRAY_COUNT(sites); ++i) {
        unsigned size;
        if (!i) {
            uintptr_t native = (uintptr_t)(base + 0x8b1fd0), callback = (uintptr_t)check_enabled;
            size = 8;
            memory_copy(originals[i], &native, size);
            memory_copy(targets[i], &callback, size);
        } else if (i == 1) {
            size = 12;
            for (unsigned j = 0; j < size; ++j) {
                originals[i][j] = 0xcc;
            }
            static const unsigned char code[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
            uintptr_t callback = (uintptr_t)select_car;
            memory_copy(targets[i], code, size);
            memory_copy(targets[i] + 2, &callback, 8);
        } else {
            size = 6;
            static const unsigned char native[] = {0xff, 0x97, 0xa8, 0x01, 0, 0};
            memory_copy(originals[i], native, size);
            int32_t target = (int32_t)(sites[1] - sites[i] - 5);
            targets[i][0] = 0xe8;
            memory_copy(targets[i] + 1, &target, 4);
            targets[i][5] = 0x90;
        }
        accepted[i][0] = originals[i];
        accepted[i][1] = targets[i];
        custom_showcases_hook_patches[i] =
            (PatchDefinition){sites[i],     0,          size,       GROUP_FRONTEND_XML, names[i],
                              originals[i], targets[i], targets[i], accepted[i],        2};
    }
}
