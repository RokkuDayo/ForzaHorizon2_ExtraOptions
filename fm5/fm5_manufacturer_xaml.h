/* Bounded, left-aligned names are trimmed before item construction. */
static const char manufacturer_styles[]=
"<Style x:Key=\"EoMakeScroll\" TargetType=\"{x:Type ScrollViewer}\">"
"<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"{x:Type ScrollViewer}\">"
"<Grid><Grid.RowDefinitions><RowDefinition Height=\"*\"/><RowDefinition Height=\"Auto\"/></Grid.RowDefinitions>"
"<ScrollContentPresenter x:Name=\"PART_ScrollContentPresenter\" ClipToBounds=\"True\" Content=\"{TemplateBinding Content}\" "
"CanContentScroll=\"{TemplateBinding ScrollViewer.CanContentScroll}\"/>"
"<ScrollBar x:Name=\"PART_HorizontalScrollBar\" Grid.Row=\"1\" Orientation=\"Horizontal\" Height=\"9\" Margin=\"4,12,4,0\" "
"Minimum=\"0\" Maximum=\"{TemplateBinding ScrollableWidth}\" Value=\"{TemplateBinding HorizontalOffset}\" "
"ViewportSize=\"{TemplateBinding ViewportWidth}\" Visibility=\"{TemplateBinding ComputedHorizontalScrollBarVisibility}\"/>"
"</Grid></ControlTemplate></Setter.Value></Setter></Style>"
"<Style x:Key=\"EoMakeItem\" TargetType=\"ListBoxItem\">"
"<Setter Property=\"Width\" Value=\"238\"/><Setter Property=\"Height\" Value=\"170\"/>"
"<Setter Property=\"Margin\" Value=\"0,0,2,2\"/><Setter Property=\"FocusVisualStyle\" Value=\"{x:Null}\"/>"
"<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"ListBoxItem\">"
"<Border x:Name=\"Tile\" ClipToBounds=\"True\" Background=\"{StaticResource DarkPanelBackgroundBrush}\" BorderBrush=\"Transparent\" BorderThickness=\"0,0,0,4\">"
"<Grid><Grid.RowDefinitions><RowDefinition Height=\"120\"/><RowDefinition Height=\"46\"/></Grid.RowDefinitions>"
"<Image Source=\"{Binding EoMakeCar.ManufacturerIcon}\" Stretch=\"Uniform\" Margin=\"20,12,20,4\"/>"
"<Image x:Name=\"RimLogo\" Source=\"{Binding EoMakeLogo}\" Style=\"{x:Null}\" Effect=\"{x:Null}\" Stretch=\"Uniform\" Margin=\"20,12,20,4\"/>"
"<Grid x:Name=\"AftermarketLogo\" Visibility=\"Collapsed\" Width=\"104\" Height=\"104\" ClipToBounds=\"True\" HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\">"
"<Image Source=\"{Binding EoMakeLogo}\" Style=\"{x:Null}\" Effect=\"{x:Null}\" Stretch=\"Uniform\" Width=\"120\" Height=\"120\" Margin=\"-8\"/>"
"</Grid>"
"<Grid Grid.Row=\"1\" Width=\"222\" Height=\"30\" HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\" ClipToBounds=\"True\">"
"<TextBlock x:Name=\"MakeName\" Text=\"{Binding Text}\" Style=\"{StaticResource BaseTextBlockStyle}\" FontSize=\"22\" Foreground=\"{StaticResource AlmostWhiteBrush}\" "
"TextAlignment=\"Left\" Width=\"222\" HorizontalAlignment=\"Stretch\" VerticalAlignment=\"Center\" TextTrimming=\"CharacterEllipsis\" Margin=\"0\"/>"
"</Grid>"
"</Grid></Border><ControlTemplate.Triggers><Trigger Property=\"Selector.IsSelected\" Value=\"True\">"
"<Setter TargetName=\"Tile\" Property=\"BorderBrush\" Value=\"{StaticResource ForzaRedBrush}\"/>"
"</Trigger><DataTrigger Binding=\"{Binding EoMakeAftermarket}\" Value=\"1\">"
"<Setter TargetName=\"Tile\" Property=\"Background\" Value=\"#FFFFFFFF\"/>"
"<Setter TargetName=\"MakeName\" Property=\"Foreground\" Value=\"#FF252D32\"/>"
"<Setter TargetName=\"RimLogo\" Property=\"Visibility\" Value=\"Collapsed\"/>"
"<Setter TargetName=\"AftermarketLogo\" Property=\"Visibility\" Value=\"Visible\"/>"
"</DataTrigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>"
"<Style x:Key=\"EoMakeMenu\" TargetType=\"{x:Type local:CommonListBox}\" BasedOn=\"{StaticResource DialogVerticalListBoxStyle}\">"
"<Setter Property=\"Width\" Value=\"{Binding ActualWidth, ElementName=Control_Header}\"/>"
"<Setter Property=\"ItemContainerStyle\" Value=\"{StaticResource VerticalMenuListBoxItemStyle}\"/>"
"<Style.Triggers><DataTrigger Binding=\"{Binding EoMakeGrid}\" Value=\"1\">"
"<Setter Property=\"Width\" Value=\"1680\"/><Setter Property=\"Height\" Value=\"728\"/>"
"<Setter Property=\"ScrollViewer.CanContentScroll\" Value=\"True\"/>"
"<Setter Property=\"ItemContainerStyle\" Value=\"{StaticResource EoMakeItem}\"/>"
"<Setter Property=\"Template\"><Setter.Value><ControlTemplate TargetType=\"{x:Type local:CommonListBox}\">"
"<Border Background=\"{x:Null}\"><VisualStateManager.VisualStateGroups><VisualStateGroup x:Name=\"ValidationStates\">"
"<VisualState x:Name=\"Valid\"/><VisualState x:Name=\"InvalidFocused\"/><VisualState x:Name=\"InvalidUnfocused\"/>"
"</VisualStateGroup></VisualStateManager.VisualStateGroups>"
"<ScrollViewer Focusable=\"False\" CanContentScroll=\"True\" HorizontalScrollBarVisibility=\"Auto\" VerticalScrollBarVisibility=\"Disabled\" Style=\"{StaticResource EoMakeScroll}\">"
"<local:VirtualizingTilePanel Margin=\"0,0,0,16\" Height=\"688\" ChildWidth=\"240\" ChildHeight=\"172\" IsItemsHost=\"True\"/>"
"</ScrollViewer></Border></ControlTemplate></Setter.Value></Setter>"
"</DataTrigger></Style.Triggers></Style>"
"<Style x:Key=\"EoMakeHeader\" TargetType=\"ContentControl\" BasedOn=\"{StaticResource DialogHeaderStyle}\">"
"<Style.Triggers><DataTrigger Binding=\"{Binding EoMakeGrid}\" Value=\"1\">"
"<Setter Property=\"Width\" Value=\"1680\"/><Setter Property=\"MaxWidth\" Value=\"1680\"/>"
"<Setter Property=\"HorizontalAlignment\" Value=\"Stretch\"/>"
"</DataTrigger></Style.Triggers></Style>"
"<Style x:Key=\"EoMakeOuter\" TargetType=\"Border\">"
"<Setter Property=\"Canvas.Left\" Value=\"{StaticResource DialogAreaLeftMargin}\"/>"
"<Setter Property=\"Canvas.Top\" Value=\"{StaticResource DialogAreaTopMargin}\"/>"
"<Setter Property=\"Width\" Value=\"{StaticResource DialogAreaWidth}\"/>"
"<Setter Property=\"Height\" Value=\"{StaticResource DialogAreaHeight}\"/>"
"<Style.Triggers><DataTrigger Binding=\"{Binding EoMakeGrid}\" Value=\"1\">"
"<Setter Property=\"Canvas.Left\" Value=\"120\"/><Setter Property=\"Canvas.Top\" Value=\"80\"/>"
"<Setter Property=\"Width\" Value=\"1680\"/><Setter Property=\"Height\" Value=\"860\"/>"
"</DataTrigger></Style.Triggers></Style>"
"<Style x:Key=\"EoMakeDialog\" TargetType=\"Grid\">"
"<Setter Property=\"MaxHeight\" Value=\"{StaticResource DialogMaxHeight}\"/>"
"<Setter Property=\"MinWidth\" Value=\"{StaticResource DialogMinWidth}\"/>"
"<Setter Property=\"MaxWidth\" Value=\"{StaticResource DialogMaxWidth}\"/>"
"<Style.Triggers><DataTrigger Binding=\"{Binding EoMakeGrid}\" Value=\"1\">"
"<Setter Property=\"MaxHeight\" Value=\"860\"/><Setter Property=\"MinWidth\" Value=\"1680\"/><Setter Property=\"MaxWidth\" Value=\"1680\"/>"
"</DataTrigger></Style.Triggers></Style></Page.Resources>";

