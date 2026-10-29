#include "video_options_xaml.h"
#include "support.h"
#include "video_options_text.h"
#include "video_settings.h"

typedef struct {
    const char *property;
    const char *resource;
} VideoRow;

static const VideoRow drift_rows[] = {
    {"DriftMode", "EO_DriftMode"},
    {"DriftChaseSensitivity", "EO_ChaseSensitivity"},
    {"DriftChaseSpeed", "EO_ChaseSpeed"},
    {"DriftChaseRange", "EO_ChaseRange"},
    {"DriftDriverSensitivity", "EO_DriverSensitivity"},
    {"DriftDriverSpeed", "EO_DriverSpeed"},
    {"DriftDriverRange", "EO_DriverRange"},
};

static const VideoRow fov_rows[] = {
    {"FovChase", "EO_FovChase"}, {"FovFarChase", "EO_FovFarChase"}, {"FovDriver", "EO_FovDriver"},
    {"FovHood", "EO_FovHood"},   {"FovBumper", "EO_FovBumper"},
};

static const char brightness_preview_tag[] =
    "<StackPanel Margin=\"0, 0, 0, 6\" Orientation=\"Horizontal\" "
    "Style=\"{StaticResource "
    "A_Style_FrameworkElement_DefaultTransition_0}\" Background=\"Black\">";

unsigned video_options_page(const char *text, size_t length) {
    return text_find(text, length, "x:Class=\"ForzaUI.Scenes.Anthem.A_AssistOptions\"")
               ? DIFFICULTY_PAGE
           : text_find(text, length, "x:Class=\"ForzaUI.A_AudioOptions\"") ? AUDIO_PAGE
           : text_find(text, length, "x:Class=\"ForzaUI.A_HudOptions\"")   ? HUD_PAGE
                                                                           : VIDEO_PAGE;
}

int video_options_is_xaml(const char *text, size_t length) {
    // Plugins may reuse a native options class with their own isolated page.
    if(text_find(text,length,"<!-- EO:StandaloneOptionsPage -->"))return 0;
    return text_find(text, length, "x:Class=\"ForzaUI.Scenes.Anthem.A_AssistOptions\"") != NULL ||
           text_find(text, length, "x:Class=\"ForzaUI.A_VideoOptions\"") != NULL ||
           text_find(text, length, "x:Class=\"ForzaUI.A_HudOptions\"") != NULL ||
           text_find(text, length, "x:Class=\"ForzaUI.A_AudioOptions\"") != NULL;
}

typedef struct {
    char *bytes;
    size_t length, capacity;
    int valid;
} XamlBuffer;

static void append_bytes(XamlBuffer *buffer, const char *text, size_t length) {
    if (!buffer->valid || length >= buffer->capacity - buffer->length) {
        buffer->valid = 0;
        return;
    }
    memory_copy(buffer->bytes + buffer->length, text, length);
    buffer->length += length;
}

static void append_text(XamlBuffer *buffer, const char *text) {
    append_bytes(buffer, text, text_length(text));
}

static void append_attribute(XamlBuffer *buffer, const char *text) {
    for (; *text; ++text) {
        const char *escaped = *text == '&'   ? "&amp;"
                              : *text == '<' ? "&lt;"
                              : *text == '>' ? "&gt;"
                              : *text == '"' ? "&quot;"
                                             : NULL;
        if (escaped) {
            append_text(buffer, escaped);
        } else {
            append_bytes(buffer, text, 1);
        }
    }
}

static void append_heading(XamlBuffer *buffer, const VideoOptionsText *text, unsigned index) {
    append_attribute(buffer, text->camera);
    if (index) {
        if (index > 3) {
            append_text(buffer, " (");
            append_attribute(buffer, text->driver);
            append_text(buffer, ")");
        }
        append_text(buffer, " - ");
        append_attribute(buffer, text->parameters[(index - 1) % 3]);
    }
}

