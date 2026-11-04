#include "driver_options.h"
#include "photo_environment.h"
#include "addon_database.h"
#include "autodrive_game.h"
#include "video_settings.h"
#include "video_options_text.h"
#include "support.h"
#include "logging.h"

typedef struct {char data[16];size_t length,capacity;} GameString;
typedef struct Model {struct Model *next;int id;char media[128];wchar_t label[128];} Model;
static unsigned char *image;
static Model *models;
static unsigned model_count=1,revision;
static SRWLOCK lock=SRWLOCK_INIT;
static LONG photo_off;
static int visibility_ready;
static LONG reported_model=-1;

unsigned driver_model_revision(void){return revision;}
unsigned driver_model_count(void){return model_count+1;}
static Model *at(unsigned index){Model *m=models;while(m && --index)m=m->next;return m;}
int driver_model_index(int id){
    if(!id)return 0;
    AcquireSRWLockShared(&lock);
    unsigned index=1;Model *m=models;
    if(!m){ReleaseSRWLockShared(&lock);return id==1?1:0;}
    while(m && m->id!=id){m=m->next;++index;}
    ReleaseSRWLockShared(&lock);return m?(int)index:0;
}
int driver_model_id(unsigned index){
    if(!index)return 0;
    AcquireSRWLockShared(&lock);Model *m=at(index);
    int id=m?m->id:(!models && index==1?1:-1);
    ReleaseSRWLockShared(&lock);return id;
}
void driver_model_label(unsigned index,wchar_t output[128]){
    AcquireSRWLockShared(&lock);Model *m=index?at(index):NULL;
    const wchar_t *label=m?m->label:index?L"Stig":L"Default";
    size_t i=0;for(;i<127 && label[i];++i)output[i]=label[i];output[i]=0;
    ReleaseSRWLockShared(&lock);
}
void driver_models_database(void *database){
    const AddonSqlite *api=addon_database_api();void *statement=NULL;
    if(api->prepare(database,"SELECT Id,MediaName FROM DriverModels WHERE Id>0 ORDER BY CASE WHEN lower(MediaName)='stig' THEN 0 ELSE 1 END,Id",-1,&statement,NULL))return;
    Model *first=NULL,**tail=&first;unsigned count=0;
    while(api->step(statement)==100){
        int64_t id=api->column_int64(statement,0);const char *media=api->column_text(statement,1);
        size_t len=media?text_length(media):0;
        if(id<=0 || id>0x7fffffff || !len || len>=128)continue;
        Model *m=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*m));if(!m)break;
        m->id=(int)id;memory_copy(m->media,media,len+1);
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,media,-1,m->label,128)){
            HeapFree(GetProcessHeap(),0,m);continue;
        }
        for(unsigned i=0;m->label[i];++i)if(m->label[i]=='_')m->label[i]=' ';
        if(m->label[0]>='a' && m->label[0]<='z')m->label[0]-='a'-'A';
        *tail=m;tail=&m->next;++count;
    }
    api->finalize(statement);
    AcquireSRWLockExclusive(&lock);Model *old=models;models=first;model_count=count;++revision;
    ReleaseSRWLockExclusive(&lock);
    while(old){Model *next=old->next;HeapFree(GetProcessHeap(),0,old);old=next;}
    log_append("DRIVER: player character choices read from the merged DriverModels table.\r\n");
}

int driver_photo_get(void){return InterlockedCompareExchange(&photo_off,0,0)!=0;}
int driver_photo_set(int off){
    if(off<0 || off>1 || !visibility_ready)return 0;
    unsigned char *branch=image+0x139bf90;
    // Only change the visibility branch. The original character visibility and
    // all driver resources remain owned and updated by the game.
    if(branch[1]!=0x2d || (branch[0]!=0x74 && branch[0]!=0xeb))return 0;
    DWORD prior,ignored;
    if(!VirtualProtect(branch,1,PAGE_EXECUTE_READWRITE,&prior))return 0;
    *branch=off?0xeb:0x74;
    FlushInstructionCache(GetCurrentProcess(),branch,1);
    VirtualProtect(branch,1,prior,&ignored);
    return InterlockedExchange(&photo_off,off)!=off;
}
void driver_photo_reset(void){driver_photo_set(0);}

