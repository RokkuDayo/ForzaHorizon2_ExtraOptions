#include "fm5/fm5_camera.h"
#include "shared/support.h"
#include "shared/video_options_text.h"
#include "fm5/fm5_camera_styles.h"
#include "fm5/fm5_manufacturer.h"
#include "fm5/fm5_video.h"
#include "fm5/fm5_audio.h"
#include "fm5/fm5_start.h"
#include "fm5/fm5_vehicle.h"
#include "fm5/fm5_driver.h"
#include "fm5/fm5_photo_vehicle_style.h"

#define XAML_LIMIT 262144u
#define CHOICES FM5_CAMERA_SETTING_COUNT

typedef struct {
    void **vtable; unsigned char header[0x20];
    void *getter,*setter,*unused; const char *name; void *type; uint64_t flags;
} NativeProperty;
typedef struct Link { NativeProperty *property; struct Link *next; } Link;
typedef struct {
    void **vtable; unsigned char header[0x20]; char *bytes; int length,capacity;
} Resource;
_Static_assert(sizeof(NativeProperty)==0x58,"FM5 property descriptor");
_Static_assert(sizeof(Resource)==0x38,"FM5 resource");
static const char *const names[]={"EoDriftMode","EoChaseSensitivity","EoChaseSpeed","EoChaseRange",
    "EoDriverSensitivity","EoDriverSpeed","EoDriverRange","EoFovChase","EoFovFar",
    "EoFovDriver","EoFovHood","EoFovBumper","EoCockpitBlur","EoDriftChoices"};
static unsigned char *image;
static int drift_enabled,fov_enabled,registered;
static unsigned reported;
static void (*log_message)(const char *);
static NativeProperty properties[FM5_CAMERA_SETTING_COUNT+1];
static Link links[FM5_CAMERA_SETTING_COUNT+1];
static void *choices,*resource_vtable[7];
static void *(*allocate)(size_t);
static void (*free_object)(void *),(*initialize_object)(void *),(*cleanup_object)(void *);
static void (*property_changed)(void *,void *);

static int enabled(unsigned i) { return i==FM5_COCKPIT_BLUR || i==CHOICES || (i<FM5_FOV_CHASE ? drift_enabled : fov_enabled); }
static const wchar_t *uppercase(const wchar_t *text,wchar_t out[256]) {
    return LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,text,-1,out,256,NULL,NULL,0)
        ? out : text;
}
static const VideoOptionsText *labels(void) {
    void *manager; unsigned char code[32]; size_t length,capacity;
    if(!memory_read_pointer(image+0x25e7ae8,&manager) || !manager ||
        !memory_read((unsigned char *)manager+0x30,code,sizeof(code)))return video_options_text_code(NULL);
    memory_copy(&length,code+16,8);memory_copy(&capacity,code+24,8);
    return video_options_text_code(capacity==15 && length>=2 && length<=3 && !code[length] ? (char *)code : NULL);
}
static void release(void *object) {
    if(object && --*(unsigned *)((unsigned char *)object+0x10)==0)
        ((void (*)(void *))(image+0x137710))(object);
}
static unsigned index_of(NativeProperty *property) {
    for(unsigned i=0;i<ARRAY_COUNT(properties);++i)if(property==properties+i)return i;
    return ARRAY_COUNT(properties);
}
static void **get_value(void **out,NativeProperty *property,void *page) {
    (void)page; unsigned i=index_of(property); *out=NULL;
    if(i==CHOICES){*out=choices;if(choices)++*(unsigned *)((unsigned char *)choices+0x10);}
    else if(i<FM5_CAMERA_SETTING_COUNT){
        int value=fm5_camera_get(i);
        /* Native CameraEffectsOptions orders OFF=0, ON=1. */
        if(i==FM5_DRIFT_MODE || i==FM5_COCKPIT_BLUR)((void **(*)(void **,const int *))(image+0x296b00))(out,&value);
        else {float number=i<FM5_FOV_CHASE ? (float)value : value*.5f;
            ((void **(*)(void **,const float *))(image+0x296a30))(out,&number);}
    }
    return out;
}
static void set_value(NativeProperty *property,void *page,void *boxed) {
    unsigned i=index_of(property);if(!boxed || i>=FM5_CAMERA_SETTING_COUNT)return;
    int value;
    if(i==FM5_DRIFT_MODE || i==FM5_COCKPIT_BLUR){if(!memory_read((unsigned char *)boxed+0x28,&value,4))return; if(i==FM5_COCKPIT_BLUR && (value<0 || value>1))return;}
    else {
        float number;if(!memory_read((unsigned char *)boxed+0x28,&number,4) || !float_is_finite(number))return;
        if(number<(i<FM5_FOV_CHASE?0:20) || number>(i<FM5_FOV_CHASE?100:120))return;
        value=(int)(number*(i<FM5_FOV_CHASE?1:2)+.5f);
    }
    if(fm5_camera_set(i,value))property_changed(page,property);
}
static int make_choices(void) {
    void *items=allocate(0x58);if(!items)return 0;
    ((void (*)(void *))(image+0x33d020))(items);
    const VideoOptionsText *text=labels();
    for(int i=0;i<3;++i){
        /* AB1770 constructs a native UTF-16 String in allocated object storage;
           AB1640 is the narrow constructor, not an output-pointer factory. */
        void *label=allocate(0x40);
        if(!label){release(items);return 0;}
        wchar_t upper[256];
        ((void *(*)(void *,const wchar_t *,int))(image+0xab1770))(label,uppercase(text->choices[i],upper),-1);
        ((void (*)(void *,int,void *))(*(void ***)items)[0x40/8])(items,i,label);
        release(label);
    }
    choices=items;return 1;
}
static NativeProperty player_properties[2];
static Link player_links[2];
static void **player_get(void **out,NativeProperty *property,void *page){
    (void)page;*out=NULL;
    if(property==player_properties){int value=fm5_driver_index();return ((void **(*)(void **,const int *))(image+0x296b00))(out,&value);}
    void *items=allocate(0x58);if(!items)return out;
    ((void (*)(void *))(image+0x33d020))(items);
    for(unsigned i=0,n=fm5_driver_count();i<n;++i){
        wchar_t name[128],upper[256];fm5_driver_label(i,name);
        const wchar_t *text=i?name:labels()->default_choice;
        void *label=allocate(0x40);if(!label){release(items);return out;}
        ((void *(*)(void *,const wchar_t *,int))(image+0xab1770))(label,uppercase(text,upper),-1);
        ((void (*)(void *,int,void *))(*(void ***)items)[8])(items,(int)i,label);release(label);
    }
    *out=items;return out;
}
static void player_set(NativeProperty *property,void *page,void *boxed){
    int value;if(property!=player_properties || !boxed || !memory_read((unsigned char *)boxed+0x28,&value,4) || value<0)return;
    if(fm5_driver_select((unsigned)value))property_changed(page,property);
}
static int register_player(Link **head){
    static const char *const names[]={"EoPlayerCharacter","EoPlayerCharacters"};
    for(unsigned i=0;i<2;++i){
        NativeProperty *original=((NativeProperty *(*)(void))(image+(i?0x357260:0x357080)))();if(!original)return 0;
        NativeProperty *p=player_properties+i;initialize_object(p);p->vtable=original->vtable;
        memory_copy((unsigned char *)p+0x28,(unsigned char *)original+0x28,0x30);
        p->name=names[i];p->getter=(void *)player_get;p->setter=i?NULL:(void *)player_set;
        player_links[i]=(Link){p,*head};*head=player_links+i;
    }
    return 1;
}
static int register_properties(void) {
    if(registered)return 1;
    ((void (*)(void))(image+0x35bf00))();
    if(drift_enabled && !choices && !make_choices())return 0;
    /* FM5's initializer prepends precisely these links to its type registry.
       The page layout is unchanged; values live exclusively in our own state. */
    for(unsigned i=0;i<ARRAY_COUNT(properties);++i){
        if(!enabled(i) || (i==CHOICES && !drift_enabled))continue;
        unsigned factory=i==CHOICES?0x357260:(i==FM5_DRIFT_MODE || i==FM5_COCKPIT_BLUR)?0x357080:0x2ccbe0;
        NativeProperty *original=((NativeProperty *(*)(void))(image+factory))();
        if(!original)return 0;
        NativeProperty *property=properties+i;
        initialize_object(property);property->vtable=original->vtable;
        memory_copy((unsigned char *)property+0x28,(unsigned char *)original+0x28,0x30);
        property->name=names[i];property->getter=(void *)get_value;
        property->setter=i==CHOICES?NULL:(void *)set_value;
    }
    Link **head=(Link **)(image+0x2463490+0x40);
    for(unsigned i=0;i<ARRAY_COUNT(properties);++i){
        if(!enabled(i) || (i==CHOICES && !drift_enabled))continue;
        links[i]=(Link){properties+i,*head};*head=links+i;
    }
    if(!register_player(head))return 0;
    registered=1;return 1;
}

