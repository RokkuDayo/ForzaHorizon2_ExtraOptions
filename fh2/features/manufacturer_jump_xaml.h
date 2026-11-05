// The native PopupMenu owns focus/input/lifetime. Only our marked instance uses
// the native car-category WrapPanel, with physical scrolling and fixed cells.
static const char make_styles[] =
    "<Style x:Key=\"EO_MakeThumb\" TargetType=\"Thumb\">"
    "<Setter Property=\"Focusable\" Value=\"False\"/>"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"Thumb\">"
    "<Border CornerRadius=\"3\" Background=\"#FFCDCDCD\"/>"
    "</ControlTemplate></Setter.Value></Setter></Style>"
    "<Style x:Key=\"EO_MakeScrollBar\" TargetType=\"ScrollBar\">"
    "<Setter Property=\"Focusable\" Value=\"False\"/>"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"ScrollBar\">"
    "<Grid Width=\"1416\" Height=\"8\" SnapsToDevicePixels=\"True\"><Border CornerRadius=\"3\" "
    "Background=\"#665A646A\"/>"
    "<Track x:Name=\"PART_Track\" Orientation=\"Horizontal\" IsDirectionReversed=\"False\" "
    "Minimum=\"{TemplateBinding Minimum}\" Maximum=\"{TemplateBinding Maximum}\" "
    "Value=\"{TemplateBinding Value}\" ViewportSize=\"{TemplateBinding ViewportSize}\" "
    "SnapsToDevicePixels=\"True\">"
    "<Track.Thumb><Thumb MinWidth=\"36\" Height=\"8\" Style=\"{StaticResource "
    "EO_MakeThumb}\"/></Track.Thumb>"
    "</Track></Grid></ControlTemplate></Setter.Value></Setter></Style>"
    "<Style x:Key=\"EO_MakeScrollViewer\" TargetType=\"ScrollViewer\">"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"ScrollViewer\">"
    "<Grid Width=\"1440\" Height=\"720\" UseLayoutRounding=\"True\" "
    "SnapsToDevicePixels=\"True\"><Grid.RowDefinitions><RowDefinition "
    "Height=\"688\"/><RowDefinition Height=\"32\"/>"
    "</Grid.RowDefinitions><ScrollContentPresenter x:Name=\"PART_ScrollContentPresenter\" "
    "Content=\"{TemplateBinding Content}\" CanContentScroll=\"{TemplateBinding CanContentScroll}\" "
    "ClipToBounds=\"True\"/>"
    "<ScrollBar x:Name=\"PART_HorizontalScrollBar\" Grid.Row=\"1\" Width=\"1416\" Height=\"8\" "
    "Margin=\"12,16,12,8\" Orientation=\"Horizontal\" Minimum=\"0\" "
    "Maximum=\"{TemplateBinding ScrollableWidth}\" Value=\"{TemplateBinding HorizontalOffset}\" "
    "ViewportSize=\"{TemplateBinding ViewportWidth}\" "
    "Visibility=\"{TemplateBinding ComputedHorizontalScrollBarVisibility}\" "
    "Style=\"{StaticResource EO_MakeScrollBar}\"/>"
    "</Grid></ControlTemplate></Setter.Value></Setter></Style>"
    "<Style x:Key=\"EO_MakeItem\" TargetType=\"ListBoxItem\">"
    "<Setter Property=\"Width\" Value=\"238\"/><Setter Property=\"Height\" Value=\"170\"/>"
    "<Setter Property=\"Margin\" Value=\"0,0,2,2\"/>"
    "<Setter Property=\"FocusVisualStyle\" Value=\"{x:Null}\"/>"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"ListBoxItem\">"
    "<Border x:Name=\"Tile\" Background=\"#FF252E34\" "
    "BorderThickness=\"0,0,0,6\" BorderBrush=\"Transparent\"><Grid>"
    "<Grid.RowDefinitions><RowDefinition Height=\"128\"/><RowDefinition/></Grid.RowDefinitions>"
    "<local:FZMaterial x:Name=\"CarLogo\" ImageSource=\"{Binding EOMakeCar.ManufacturerIcon}\" Stretch=\"Uniform\" "
    "Margin=\"20,12,20,6\"/>"
    "<local:FZMaterial x:Name=\"RimLogo\" Visibility=\"Collapsed\" ImageSource=\"{Binding EOMakeLogo}\" Stretch=\"Uniform\" Margin=\"20,12,20,6\"/>"
    "<local:FZTextBlock x:Name=\"MakerName\" Grid.Row=\"1\" Text=\"{Binding EOMakeLabel}\" "
    "Style=\"{StaticResource A_Style_FZTextBlock_Body_Tight}\" Color=\"#FFF5F5F5\" "
    "HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\" Margin=\"6,0\"/>"
    "</Grid></Border><ControlTemplate.Triggers>"
    "<DataTrigger Binding=\"{Binding EOMakeRim}\" Value=\"True\">"
    "<Setter TargetName=\"CarLogo\" Property=\"Visibility\" Value=\"Collapsed\"/>"
    "<Setter TargetName=\"RimLogo\" Property=\"Visibility\" Value=\"Visible\"/>"
    "</DataTrigger>"
    "<Trigger Property=\"IsSelected\" Value=\"True\">"
    "<Setter TargetName=\"Tile\" Property=\"BorderBrush\" Value=\"#FFFF4C14\"/>"
    "<Setter TargetName=\"Tile\" Property=\"Background\" Value=\"#FF414E57\"/>"
    "</Trigger>"
    "<DataTrigger Binding=\"{Binding EOMakeAftermarket}\" Value=\"True\">"
    "<Setter TargetName=\"Tile\" Property=\"Background\" Value=\"#FFFFFFFF\"/>"
    "<Setter TargetName=\"MakerName\" Property=\"Color\" Value=\"#FF252E34\"/>"
    "</DataTrigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>"
    "<Style x:Key=\"EO_MakeMenu\" TargetType=\"{x:Type local:CommonListBox}\" "
    "BasedOn=\"{StaticResource A_Style_ListBox_TextMenu_Default}\">"
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOMakeGrid}\" Value=\"True\">"
    "<Setter Property=\"Width\" Value=\"1440\"/><Setter Property=\"MaxWidth\" Value=\"1440\"/>"
    "<Setter Property=\"Height\" Value=\"720\"/>"
    "<Setter Property=\"ScrollViewer.CanContentScroll\" Value=\"False\"/>"
    "<Setter Property=\"ItemContainerStyle\" Value=\"{StaticResource EO_MakeItem}\"/>"
    "<Setter Property=\"Template\"><Setter.Value>"
    "<ControlTemplate TargetType=\"{x:Type local:CommonListBox}\">"
    "<ScrollViewer Width=\"1440\" Height=\"720\" Focusable=\"False\" UseLayoutRounding=\"True\" "
    "SnapsToDevicePixels=\"True\" HorizontalScrollBarVisibility=\"Auto\" "
    "VerticalScrollBarVisibility=\"Disabled\" CanContentScroll=\"False\" "
    "Style=\"{StaticResource EO_MakeScrollViewer}\">"
    "<WrapPanel Height=\"688\" Orientation=\"Vertical\" ItemWidth=\"240\" "
    "ItemHeight=\"172\" "
    "IsItemsHost=\"True\"/>"
    "</ScrollViewer></ControlTemplate></Setter.Value></Setter>"
    "</DataTrigger></Style.Triggers></Style>"
    "<Style x:Key=\"EO_MakeFrame\" TargetType=\"ContentControl\" "
    "BasedOn=\"{StaticResource A_Style_ContentControl_ScreenTilt}\">"
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOMakeGrid}\" Value=\"True\">"
    "<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"ContentControl\">"
    "<ContentPresenter Content=\"{TemplateBinding Content}\" "
    "ContentTemplate=\"{TemplateBinding ContentTemplate}\"/>"
    "</ControlTemplate></Setter.Value></Setter></DataTrigger></Style.Triggers></Style>"
    "<Style x:Key=\"EO_MakeDialog\" TargetType=\"StackPanel\" "
    "BasedOn=\"{StaticResource A_Style_FrameworkElement_DefaultTransition_0}\">"
    "<Setter Property=\"Width\" Value=\"550\"/>"
    "<Style.Triggers><DataTrigger Binding=\"{Binding EOMakeGrid}\" Value=\"True\">"
    "<Setter Property=\"Width\" Value=\"1440\"/>"
    "</DataTrigger></Style.Triggers></Style>";