static int is_player_request(unsigned char *car){
    // The native local-driver predicate is available during construction,
    // before the player's new car has been published by the player manager.
    // It checks car roles 0/3 and the owning component's local-driver index.
    if(((unsigned char (*)(void *))(image+0xf6b040))(car))return 1;
    unsigned char *player=autodrive_game_get_car();if(!player)return 0;
    void *owner=NULL,*local_owner=NULL;
    // Cinematic copies keep the same owning player as the gameplay car.
    memory_read_pointer(car+0x4d00,&owner);
    memory_read_pointer(player+0x4d00,&local_owner);
    int result=car==player || (owner && owner==local_owner);
    autodrive_game_release_car(player);return result;
}
static void build_car(unsigned char *car,unsigned char *out,void *request,unsigned char flag){
    ((void (*)(void*,void*,void*,unsigned char))(image+0xf67dc0))(car,out,request,flag);
    int selected=video_setting_get(PLAYER_CHARACTER);
    if(!selected || !is_player_request(car))return;
    char name[128]={0};
    AcquireSRWLockShared(&lock);
    if(!models && selected==1)memory_copy(name,"stig",5);
    else for(Model *m=models;m;m=m->next)if(m->id==selected){memory_copy(name,m->media,128);break;}
    ReleaseSRWLockShared(&lock);
    if(!name[0])return;
    size_t len=text_length(name);
    // This copies into native transient render parameters, never into a save,
    // car record, AI driver record or the database itself.
    GameString source={0};const char *ptr=name;
    memory_copy(source.data,&ptr,sizeof(ptr));source.length=len;source.capacity=len<16?16:len;
    ((void *(*)(void*,const void*,size_t,size_t))(image+0x22c1d0))(out+0x20,&source,0,(size_t)-1);
    if(InterlockedExchange(&reported_model,selected)!=selected){
        char message[176];const char prefix[]="DRIVER: selected player model: ";
        memory_copy(message,prefix,sizeof(prefix)-1);memory_copy(message+sizeof(prefix)-1,name,len);
        memory_copy(message+sizeof(prefix)-1+len,"\r\n",3);log_append(message);
    }
}

// The normal player renderer creates its freeroam driver separately from
// per-frame car parameters. This call site is reached only by the native
// wristbanded/player branch; AIUnsuited uses a different, untouched call.
static void *make_player_asset(void *out,const char *original,size_t length){
    int selected=video_setting_get(PLAYER_CHARACTER);
    char name[128]={0};
    if(selected){
        AcquireSRWLockShared(&lock);
        if(!models && selected==1)memory_copy(name,"stig",5);
        else for(Model *m=models;m;m=m->next)if(m->id==selected){memory_copy(name,m->media,128);break;}
        ReleaseSRWLockShared(&lock);
    }
    if(name[0]){
        static LONG reported_asset=-1;
        if(InterlockedExchange(&reported_asset,selected)!=selected){
            char message[176];const char prefix[]="DRIVER: native player asset request: ";
            size_t n=text_length(name);memory_copy(message,prefix,sizeof(prefix)-1);
            memory_copy(message+sizeof(prefix)-1,name,n);memory_copy(message+sizeof(prefix)-1+n,"\r\n",3);
            log_append(message);
        }
        original=name;length=text_length(name);
    }
    return ((void *(*)(void*,const char*,size_t))(image+0x22c300))(out,original,length);
}

PatchDefinition driver_options_patches[5];
static unsigned char originals[5][12],replacements[5][12];
static const unsigned char *accepted[5][3];
static const unsigned char hidden_branch[2]={0xeb,0x2d};
void driver_options_prepare(unsigned char *base){
    image=base;InterlockedExchange(&photo_off,0);InterlockedExchange(&reported_model,-1);
    static const unsigned char visibility_test[]={0x41,0x80,0xbf,0x38,0x05,0,0,0};
    visibility_ready=memory_equal(base+0x139bf88,visibility_test,sizeof(visibility_test)) &&
        (base[0x139bf90]==0x74 || base[0x139bf90]==0xeb) && base[0x139bf91]==0x2d;
    static const unsigned slots[]={0x13002c3,0x295068,0x139bf90};
    static const unsigned sizes[]={12,5,2};
    for(unsigned i=0;i<3;++i){
        unsigned size=sizes[i];
        if(i==0){
            for(unsigned j=0;j<12;++j)originals[i][j]=0xcc;
            static const unsigned char jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
            uintptr_t cb=(uintptr_t)build_car;memory_copy(replacements[i],jump,12);memory_copy(replacements[i]+2,&cb,8);
        }else if(i==1){
            originals[i][0]=replacements[i][0]=0xe8;
            int32_t before=0xf67dc0-slots[i]-5,after=slots[0]-slots[i]-5;
            memory_copy(originals[i]+1,&before,4);memory_copy(replacements[i]+1,&after,4);
        }else{originals[i][0]=replacements[i][0]=0x74;originals[i][1]=replacements[i][1]=0x2d;}
        accepted[i][0]=originals[i];accepted[i][1]=replacements[i];accepted[i][2]=hidden_branch;
        driver_options_patches[i]=(PatchDefinition){slots[i],0,size,GROUP_VIDEO_OPTIONS,
            "player_character_and_photo_driver",originals[i],replacements[i],replacements[i],accepted[i],i==2?3:2};
    }
    const unsigned thunk=0x1300372,site=0x138ed43;
    for(unsigned i=3;i<5;++i){
        unsigned size=i==3?12:5;
        if(i==3){
            for(unsigned j=0;j<12;++j)originals[i][j]=0xcc;
            static const unsigned char jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
            uintptr_t cb=(uintptr_t)make_player_asset;memory_copy(replacements[i],jump,12);
            memory_copy(replacements[i]+2,&cb,8);
        }else{
            originals[i][0]=replacements[i][0]=0xe8;
            int32_t before=0x22c300-site-5,after=thunk-site-5;
            memory_copy(originals[i]+1,&before,4);memory_copy(replacements[i]+1,&after,4);
        }
        accepted[i][0]=originals[i];accepted[i][1]=replacements[i];
        driver_options_patches[i]=(PatchDefinition){i==3?thunk:site,0,size,GROUP_VIDEO_OPTIONS,
            "player_driver_asset_request",originals[i],replacements[i],replacements[i],accepted[i],2};
    }

}