/* Bounded UTF-8 edits; no allocations in camera or physics updates. */
typedef struct {char *out;size_t length,capacity;int ok;} Writer;
static void span(Writer *w,const char *s,size_t n) {
    if(!w->ok || n>=w->capacity-w->length){w->ok=0;return;}
    memory_copy(w->out+w->length,s,n);w->length+=n;w->out[w->length]=0;
}
static void put(Writer *w,const char *s){span(w,s,text_length(s));}
static void escaped(Writer *w,const char *s){
    for(;*s;++s)switch(*s){
        case '&':put(w,"&amp;");break;case '"':put(w,"&quot;");break;
        case '<':put(w,"&lt;");break;case '>':put(w,"&gt;");break;
        default:span(w,s,1);break;
    }
}
static void row_label(Writer *w,const VideoOptionsText *text,unsigned i) {
    /* Keep FH2's wording and order; FM5 presents option headings in capitals. */
    char label[512],utf8[1024];wchar_t wide[256],upper[256];
    Writer heading={label,0,sizeof(label),1};label[0]=0;
    if(i==FM5_COCKPIT_BLUR){put(&heading,"Cockpit Motion Blur");}
    else if(i<FM5_FOV_CHASE){
        put(&heading,i && text_equal_ignore_case(text->camera,"Drift Camera") ? "Drift Cam." : text->camera);
        if(i){
            if(i>=FM5_DRIVER_SENSITIVITY){put(&heading," (");put(&heading,text->driver);put(&heading,")");}
            put(&heading," - ");put(&heading,text->parameters[(i-1)%3]);
        }
    }else {put(&heading,text->fov_label);put(&heading," - ");put(&heading,text->fov_views[i-FM5_FOV_CHASE]);}
    if(!heading.ok){w->ok=0;return;}
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,label,-1,wide,256) ||
        !WideCharToMultiByte(CP_UTF8,0,uppercase(wide,upper),-1,utf8,sizeof(utf8),NULL,NULL)){
        w->ok=0;return;
    }
    escaped(w,utf8);
}
static size_t transform(const char *src,size_t length,char *out,size_t capacity) {
    if(!src || length>131072 || text_find(src,length,"EoPlayerCharacter") ||
        !text_find(src,length,"x:Class=\"ForzaUI.Scenes.WPF.HudOptions\""))return 0;
    const char *resources=text_find_unique(src,length,"<Page.Resources>");
    const char *triggers=text_find_unique(src,length,"<Page.Triggers>");
    const char *stack=text_find_unique(src,length,"<StackPanel x:Name=\"StackPanel_OptionsList\"");
    const char *damage=text_find_unique(src,length,"<local:SpinnerControl x:Name=\"Damage\"");
    const char *end=stack?text_find(stack,length-(size_t)(stack-src),"</StackPanel>"):NULL;
    const char *root=text_find_unique(src,length,"xmlns:local=\"clr-namespace:ForzaUI\"");
    if(!resources || !triggers || !stack || !damage || !end || !root ||
        !(root<resources && resources<triggers && triggers<stack && stack<damage && damage<end))return 0;
    Writer w={out,0,capacity,1};const char *at=src;
    /* AVUI defaults to byte widening without an explicit UTF-8 marker. */
    if(length<3 || !memory_equal(src,"\xef\xbb\xbf",3))span(&w,"\xef\xbb\xbf",3);
#define COPY_TO(p) do {span(&w,at,(size_t)((p)-at));at=(p);} while(0)
    root+=text_length("xmlns:local=\"clr-namespace:ForzaUI\"");COPY_TO(root);
    put(&w," xmlns:System=\"clr-namespace:System;assembly=mscorlib\"");
    resources+=text_length("<Page.Resources>");COPY_TO(resources);put(&w,camera_styles);
    const VideoOptionsText *text=labels();
    triggers+=text_length("<Page.Triggers>");COPY_TO(triggers);
    for(unsigned i=0;i<FM5_CAMERA_SETTING_COUNT;++i)if(i!=FM5_COCKPIT_BLUR && enabled(i)){
        put(&w,"<EventTrigger RoutedEvent=\"Keyboard.GotKeyboardFocus\" SourceName=\"");put(&w,names[i]);
        put(&w,"\"><BeginStoryboard><Storyboard><StringAnimationUsingKeyFrames Storyboard.TargetName=\"TextBlock_FocusedDescription\" Storyboard.TargetProperty=\"Text\"><DiscreteStringKeyFrame KeyTime=\"0\" Value=\"");
        escaped(&w,i==FM5_COCKPIT_BLUR?"Enable motion blur in the cockpit view.":i>=FM5_FOV_CHASE?text->fov_description:text->descriptions[i?1+(i-1)%3:0]);
        put(&w,"\"/></StringAnimationUsingKeyFrames></Storyboard></BeginStoryboard></EventTrigger>");
    }
    put(&w,"<EventTrigger RoutedEvent=\"Keyboard.GotKeyboardFocus\" SourceName=\"EoPlayerCharacter\"><BeginStoryboard><Storyboard><StringAnimationUsingKeyFrames Storyboard.TargetName=\"TextBlock_FocusedDescription\" Storyboard.TargetProperty=\"Text\"><DiscreteStringKeyFrame KeyTime=\"0\" Value=\"");
    escaped(&w,text->player_character_description);
    put(&w,"\"/></StringAnimationUsingKeyFrames></Storyboard></BeginStoryboard></EventTrigger>");
    COPY_TO(stack);
    /* The native body centers an Auto row. An unconstrained viewer in that row
       grows with its contents instead of scrolling. Bound it to the original
       HUD list's viewport and leave focus-driven scrolling to the native UI. */
    put(&w,"<ScrollViewer Grid.Column=\"1\" Grid.Row=\"1\" Height=\"590\" Focusable=\"False\" CanContentScroll=\"True\" ClipToBounds=\"True\" VerticalScrollBarVisibility=\"Auto\" HorizontalScrollBarVisibility=\"Disabled\">");
    COPY_TO(damage);
    for(unsigned i=0;i<FM5_CAMERA_SETTING_COUNT;++i)if(i!=FM5_COCKPIT_BLUR && enabled(i)){
        if(!i){put(&w,"<local:SpinnerControl x:Name=\"EoDriftMode\" Label=\"");row_label(&w,text,i);
            put(&w,"\" SelectedIndex=\"{Binding EoDriftMode}\" ItemsSource=\"{Binding EoDriftChoices}\"/>");
        }else {
            put(&w,"<Grid Height=\"35\" Margin=\"0,2,0,0\"><Grid.ColumnDefinitions><ColumnDefinition Width=\"*\"/><ColumnDefinition Width=\"*\"/></Grid.ColumnDefinitions><Slider Grid.ColumnSpan=\"2\" x:Name=\"");put(&w,names[i]);
            put(&w,"\" Style=\"{StaticResource EoCameraSliderStyle}\" Value=\"{Binding ");put(&w,names[i]);
            put(&w,i<FM5_FOV_CHASE?"}\" Minimum=\"0\" Maximum=\"100\" SmallChange=\"1\"/>":"}\" Minimum=\"20\" Maximum=\"120\" SmallChange=\"0.5\"/>");
            put(&w,"<TextBlock Grid.Column=\"0\" ClipToBounds=\"True\" TextTrimming=\"CharacterEllipsis\" IsHitTestVisible=\"False\" Style=\"{StaticResource EoCameraLabelStyle}\" Text=\"");row_label(&w,text,i);
            put(&w,"\" Tag=\"{Binding IsFocused, ElementName=");put(&w,names[i]);put(&w,"}\"/>");
            put(&w,"<TextBlock Grid.Column=\"1\" Width=\"90\" IsHitTestVisible=\"False\" HorizontalAlignment=\"Right\" TextAlignment=\"Right\" Style=\"{StaticResource EoCameraLabelStyle}\" Text=\"{Binding ");put(&w,names[i]);
            put(&w,"}\" Tag=\"{Binding IsFocused, ElementName=");put(&w,names[i]);put(&w,"}\"/></Grid>");
        }
    }
    COPY_TO(end);
    put(&w,"<local:SpinnerControl x:Name=\"EoPlayerCharacter\" Label=\"");
    wchar_t player_upper[256],player_wide[256];char player_utf8[1024];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text->player_character,-1,player_wide,256) ||
       !WideCharToMultiByte(CP_UTF8,0,uppercase(player_wide,player_upper),-1,player_utf8,sizeof(player_utf8),NULL,NULL))return 0;
    escaped(&w,player_utf8);
    put(&w,"\" SelectedIndex=\"{Binding EoPlayerCharacter}\" ItemsSource=\"{Binding EoPlayerCharacters}\"/>");
    end+=text_length("</StackPanel>");COPY_TO(end);put(&w,"</ScrollViewer>");COPY_TO(src+length);