int manufacturer_jump_is_xaml(const char *text, size_t length) {
    return text_find(text, length, "x:Class=\"ForzaUI.Scenes.Anthem.A_MessageBox\"") != NULL;
}

size_t manufacturer_jump_xaml(const char *text, size_t length, char *output, size_t capacity) {
    if (!manufacturer_jump_is_xaml(text, length) || text_find(text, length, "EO_MakeMenu")) {
        return 0;
    }
    const char *resources = text_find_unique(text, length, "</ResourceDictionary>");
    const char *container = text_find_unique(text, length, "<StackPanel x:Name=\"MainContent\"");
    const char *menu = text_find_unique(text, length, "<local:CommonListBox x:Name=\"Menu\"");
    static const char native_frame[] = "<ContentControl Style=\"{StaticResource "
                                       "A_Style_ContentControl_ScreenTilt}\" Focusable=\"False\">";
    static const char frame[] =
        "<ContentControl Style=\"{StaticResource EO_MakeFrame}\" Focusable=\"False\">";
    const char *frame_at = text_find_unique(text, length, native_frame);
    if (!resources || !container || !menu || !frame_at || resources >= frame_at ||
        frame_at >= container || container >= menu) {
        return 0;
    }
    const char *container_end = text_find(container, length - (size_t)(container - text), ">");
    const char *menu_end = text_find(menu, length - (size_t)(menu - text), "/>");
    if (!container_end || !menu_end || container_end >= menu) {
        return 0;
    }
    ++container_end;
    menu_end += 2;
    static const char outer[] =
        "<StackPanel x:Name=\"MainContent\" Style=\"{StaticResource EO_MakeDialog}\" "
        "HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\" UseLayoutRounding=\"True\" "
        "SnapsToDevicePixels=\"True\">";
    static const char list[] =
        "<local:CommonListBox x:Name=\"Menu\" ItemsSource=\"{Binding Collection}\" "
        "Style=\"{StaticResource EO_MakeMenu}\"/>";
    size_t size = length + sizeof(frame) - sizeof(native_frame) + sizeof(make_styles) - 1 +
                  sizeof(outer) - 1 + sizeof(list) - 1 - (size_t)(container_end - container) -
                  (size_t)(menu_end - menu);
    if (size >= capacity || !register_properties()) {
        return 0;
    }
    char *p = output;
#define MAKE_COPY(source, n)                                                                       \
    do {                                                                                           \
        size_t count_ = (n);                                                                       \
        memory_copy(p, (source), count_);                                                          \
        p += count_;                                                                               \
    } while (0)
    MAKE_COPY(text, (size_t)(resources - text));
    MAKE_COPY(make_styles, sizeof(make_styles) - 1);
    MAKE_COPY(resources, (size_t)(frame_at - resources));
    MAKE_COPY(frame, sizeof(frame) - 1);
    const char *after_frame = frame_at + sizeof(native_frame) - 1;
    MAKE_COPY(after_frame, (size_t)(container - after_frame));
    MAKE_COPY(outer, sizeof(outer) - 1);
    MAKE_COPY(container_end, (size_t)(menu - container_end));
    MAKE_COPY(list, sizeof(list) - 1);
    MAKE_COPY(menu_end, length - (size_t)(menu_end - text));
    *p = 0;
#undef MAKE_COPY
    return size;
}