int fm5_manufacturer_resource(const char *s,size_t n){
    return image && (text_find(s,n,"x:Class=\"ForzaUI.F877_VerticalPopupMenu\"") ||
                    text_find(s,n,"x:Class=\"ForzaUI.Scenes.F111_ModelSelect\"") ||
                    text_find(s,n,"x:Class=\"ForzaUI.F453_MarketplaceModelSelect\""));
}
typedef struct {const char *from,*to;} Edit;
static size_t edit_xaml(const char *s,size_t n,char *out,size_t capacity,const Edit *edits,unsigned count){
    const char *positions[8];size_t written=0,cursor=0;
    if(count>ARRAY_COUNT(positions))return 0;
    for(unsigned i=0;i<count;++i){positions[i]=text_find_unique(s,n,edits[i].from);if(!positions[i])return 0;}
    for(unsigned pass=0;pass<count;++pass){
        unsigned next=count;
        for(unsigned i=0;i<count;++i)if(positions[i] && (next==count || positions[i]<positions[next]))next=i;
        if(next==count)return 0;
        size_t at=(size_t)(positions[next]-s),plain=at-cursor,added=text_length(edits[next].to);
        if(at<cursor || plain>=capacity-written || added>=capacity-written-plain)return 0;
        memory_copy(out+written,s+cursor,plain);written+=plain;
        memory_copy(out+written,edits[next].to,added);written+=added;
        cursor=at+text_length(edits[next].from);positions[next]=NULL;
    }
    if(n-cursor>=capacity-written)return 0;
    memory_copy(out+written,s+cursor,n-cursor);written+=n-cursor;out[written]=0;return written;
}
size_t fm5_manufacturer_xaml(const char *s,size_t n,char *out,size_t capacity){
    if(!fm5_manufacturer_resource(s,n) || text_find(s,n,"EoMake") || !register_properties())return 0;
    if(text_find(s,n,"ForzaUI.F877_VerticalPopupMenu")){
        /* The native vertical-list style is local to this resource dictionary.
           Declare our derived styles AFTER it: AVUI resolves StaticResource
           while reading the dictionary. A forward BasedOn reference loses the
           native vertical template and ordinary popups fall back to a row. */
        const Edit edits[]={
            {"</Page.Resources>",manufacturer_styles},
            {"<Border x:Name=\"Border_OuterContainer\" Canvas.Left=\"{StaticResource DialogAreaLeftMargin}\" Canvas.Top=\"{StaticResource DialogAreaTopMargin}\" Width=\"{StaticResource DialogAreaWidth}\" Height=\"{StaticResource DialogAreaHeight}\">",
             "<Border x:Name=\"Border_OuterContainer\" Style=\"{StaticResource EoMakeOuter}\">"},
            {"<Grid x:Name=\"Grid_DialogContainer\" MaxHeight=\"{StaticResource DialogMaxHeight}\" MinWidth=\"{StaticResource DialogMinWidth}\" MaxWidth=\"{StaticResource DialogMaxWidth}\" HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\">",
             "<Grid x:Name=\"Grid_DialogContainer\" Style=\"{StaticResource EoMakeDialog}\" HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\">"},
            {"Style=\"{StaticResource DialogHeaderStyle}\"","Style=\"{StaticResource EoMakeHeader}\""},
            {"Style=\"{StaticResource DialogVerticalListBoxStyle}\"","Style=\"{StaticResource EoMakeMenu}\""},
            {"ItemContainerStyle=\"{StaticResource VerticalMenuListBoxItemStyle}\"",""},
            {"Width=\"{Binding ActualWidth, ElementName=Control_Header}\"",""}
        };
        return edit_xaml(s,n,out,capacity,edits,ARRAY_COUNT(edits));
    }
    void *manager=NULL;unsigned char code[32]={0};
    if(memory_read_pointer(image+0x25e7ae8,&manager) && manager)memory_read((unsigned char *)manager+0x30,code,32);
    const char *language=code[24]==15 && code[16]>=2 && code[16]<=3?(char *)code:NULL;
    char label[1024],prompt[2048];wchar_t upper[256];
    const wchar_t *heading=video_options_text_code(language)->manufacturer_jump;
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,heading,-1,upper,256,NULL,NULL,0) ||
       !WideCharToMultiByte(CP_UTF8,0,upper,-1,label,sizeof(label),NULL,NULL))return 0;
    /* These translations contain no XML metacharacters; preserve the existing
       native controller panel's input scheme and right-to-left ordering. */
    const char *prefix="<local:ControllerButtonControl x:Name=\"ButtonMakeJump\" Button=\"Back\" Description=\"";
    const char *suffix="\"><local:ControllerButtonControl.Style><Style TargetType=\"local:ControllerButtonControl\" BasedOn=\"{StaticResource {x:Type local:ControllerButtonControl}}\"><Setter Property=\"Visibility\" Value=\"Collapsed\"/><Style.Triggers><DataTrigger Binding=\"{Binding EoMakeJump, ElementName=Page}\" Value=\"1\"><Setter Property=\"Visibility\" Value=\"Visible\"/></DataTrigger></Style.Triggers></Style></local:ControllerButtonControl.Style></local:ControllerButtonControl></local:ControllerButtonPanel>";
    size_t a=text_length(prefix),b=text_length(label),c=text_length(suffix);
    if(a+b+c>=sizeof(prompt) || text_find(label,b,"&") || text_find(label,b,"\"") || text_find(label,b,"<"))return 0;
    memory_copy(prompt,prefix,a);memory_copy(prompt+a,label,b);memory_copy(prompt+a+b,suffix,c+1);
    Edit edit={"</local:ControllerButtonPanel>",prompt};
    return edit_xaml(s,n,out,capacity,&edit,1);
}