#undef COPY_TO
    return w.ok?w.length:0;
}
static NativeProperty photo_available,photo_focus_limit;
static Link photo_link,photo_focus_link;
static float photo_focus_width;
static int photo_focus_attempts,photo_focus_state,photo_effects_seen;
static int photo_properties;
static NativeProperty vehicle_properties[4];
static Link vehicle_links[4];
static void *vehicle_choices[2];
static void restart_capture_settled(void *page);
static void restart_capture_preview(void *page);
static void **photo_vehicle_get(void **out,NativeProperty *p,void *page){
    (void)page;unsigned i=(unsigned)(p-vehicle_properties);*out=NULL;
    if(i<2){int value=fm5_vehicle_photo_get(i);return ((void **(*)(void **,const int *))(image+0x296b00))(out,&value);}
    if(i<4){*out=vehicle_choices[i-2];if(*out)++*(unsigned *)((unsigned char *)*out+0x10);}
    return out;
}
static void photo_vehicle_set(NativeProperty *p,void *page,void *boxed){
    unsigned i=(unsigned)(p-vehicle_properties);int value;
    if(i<2 && boxed && memory_read((unsigned char *)boxed+0x28,&value,4) && fm5_vehicle_photo_set(page,i,value)){
        property_changed(page,p);
        /* Resume rendering now; settle only after the material update completes. */
        if(i)restart_capture_preview(page);else restart_capture_settled(page);
    }
}
static int photo_vehicle_register(Link **head){
    const VideoOptionsText *text=labels();
    const wchar_t *values[]={text->default_choice,text->choices[2],text->choices[0]};
    for(unsigned i=0;i<2;++i){
        void *items=allocate(0x58);if(!items)return 0;
        ((void (*)(void *))(image+0x33d020))(items);vehicle_choices[i]=items;
        for(unsigned j=0;j<2+i;++j){
            void *label=allocate(0x40);if(!label)return 0;
            ((void *(*)(void *,const wchar_t *,int))(image+0xab1770))(label,values[j],-1);
            ((void (*)(void *,int,void *))(*(void ***)items)[8])(items,j,label);release(label);
        }
    }
    const char *names[]={"EoPhotoDriver","EoPhotoLights","EoPhotoDriverChoices","EoPhotoLightChoices"};
    for(unsigned i=0;i<4;++i){
        NativeProperty *original=((NativeProperty *(*)(void))(image+(i<2?0x357080:0x357260)))();
        NativeProperty *p=vehicle_properties+i;initialize_object(p);p->vtable=original->vtable;
        memory_copy((unsigned char *)p+0x28,(unsigned char *)original+0x28,0x30);
        p->name=names[i];p->getter=(void *)photo_vehicle_get;p->setter=i<2?(void *)photo_vehicle_set:NULL;
        vehicle_links[i]=(Link){p,*head};*head=vehicle_links+i;
    }
    return 1;
}
static int can_reset_photo(void *object){
    unsigned char *page=object;
    int state=*(int *)(page+0xba4);
    return page[0xbd9]==0 && *(void **)(page+0xb70)!=NULL && state>=0 && state<=2;
}
static void **photo_available_get(void **out,NativeProperty *p,void *page){
    (void)p;int available=can_reset_photo(page);
    return ((void **(*)(void **,const int *))(image+0x296b00))(out,&available);
}
static void **photo_focus_limit_get(void **out,NativeProperty *p,void *page){
    (void)p;int state=*(int *)((unsigned char *)page+0xba4);
    uint32_t bits=0x7f800000;float limit;memory_copy(&limit,&bits,4);
    if((state==0 || state==2) && photo_focus_width>0)limit=photo_focus_width;
    return ((void **(*)(void **,const float *))(image+0x296a30))(out,&limit);
}
static float initial_photo_focus_width(void *page){
    /* Native namescope lookup, as used at 339780. Borrow the stack string;
       the lookup neither retains nor destroys it. */
    struct {void *header[5];const wchar_t *text;int length,borrowed,hash,padding;} name={0};
    name.header[0]=image+0x2220fd0;name.text=L"ButtonX";
    name.length=7;name.borrowed=1;name.hash=-1;
    void *owner=NULL;
    void *scope=((void *(*)(void *,void **))(image+0x9e3b60))(page,&owner);
    if(!scope)return 0;
    void *control=((void *(*)(void *,const void *))(*(void ***)scope)[0])(scope,&name);
    if(!control || *(void **)control!=image+0x2253300)return 0;
    float width=*(float *)((unsigned char *)control+0xf0);
    return float_is_finite(width) && width>0 && width<2048 ? width:0;
}
static void register_photo_property(void){
    if(photo_properties)return;
    ((void (*)(void))(image+0x37d970))();
    NativeProperty *original=((NativeProperty *(*)(void))(image+0x357080))();
    initialize_object(&photo_available);photo_available.vtable=original->vtable;
    memory_copy((unsigned char *)&photo_available+0x28,(unsigned char *)original+0x28,0x30);
    photo_available.name="EoDamageResetAvailable";photo_available.getter=(void *)photo_available_get;photo_available.setter=NULL;
    Link **head=(Link **)(image+0x2461ef0);photo_link=(Link){&photo_available,*head};*head=&photo_link;
    original=((NativeProperty *(*)(void))(image+0x2ccbe0))();
    initialize_object(&photo_focus_limit);photo_focus_limit.vtable=original->vtable;
    memory_copy((unsigned char *)&photo_focus_limit+0x28,(unsigned char *)original+0x28,0x30);
    photo_focus_limit.name="EoPhotoFocusMaxWidth";photo_focus_limit.getter=(void *)photo_focus_limit_get;photo_focus_limit.setter=NULL;
    photo_focus_link=(Link){&photo_focus_limit,*head};*head=&photo_focus_link;
    photo_properties=photo_vehicle_register(head);
}
static void photo_help(void *page){
    ((void (*)(void *))(image+0x3814f0))(page);
    if(photo_properties)property_changed(page,&photo_available);
}
static void *photo_last_page;
static int photo_last_available;
static void photo_tick(void *page){
    ((void (*)(void *))(image+0x37e100))(page);
    if(fm5_vehicle_photo_tick(page))restart_capture_settled(page);
    int available=can_reset_photo(page);
    int state=*(int *)((unsigned char *)page+0xba4),width_changed=0;
    if(state==1)photo_effects_seen=1;
    if(photo_properties && !photo_effects_seen && state==0 && photo_focus_width<=0 && photo_focus_attempts++<30){
        photo_focus_width=initial_photo_focus_width(page);width_changed=photo_focus_width>0;
    }
    if(photo_properties && (photo_focus_state!=state || width_changed)){
        photo_focus_state=state;property_changed(page,&photo_focus_limit);
    }
    if(photo_properties && (photo_last_page!=page || photo_last_available!=available)){
        photo_last_page=page;photo_last_available=available;
        property_changed(page,&photo_available);
    }
}
/* Native Photo Mode owns input focus and lifetime. Only the input callback
   touches the focused car; use the camera target's native RTTI cast. */