static int photo_title_xml(unsigned setting,char *out,size_t cap){
    char utf8[256];
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,photo_environment_title(setting),-1,utf8,sizeof(utf8),NULL,NULL))return 0;
    size_t used=0;
    for(unsigned i=0;utf8[i];++i){
        const char *replace=utf8[i]=='&'?"&amp;":utf8[i]=='<'?"&lt;":utf8[i]=='>'?"&gt;":utf8[i]=='\"'?"&quot;":NULL;
        size_t count=replace?text_length(replace):1;
        if(used+count>=cap)return 0;
        memory_copy(out+used,replace?replace:utf8+i,count);used+=count;
    }
    out[used]=0;return 1;
}

// Preserve the shipped control templates; change only their keys/bindings and
// the space needed for an hour readout rather than a two-digit percentage.
static void copy_photo_style(char *out,size_t *used,const char *start,size_t length,
                             const char *const *from,const char *const *to,unsigned count){
    for(size_t i=0;i<length;){
        unsigned j;
        for(j=0;j<count;++j){
            size_t n=text_length(from[j]);
            if(n<=length-i && memory_equal(start+i,from[j],n)){
                size_t m=text_length(to[j]);memory_copy(out+*used,to[j],m);*used+=m;i+=n;break;
            }
        }
        if(j==count)out[(*used)++]=start[i++];
    }
}