static void append_resources(XamlBuffer *buffer, const VideoOptionsText *text) {
    for (unsigned index = 0; index < ARRAY_COUNT(drift_rows); ++index) {
        append_text(buffer, "<local:TileMenuDataItem x:Key=\"");
        append_text(buffer, drift_rows[index].resource);
        append_text(buffer, "\" Heading=\"");
        append_heading(buffer, text, index);
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, text->descriptions[index ? 1 + (index - 1) % 3 : 0]);
        append_text(buffer, "\"/>");
    }
}

static void append_drift_rows(XamlBuffer *buffer, const VideoOptionsText *text) {
    append_text(buffer, "<local:SpinnerControl Tag=\"{StaticResource EO_DriftMode}\" "
                        "Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\"");
    append_attribute(buffer, text->camera);
    append_text(buffer, "\" SelectedIndex=\"{Binding DriftMode}\" "
                        "ItemsSource=\"{Binding DriftModes}\"/>");
    for (unsigned index = 1; index < ARRAY_COUNT(drift_rows); ++index) {
        append_text(buffer, "<Slider Value=\"{Binding ");
        append_text(buffer, drift_rows[index].property);
        append_text(buffer, "}\" Tag=\"{StaticResource ");
        append_text(buffer, drift_rows[index].resource);
        append_text(buffer, "}\" Style=\"{StaticResource A_Style_Slider_Options}\"/>");
    }
}

static const char fov_style[] =
    "<local:NumUnitConverter x:Key=\"EO_FovDegrees\" FractionDigits=\"1\" "
    "ShowUnit=\"False\"/>"
    "<Style x:Key=\"EO_FovSlider\" TargetType=\"Slider\" "
    "BasedOn=\"{StaticResource "
    "A_Style_Slider_Options}\">"
    "<Setter Property=\"Minimum\" Value=\"20\"/><Setter Property=\"Maximum\" "
    "Value=\"120\"/>"
    "<Setter Property=\"SmallChange\" Value=\"0.5\"/>"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate "
    "TargetType=\"Slider\">"
    "<Grid "
    "FocusManager.IsFocusScope=\"True\"><Grid.ColumnDefinitions><"
    "ColumnDefinition/"
    "><ColumnDefinition/></Grid.ColumnDefinitions>"
    "<local:FZTextBlock x:Name=\"PART_Label\" Text=\"{Binding Tag.Heading, "
    "RelativeSource={RelativeSource TemplatedParent}}\" "
    "Style=\"{StaticResource "
    "A_Style_SliderControlText}\" HorizontalAlignment=\"Left\"/>"
    "<DockPanel Grid.Column=\"1\">"
    "<local:FZTextBlock DockPanel.Dock=\"Right\" Width=\"64\" "
    "TextAlignment=\"Right\" "
    "HorizontalAlignment=\"Right\" Text=\"{Binding Value, "
    "RelativeSource={RelativeSource "
    "TemplatedParent}, Converter={StaticResource EO_FovDegrees}}\" "
    "Style=\"{StaticResource "
    "A_Style_SliderControlText}\"/>"
    "<Grid Margin=\"32, 0, 8, 0\" Height=\"22\"><local:FZMaterial "
    "x:Name=\"PART_SliderMaterial\" "
    "Color=\"{StaticResource A_Color_MediumGreyText}\"/>"
    "<Track x:Name=\"PART_Track\"><Track.DecreaseRepeatButton><RepeatButton "
    "x:Name=\"Left\" "
    "Style=\"{StaticResource "
    "A_Style_RepeatButton_Left_Unfocused}\"/></Track.DecreaseRepeatButton></"
    "Track>"
    "</Grid>"
    "</DockPanel>"
    "</Grid>"
    "<ControlTemplate.Triggers>"
    "<DataTrigger Binding=\"{Binding RelativeSource={RelativeSource "
    "Mode=FindAncestor, "
    "AncestorType=ListBoxItem}, Path=IsKeyboardFocusWithin}\" Value=\"True\">"
    "<Setter TargetName=\"PART_SliderMaterial\" Property=\"Color\" "
    "Value=\"{StaticResource "
    "A_Color_Header_Background}\"/>"
    "<Setter TargetName=\"Left\" Property=\"Style\" Value=\"{StaticResource "
    "A_Style_RepeatButton_Left_Focused}\"/>"
    "</DataTrigger>"
    "</ControlTemplate.Triggers>"
    "</ControlTemplate></Setter.Value></Setter>"
    "</Style>";