typedef struct { void **vtable; void *page; void *reserved; } PhotoCallback;
typedef struct { PhotoCallback storage; void *target; } PhotoFunction;
static void *photo_clone(PhotoCallback *self,void *out){
    if(!out)out=allocate(sizeof(*self));
    if(out)memory_copy(out,self,sizeof(*self));return out;
}
static void photo_destroy(PhotoCallback *self,unsigned char owned){if(owned)free_object(self);}
static void *photo_type(void *self){(void)self;return NULL;}
static unsigned char reset_photo_damage(PhotoCallback *self){
    unsigned char *page=self->page;
    if(!can_reset_photo(page))return 0;
    void *camera=*(void **)(page+0xb70);
    if(!camera)camera=*(void **)(page+0xb80);
    if(!camera)camera=*(void **)(page+0xb90);
    if(!camera)return 1;
    void *target=NULL;
    if(!memory_read_pointer((unsigned char *)camera+8,&target)||!target)return 1;
    /* CPredictedCameraTarget forwards to its underlying target at +8. */
    void *vtable=NULL;
    if(memory_read_pointer(target,&vtable) && vtable==image+0x22bcff8){
        if(!memory_read_pointer((unsigned char *)target+8,&target)||!target)return 1;
    }
    void *car=((void *(*)(void *,int,void *,void *,int))(image+0xe79f0c))
        (target,0,image+0x234e528,image+0x234d548,0);
    void *presentation=NULL;
    if(car && memory_read_pointer((unsigned char *)car+0x4c18,&presentation) && presentation){
        ((void (*)(void *))(*(void ***)presentation)[0xf8/8])(presentation);
        void *audio=*(void **)(image+0x25f88a0);
        if(audio){
            /* PlaySound takes a by-value native string and destroys its buffer.
               Match FM5's own call at 37E209; never pass borrowed game memory. */
            struct {char storage[16];size_t length,capacity;} event={{0},0,15};
            ((void *(*)(void *,const char *,size_t))(image+0x14a180))
                (&event,(const char *)(image+0x1c6f630),24);
            ((void (*)(void *,void *))(*(void ***)audio)[1])(audio,&event);
        }
    }
    return 1;
}
static void *photo_callback_table[]={(void *)photo_clone,(void *)photo_clone,
    (void *)reset_photo_damage,(void *)photo_type,(void *)photo_destroy};
