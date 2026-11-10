#include "fm5/fm5_audio.h"
#include "fm5/fm5_settings.h"
#include "shared/support.h"

typedef struct {const char *name,*label,*description;uint64_t guid[2];} Channel;
#include "fm5/fm5_audio_channels.h"
typedef struct {void **vtable;unsigned char header[0x20];void *get,*set,*unused;const char *name;void *type;uint64_t flags;} Property;
typedef struct Link {Property *property;struct Link *next;} Link;
static unsigned char *image;
static Property props[FM5_AUDIO_COUNT];static Link links[FM5_AUDIO_COUNT];
static int registered,snapshot[FM5_AUDIO_COUNT];
static volatile LONG audio_active,audio_revision;
static LONG applied_revision;
#define values (fm5_settings_values()+FM5_SAVED_AUDIO)

/* Runs inside FMOD's existing mixer operation, after its authored automation.
   No UI-thread FMOD calls, retained bus handles, allocation or disk I/O. A bus
   fader attenuates its own subtree, so native ducking and music switches remain
   effective, and zero is actual silence rather than a parameter's dB floor. */
static int channel_index(void *bus){
    void *description=*(void **)((unsigned char *)bus+0xb0);
    if(!description)return -1;
    uint64_t id[2]={0};
    const void *identity=((void *(*)(void *,void *))(*(void ***)description)[3])(description,id);
    if(!identity)return -1;
    memory_copy(id,identity,sizeof(id));
    for(unsigned i=0;i<FM5_AUDIO_COUNT;++i)if(id[0]==channels[i].guid[0] && id[1]==channels[i].guid[1])return (int)i;
    return -1;
}
static int mixer_gain(void *group,float gain,void *bus){
    if(audio_active && bus){
        int i=channel_index(bus);
        if(i>=0)gain*=((const volatile int *)values)[i]*.01f;
    }
    return ((int (*)(void *,float))(image+0xcbd0e0))(group,gain);
}
/* DD7CD0 runs on every studio update. Refresh only after a setting change,
   using the same existing-bus lookup as native DD80A0. The +338 list is a
   temporary format-change queue, not the collection of live buses.
   Newly created/automated groups also pass through mixer_gain above. */