static void append_fov_resources(XamlBuffer *buffer, const VideoOptionsText *text) {
    append_text(buffer, fov_style);
    for (unsigned index = 0; index < ARRAY_COUNT(fov_rows); ++index) {
        append_text(buffer, "<local:TileMenuDataItem x:Key=\"");
        append_text(buffer, fov_rows[index].resource);
        append_text(buffer, "\" Heading=\"");
        append_attribute(buffer, text->fov_label);
        append_text(buffer, " - ");
        append_attribute(buffer, text->fov_views[index]);
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, text->fov_description);
        append_text(buffer, "\"/>");
    }
}

static const char *const settings_properties[] = {"RealtimeShadows",
                                                  NULL,
                                                  "PhotoSamplesPreset",
                                                  NULL,
                                                  "MinimapOrientation",
                                                  "AutodriveCinematic",
                                                  "SkipChampionshipDialogue",
                                                  "SpawnLocation",
                                                  "CarChangePosition",
                                                  "LiveryResolution",
                                                  "LiveryMsaa",
                                                  "LiverySampling",
                                                  "ShowAllEvents",
                                                  "AutodriveEvents",
                                                  "CustomShowcases",
                                                  "BenPhotoDialogue",
                                                  "AshleyGarageDialogue",
                                                  "WheelspinNotification",
                                                  "CheckpointSound"};
static const char *const settings_collections[] = {"ShadowChoices",      NULL,
                                                   "PhotoChoices",       NULL,
                                                   "MinimapChoices",     "ToggleChoices",
                                                   "ToggleChoices",      "SpawnChoices",
                                                   "CarPositionChoices", "LiveryResolutionChoices",
                                                   "LiveryMsaaChoices",  "LiverySamplingChoices",
                                                   "ToggleChoices",      "ToggleChoices",
                                                   "ToggleChoices",      "ToggleChoices",
                                                   "ToggleChoices",      "ToggleChoices",
                                                   "ToggleChoices"};

static void append_settings_resources(XamlBuffer *buffer, const VideoOptionsText *text,
                                      unsigned first, unsigned end) {
    for (unsigned index = first; index < end; ++index) {
        if (!settings_properties[index]) {
            continue;
        }
        append_text(buffer, "<local:TileMenuDataItem x:Key=\"EO_");
        append_text(buffer, settings_properties[index]);
        append_text(buffer, "\" Heading=\"");
        append_attribute(buffer, text->settings_names[index]);
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, text->settings_descriptions[index]);
        append_text(buffer, "\"/>");
    }
}

static void append_settings_rows(XamlBuffer *buffer, const VideoOptionsText *text, unsigned first,
                                 unsigned end) {
    for (unsigned index = first; index < end; ++index) {
        if (!settings_properties[index]) {
            continue;
        }
        append_text(buffer, "<local:SpinnerControl Tag=\"{StaticResource EO_");
        append_text(buffer, settings_properties[index]);
        append_text(buffer, "}\" Style=\"{StaticResource "
                            "A_Style_SpinnerControl_Default}\" Label=\"");
        append_attribute(buffer, text->settings_names[index]);
        append_text(buffer, "\" SelectedIndex=\"{Binding ");
        append_text(buffer, settings_properties[index]);
        append_text(buffer, "}\" ItemsSource=\"{Binding ");
        append_text(buffer, settings_collections[index]);
        append_text(buffer, "}\"/>");
    }
}

static void append_skill_notification(XamlBuffer *buffer, const VideoOptionsText *text, int row) {
    const char *label = text->skill_point_notification ? text->skill_point_notification
        : "Skill Point Earned Notification";
    const char *description = text->skill_point_description ? text->skill_point_description
        : "Show the Skill Point Earned notification and play its accompanying dialogue. Skill points are still awarded when disabled.";
    append_text(buffer, row ? "<local:SpinnerControl Tag=\"{StaticResource EO_SkillPointNotification}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\""
                            : "<local:TileMenuDataItem x:Key=\"EO_SkillPointNotification\" Heading=\"");
    append_attribute(buffer, label);
    append_text(buffer, row ? "\" SelectedIndex=\"{Binding SkillPointNotification}\" ItemsSource=\"{Binding ToggleChoices}\"/>"
                            : "\" Description=\"");
    if (!row) {
        append_attribute(buffer, description);
        append_text(buffer, "\"/>");
    }
}