static void photo_register(void *page){
    fm5_vehicle_photo_begin(page);
    photo_last_page=NULL;photo_last_available=-1;
    photo_focus_width=0;photo_focus_attempts=0;photo_focus_state=-1;photo_effects_seen=0;
    register_photo_property();
    ((void (*)(void *))(image+0x3812e0))(page);
    PhotoFunction function={{photo_callback_table,page,NULL},NULL};function.target=&function.storage;
    ((void (*)(void *,int,PhotoFunction *))(image+0x8e2f30))(page,0x13,&function);
}
static void photo_close(void *page){
    fm5_vehicle_photo_end(page);
    ((void (*)(void *))(image+0x37e050))(page);
}

/* FM5 already binds View to FullScreenMode. Supply its missing footer
   prompt and hide the focus ring in that state; input and capture stay native. */
static size_t photo_transform(const char *src,size_t length,char *out,size_t capacity) {
    if(!src || length>131072 || text_find(src,length,"ButtonHideUI") ||
       !text_find(src,length,"x:Class=\"ForzaUI.F730_PhotoMode\""))return 0;
    register_photo_property();
    const char *full=text_find_unique(src,length,"<VisualState x:Name=\"FullScreenMode\">");
    const char *full_end=full?text_find(full,length-(size_t)(full-src),"</VisualState>"):NULL;
    const char *ring=full_end?text_find(full,(size_t)(full_end-full),"Storyboard.TargetName=\"Grid_FocusRing\""):NULL;
    const char *visible=ring?text_find(ring,(size_t)(full_end-ring),"{x:Static Visibility.Visible}"):NULL;
    const char *panel=text_find_unique(src,length,"<local:ControllerButtonPanel x:Name=\"HelpPanel\"");
    const char *panel_close=panel?text_find(panel,length-(size_t)(panel-src),"</local:ControllerButtonPanel>"):NULL;
    const char *button_x=text_find_unique(src,length,"<local:ControllerButtonControl x:Name=\"ButtonX\"");
    if(!visible || !panel_close || !button_x || visible>=button_x || button_x>=panel_close)return 0;
    Writer w={out,0,capacity,1};const char *at=src;
    /* Keep the prompt out of the save/cancel screen and restore it on return. */
    const char *resource_end=text_find_unique(src,length,"</Page.Resources>");
    const char *effects=text_find_unique(src,length,"<StackPanel x:Name=\"StackPanel_CameraEffects\"");
    const char *effects_end=effects?text_find(effects,length-(size_t)(effects-src),"</StackPanel>"):NULL;
    const char *effect_transform=effects?text_find(effects,length-(size_t)(effects-src),"<StackPanel.RenderTransform>"):NULL;
    const char *effect_transform_end=effect_transform?text_find(effect_transform,length-(size_t)(effect_transform-src),"</StackPanel.RenderTransform>"):NULL;
    if(!resource_end || !effects_end || !effect_transform_end || resource_end>=full || effects_end>=button_x || effect_transform_end>=effects_end)return 0;
    span(&w,at,(size_t)(resource_end-at));at=resource_end;put(&w,photo_vehicle_style);
    const char *states[]={"<VisualState x:Name=\"EffectsModeOff\">",
        "<VisualState x:Name=\"EffectsModeOn\">","<VisualState x:Name=\"PreSaveMode\">"};
    const char *inserts[3];
    for(unsigned i=0;i<3;++i){
        const char *state=text_find_unique(src,length,states[i]);
        inserts[i]=state?text_find(state,length-(size_t)(state-src),"<Storyboard>"):NULL;
        if(!inserts[i])return 0;
        inserts[i]+=text_length("<Storyboard>");
    }
    /* Insertions and the ring edit follow the native document order. */
    for(unsigned i=0;i<4;++i){
        const char *next=i<2?inserts[i]:i==2?visible:inserts[2];
        if(next<at)return 0;
        span(&w,at,(size_t)(next-at));at=next;
        if(i==2){put(&w,"{x:Static Visibility.Hidden}");at+=text_length("{x:Static Visibility.Visible}");}
        else {
            put(&w,"<ObjectAnimationUsingKeyFrames Storyboard.TargetProperty=\"(UIElement.Visibility)\" Storyboard.TargetName=\"ButtonHideUI\"><DiscreteObjectKeyFrame KeyTime=\"0\" Value=\"{x:Static Visibility.");
            put(&w,i==3?"Collapsed":"Visible");
            put(&w,"}\"/></ObjectAnimationUsingKeyFrames>");

        }
    }
    /* Constrain only the normal Focus prompt to its own initial localized
       width. Effects Mode removes the cap, so Reset to Default can expand.
       This prevents the observed 135 -> 243 -> 243 retained-width sequence. */
    const char *header_width=text_find_unique(src,length,"Width=\"{Binding ActualWidth, ElementName=StackPanel_CameraEffects}\"");
    if(!header_width || header_width<at || header_width>=effects)return 0;
    span(&w,at,(size_t)(header_width-at));
    put(&w,"Width=\"{Binding ActualWidth, ElementName=EoPhotoEffectsItems}\"");
    at=header_width+text_length("Width=\"{Binding ActualWidth, ElementName=StackPanel_CameraEffects}\"");
    span(&w,at,(size_t)(effects-at));at=effects;
    /* Keep the native storyboard target on the viewport itself. Moving only
       the contents leaves the scrollbar visible when the panel slides out. */
    put(&w,"<ScrollViewer x:Name=\"StackPanel_CameraEffects\" Canvas.Right=\"95\" Canvas.Top=\"228\" Width=\"294\" Height=\"730\" IsEnabled=\"False\" KeyboardNavigation.DirectionalNavigation=\"Contained\" Focusable=\"False\" CanContentScroll=\"True\" ClipToBounds=\"True\" VerticalScrollBarVisibility=\"Auto\" HorizontalScrollBarVisibility=\"Disabled\"><ScrollViewer.RenderTransform>");
    at=effect_transform+text_length("<StackPanel.RenderTransform>");
    span(&w,at,(size_t)(effect_transform_end-at));
    at=effect_transform_end+text_length("</StackPanel.RenderTransform>");
    put(&w,"</ScrollViewer.RenderTransform><StackPanel x:Name=\"EoPhotoEffectsItems\" Width=\"274\" HorizontalAlignment=\"Left\">");
    span(&w,at,(size_t)(effects_end-at));at=effects_end;
    const VideoOptionsText *text=labels();char light_label[256];
    if(!WideCharToMultiByte(CP_UTF8,0,text->photo_lights,-1,light_label,sizeof(light_label),NULL,NULL))return 0;
    for(unsigned i=0;i<2;++i){
        /* Share the existing race-photo availability binding: it already
           excludes showroom Photo Mode and refreshes after camera setup. */
        put(&w,"<local:SpinnerControl Height=\"66\" Margin=\"0,0,0,2\" Visibility=\"{Binding EoDamageResetAvailable, Converter={StaticResource BoolVisConverter}}\" Style=\"{StaticResource EoPhotoVehicleSpinner}\" Label=\"");
        escaped(&w,i?light_label:text->driver);
        put(&w,i?"\" SelectedIndex=\"{Binding EoPhotoLights}\" ItemsSource=\"{Binding EoPhotoLightChoices}\"/>":"\" SelectedIndex=\"{Binding EoPhotoDriver}\" ItemsSource=\"{Binding EoPhotoDriverChoices}\"/>");
    }
    effects_end+=text_length("</StackPanel>");span(&w,at,(size_t)(effects_end-at));at=effects_end;put(&w,"</ScrollViewer>");
    const char *name_end=button_x+text_length("<local:ControllerButtonControl x:Name=\"ButtonX\"");
    span(&w,at,(size_t)(name_end-at));at=name_end;
    put(&w," MaxWidth=\"{Binding EoPhotoFocusMaxWidth}\"");
    /* Preserve FM5's own footer sizing and state-dependent prompt order.
       Append our controls after all native prompts; Hide UI stays last
       even when X changes to Reset to Default or the effects-only RB appears. */
    if(panel_close<at)return 0;
    span(&w,at,(size_t)(panel_close-at));at=panel_close;
    put(&w,"<local:ControllerButtonControl x:Name=\"ButtonResetDamage\" Button=\"LBumper\" Visibility=\"{Binding EoDamageResetAvailable, Converter={StaticResource BoolVisConverter}}\" Description=\"RESET DAMAGE\"/>");
    put(&w,"<local:ControllerButtonControl x:Name=\"ButtonHideUI\" Button=\"Back\" Description=\"{local:Loc HelpButtons.IDS_HideUI}\"/>");
    span(&w,at,length-(size_t)(at-src));return w.ok?w.length:0;
}