static int update_bus(void *system){
    int result=((int (*)(void *))(image+0xdd7cd0))(system);
    LONG revision=audio_revision;
    if(!result && audio_active && revision!=applied_revision){
        for(unsigned i=0;i<FM5_AUDIO_COUNT;++i){
            void *bus=((void *(*)(void *,const void *,int))(image+0xdbb670))(
                (unsigned char *)system+0xb8,channels[i].guid,-1);
            if(bus)((int (*)(void *))(image+0xdba3e0))(bus);
        }
        applied_revision=revision;
    }
    return result;
}
static void **get(void **out,Property *property,void *page){
    (void)page;unsigned i=(unsigned)(property-props);*out=NULL;
    if(i<FM5_AUDIO_COUNT){float v=values[i]*.01f;((void **(*)(void **,const float *))(image+0x296a30))(out,&v);}return out;
}
static void set(Property *property,void *page,void *boxed){
    unsigned i=(unsigned)(property-props);float v;
    if(i>=FM5_AUDIO_COUNT || !boxed || !memory_read((unsigned char *)boxed+0x28,&v,4) || !float_is_finite(v) || v<0 || v>1)return;
    int next=(int)(v*100+.5f);if(next==values[i])return;InterlockedExchange(&audio_active,1);values[i]=next;InterlockedIncrement(&audio_revision);
    ((void (*)(void *,void *))(image+0x3c02a0))(page,props+i);
}
static int register_properties(void){
    if(registered)return 1;
    ((void (*)(void))(image+0x2cda90))();
    Property *native=((Property *(*)(void))(image+0x2ccbe0))();
    Link **head=(Link **)(image+0x245ee30);
    for(unsigned i=0;i<FM5_AUDIO_COUNT;++i){
        ((void (*)(void *))(image+0x9045f0))(props+i);props[i].vtable=native->vtable;
        memory_copy((unsigned char *)(props+i)+0x28,(unsigned char *)native+0x28,0x30);
        props[i].name=channels[i].name;props[i].get=(void *)get;props[i].set=(void *)set;
        links[i]=(Link){props+i,*head};*head=links+i;
    }registered=1;return 1;
}
static void initialize(void *page){memory_copy(snapshot,values,sizeof(snapshot));((void (*)(void *))(image+0x2ce230))(page);}
static unsigned char accept(void *self){fm5_settings_save();return ((unsigned char (*)(void *))(image+0x2ce310))((unsigned char *)self+8);}
static unsigned char cancel(void *self){memory_copy(values,snapshot,sizeof(snapshot));InterlockedIncrement(&audio_revision);return ((unsigned char (*)(void *))(image+0x2ce3a0))((unsigned char *)self+8);}
static unsigned char reset(void *self){
    unsigned char result=((unsigned char (*)(void *))(image+0x2eea80))(self);
    void *page=*(void **)((unsigned char *)self+8);
    for(unsigned i=0;i<FM5_AUDIO_COUNT;++i){values[i]=100;((void (*)(void *,void *))(image+0x3c02a0))(page,props+i);}InterlockedIncrement(&audio_revision);return result;
}
typedef struct {char *out;size_t n,cap;int ok;} Writer;
static void span(Writer *w,const char *s,size_t n){if(n>=w->cap-w->n){w->ok=0;return;}memory_copy(w->out+w->n,s,n);w->n+=n;w->out[w->n]=0;}
static void put(Writer *w,const char *s){span(w,s,text_length(s));}
size_t fm5_audio_xaml(const char *s,size_t n,char *out,size_t cap){
    if(!s || text_find(s,n,"EoAudioMusic") || !text_find(s,n,"x:Class=\"ForzaUI.F865_AudioOptions\""))return 0;
    const char *tr=text_find_unique(s,n,"<Page.Triggers>");
    const char *stack=text_find_unique(s,n,"<StackPanel x:Name=\"StackPanel_OptionsList\"");
    const char *row=text_find_unique(s,n,"<Grid x:Name=\"Grid_HeaderCategory\"");
    const char *end=stack?text_find(stack,n-(size_t)(stack-s),"</StackPanel>"):NULL;
    if(!tr || !stack || !row || !end || !(tr<stack && stack<row && row<end) || !register_properties())return 0;
    Writer w={out,0,cap,1};const char *at=s;
#define COPY(p) do{span(&w,at,(size_t)((p)-at));at=(p);}while(0)
    tr+=text_length("<Page.Triggers>");COPY(tr);
    for(unsigned i=0;i<FM5_AUDIO_COUNT;++i){
        put(&w,"<EventTrigger RoutedEvent=\"Keyboard.GotKeyboardFocus\" SourceName=\"");put(&w,channels[i].name);
        put(&w,"\"><BeginStoryboard><Storyboard><StringAnimationUsingKeyFrames Storyboard.TargetName=\"TextBlock_FocusedDescription\" Storyboard.TargetProperty=\"Text\"><DiscreteStringKeyFrame KeyTime=\"0\" Value=\"");
        put(&w,channels[i].description);put(&w," - Adjust the volume of this sound category. Set to 0 to mute or 100 for its original volume.\"/></StringAnimationUsingKeyFrames></Storyboard></BeginStoryboard></EventTrigger>");
    }
    COPY(stack);put(&w,"<ScrollViewer Grid.Row=\"1\" Grid.Column=\"1\" Height=\"590\" Focusable=\"False\" CanContentScroll=\"True\" ClipToBounds=\"True\" VerticalScrollBarVisibility=\"Auto\" HorizontalScrollBarVisibility=\"Disabled\">");
    COPY(row);
    for(unsigned i=0;i<FM5_AUDIO_COUNT;++i){
        put(&w,"<Grid Height=\"35\" Margin=\"0,2,0,0\"><Slider x:Name=\"");put(&w,channels[i].name);
        put(&w,"\" Value=\"{Binding ");put(&w,channels[i].name);put(&w,"}\" Minimum=\"0\" Maximum=\"1\" SmallChange=\"0.01\" Style=\"{StaticResource AudioAdjustSliderStyle}\"/>");
        put(&w,"<TextBlock Text=\"");put(&w,channels[i].label);put(&w,"\" IsHitTestVisible=\"False\" Style=\"{StaticResource AudioSliderLabelTextBlockStyle}\" Tag=\"{Binding IsFocused, ElementName=");put(&w,channels[i].name);put(&w,"}\"/>");
        put(&w,"<TextBlock HorizontalAlignment=\"Right\" TextAlignment=\"Right\" IsHitTestVisible=\"False\" Text=\"{Binding ");put(&w,channels[i].name);
        put(&w,", ConverterParameter=fracpercentage, Converter={StaticResource AudioOptionsNumUnitConverter}}\" Style=\"{StaticResource AudioSliderLabelTextBlockStyle}\" Tag=\"{Binding IsFocused, ElementName=");put(&w,channels[i].name);put(&w,"}\"/></Grid>");
    }
    end+=text_length("</StackPanel>");COPY(end);put(&w,"</ScrollViewer>");COPY(s+n);
#undef COPY
    return w.ok?w.n:0;
}
static void pointer_site(Fm5Site *s,unsigned rva,unsigned original,void *replacement){*s=(Fm5Site){.rva=rva,.size=8};void *p=image+original;memory_copy(s->original,&p,8);memory_copy(s->replacement,&replacement,8);}
unsigned fm5_audio_prepare(unsigned char *game,Fm5Site *sites){
    image=game;registered=0;audio_active=0;audio_revision=1;applied_revision=0;
    for(unsigned i=0;i<FM5_AUDIO_COUNT;++i)if(values[i]!=100)audio_active=1;
    memory_copy(snapshot,values,sizeof(snapshot));unsigned count=0;
    pointer_site(sites+count++,0x2237218,0x2ce230,(void *)initialize);
    pointer_site(sites+count++,0x2230140,0x2ee9c0,(void *)accept);
    pointer_site(sites+count++,0x2230ad0,0x2eea20,(void *)cancel);
    pointer_site(sites+count++,0x222ca78,0x2eea80,(void *)reset);
    /* All four authored/automated bus-gain paths retain the bus in RSI. */
    Fm5Site *t=sites+count++;*t=(Fm5Site){.rva=0x138550,.size=16,.replacement={0x49,0x89,0xf0,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0,0x90}};
    for(unsigned i=0;i<16;++i)t->original[i]=0xcc;void *fn=(void *)mixer_gain;memory_copy(t->replacement+5,&fn,8);
    const unsigned calls[]={0xdba4b5,0xdba6d7,0xdbd6b5,0xdbd7e5};
    for(unsigned i=0;i<ARRAY_COUNT(calls);++i){t=sites+count++;*t=(Fm5Site){.rva=calls[i],.size=5,.original={0xe8},.replacement={0xe8}};
        int32_t old=(int32_t)(0xcbd0e0-calls[i]-5),next=(int32_t)(0x138550-calls[i]-5);memory_copy(t->original+1,&old,4);memory_copy(t->replacement+1,&next,4);}
    t=sites+count++;*t=(Fm5Site){.rva=0x138763,.size=12,.replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    for(unsigned i=0;i<12;++i)t->original[i]=0xcc;fn=(void *)update_bus;memory_copy(t->replacement+2,&fn,8);
    t=sites+count++;*t=(Fm5Site){.rva=0xdd9fd0,.size=5,.original={0xe8},.replacement={0xe8}};
    int32_t old=(int32_t)(0xdd7cd0-0xdd9fd5),next=(int32_t)(0x138763-0xdd9fd5);
    memory_copy(t->original+1,&old,4);memory_copy(t->replacement+1,&next,4);
    return count;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) size_t Fm5TestAudioXaml(const char *s,size_t n,char *out,size_t cap){return fm5_audio_xaml(s,n,out,cap);}
__declspec(dllexport) unsigned Fm5TestAudioPrepare(unsigned char *game,Fm5Site *sites){return fm5_audio_prepare(game,sites);}
__declspec(dllexport) void *Fm5TestAudioProperty(unsigned i){return i<FM5_AUDIO_COUNT?props+i:NULL;}
__declspec(dllexport) int Fm5TestAudioUpdate(void *bus){return update_bus(bus);}
__declspec(dllexport) int Fm5TestAudioGain(void *group,float gain,void *bus){return mixer_gain(group,gain,bus);}
#endif