static void append_skill_counter_shake(XamlBuffer *buffer, const VideoOptionsText *text, int row) {
    const char *label = text->skill_counter_shake ? text->skill_counter_shake : "Skill Counter Shake";
    const char *description = text->skill_counter_shake_description ? text->skill_counter_shake_description
        : "Enable the shaking effect on the skill-chain counter.";
    append_text(buffer, row ? "<local:SpinnerControl Tag=\"{StaticResource EO_SkillCounterShake}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\""
                            : "<local:TileMenuDataItem x:Key=\"EO_SkillCounterShake\" Heading=\"");
    append_attribute(buffer, label);
    append_text(buffer, row ? "\" SelectedIndex=\"{Binding SkillCounterShake}\" ItemsSource=\"{Binding ToggleChoices}\"/>"
                            : "\" Description=\"");
    if (!row) {append_attribute(buffer, description);append_text(buffer,"\"/>");}
}

static void append_vignette_effect(XamlBuffer *buffer, const VideoOptionsText *text, int row) {
    const char *label = text->vignette_effect ? text->vignette_effect : "Vignette Effect";
    const char *description = text->vignette_effect_description ? text->vignette_effect_description
        : "Enable the main vignette effect that darkens the edges of the screen.";
    append_text(buffer, row ? "<local:SpinnerControl Tag=\"{StaticResource EO_VignetteEffect}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\""
                            : "<local:TileMenuDataItem x:Key=\"EO_VignetteEffect\" Heading=\"");
    append_attribute(buffer, label);
    append_text(buffer, row ? "\" SelectedIndex=\"{Binding VignetteEffect}\" ItemsSource=\"{Binding ToggleChoices}\"/>"
                            : "\" Description=\"");
    if (!row) {append_attribute(buffer, description);append_text(buffer,"\"/>");}
}

static void append_speed_zone_vignette(XamlBuffer *buffer, const VideoOptionsText *text, int row) {
    const char *label = text->speed_zone_vignette ? text->speed_zone_vignette : "Speed Zone Vignette";
    const char *description = text->speed_zone_vignette_description ? text->speed_zone_vignette_description
        : "Enable the extra vignette shown during speed zones.";
    append_text(buffer, row ? "<local:SpinnerControl Tag=\"{StaticResource EO_SpeedZoneVignette}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\""
                            : "<local:TileMenuDataItem x:Key=\"EO_SpeedZoneVignette\" Heading=\"");
    append_attribute(buffer, label);
    append_text(buffer, row ? "\" SelectedIndex=\"{Binding SpeedZoneVignette}\" ItemsSource=\"{Binding ToggleChoices}\"/>"
                            : "\" Description=\"");
    if (!row) {append_attribute(buffer, description);append_text(buffer,"\"/>");}
}

static void append_player_character(XamlBuffer *buffer, const VideoOptionsText *text, int row) {
    append_text(buffer, row ? "<local:SpinnerControl Tag=\"{StaticResource EO_PlayerCharacter}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\""
                            : "<local:TileMenuDataItem x:Key=\"EO_PlayerCharacter\" Heading=\"");
    append_attribute(buffer, text->player_character);
    if (row) {
        append_text(buffer, "\" SelectedIndex=\"{Binding PlayerCharacter}\" ItemsSource=\"{Binding PlayerCharacterChoices}\"/>");
    } else {
        append_text(buffer, "\" Description=\"");
        append_attribute(buffer, text->player_character_description);
        append_text(buffer, "\"/>");
    }
}