/* Hide only the pre/post-race and Free Play upsell banner, not acceleration status, rewards,
   the Y action, or banners on unrelated screens. Runs only when loading XAML. */
static int hide_level_faster;
void fm5_camera_menu_hide_level_faster(int hide){hide_level_faster=hide;}
static int race_banner_resource(const char *src,size_t length){
    return hide_level_faster &&
        (text_find(src,length,"x:Class=\"ForzaUI.F920_PreRace\"") ||
         text_find(src,length,"x:Class=\"ForzaUI.F930_PostRace\"") ||
         text_find(src,length,"x:Class=\"ForzaUI.F300_PlaygroundPlayerLobby\""));
}
static size_t race_banner_transform(const char *src,size_t length,char *out,size_t capacity){
    if(!race_banner_resource(src,length))return 0;
    const char *control=text_find_unique(src,length,"<Control Style=\"{StaticResource AcceleratorBannerControlStyle}\"");
    if(!control)return 0;
    const char *end=text_find(control,length-(size_t)(control-src),"/>");
    if(!end)return 0;
    end+=2;
    Writer w={out,0,capacity,1};
    span(&w,src,(size_t)(control-src));span(&w,end,length-(size_t)(end-src));
    return w.ok?w.length:0;
}

static int hide_drivatar_designs;
void fm5_camera_menu_hide_drivatar_designs(int hide){hide_drivatar_designs=hide;}

static int pause_designs_resource(const char *src,size_t length){
    return hide_drivatar_designs &&
        text_find(src,length,"x:Class=\"ForzaUI.Scenes.WPF.Pause\"")!=NULL;
}

static size_t pause_designs_transform(const char *src,size_t length,char *out,size_t capacity){
    if(!pause_designs_resource(src,length))return 0;
    const char *end=src+length;
    const char *button=text_find(src,length,"<Button x:Name=\"Button_DrivatarLiveries\"");
    const char *button_end=button?text_find(button,(size_t)(end-button),"/>"):NULL;
    if(!button_end)return 0;
    button_end+=2;

    // Remove the shortcut and its visibility animations together so the
    // native storyboards never target a missing button.
    const char *animation="<ObjectAnimationUsingKeyFrames Storyboard.TargetProperty=\"(UIElement.Visibility)\" Storyboard.TargetName=\"Button_DrivatarLiveries\">";
    const char *close="</ObjectAnimationUsingKeyFrames>";
    const char *cursor=src;
    Writer w={out,0,capacity,1};
    for(;;){
        const char *track=text_find(cursor,(size_t)(end-cursor),animation);
        if(button && (!track || button<track)){
            span(&w,cursor,(size_t)(button-cursor));
            cursor=button_end;
            button=NULL;
        }else if(track){
            const char *track_end=text_find(track,(size_t)(end-track),close);
            if(!track_end)return 0;
            span(&w,cursor,(size_t)(track-cursor));
            cursor=track_end+text_length(close);
        }else break;
    }
    span(&w,cursor,(size_t)(end-cursor));
    return w.ok?w.length:0;
}

