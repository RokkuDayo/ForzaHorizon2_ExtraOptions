// EO Weather Control. Pause-menu UI adapted from Extra Options' native UI hooks.
// Clock/weather implementation by Pantsedit; see CREDITS.md and LICENSE.
#include "fh2_plugin.h"
#include "config.h"
#include "clock_hold.h"
#include "weather_hold.h"
#include "settings_page.h"
#include "translations.h"
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

typedef void (*Action)(void *);
static unsigned char *game;
static Action pause_init_next,pause_update_next,reset_next;
static HANDLE log_file=INVALID_HANDLE_VALUE;
static wchar_t media_root[MAX_PATH],ini_path[MAX_PATH];
static void *pause_page,*controller_seen,*weather_page;
static int pressed=1,ready;
static void *hidden_pause,*pause_visibility,*pause_opacity;
// Global_Transitions.xaml uses 0.2 s for its native element transitions.
static int pause_fade;
static ULONGLONG pause_fade_at;
static float pause_alpha,pause_fade_from,pause_saved_alpha;
static unsigned xbox=0x8000,keyboard;
static int footer=3;
#ifdef WEATHER_CONTROL_TEST
static int test_focus=1,test_key;
static ULONGLONG test_time;
#endif
static ULONGLONG milliseconds(void){
#ifdef WEATHER_CONTROL_TEST
    return test_time;
#else
    return GetTickCount64();
#endif
}