static void append_unit_selectors(XamlBuffer *buffer, const VideoOptionsText *text, int rows) {
    static const char *const keys[] = {"EO_PowerUnits", "EO_SpringUnits"};
    static const char *const properties[] = {"PowerUnits", "SpringUnits"};
    static const char *const choices[] = {"PowerUnitChoices", "SpringUnitChoices"};
    static const char *const labels[] = {"Power Units", "Spring Units"};
    static const char *const descriptions[] = {
        "Choose the units used to display engine power. Default follows the Units setting.",
        "Choose the units used to display spring stiffness. Default follows the Units setting."};
    for (unsigned i = 0; i < 2; ++i) {
        append_text(buffer, rows ? "<local:SpinnerControl Tag=\"{StaticResource "
                                : "<local:TileMenuDataItem x:Key=\"");
        append_text(buffer, keys[i]);
        append_text(buffer, rows ? "}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" Label=\""
                                : "\" Heading=\"");
        append_attribute(buffer, text->unit_names[i] ? text->unit_names[i] : labels[i]);
        if (rows) {
            append_text(buffer, "\" SelectedIndex=\"{Binding ");
            append_text(buffer, properties[i]);
            append_text(buffer, "}\" ItemsSource=\"{Binding ");
            append_text(buffer, choices[i]);
            append_text(buffer, "}\"/>");
        } else {
            append_text(buffer, "\" Description=\"");
            append_attribute(buffer, text->unit_descriptions[i] ? text->unit_descriptions[i] : descriptions[i]);
            append_text(buffer, "\"/>");
        }
    }
}

static void append_fov_rows(XamlBuffer *buffer) {
    for (unsigned index = 0; index < ARRAY_COUNT(fov_rows); ++index) {
        append_text(buffer, "<Slider Value=\"{Binding ");
        append_text(buffer, fov_rows[index].property);
        append_text(buffer, "}\" Tag=\"{StaticResource ");
        append_text(buffer, fov_rows[index].resource);
        append_text(buffer, "}\" Style=\"{StaticResource EO_FovSlider}\"/>");
    }
}

