#include "photo_environment.h"
#include "vehicle_controls.h"
#include "video_options_text.h"
#include "support.h"
#include "../features.h"

// Native weather-option output populated by A_OnlineEventOptions' own builder.
typedef struct {
    unsigned char prefix[24];
    float precipitation;
    int preset;
    float wetness;
} WeatherOption;
_Static_assert(offsetof(WeatherOption, precipitation)==0x18,"Native weather option");

static unsigned char *image;
static void *photo_page;
static unsigned char *clock_manager, *weather_manager;
static unsigned char clock_state[32], weather_state[32];
static unsigned char weather_controls[60], paused_controls[60];
static float wetness;
static int selection[2], hour_quarters;
static WeatherOption weather_options[8];
static unsigned weather_indices[8], weather_count=1;
static int time_changed,weather_changed;

static unsigned char *world(void) {
    void *root=NULL;
    if(!image || !memory_read_pointer(image+0x34aba60,&root))return NULL;
    return ((void *(*)(void *,int))(image+0x21e0c0))(root,0);
}
static int same_manager(unsigned offset,void *expected) {
    unsigned char *current=world();void *value=NULL;
    return current && memory_read_pointer(current+offset,&value) && value==expected;
}
static void apply_controls(const unsigned char controls[60],int restore_state) {
    if(!weather_manager || !same_manager(0x488,weather_manager))return;
    float before[2];memory_copy(&before[0],weather_manager+8,4);memory_copy(&before[1],weather_manager+0x10,4);
    ((void (*)(void *,const void *))(image+0x1981860))(weather_manager,controls);
    if(restore_state){
        // Only scalar progress/intensity state; no vectors, assets or ownership.
        memory_copy(weather_manager+8,weather_state,sizeof(weather_state));
        memory_copy(weather_manager+0x5c,&wetness,sizeof(wetness));
        ((void (*)(void *,float,float))(image+0x19852a0))(weather_manager,before[0],before[1]);
        ((void (*)(void *))(image+0x1985de0))(weather_manager);
    }
}
static void restore_clock(int keep_paused) {
    if(!clock_manager || !same_manager(0x480,clock_manager))return;
    memory_copy(clock_manager+0xc,clock_state,sizeof(clock_state));
    if(keep_paused)clock_manager[0x24]=1;
    // Invalidate the time-of-day override cache after restoring its input.
    clock_manager[0x361]=0;
}
void photo_environment_prepare(unsigned char *base){image=base;}
void photo_environment_begin(void *page) {
    if(photo_page==page)return;
    if(photo_page)photo_environment_end(photo_page);
    selection[0]=selection[1]=0;time_changed=weather_changed=0;
    clock_manager=weather_manager=NULL;weather_count=1;
    unsigned char *current=world();void *clock=NULL,*weather=NULL;
    if(current && memory_read_pointer(current+0x480,&clock) &&
       memory_read((unsigned char *)clock+0xc,clock_state,sizeof(clock_state))){
        clock_manager=clock;
        float seconds=((float (*)(void *,int))(image+0x197a880))(clock,1);
        hour_quarters=float_is_finite(seconds) && seconds>=0 && seconds<86400?(int)(seconds/900.0f):48;
    }
    if(current && memory_read_pointer(current+0x488,&weather) &&
       memory_read((unsigned char *)weather+8,weather_state,sizeof(weather_state)) &&
       memory_read((unsigned char *)weather+0xe0,weather_controls,sizeof(weather_controls)) &&
       memory_read((unsigned char *)weather+0x5c,&wetness,sizeof(wetness))){
        weather_manager=weather;
        for(unsigned i=1;i<=7;++i){
            WeatherOption choice={0};choice.preset=-1;
            ((void (*)(void *,void *,int))(image+0xd6f110))(weather,&choice,(int)i);
            if(choice.preset>=0 && float_is_finite(choice.precipitation) && float_is_finite(choice.wetness)){
                weather_options[weather_count]=choice;weather_indices[weather_count]=i;++weather_count;
            }
        }
    }
    vehicle_photo_lights_begin();
    photo_page=page;
}
void photo_environment_end(void *page) {
    if(!photo_page || page!=photo_page)return;
    vehicle_photo_lights_end();
    if(time_changed)restore_clock(0);
    if(weather_changed)apply_controls(weather_controls,1);
    photo_page=NULL;clock_manager=weather_manager=NULL;
    selection[0]=selection[1]=0;weather_count=1;time_changed=weather_changed=0;
}
void photo_environment_invalidate(void *page){
    if(page && page==photo_page)photo_capture_invalidate(page);
}
int photo_environment_get(unsigned setting){return setting==3?hour_quarters:setting==2?vehicle_photo_lights_get():setting<2?selection[setting]:0;}
unsigned photo_environment_count(unsigned setting){return setting==3?96:setting==2?3:setting?weather_count:2;}
static const wchar_t *localized(const char *table,unsigned key,const wchar_t *fallback){
    void *manager=NULL;
    if(!image || !memory_read_pointer(image+0x3662990,&manager))return fallback;
    unsigned short id=((unsigned short (*)(void *,const char *))(image+0x123d940))(manager,table);
    if(id==0xffff)return fallback;
    const wchar_t *text=((const wchar_t *(*)(void *,unsigned short,unsigned))(image+0x123d780))(manager,id,key);
    return text && *text?text:fallback;
}
const wchar_t *photo_environment_title(unsigned setting){
    if(setting==3)return video_options_text(image)->photo_hour;
    if(!setting)return video_options_text(image)->photo_custom_time;
    if(setting==2)return video_options_text(image)->photo_lights;
    return setting?localized("GameOptions2",29290,L"Weather"):localized("GameOptions2",6883,L"Time of Day");
}
const wchar_t *photo_environment_label(unsigned setting,unsigned index){
    if(setting==2){
        const VideoOptionsText *text=video_options_text(image);
        return index==0?text->default_choice:index==1?text->choices[2]:index==2?text->choices[0]:NULL;
    }
    // These entries exist in the shipped language tables. The similarly named
    // IDS_Weather_* / IDS_TimeOfDay_* symbols are leftover online code keys.
    static const unsigned weather[]={0,21549,44895,9469,23825,34896,19978,21974};
    static const wchar_t *const weather_fallback[]={L"Default",L"Clear",L"Wet",L"Foggy",L"Heavy Fog",L"Rain",L"Storm",L"Foggy Storm"};
    if(!setting)return index<2?video_options_text(image)->choices[index?0:2]:NULL;
    if(index>=weather_count)return NULL;
    unsigned native=weather_indices[index];
    return localized(native==3 || native==4 || native==7?"TheIsland":"GameOptions2",weather[native],weather_fallback[native]);
}
static void apply_hour(void){
    clock_manager[0x25]=0;clock_manager[0x28]=0;
    ((void (*)(void *,float,int))(image+0x197aec0))(clock_manager,hour_quarters*900.0f,1);
    clock_manager[0x24]=1;clock_manager[0x25]=clock_state[0x25-0xc];time_changed=1;
}
int photo_environment_set(unsigned setting,int value) {
    if(setting==3){
        if(!photo_page || value<0 || value>95 || value==hour_quarters ||
           !clock_manager || !same_manager(0x480,clock_manager))return 0;
        hour_quarters=value;if(selection[0])apply_hour();return 1;
    }
    if(setting==2)return photo_page?vehicle_photo_lights_set(value):0;
    if(!photo_page || setting>1 || value<0 || (unsigned)value>=photo_environment_count(setting) || selection[setting]==value)return 0;
    if(!setting){
        if(!clock_manager || !same_manager(0x480,clock_manager))return 0;
        if(!value)restore_clock(1);
        else apply_hour();
        time_changed=1;
    }else{
        if(!weather_manager || !same_manager(0x488,weather_manager))return 0;
        if(!weather_changed){
            // Native PauseWeather freezes the existing blended conditions.
            ((void (*)(void *))(image+0x1981bc0))(weather_manager);
            memory_copy(paused_controls,weather_manager+0xe0,sizeof(paused_controls));
        }
        if(!value)apply_controls(paused_controls,1);
        else{
            unsigned char controls[60];memory_copy(controls,paused_controls,sizeof(controls));
            const WeatherOption *choice=&weather_options[value];
            controls[0x08]=controls[0x10]=0; // Let the preset supply severity and atmosphere.
            controls[0]=1;memory_copy(controls+4,&choice->preset,4);
            controls[0x18]=controls[0x20]=1;
            memory_copy(controls+0x1c,&choice->precipitation,4);
            memory_copy(controls+0x24,&choice->wetness,4);
            apply_controls(controls,0);
        }
        weather_changed=1;
    }
    selection[setting]=value;return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) const wchar_t *Fh2TestPhotoEnvironmentTitle(unsigned setting){return photo_environment_title(setting);}
__declspec(dllexport) const wchar_t *Fh2TestPhotoEnvironmentLabel(unsigned setting,unsigned index){return photo_environment_label(setting,index);}
__declspec(dllexport) void Fh2TestPhotoEnvironmentBegin(void *page){photo_environment_begin(page);}
__declspec(dllexport) void Fh2TestPhotoEnvironmentEnd(void *page){photo_environment_end(page);}
__declspec(dllexport) int Fh2TestPhotoEnvironmentSet(unsigned setting,int value){return photo_environment_set(setting,value);}
__declspec(dllexport) int Fh2TestPhotoEnvironmentGet(unsigned setting){return photo_environment_get(setting);}
__declspec(dllexport) unsigned Fh2TestPhotoEnvironmentCount(unsigned setting){return photo_environment_count(setting);}
#endif