static void log_line(const char *text){
    if(log_file==INVALID_HANDLE_VALUE)return;
    DWORD n=0;WriteFile(log_file,text,(DWORD)strlen(text),&n,NULL);
}
static void release_ui(void *p){
    if(p && InterlockedDecrement((volatile LONG *)((unsigned char *)p+0x10))==0)
        ((Action)(game+0x257730))(p);
}
static const Translation *language(void){
    unsigned char *manager=*(void **)(game+0x3662990);
    if(manager){
        const unsigned char *name=manager+0x30;
        size_t count=*(const size_t *)(name+16),cap=*(const size_t *)(name+24);
        if(cap==15 && count>=2 && count<=3 && !name[count]){
            for(unsigned i=0;i<sizeof(translations)/sizeof(*translations);++i)
                if(!_stricmp((const char *)name,translations[i].code))return &translations[i];
        }
    }
    return &translations[0];
}
static int focused(void){
#ifdef WEATHER_CONTROL_TEST
    return test_focus;
#endif
    DWORD id=0;HWND window=GetForegroundWindow();
    if(window)GetWindowThreadProcessId(window,&id);
    return id==GetCurrentProcessId();
}
static unsigned buttons(void **selected){
    *selected=NULL;
    unsigned char *manager=*(void **)(game+0x365d7c8);
    if(!manager || *(void **)manager!=game+0x2e7a2e8)return 0;
    unsigned char *pad=*(void **)(manager+0xab28);
    uintptr_t first=(uintptr_t)manager+0x10,current=(uintptr_t)pad;
    if(current<first || current>=first+8*0x1300 || (current-first)%0x1300 || pad[4]!=1)return 0;
    *selected=pad;return *(unsigned short *)(pad+0xc0);
}
static int available(void *page){
    unsigned char *p=page;
    void *behavior=p?*(void **)(p+0x310):NULL;
    if(!ready || !behavior || !*((unsigned char *)behavior+0x78) || *(void **)(p+0x2a0))return 0;
    void *manager=*(void **)(game+0x34c0f40);
    if(!manager)return 0;
    struct {void *object,*counter;} context={0};
    ((void *(*)(void *,void *))(game+0x2129a0))(manager,&context);
    int mode=context.object?*(int *)((unsigned char *)context.object+0x60):-1;
    if(context.counter && InterlockedDecrement((volatile LONG *)((unsigned char *)context.counter+8))==0){
        ((Action)(*(void ***)context.counter)[0])(context.counter);
        if(InterlockedDecrement((volatile LONG *)((unsigned char *)context.counter+12))==0)
            ((Action)(*(void ***)context.counter)[1])(context.counter);
    }
    return mode==2;
}
static void move_last(void *panel){
    void *button=((void *(*)(void *,int))(game+0xb2aae0))(panel,footer);
    unsigned char *children=((void *(*)(void *))(game+0x14a8e60))(panel);
    if(!button || !children)return;
    void *list=children+0x30;void **methods=*(void ***)list;
    int count=((int (*)(void *))methods[0x48/8])(list);
    void **items=*(void ***)(children+0x70);
    if(!items || count<2 || count>64 || items[count-1]==button)return;
    int found=0;for(int i=0;i<count;++i)if(items[i]==button){found=1;break;}
    if(!found)return;
    InterlockedIncrement((volatile LONG *)((unsigned char *)button+0x10));
    ((void (*)(void *,void *))methods[0x18/8])(list,button);
    if(((int (*)(void *))methods[0x48/8])(list)==count-1)
        ((void (*)(void *,void *))methods[0])(list,button);
    release_ui(button);
}
static void prompt(void *page){
    if(!xbox)return;
    void *panel=((void *(*)(void *))(game+0xced910))(page);
    if(!panel)return;
    void *button=((void *(*)(void *,int))(game+0xb2aae0))(panel,footer);
    if(button){
        void *attached=NULL,*description=NULL;
        ((void **(*)(void *,void **,void *))(game+0x15e0f10))
            (button,&attached,*(void **)(game+0x34e2ce8));
        int xaml=attached && *((unsigned char *)attached+0x28);release_ui(attached);
        void *key=xaml?((void *(*)(int))(game+0xb2e040))(footer):*(void **)(game+0x34e2c18);
        ((void **(*)(void *,void **,void *))(game+0x15e0f10))(xaml?panel:button,&description,key);
        const wchar_t *text=description?*(const wchar_t **)((unsigned char *)description+0x28):NULL;
        int own=text&&!wcscmp(text,language()->prompt);release_ui(description);
        if(own){move_last(panel);return;}
    }
    void *label=NULL;
    ((void **(*)(void **,const wchar_t *))(game+0x13fe8e0))(&label,language()->prompt);
    if(label)((void (*)(void *,int,void **))(game+0xb00010))(panel,footer,&label);
    move_last(panel);
}
// UIPage is a native controller; LayoutRoot is its rendered visual. Hide that
// named element, retaining its exact visibility until the separate modal closes.
static void visual_value(void *visual,unsigned key,void *value){
    if(value && *(void **)(game+key))
        ((void (*)(void *,void *,void *))(game+0x15f8a70))(visual,*(void **)(game+key),value);
}
static void restore_pause(void){
    if(hidden_pause){
        visual_value(hidden_pause,0x36a47b0,pause_visibility);
        visual_value(hidden_pause,0x36a5a08,pause_opacity);
        release_ui(pause_visibility);release_ui(pause_opacity);release_ui(hidden_pause);
    }
    hidden_pause=pause_visibility=pause_opacity=NULL;
    pause_fade=0;
}
static void set_pause_alpha(float value){
    void *boxed=NULL;
    ((void **(*)(void **,const float *))(game+0x336ac0))(&boxed,&value);
    if(boxed){visual_value(hidden_pause,0x36a5a08,boxed);release_ui(boxed);pause_alpha=value;}
}
static void fade_pause(int direction){
    pause_fade=direction;pause_fade_from=pause_alpha;pause_fade_at=milliseconds();
}
static void return_pause(void){
    if(!hidden_pause)return;
    visual_value(hidden_pause,0x36a47b0,pause_visibility);
    fade_pause(2);
}
static void tick_pause_fade(void){
    if(!hidden_pause || !pause_fade)return;
    ULONGLONG elapsed=milliseconds()-pause_fade_at;
    float progress=elapsed>=200?1.0f:(float)elapsed/200.0f;
    float target=pause_fade==1?0.0f:pause_saved_alpha;
    set_pause_alpha(pause_fade_from+(target-pause_fade_from)*progress);
    if(progress<1.0f)return;
    if(pause_fade==2){restore_pause();return;}
    void *boxed=NULL;int collapsed=2;
    ((void **(*)(void **,const int *))(game+0x336a10))(&boxed,&collapsed);
    if(boxed){visual_value(hidden_pause,0x36a47b0,boxed);release_ui(boxed);}
    pause_fade=0;
}
static void hide_pause(void *page){
    if(hidden_pause){if(pause_fade==2)fade_pause(1);return;}
    void *name=NULL;
    ((void **(*)(void **,const wchar_t *))(game+0x13fe8e0))(&name,L"LayoutRoot");
    if(!name)return;
    void *root=((void *(*)(void **,void *))(game+0xc63120))(&name,page);
    void *key=*(void **)(game+0x36a47b0),*alpha=*(void **)(game+0x36a5a08);
    if(!root || !key || !alpha){log_line("WEATHER CONTROL: pause visual/properties unavailable.\r\n");return;}
    void *before=NULL,*opacity=NULL;
    ((void **(*)(void *,void **,void *))(game+0x15e0f10))(root,&before,key);
    ((void **(*)(void *,void **,void *))(game+0x15e0f10))(root,&opacity,alpha);
    // Unset properties have no boxed local value. They still need hiding.
    int visible=0;float opaque=1.0f;
    if(!before)((void **(*)(void **,const int *))(game+0x336a10))(&before,&visible);
    if(!opacity)((void **(*)(void **,const float *))(game+0x336ac0))(&opacity,&opaque);
    if(before && opacity){
        InterlockedIncrement((volatile LONG *)((unsigned char *)root+0x10));
        hidden_pause=root;pause_visibility=before;pause_opacity=opacity;
        pause_saved_alpha=*(float *)((unsigned char *)opacity+0x28);
        pause_alpha=pause_saved_alpha;fade_pause(1);
        if(log_file!=INVALID_HANDLE_VALUE){
            void *shown=NULL,*amount=NULL;char line[192];
            ((void **(*)(void *,void **,void *))(game+0x15e0f10))(root,&shown,key);
            ((void **(*)(void *,void **,void *))(game+0x15e0f10))(root,&amount,alpha);
            snprintf(line,sizeof(line),"WEATHER CONTROL: pause visual=%p visibility=%d opacity=%.3f\r\n",
                     root,shown?*(int *)((unsigned char *)shown+0x28):-1,
                     amount?(double)*(float *)((unsigned char *)amount+0x28):-1.0);
            log_line(line);release_ui(shown);release_ui(amount);
        }
    }else{release_ui(before);release_ui(opacity);}
}
static void open_menu(void *page){
    if(!available(page))return;
    void *dialog=NULL;
    // Native VideoOptions factory: retains its normal input, focus and close lifecycle.
    ((void **(*)(void **))(game+0xb11e30))(&dialog);
    if(!dialog)return;
    if(*(void **)dialog!=game+0x2e60370){release_ui(dialog);return;}
    wchar_t path[96];
    swprintf(path,96,L"Anthem\\EO_WeatherControl_%hs",language()->code);
    // UIPage's descriptor is at +228. Only this instance gets the custom URI.
    // Assign through native std::wstring so ownership/capacity remain correct.
    ((void *(*)(void *,const wchar_t *,size_t))(game+0x22c570))
        ((unsigned char *)dialog+0x250,path,wcslen(path));
    weather_page=dialog;
    void *screens=((void *(*)(void))(game+0x1412fe0))();
    if(screens)((void (*)(void *,void *,void *))(*(void ***)screens)[0x1d8/8])(screens,page,dialog);
    else weather_page=NULL;
    if(weather_page && *(void **)((unsigned char *)page+0x2a0)==dialog)hide_pause(page);
    release_ui(dialog);
    log_line("WEATHER CONTROL: native standalone time/weather page requested.\r\n");
}
static void sample_input(void *page,int initial){
    void *selected=NULL;unsigned state=buttons(&selected);
    int down=(xbox && (state&xbox)==xbox);
#ifdef WEATHER_CONTROL_TEST
    down=down || (keyboard && test_key);
#else
    down=down || (keyboard && (GetAsyncKeyState((int)keyboard)&0x8000));
#endif
    int focus=focused();
    if(!initial && pause_page==page && selected==controller_seen && !pressed && down && focus)
        open_menu(page);
    pause_page=page;controller_seen=selected;pressed=!focus || down;
}
static void pause_init(void *page){
    restore_pause();pause_init_next(page);weather_page=NULL;
    sample_input(page,1);
    if(available(page))prompt(page);
}
static void pause_update(void *page){
    pause_update_next(page);
    if(weather_page && *(void **)((unsigned char *)page+0x2a0)!=weather_page){return_pause();weather_page=NULL;}
    if(weather_page && !hidden_pause)hide_pause(page);
    tick_pause_fade();
    if(available(page))prompt(page);
    sample_input(page,0);
}
static void reset(void *page){
    if(page!=weather_page){reset_next(page);return;}
    config_set_enabled(0);config_set_hour(12);config_set_weather(WEATHER_DYNAMIC);
    settings_page_refresh(page);
}
static int patch_slot(unsigned rva,void *before,void *after){
    void **slot=(void **)(game+rva);DWORD old,unused;
    if(*slot!=before || !VirtualProtect(slot,8,PAGE_READWRITE,&old))return 0;
    *slot=after;VirtualProtect(slot,8,old,&unused);return 1;
}
static int install_menu(void){
    pause_init_next=*(Action *)(game+0x2e14310);
    pause_update_next=*(Action *)(game+0x2e14350);
    reset_next=*(Action *)(game+0x2e60900);
    if(!pause_init_next || !pause_update_next || !reset_next)return 0;
    if(!patch_slot(0x2e14310,(void *)pause_init_next,pause_init))return 0;
    if(!patch_slot(0x2e14350,(void *)pause_update_next,pause_update)){
        patch_slot(0x2e14310,pause_init,(void *)pause_init_next);return 0;
    }
    if(!patch_slot(0x2e60900,(void *)reset_next,reset)){
        patch_slot(0x2e14350,pause_update,(void *)pause_update_next);
        patch_slot(0x2e14310,pause_init,(void *)pause_init_next);return 0;
    }
    return 1;
}
static void load_binding(void){
    static const struct {const wchar_t *name;unsigned mask;int id;} choices[]={
        {L"None",0,3},{L"A",0x1000,0},{L"B",0x2000,1},{L"X",0x4000,2},{L"Y",0x8000,3},{L"View",0x20,5}};
    wchar_t value[32];GetPrivateProfileStringW(L"Bindings",L"ChangeTimeXbox",L"Y",value,32,ini_path);
    int found=0;
    for(unsigned i=0;i<sizeof(choices)/sizeof(*choices);++i){
        if(!_wcsicmp(value,choices[i].name)){xbox=choices[i].mask;footer=choices[i].id;found=1;break;}
    }
    if(!found)log_line("WEATHER CONTROL: invalid ChangeTimeXbox; using Y.\r\n");
    int key=GetPrivateProfileIntW(L"Bindings",L"ChangeTimeKeyboard",0,ini_path);
    keyboard=key>=0 && key<=255?(unsigned)key:0;
}
__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *ctx){
    if(!ctx || ctx->size<sizeof(*ctx) || ctx->api_version<1 || !ctx->plugins_directory ||
       !ctx->game_image || ctx->game_image_size!=0x3a18000)return FALSE;
    game=ctx->game_image;
    if(swprintf(ini_path,MAX_PATH,L"%ls\\EO_WeatherControl.ini",ctx->plugins_directory)<0 ||
       swprintf(media_root,MAX_PATH,L"%ls\\EO_WeatherControl\\Media",ctx->plugins_directory)<0)return FALSE;
    if(GetPrivateProfileIntW(L"Logging",L"Enabled",0,ini_path)){
        wchar_t path[MAX_PATH];
        if(swprintf(path,MAX_PATH,L"%ls\\EO_WeatherControl.log",ctx->plugins_directory)>=0)
            log_file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    }
    if(!clock_hold_verify(game,ctx->game_image_size,log_line) ||
       !weather_hold_verify(game,ctx->game_image_size,log_line) ||
       !settings_page_verify(game,ctx->game_image_size,log_line))return FALSE;
    config_load(ini_path,log_line);config_load_weather();load_binding();
    // Once native properties are linked, keep this DLL resident even if a later
    // hook is refused. No dangling callbacks into an unloaded module.
    if(!settings_page_install(game,ctx->game_image_size,log_line,NULL,1))return TRUE;
    int weather=weather_hold_init(game,ctx->game_image_size,log_line);
    int clock=weather && clock_hold_install(game,ctx->game_image_size,log_line);
    ready=clock && install_menu();
    log_line(ready?"EO Weather Control 1.0.4 ready. Pause menu shortcut; standalone native page.\r\n":
                   "WEATHER CONTROL: UI or clock hook refused; standalone menu unavailable.\r\n");
    return TRUE;
}
__declspec(dllexport) const wchar_t *WINAPI FH2PluginMediaDirectory(void){return ready?media_root:NULL;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved){
    (void)reserved;if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;
}
#ifdef WEATHER_CONTROL_TEST
__declspec(dllexport) void WeatherTestBind(void *base){game=base;ready=1;settings_page_test_bind(base);}
__declspec(dllexport) int WeatherTestInstallMenu(void){return install_menu();}
__declspec(dllexport) void WeatherTestFocus(int value){test_focus=value;}
__declspec(dllexport) void WeatherTestKey(int value){test_key=value;}
__declspec(dllexport) void WeatherTestTime(ULONGLONG value){test_time=value;}
__declspec(dllexport) void WeatherTestConfig(const wchar_t *path){config_load(path,log_line);config_load_weather();}
__declspec(dllexport) int WeatherTestValue(int which){return which==0?config_enabled():which==1?(int)(config_hour()*4):config_weather();}
__declspec(dllexport) void WeatherTestOpen(void *page){open_menu(page);}
__declspec(dllexport) void WeatherTestReset(void *page){reset(page);}
__declspec(dllexport) void WeatherTestInput(void *page,int init){sample_input(page,init);}
__declspec(dllexport) void *WeatherTestPage(void){return weather_page;}
__declspec(dllexport) void WeatherTestBinding(const wchar_t *path){wcsncpy(ini_path,path,MAX_PATH-1);load_binding();}
#endif