static size_t extend_list_xaml(unsigned page, unsigned char *image, const char *text, size_t length,
                               char *output, size_t capacity) {
    const char *resources = text_find(text, length, "</ResourceDictionary>");
    const char *list = text_find(text, length, "<local:CommonListBox x:Name=\"OptionsList\"");
    const char *end =
        list ? text_find(list, length - (size_t)(list - text), "</local:CommonListBox>") : NULL;
    static const char native_width[] = "Width=\"688\"";
    const char *opening_end = list && end ? text_find(list, (size_t)(end - list), ">") : NULL;
    const char *width =
        opening_end ? text_find(list, (size_t)(opening_end - list), native_width) : NULL;
    if (!capacity || !resources || !end || !width || resources >= width || width >= end ||
        text_find(text, length,
                  page == HUD_PAGE ? "EO_MinimapOrientation" : "EO_BenPhotoDialogue")) {
        return 0;
    }
    static const char radio_row_tag[] = "<Slider Value=\"{Binding RadioVolume}\"";
    const char *radio_row =
        page == AUDIO_PAGE ? text_find_unique(list, (size_t)(end - list), radio_row_tag) : NULL;
    if (page == AUDIO_PAGE && !radio_row) {
        return 0;
    }
    static const char units_tag[] = "Tag=\"{StaticResource A_DataItem_Units}\"";
    const char *units = page == HUD_PAGE ? text_find_unique(list, (size_t)(end-list), units_tag) : NULL;
    const char *units_end = units ? text_find(units, (size_t)(end-units), "/>") : NULL;
    if (page == HUD_PAGE && !units_end) return 0;
    if (units_end) units_end += 2;
    const VideoOptionsText *localized = video_options_text(image);
    XamlBuffer buffer = {output, 0, capacity, 1};
    if (length < 3 || !memory_equal(text, "\xef\xbb\xbf", 3)) {
        append_bytes(&buffer, "\xef\xbb\xbf", 3);
    }
    for (size_t index = 0; index < length && buffer.valid;) {
        if (text + index == resources) {
            if (page == HUD_PAGE) {
                append_resources(&buffer, localized);
                append_fov_resources(&buffer, localized);
                append_settings_resources(&buffer, localized, 4, 9);
                append_settings_resources(&buffer, localized, 12, 15);
                append_settings_resources(&buffer, localized, 17, 18);
                append_skill_notification(&buffer, localized, 0);
                append_skill_counter_shake(&buffer, localized, 0);
                append_vignette_effect(&buffer, localized, 0);
                append_speed_zone_vignette(&buffer, localized, 0);
                append_player_character(&buffer, localized, 0);
                append_unit_selectors(&buffer, localized, 0);
            } else {
                append_settings_resources(&buffer, localized, 15, 17);
                append_settings_resources(&buffer, localized, 18, 19);
                append_text(&buffer,
                            "<local:TileMenuDataItem x:Key=\"EO_MasterVolume\" Heading=\"");
                append_attribute(&buffer, localized->master_volume);
                append_text(
                    &buffer,
                    "\" Description=\"{local:Loc "
                    "MyProfile.IDS_MasterGameVolumeDescription}\"/>"
                    "<Style x:Key=\"EO_AudioSlider\" TargetType=\"Slider\" "
                    "BasedOn=\"{StaticResource A_Style_Slider_Options_Audio_Min1}\"><Setter "
                    "Property=\"Minimum\" Value=\"0\"/></Style>");
            }
        }
        if (page == AUDIO_PAGE && text + index == radio_row) {
            append_text(&buffer,
                        "<Slider x:Name=\"EO_MasterVolumeSlider\" Value=\"{Binding MasterVolume}\" "
                        "Tag=\"{StaticResource EO_MasterVolume}\" Style=\"{StaticResource "
                        "EO_AudioSlider}\"/>");
        }
        if (page == HUD_PAGE && text + index == units_end) {
            append_unit_selectors(&buffer, localized, 1);
        }
        if (text + index == end) {
            if (page == HUD_PAGE) {
                append_drift_rows(&buffer, localized);
                append_fov_rows(&buffer);
                append_settings_rows(&buffer, localized, 4, 5);
                append_settings_rows(&buffer, localized, 13, 14);
                append_settings_rows(&buffer, localized, 5, 9);
                append_settings_rows(&buffer, localized, 12, 13);
                append_settings_rows(&buffer, localized, 14, 15);
                append_settings_rows(&buffer, localized, 17, 18);
                append_skill_notification(&buffer, localized, 1);
                append_skill_counter_shake(&buffer, localized, 1);
                append_vignette_effect(&buffer, localized, 1);
                append_speed_zone_vignette(&buffer, localized, 1);
                append_player_character(&buffer, localized, 1);
            } else {
                append_settings_rows(&buffer, localized, 15, 17);
                append_settings_rows(&buffer, localized, 18, 19);
            }
        }
        static const char audio_style[] = "A_Style_Slider_Options_Audio_Min1";
        if (page == AUDIO_PAGE && length - index >= sizeof(audio_style) - 1 &&
            memory_equal(text + index, audio_style, sizeof(audio_style) - 1)) {
            append_text(&buffer, "EO_AudioSlider");
            index += sizeof(audio_style) - 1;
        } else if (text + index == width) {
            append_text(&buffer, page == HUD_PAGE ? "Width=\"1000\"" : "Width=\"688\"");
            index += sizeof(native_width) - 1;
        } else {
            append_bytes(&buffer, text + index, 1);
            ++index;
        }
    }
    if (!buffer.valid) {
        return 0;
    }
    buffer.bytes[buffer.length] = 0;
    return buffer.length;
}

static size_t extend_difficulty(unsigned char *image, const char *text, size_t length, char *output, size_t capacity) {
    const char *row =
        text_find_unique(text, length, "<local:SpinnerControl x:Name=\"DrivatarDifficulty\"");
    const char *end = row ? text_find(row, length - (size_t)(row - text), "/>") : NULL;
    if (!end || text_find(text, length, "EO_OpenWorldDrivatars")) return 0;
    end += 2;
    const VideoOptionsText *localized = video_options_text(image);
    XamlBuffer buffer = {output, 0, capacity, 1};
    if (length < 3 || !memory_equal(text, "\xef\xbb\xbf", 3))
        append_bytes(&buffer, "\xef\xbb\xbf", 3);
    size_t before = (size_t)(end - text);
    append_bytes(&buffer, text, before);
    append_text(&buffer, "<local:SpinnerControl x:Name=\"EO_OpenWorldDrivatars\" Label=\"");
    append_attribute(&buffer, localized->open_world_drivatars);
    append_text(&buffer, "\" ItemsSource=\"{Binding ToggleChoices}\" Style=\"{StaticResource A_Style_SpinnerControl_Default}\" SelectedIndex=\"{Binding OpenWorldDrivatars}\" Tag=\"");
    append_attribute(&buffer, localized->open_world_drivatars_description);
    append_text(&buffer, "\"/>");
    append_bytes(&buffer, end, length - before);
    if (!buffer.valid) return 0;
    output[buffer.length] = 0;
    return buffer.length;
}