int driver_photo_is_xaml(const char *text,size_t length){return text_find(text,length,"x:Class=\"ForzaUI.A_PhotoMode\"")!=NULL;}
size_t driver_photo_xaml(const char *text,size_t length,char *out,size_t capacity){
    static const char style_marker[]="<Style x:Key=\"A_Style_ContentControl_EffectsSpinner\"";
    const char *style=text_find_unique(text,length,style_marker);
    const char *end=style?text_find_unique(style,(text+length)-style,"<!-- Base Content Style -->"):NULL;
    const char *last=text_find_unique(text,length,"DataContext=\"{Binding CurrentSettings.Vignette}\"/>");
    const char *hour_resources=text_find_unique(text,length,"</ResourceDictionary>");
    if(!style || !end || !last || !hour_resources || text_find(text,length,"PhotoDriver"))return 0;
    size_t style_len=(size_t)(end-style),row_at=(size_t)(last-text)+text_length("DataContext=\"{Binding CurrentSettings.Vignette}\"/>");
    static const char row[]=
        "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_PhotomodeDivider}\"/>"
        "<local:SpinnerControl ItemsSource=\"{Binding PhotoDriverChoices}\" SelectedIndex=\"{Binding PhotoDriver}\" Style=\"{StaticResource EO_PhotoDriverSpinner}\"/>"
        "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_PhotomodeDivider}\"/>"
        "<local:SpinnerControl ItemsSource=\"{Binding ShadowChoices}\" SelectedIndex=\"{Binding PhotoLights}\" Style=\"{StaticResource EO_PhotoLightsSpinner}\"/>"
        "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_PhotomodeDivider}\"/>"
        "<local:SpinnerControl ItemsSource=\"{Binding PhotoTimeChoices}\" SelectedIndex=\"{Binding PhotoTimeOfDay}\" Style=\"{StaticResource EO_PhotoTimeSpinner}\"/>"
        "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_PhotomodeDivider}\"/>"
        "<ContentControl Style=\"{StaticResource EO_PhotoHour}\"/>"
        "<local:FZMaterial Style=\"{StaticResource A_Style_FZMaterial_PhotomodeDivider}\"/>"
        "<local:SpinnerControl ItemsSource=\"{Binding PhotoWeatherChoices}\" SelectedIndex=\"{Binding PhotoWeather}\" Style=\"{StaticResource EO_PhotoWeatherSpinner}\"/>";
    if(length+style_len*4+sizeof(row)+4096>=capacity)return 0;
    // Copy the native spinner style with its title changed, rather than add a
    // second control implementation or change the existing preset binding.
    const char *effect_style=text_find_unique(text,length,"<Style x:Key=\"A_Style_ContentControl_PhotoEffects\"");
    const char *effect_height=effect_style?text_find(effect_style,(text+length)-effect_style,"<Setter Property=\"Height\" Value=\"{StaticResource A_Double_EffectsItem_Height}\"/>"):NULL;
    const char *effect_end=effect_style?text_find(effect_style,(text+length)-effect_style,"</Style>"):NULL;
    const char *fill_style=text_find_unique(text,length,"<Style x:Key=\"A_Style_Slider_PhotoEffects\"");
    const char *fill_end=fill_style?text_find(fill_style,(text+length)-fill_style,"</Style>"):NULL;
    if(!effect_height || !effect_end || !fill_end)return 0;
    size_t effect_len=(size_t)(effect_end-effect_style)+8,fill_len=(size_t)(fill_end-fill_style)+8;
    if(length+style_len*4+effect_len+fill_len+sizeof(row)+4096>=capacity)return 0;
    const char *heading=text_find_unique(text,length,"Content=\"{local:Loc InGame.IDS_Label_EffectsMode}\"");
    const char *heading_end=heading?text_find(heading,(text+length)-heading,"/>"):NULL;
    const char *panel_end=text_find(text+row_at,length-row_at,"</StackPanel>");
    if(!heading_end || !panel_end)return 0;
    size_t scroll_at=(size_t)(heading_end-text)+2;
    char time_title[512],weather_title[512],lights_title[512],hour_title[512];
    if(!photo_title_xml(3,hour_title,sizeof(hour_title)))return 0;
    if(!photo_title_xml(2,lights_title,sizeof(lights_title)))return 0;
    if(!photo_title_xml(0,time_title,sizeof(time_title)) || !photo_title_xml(1,weather_title,sizeof(weather_title)))return 0;
    size_t used=0;
    if(length<3 || !memory_equal(text,"\xef\xbb\xbf",3)){memory_copy(out,"\xef\xbb\xbf",3);used=3;}
    for(size_t i=0;i<length;){
        if(text+i==hour_resources){
            static const char format[]="<local:NumUnitConverter x:Key=\"EO_PhotoHourFormat\" FractionDigits=\"2\" ShowUnit=\"False\"/>";
            memory_copy(out+used,format,sizeof(format)-1);used+=sizeof(format)-1;
            const char *fill_from[]={"A_Style_Slider_PhotoEffects","{Binding Value}"};
            const char *fill_to[]={"EO_PhotoHourSlider","{Binding PhotoHourPosition}"};
            copy_photo_style(out,&used,fill_style,fill_len,fill_from,fill_to,2);
            const char *hour_from[]={
                "A_Style_ContentControl_PhotoEffects","A_Style_Slider_PhotoEffects",
                "{Binding Value}","SmallChange=\"0.01\"",
                "{TemplateBinding Content}",
                "{Binding Value, ConverterParameter=fracpercentage, Converter={StaticResource A_NumUnitConverter_EffectSettingScaleUp}}",
                "Style=\"{StaticResource A_Style_FZTextBlock_EffectValue}\"",
                "<Setter Property=\"Height\" Value=\"{StaticResource A_Double_EffectsItem_Height}\"/>",
                "Margin=\"20,0,64,16\"","Margin=\"20,10,20,0\"","Margin=\"20,16,20,8\""};
            const char *hour_to[]={
                "EO_PhotoHour","EO_PhotoHourSlider",
                "{Binding PhotoHourPosition, Mode=TwoWay}","SmallChange=\"0.010526315789473684\"",
                hour_title,
                "{Binding PhotoHour, Converter={StaticResource EO_PhotoHourFormat}}",
                "MaxWidth=\"80\" Style=\"{StaticResource A_Style_FZTextBlock_EffectValue}\"",
                "<Setter Property=\"Height\" Value=\"72\"/>",
                "Margin=\"20,0,104,12\"","Margin=\"20,6,20,0\"","Margin=\"20,16,20,4\""};
            copy_photo_style(out,&used,effect_style,effect_len,hour_from,hour_to,11);
        }
        if(text+i==end){
            for(unsigned variant=0;variant<4;++variant){
            static const char *const styles[]={"EO_PhotoDriverSpinner","EO_PhotoTimeSpinner","EO_PhotoWeatherSpinner","EO_PhotoLightsSpinner"};
            const char *titles[]={video_options_text(image)->driver,time_title,weather_title,lights_title};
            const char *part=style;size_t left=style_len;
            while(left){
                const char *key="A_Style_ContentControl_EffectsSpinner",*title="{local:Loc InGame.IDS_Photo_Label_Presets}";
                const char *needle=NULL,*replacement=NULL;
                if(left>=text_length(key) && memory_equal(part,key,text_length(key))){needle=key;replacement=styles[variant];}
                else if(left>=text_length(title) && memory_equal(part,title,text_length(title))){needle=title;replacement=titles[variant];}
                if(needle){size_t n=text_length(replacement);memory_copy(out+used,replacement,n);used+=n;part+=text_length(needle);left-=text_length(needle);}
                else{out[used++]=*part++;--left;}
            }
            }
        }
        if(i==scroll_at){
            static const char open[]="<ScrollViewer Focusable=\"False\" Height=\"800\" Margin=\"0,0,-40,0\" HorizontalScrollBarVisibility=\"Disabled\" VerticalScrollBarVisibility=\"Auto\"><StackPanel Width=\"{StaticResource A_Double_Effects_Width}\" HorizontalAlignment=\"Left\">";
            memory_copy(out+used,open,sizeof(open)-1);used+=sizeof(open)-1;
        }
        if(text+i==panel_end){
            static const char close[]="</StackPanel></ScrollViewer>";
            memory_copy(out+used,close,sizeof(close)-1);used+=sizeof(close)-1;
        }
        if(i==row_at){memory_copy(out+used,row,sizeof(row)-1);used+=sizeof(row)-1;}
        // Keep the two spinner rows at their original 80px. For the nine
        // slider rows, redistribute 8px of outer padding instead of taking it
        // out of the label-to-slider gap (native gap: 38px, still 38px here).
        static const char *const from[]={
            "<Setter Property=\"Height\" Value=\"{StaticResource A_Double_EffectsItem_Height}\"/>",
            "Margin=\"20,0,64,16\"", "x:Name=\"TextBlock_Label\" Margin=\"20,10,20,0\"",
            "x:Name=\"TextBlock_Value\" Margin=\"20,16,20,8\"",
            "Storyboard.TargetProperty=\"(UIElement.Visibility)\" Storyboard.TargetName=\"DefaultHelpPanel\""};
        static const char *const to[]={
            "<Setter Property=\"Height\" Value=\"72\"/>", "Margin=\"20,0,64,12\"",
            "x:Name=\"TextBlock_Label\" Margin=\"20,6,20,0\"", "x:Name=\"TextBlock_Value\" Margin=\"20,16,20,4\"",
            "Storyboard.TargetProperty=\"(UIElement.Visibility)\" Storyboard.TargetName=\"BottomBar\""};
        int replaced=0;
        // Visibility states must hide the parent bar: its template owns the
        // soft black prompt backdrop. Prompt-description animations continue
        // targeting DefaultHelpPanel; other scenes/shared styles are untouched.
        for(unsigned j=0;j<5;++j){
            size_t n=text_length(from[j]);
            if((j || text+i==effect_height) && length-i>=n && memory_equal(text+i,from[j],n)){
                size_t m=text_length(to[j]);memory_copy(out+used,to[j],m);used+=m;i+=n;replaced=1;break;
            }
        }
        if(!replaced)out[used++]=text[i++];
    }
    out[used]=0;return used;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fh2TestDriverCount(void){return driver_model_count();}
__declspec(dllexport) void Fh2TestDriverDatabase(void *db){driver_models_database(db);}
__declspec(dllexport) int Fh2TestDriverId(unsigned index){return driver_model_id(index);}
__declspec(dllexport) int Fh2TestPhotoDriver(int value){return driver_photo_set(value);}
__declspec(dllexport) int Fh2TestPhotoDriverGet(void){return driver_photo_get();}
__declspec(dllexport) void Fh2TestPhotoDriverReset(void){driver_photo_reset();}
__declspec(dllexport) size_t Fh2TestPhotoDriverXaml(const char *in,size_t n,char *out,size_t cap){return driver_photo_xaml(in,n,out,cap);}
#endif