static void *delete_resource(Resource *resource,unsigned flags){
    cleanup_object(resource);if(flags&1)free_object(resource);return resource;
}
static void **get_resource(void *reader,void **out) {
    /* This getter is shared by both file-backed and application-provided
       resources. Preserve its original smart-pointer ABI before filtering by
       the exact native ByteArray type; other folded getter uses stay native. */
    Resource *original=*(Resource **)((unsigned char *)reader+0x38);
    *out=original;if(original)++*(unsigned *)((unsigned char *)original+0x10);
    if(!original || original->vtable!=(void **)(image+0x2325760))return out;
    if(!original->bytes || original->length<=0 || original->length>196608)return out;
    size_t prefix=original->length<4096?original->length:4096;
    int photo=text_find(original->bytes,prefix,"x:Class=\"ForzaUI.F730_PhotoMode\"")!=NULL;
    int hud=text_find(original->bytes,prefix,"x:Class=\"ForzaUI.Scenes.WPF.HudOptions\"")!=NULL;
    int manufacturer=fm5_manufacturer_resource(original->bytes,prefix);
    int video=text_find(original->bytes,prefix,"x:Class=\"ForzaUI.F866_VideoOptions\"")!=NULL;
    int audio=text_find(original->bytes,prefix,"x:Class=\"ForzaUI.F865_AudioOptions\"")!=NULL;
    int race=race_banner_resource(original->bytes,prefix);
    int start=fm5_start_resource(original->bytes,prefix);
    int pause=pause_designs_resource(original->bytes,prefix);
    if(!photo && !hud && !manufacturer && !video && !race && !audio && !start && !pause)return out;
    Resource *replacement=allocate(sizeof(Resource)+XAML_LIMIT);if(!replacement)return out;
    char *bytes=(char *)(replacement+1);
    size_t length=(pause?pause_designs_transform:start?fm5_start_xaml:audio?fm5_audio_xaml:race?race_banner_transform:video?fm5_video_xaml:manufacturer?fm5_manufacturer_xaml:photo?photo_transform:transform)(original->bytes,original->length,bytes,XAML_LIMIT);
    if(!length || (hud && !register_properties())){
        if(!(reported&1) && log_message)log_message("HUD: camera page matched but extension failed; original resource retained.\r\n");
        reported|=1;free_object(replacement);return out;
    }
    unsigned bit=pause?1024:start?512:audio?256:race?128:video?64:manufacturer?32:photo?16:2;
    if(!(reported&bit) && log_message)log_message(pause?"UI: race pause menu Drivatar Designs shortcut hidden.\r\n":start?"START: native E3 splash layout/animations enabled with retail sign-in flow.\r\n":audio?"AUDIO: native mixer channel volume controls added.\r\n":race?
        "UI: pre/post-race and Free Play level-faster banner hidden.\r\n":video?
        "VIDEO: native Video resource extended with brightness and livery settings.\r\n":manufacturer?
        "MANUFACTURERS: native Buy Cars/popup resource extended.\r\n":photo?
        "PHOTO: native Hide UI prompt added; focus ring hidden with UI.\r\n":
        "HUD: camera controls added to the native FM5 HUD resource.\r\n");
    reported|=bit;
    initialize_object(replacement);
    memory_copy(resource_vtable,original->vtable-1,sizeof(resource_vtable));
    resource_vtable[1]=(void *)delete_resource;replacement->vtable=resource_vtable+1;
    replacement->bytes=bytes;replacement->length=(int)length;replacement->capacity=XAML_LIMIT;
    *out=replacement;release(original);return out;
}
static void reset_hud(void *page){
    ((void (*)(void *))(image+0x35d920))(page);
    /* Respect disabled feature groups when resetting unrelated native HUD fields. */
    if(registered)for(unsigned i=0;i<FM5_CAMERA_SETTING_COUNT;++i)if(i!=FM5_COCKPIT_BLUR && enabled(i)){
        static const int defaults[]={1,35,70,80,70,55,65,96,96,96,96,96,1};
        fm5_camera_set(i,defaults[i]);property_changed(page,properties+i);
    }
    if(fm5_driver_select(0))property_changed(page,player_properties);
}
static void bridge(Fm5Site *sites,unsigned thunk,unsigned call,unsigned original,void *callback){
    sites[0]=(Fm5Site){.rva=thunk,.size=12,.replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    for(unsigned i=0;i<12;++i)sites[0].original[i]=0xcc;
    memory_copy(sites[0].replacement+2,&callback,8);
    sites[1]=(Fm5Site){.rva=call,.size=5,.original={0xe8},.replacement={0xe8}};
    int32_t a=(int32_t)(original-call-5),b=(int32_t)(thunk-call-5);
    memory_copy(sites[1].original+1,&a,4);memory_copy(sites[1].replacement+1,&b,4);
}
/* UI invalidation during PreSave/Watermark/capture must not replace the
   completed full-sample photo with the one-sample preview. The explicit capture
   request, camera movement and effects invalidation remain native. */
static void restart_capture(void *object,int preview){
    unsigned char *page=object;unsigned state=*(unsigned *)(page+0xba4);
    if(state>=3 && state<=6)return;
    void *manager=NULL,*world=NULL,*renderer=NULL;
    if(!memory_read_pointer(image+0x25d0398,&manager) || !manager ||
       !memory_read_pointer((unsigned char *)manager+0x28,&world) || !world ||
       !memory_read_pointer((unsigned char *)world+0x730,&renderer) || !renderer)return;
    unsigned count=1;
    if(!preview){
        unsigned char *samples=((void *(*)(void))(image+0x6c40c0))();
        if(!samples)return;
        ((void (*)(void *))(image+0x6c41a0))(samples);
        count=(unsigned)((*(uintptr_t *)(samples+0x10)-*(uintptr_t *)(samples+8))/16);
    }
    void *interface=(unsigned char *)renderer+0x10;
    ((void (*)(void *,unsigned,unsigned,unsigned char))(*(void ***)interface)[0x98/8])(interface,count,5,0);
    page[0xbd5]=(unsigned char)preview;
}
static void restart_capture_preview(void *page){restart_capture(page,1);}
static void restart_capture_settled(void *page){restart_capture(page,0);}
static unsigned capture_sites(Fm5Site *sites){
    unsigned count=0;
    {
        static const unsigned char original[]={
            0x48,0x8b,0x05,0x34,0x20,0x25,0x02,0x45,0x33,0xc9,0x48,0x8b,0x48,0x28,0x41,0x8d,
            0x51,0x01,0x45,0x8d,0x41,0x05,0x48,0x8b,0x89,0x30,0x07,0x00,0x00,0x48,0x8b,0x41,
            0x10,0x48,0x83,0xc1,0x10,0xff,0x90,0x98,0x00,0x00,0x00,0xc6,0x86,0xd5,0x0b,0x00,
            0x00,0x01,
        };
        unsigned char replacement[sizeof(original)];
        for(unsigned i=0;i<sizeof(replacement);++i)replacement[i]=0x90;
        static const unsigned char call[]={0x48,0x8b,0xce,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0};
        void *callback=(void *)restart_capture_preview;memory_copy(replacement,call,sizeof(call));
        memory_copy(replacement+5,&callback,8);
        for(unsigned offset=0;offset<sizeof(original);offset+=16){
            unsigned size=(unsigned)sizeof(original)-offset;if(size>16)size=16;
            Fm5Site *site=sites+count++;*site=(Fm5Site){.rva=0x37e35d+offset,.size=size};
            memory_copy(site->original,original+offset,size);memory_copy(site->replacement,replacement+offset,size);
        }
    }
    {
        static const unsigned char original[]={
            0x48,0x8b,0x05,0xd8,0x1f,0x25,0x02,0x48,0x89,0x7c,0x24,0x50,0x48,0x8b,0x58,0x28,
            0xe8,0xf2,0x5c,0x34,0x00,0x48,0x8b,0xc8,0x48,0x8b,0xf8,0xe8,0xc7,0x5d,0x34,0x00,
            0x48,0x8b,0x8b,0x30,0x07,0x00,0x00,0x48,0x8b,0x57,0x10,0x48,0x2b,0x57,0x08,0x4c,
            0x8b,0x51,0x10,0x48,0x83,0xc1,0x10,0x45,0x33,0xc9,0x48,0xc1,0xfa,0x04,0x45,0x8d,
            0x41,0x05,0x41,0xff,0x92,0x98,0x00,0x00,0x00,0x48,0x8b,0x7c,0x24,0x50,0x48,0x8b,
            0xce,0xc6,0x86,0xd5,0x0b,0x00,0x00,0x00,
        };
        unsigned char replacement[sizeof(original)];
        for(unsigned i=0;i<sizeof(replacement);++i)replacement[i]=0x90;
        static const unsigned char call[]={0x48,0x8b,0xce,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0};
        void *callback=(void *)restart_capture_settled;memory_copy(replacement,call,sizeof(call));
        memory_copy(replacement+5,&callback,8);
        /* The following epilogue tail-calls Scene.Update with RCX=RSI. */
        memory_copy(replacement+sizeof(replacement)-3,call,3);
        for(unsigned offset=0;offset<sizeof(original);offset+=16){
            unsigned size=(unsigned)sizeof(original)-offset;if(size>16)size=16;
            Fm5Site *site=sites+count++;*site=(Fm5Site){.rva=0x37e3b9+offset,.size=size};
            memory_copy(site->original,original+offset,size);memory_copy(site->replacement,replacement+offset,size);
        }
    }
    return count;
}
unsigned fm5_camera_menu_prepare(unsigned char *game,Fm5Site *sites,int drift,int fov,void (*log)(const char *)){
    image=game;drift_enabled=drift;fov_enabled=fov;registered=0;choices=NULL;photo_properties=0;
    log_message=log;reported=0;
    allocate=(void *(*)(size_t))(image+0x1cdbb0);free_object=(void (*)(void *))(image+0x1cdc30);
    initialize_object=(void (*)(void *))(image+0x9045f0);cleanup_object=(void (*)(void *))(image+0x9046f0);

    property_changed=(void (*)(void *,void *))(image+0x3c02a0);
    /* Replace the pure 12-byte getter prefix, not one caller overload. Its
       complete behavior is reproduced above; no trampoline or recursion. */
    sites[0]=(Fm5Site){.rva=0x40d690,.size=12,
        .original={0x48,0x8b,0x41,0x38,0x48,0x89,0x02,0x48,0x85,0xc0,0x74,0x03},
        .replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    void *callback=(void *)get_resource;memory_copy(sites[0].replacement+2,&callback,8);
    unsigned count=1;
    count+=capture_sites(sites+count);
    bridge(sites+count,0x37dda2,0x37dd7c,0x3812e0,(void *)photo_register);count+=2;
    bridge(sites+count,0x3818f3,0x37dd84,0x3814f0,(void *)photo_help);count+=2;
    Fm5Site *tick=sites+count++;*tick=(Fm5Site){.rva=0x2259df8,.size=8};
    void *native_tick=image+0x37e100,*custom_tick=(void *)photo_tick;
    memory_copy(tick->original,&native_tick,8);memory_copy(tick->replacement,&custom_tick,8);
    Fm5Site *close=sites+count++;*close=(Fm5Site){.rva=0x2259e20,.size=8};
    void *native_close=image+0x37e050,*custom_close=(void *)photo_close;
    memory_copy(close->original,&native_close,8);memory_copy(close->replacement,&custom_close,8);
    /* Keep the native downward ground query and altitude store. Skip only
       the swept movement collision and ceiling correction; neither may move
       the camera. The old whole-block debug bypass left altitude at 1e9. */
    sites[count++]=(Fm5Site){.rva=0x53d691,.size=6,.original={0x0f,0x85,0x2f,3,0,0},.replacement={0x90,0x90,0x90,0x90,0x90,0x90}};
    sites[count++]=(Fm5Site){.rva=0x53d6c5,.size=8,.original={0xc5,0xf8,0x28,0x83,0xf0,1,0,0},.replacement={0xc5,0xf8,0x28,0x9b,0x10,1,0,0}};
    sites[count++]=(Fm5Site){.rva=0x53d6cd,.size=6,.original={0xc5,0xf8,0x29,0x44,0x24,0x30},.replacement={0xe9,0x52,0,0,0,0x90}};
    /* Extend only the telemetry ray, not camera movement bounds. */
    Fm5Site *ray=sites+count++;*ray=(Fm5Site){.rva=0x53d738,.size=8,
        .original={0xc5,0xfa,0x59,0x0d,0x88,0x0f,0x7a,1},.replacement={0xc5,0xfa,0x10,0x0d}};
    int32_t ray_displacement=(int32_t)(0x10d9258-0x53d740);
    memory_copy(ray->replacement+4,&ray_displacement,4);
    sites[count++]=(Fm5Site){.rva=0x53d8cc,.size=6,.original={0x0f,0x84,0x97,0,0,0},.replacement={0xe9,0x98,0,0,0,0x90}};
    /* Report the true signed height; the native display clamps it to the
       camera's former movement range, which no longer applies. */
    sites[count++]=(Fm5Site){.rva=0x53d979,.size=6,.original={0xc5,0xf8,0x2f,0xd6,0x73,8},.replacement={0xc5,0xf8,0x28,0xd3,0xeb,0x24}};
    sites[count++]=(Fm5Site){.rva=0x53567c,.size=2,.original={0x75,0x78},.replacement={0xeb,0x78}};
    sites[count++]=(Fm5Site){.rva=0x5356f6,.size=8,.original={0xc5,0xfa,0x10,0x05,0xca,0x13,0xce,1},.replacement={0xeb,0x59,0x90,0x90,0x90,0x90,0x90,0x90}};
    bridge(sites+count,0xb64b21,0x3acc1e,0x35d920,(void *)reset_hud);count+=2;
    return count;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) size_t Fm5TestRaceBannerXaml(const char *src,size_t length,char *out,size_t capacity,int hide){hide_level_faster=hide;return race_banner_transform(src,length,out,capacity);}
__declspec(dllexport) size_t Fm5TestPauseDesignsXaml(const char *src,size_t length,char *out,size_t capacity,int hide){hide_drivatar_designs=hide;return pause_designs_transform(src,length,out,capacity);}
__declspec(dllexport) size_t Fm5TestCameraXaml(const char *src,size_t length,char *out,size_t capacity){return transform(src,length,out,capacity);}
__declspec(dllexport) size_t Fm5TestPhotoXaml(const char *src,size_t length,char *out,size_t capacity){return photo_transform(src,length,out,capacity);}
__declspec(dllexport) void *Fm5TestPhotoFocusProperty(void){return &photo_focus_limit;}
__declspec(dllexport) void *Fm5TestPlayerProperty(unsigned i){return i<2?player_properties+i:NULL;}
__declspec(dllexport) int Fm5TestCameraRegister(void){return register_properties();}
__declspec(dllexport) void *Fm5TestCameraProperty(unsigned i){return i<ARRAY_COUNT(properties)?properties+i:NULL;}
#endif