size_t video_options_extend_xaml(unsigned char *game_image, const char *text, size_t length,
                                 char *output, size_t capacity) {
    if(text_find(text,length,"<!-- EO:StandaloneOptionsPage -->"))return 0;
    unsigned page = video_options_page(text, length);
    if (page == DIFFICULTY_PAGE) {
        return extend_difficulty(game_image, text, length, output, capacity);
    }
    if (page == HUD_PAGE || page == AUDIO_PAGE) {
        return extend_list_xaml(page, game_image, text, length, output, capacity);
    }
    const char *preview = text_find(text, length, brightness_preview_tag);
    const char *resources = text_find(text, length, "</ResourceDictionary>");
    const char *list = text_find(text, length, "<local:CommonListBox x:Name=\"OptionsList\"");
    const char *rows =
        list ? text_find(list, length - (size_t)(list - text), "</local:CommonListBox>") : NULL;
    const char *height =
        list && rows ? text_find(list, (size_t)(rows - list), "Height=\"110\"") : NULL;
    static const char description_height_tag[] =
        "Height=\"100\" Width=\"806\" Template=\"{StaticResource "
        "A_ControlTemplate_DescriptionPanelContent}\"";
    const char *description = text_find(text, length, description_height_tag);
    const char *subtitles =
        list && rows ? text_find(list, (size_t)(rows - list), "<local:SpinnerControl") : NULL;
    if (!capacity || !subtitles || !text_find(text, length, "x:Class=\"ForzaUI.A_VideoOptions\"") ||
        !preview || !resources || !height || !rows || resources >= height || height >= rows ||
        text_find(text, length, "EO_RealtimeShadows")) {
        return 0;
    }
    const VideoOptionsText *localized = video_options_text(game_image);
    XamlBuffer buffer = {output, 0, capacity, 1};
    // FH2's XML reader only enables UTF-8 decoding after seeing its byte-order
    // mark.
    static const char utf8_bom[] = "\xef\xbb\xbf";
    if (length < 3 || !memory_equal(text, utf8_bom, 3)) {
        append_bytes(&buffer, utf8_bom, 3);
    }
    for (size_t index = 0; index < length && buffer.valid;) {
        if (text + index == resources) {
            append_settings_resources(&buffer, localized, 0, 4);
            append_settings_resources(&buffer, localized, 9, 12);
        }
        if (text + index == rows) {
            append_settings_rows(&buffer, localized, 0, 4);
            append_settings_rows(&buffer, localized, 9, 12);
        }
        if (text + index == preview) {
            append_text(&buffer, "<StackPanel Width=\"806\" HorizontalAlignment=\"Center\" "
                                 "Margin=\"0, 0, "
                                 "0, 6\" Orientation=\"Horizontal\" Style=\"{StaticResource "
                                 "A_Style_FrameworkElement_DefaultTransition_0}\" "
                                 "Background=\"Black\">");
            index += sizeof(brightness_preview_tag) - 1;
        } else if (text + index == description) {
            append_text(&buffer, "Height=\"100\"");
            index += sizeof("Height=\"100\"") - 1;
        } else if (text + index == height) {
            append_text(&buffer, "Height=\"275\"");
            index += 12;
        } else {
            append_bytes(&buffer, text + index, 1);
            ++index;
        }
    }
    if (!buffer.valid) {
        return 0;
    }
    output[buffer.length] = 0;
    return buffer.length;
}
