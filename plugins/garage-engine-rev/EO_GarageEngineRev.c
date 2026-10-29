// Garage engine revving through the native Car Meet throttle controls.
#include "fh2_plugin.h"
typedef void (*Action)(void *);
typedef const float *(*InputGetter)(void *);
static InputGetter native_input;
static unsigned char *image;
static void *owner_page,*owned_car;
static void *owned_target,*owned_physics;
static float saved_clutch,saved_handbrake;
static int parked_prepared;
static HANDLE log_handle=INVALID_HANDLE_VALUE;
static volatile LONG throttle_seen,park_rejected,callback_seen,park_sync_seen;
static void *last_skipped;
static ULONGLONG attached_at;
static int warned_no_tick;
static void log_line(const char *s);
typedef struct { char text[16];size_t length,capacity; } NativeString;
typedef struct {const char *name;void *controller;float previous;int held;} SnapshotHold;
static SnapshotHold garage_mix={"FLOW_Garage",NULL,0,0};
static SnapshotHold outpost_mix={"FLOW_DestinationHub",NULL,0,0};
static SnapshotHold music_mix={"AOPT_Radio",NULL,0,0};
// AOPT_SFX is a slider/snapshot parameter, not a linear output gain. Its
// minimum can remain audible. Use the SFX mixer strip fader for real ducking.
static struct {void *engine,*strip;float previous;int held;} effects_fader;
static ULONGLONG effects_retry;
static const unsigned char sfx_id[16]={
    0x65,0x9e,0xba,0xc7,0xb0,0xd3,0x8c,0x4c,0x95,0x27,0x65,0xa1,0x2d,0xb6,0x68,0xc0};
static int sfx_strip(void **engine,void **studio,void **strip){
    unsigned char *e=*(void **)(image+0x365d3d8);
    if(!e || *(void **)e!=image+0x2e73898)return 0;
    *engine=e;*studio=*(void **)(e+0x7f0);*strip=NULL;
    return *studio && !((int (*)(void *,const void *,unsigned,void **))(image+0x1a98300))
        (*studio,sfx_id,0,strip) && *strip;
}
// Use FMOD's locked command queue, exactly as the native fader API does.
// No live audio object is modified from the UI thread.
static int set_fader(void *studio,void *strip,float value){
    void *context=NULL,*scope=NULL,*command=NULL;
    int result=((int (*)(void *,void **))(image+0x1ae90d0))(studio,&context);
    if(result)return result;
    if(!*((unsigned char *)context+0xfd))return 0x65;
    result=((int (*)(void **,void *))(image+0x1ae9240))(&scope,context);
    if(!result){
        unsigned char *queue=*(void **)((unsigned char *)context+0x78);
        command=queue+0x200;
        if(!((unsigned char (*)(void *))(image+0x1aeaf90))(queue))
            result=((int (*)(void *,void **,unsigned))(image+0x1ae9a70))
                (*(void **)(queue+0x1e8),&command,0x20);
        if(!result && command){
            unsigned char *bytes=command;
            *(void **)bytes=image+0x2347198;
            *(uint64_t *)(bytes+8)=0x20;
            *(unsigned *)(bytes+0x10)=(unsigned)(uintptr_t)strip;
            *(float *)(bytes+0x18)=value;
            result=((int (*)(void *,void *))(image+0x1aea6e0))(queue,command);
        }
    }
    ((int (*)(void **))(image+0x1ae9290))(&scope);
    return result;
}
static void hold_effects(void){
    if(effects_fader.held || GetTickCount64()<effects_retry)return;
    effects_retry=GetTickCount64()+500;
    void *engine,*studio,*strip;float previous=-1;
    if(!sfx_strip(&engine,&studio,&strip) ||
       ((int (*)(void *,float *))(image+0x1a99a50))(strip,&previous) ||
       !(previous>=0 && previous<=1) || set_fader(studio,strip,previous*.05f))return;
    effects_fader.engine=engine;effects_fader.strip=strip;
    effects_fader.previous=previous;effects_fader.held=1;
    log_line("Entire SFX mixer layer lowered to 5 percent; car layer unchanged.");
}
static void restore_effects(void){
    if(effects_fader.held){
        void *engine,*studio,*strip;
        if(sfx_strip(&engine,&studio,&strip) && engine==effects_fader.engine && strip==effects_fader.strip){
            if(set_fader(studio,strip,effects_fader.previous))
                log_line("Could not queue restoration of the SFX mixer layer.");
            else log_line("Previous SFX mixer layer restored.");
        }
    }
    effects_fader.held=0;effects_fader.engine=effects_fader.strip=NULL;effects_retry=0;
}
// Read-only names may borrow static storage. The deferred setter consumes its
// argument, so long names must receive a separate game-allocated owned buffer.
static NativeString snapshot_name(const char *text){
    NativeString name={{0},0,15};
    while(text[name.length])++name.length;
    if(name.length<16){
        for(size_t i=0;i<name.length;++i)name.text[i]=text[i];
    }else{
        *(const char **)name.text=text;name.capacity=name.length;
    }
    return name;
}
static int queue_snapshot(void *controller,const char *text,float value){
    NativeString name=snapshot_name(text);
    if(name.capacity>=16){
        char *owned=((void *(*)(size_t))(image+0x3229c0))(name.length+1);
        if(!owned)return 0;
        for(size_t i=0;i<=name.length;++i)owned[i]=text[i];
        *(char **)name.text=owned;
    }
    ((void (*)(void *,NativeString *,float))(*(void ***)controller)[0x28/8])
        (controller,&name,value); // Native setter destroys/empties the argument.
    return 1;
}
// Use the same deferred snapshot commands as native state flow. Snapshot enable
// counts, saved audio options, bank loading and global pause are untouched.
static void restore_snapshot(SnapshotHold *hold){
    if(!hold->held)return;
    void *controller=*(void **)(image+0x365a298);
    if(controller==hold->controller && *(void **)controller==image+0x2e6ae88 &&
       (*(void ***)controller)[0x28/8]==image+0xf775f0){
        if(!queue_snapshot(controller,hold->name,hold->previous))
            log_line("Could not allocate snapshot restoration command.");
    }
    hold->held=0;hold->controller=NULL;
}
static void restore_garage_mix(void){
    if(garage_mix.held||outpost_mix.held||music_mix.held||effects_fader.held)log_line("Previous garage, music and effects snapshot levels queued for restoration.");
    restore_effects();restore_snapshot(&music_mix);restore_snapshot(&outpost_mix);restore_snapshot(&garage_mix);
}
static void hold_snapshot(SnapshotHold *hold,float multiplier,int include_zero){
    if(hold->held)return;
    void *controller=*(void **)(image+0x365a298);
    void *manager=*(void **)(image+0x365d418);
    if(!controller || !manager || *(void **)controller!=image+0x2e6ae88 ||
       *(void **)manager!=image+0x2e76498 ||
       (*(void ***)controller)[0x28/8]!=image+0xf775f0 ||
       (*(void ***)manager)[0x30/8]!=image+0x1008a60)return;
    NativeString name=snapshot_name(hold->name);
    float value=((float (*)(void *,NativeString *))(*(void ***)manager)[0x30/8])(manager,&name);
    if(!(value>=0 && value<=1) || (!include_zero && value==0))return;
    if(!queue_snapshot(controller,hold->name,value*multiplier))return;
    hold->previous=value;hold->controller=controller;hold->held=1;
    log_line(hold==&music_mix?"Music level queued to 5 percent during Engine Swap.":
        hold==&outpost_mix?"Outpost audio snapshot blend queued to zero during Engine Swap.":
        "Garage audio snapshot blend queued to zero during Engine Swap.");
}
static void hold_garage_mix(void){hold_snapshot(&garage_mix,0,0);hold_snapshot(&outpost_mix,0,0);hold_snapshot(&music_mix,.05f,1);hold_effects();}
// Car Meet's XAML uses RTriggerDescription = HelpButtons.IDS_Gas.
// Native b2e040 maps footer9 to RTriggerDescription; IDS_Gas is59849.
static void release_ui(void *value){
    if(value && InterlockedDecrement((volatile LONG *)((unsigned char *)value+0x10))==0)
        ((Action)(image+0x257730))(value);
}
static void *property(void *object,void *key){
    void *value=NULL;
    if(key)((void **(*)(void *,void **,void *))(image+0x15e0f10))(object,&value,key);
    return value;
}
static int gas_visible(void *panel){
    void *button=((void *(*)(void *,int))(image+0xb2aae0))(panel,9);
    if(!button)return 0;
    void *attached=property(button,*(void **)(image+0x34e2ce8));
    int xaml=attached && *((unsigned char *)attached+0x28);release_ui(attached);
    void *key=xaml?((void *(*)(int))(image+0xb2e040))(9):*(void **)(image+0x34e2c18);
    void *label=property(xaml?panel:button,key);
    const wchar_t *text=label?*(const wchar_t **)((unsigned char *)label+0x28):NULL;
    int visible=text && *text;release_ui(label);return visible;
}
// Keep the existing RT control and its bindings, but append it after every
// other footer control. Use native collection notifications and retain the
// control across removal so XAML and dynamic entries have the same lifetime.
static void gas_last(void *panel){
    void *button=((void *(*)(void *,int))(image+0xb2aae0))(panel,9);
    if(!button)return;
    unsigned char *children=((void *(*)(void *))(image+0x14a8e60))(panel);
    if(!children)return;
    void *list=children+0x30;
    void **methods=*(void ***)list;
    int count=((int (*)(void *))methods[0x48/8])(list);
    if(count<2 || count>64)return;
    void **items=*(void ***)(children+0x70);
    if(!items || items[count-1]==button)return;
    int found=0;
    for(int i=0;i<count;++i)if(items[i]==button){found=1;break;}
    if(!found)return;
    InterlockedIncrement((volatile LONG *)((unsigned char *)button+0x10));
    ((void (*)(void *,void *))methods[0x18/8])(list,button);
    if(((int (*)(void *))methods[0x48/8])(list)==count-1)
        ((void (*)(void *,void *))methods[0])(list,button);
    release_ui(button);
}
static void gas_prompt(void *page){
    void *behavior=*(void **)((unsigned char *)page+0x310);
    if(!behavior||!*((unsigned char *)behavior+0x78)||*(void **)((unsigned char *)page+0x2a0))return;
    void *panel=((void *(*)(void *))(image+0xced910))(page);
    if(!panel)return;
    if(gas_visible(panel)){gas_last(panel);return;}
    void *localized=NULL,*label=NULL;
    ((void **(*)(void **,const char *,unsigned))(image+0x123a570))(&localized,"HelpButtons",59849);
    if(!localized)return;
    const wchar_t *text=((const wchar_t *(*)(void *))(*(void ***)localized)[7])(localized);
    if(text&&*text)((void **(*)(void **,const wchar_t *))(image+0x13fe8e0))(&label,text);
    ((Action)(*(void ***)localized)[3])(localized);
    if(label){
        ((void (*)(void *,int,void **))(image+0xb00010))(panel,9,&label);
        gas_last(panel);
    }
}
#ifdef GARAGE_REV_TEST
static int test_focus=1;
#endif
static void log_line(const char *s){
    if(log_handle==INVALID_HANDLE_VALUE)return;
    DWORD n=0;while(s[n])++n;
    DWORD written;WriteFile(log_handle,s,n,&written,NULL);
    WriteFile(log_handle,"\r\n",2,&written,NULL);FlushFileBuffers(log_handle);
}
static void release(void *p){if(p)((Action)(*(void ***)p)[3])(p);}
static int own_callback(void){
    return owned_car && *(void **)((unsigned char *)owned_car+0x5d50)==owned_target;
}
// The native CinematicCarProxy controls builder fd4bc0 sets frame+0xc
// (clutch) and +0x10 (handbrake) to 1 for a stationary preview. Copy only
// those fields through the real dynamics setter; preserve all other controls.
static int prepare_parked(void *car){
    void *physics=*(void **)((unsigned char *)car+0x4c08);
    if(!physics || physics!=owned_physics || *(void **)physics!=image+0x2eb5e18)return 0;
    const float *current=native_input(physics);
    if(!current)return 0;
    union {unsigned char bytes[28];float floats[7];} frame;
    for(unsigned i=0;i<28;++i)frame.bytes[i]=((const unsigned char *)current)[i];
    if(!parked_prepared){
        if(!(frame.floats[3]>=0&&frame.floats[3]<=1&&frame.floats[4]>=0&&frame.floats[4]<=1))return 0;
        saved_clutch=frame.floats[3];saved_handbrake=frame.floats[4];parked_prepared=1;
        log_line("Preview clutch/handbrake prepared through native input setter.");
    }
    frame.floats[3]=frame.floats[4]=1;
    ((void (*)(void *,const void *))(*(void ***)physics)[0x90/8])(physics,frame.bytes);
    return 1;
}
// ForzaCar::Update first snapshots GetInput(), then calls the throttle callable.
// Prepare the parked fields at that read boundary, before the snapshot exists.
// Ordinary world cars and callbacks owned by anyone else retain the native getter.
static const float *input_for_preview(void *physics){
    if(physics==owned_physics&&owner_page&&own_callback()&&prepare_parked(owned_car))
        InterlockedCompareExchange(&park_sync_seen,1,0);
    return native_input(physics);
}
static void restore_parked(void *car){
    void *physics=*(void **)((unsigned char *)car+0x4c08);
    if(!parked_prepared||physics!=owned_physics||!physics||*(void **)physics!=image+0x2eb5e18)return;
    const float *current=native_input(physics);
    if(!current)return;
    union {unsigned char bytes[28];float floats[7];} frame;
    for(unsigned i=0;i<28;++i)frame.bytes[i]=((const unsigned char *)current)[i];
    frame.floats[0]=0;
    // Preserve a subsequent native change rather than replacing it with our snapshot.
    if(frame.floats[3]==1)frame.floats[3]=saved_clutch;
    if(frame.floats[4]==1)frame.floats[4]=saved_handbrake;
    ((void (*)(void *,const void *))(*(void ***)physics)[0x90/8])(physics,frame.bytes);
    log_line("Preview parked-input overrides released.");
}
static void clear_car(void){
    void *car=owned_car;
    if(own_callback()){
        owner_page=NULL;
        void *physics=*(void **)((unsigned char *)car+0x4c08);
        if(physics && *(void **)physics==image+0x2eb5e18)
            ((Action)(image+0x2aaa80))(car); // Last owned frame returns throttle to idle.
        void *empty[4]={0};
        ((void (*)(void *,void *))(image+0x2a6c10))(car,empty);
        restore_parked(car);
        log_line("Native throttle detached.");
    }
    owned_car=owned_target=owner_page=owned_physics=NULL;parked_prepared=0;
    release(car);
}
static void clear(void){restore_garage_mix();clear_car();}
static float throttle(void *car){
    if(!owner_page || car!=owned_car || !own_callback())return 0;
    InterlockedCompareExchange(&callback_seen,1,0);
    unsigned char *page=owner_page;
    void *behavior=*(void **)(page+0x310);
    if(!behavior || !*((unsigned char *)behavior+0x78) || *(void **)(page+0x2a0))return 0;
#ifdef GARAGE_REV_TEST
    if(!test_focus)return 0;
#else
    HWND window=GetForegroundWindow();DWORD pid=0;
    if(window)GetWindowThreadProcessId(window,&pid);
    if(pid!=GetCurrentProcessId())return 0;
#endif
    void *physics=*(void **)((unsigned char *)car+0x4c08);
    if(!physics || *(void **)physics!=image+0x2eb5e18)return 0;
    // Require the parked clutch/handbrake state before accepting throttle.
    // The native input getter prepares it before the owning callable snapshots it.
    const float *frame=((const float *(*)(void *))(*(void ***)physics)[0x150/8])(physics);
    if(!frame || frame[3]<0.99f || frame[4]<0.99f){
        InterlockedCompareExchange(&park_rejected,1,0);return 0;
    }
    float value=((float (*)(void *))(image+0xc9fa10))(NULL);
    if(value>0.01f)InterlockedCompareExchange(&throttle_seen,1,0);
    return value>=0 && value<=1?value:0;
}
static void attach(void *page){
    unsigned char *p=page;
    // Captured native Engine Swap: UpgradeCarParts, area 8, type 0.
    if(*(void **)page!=image+0x2e5d718 || *(unsigned *)(p+0x450)!=8 || *(unsigned *)(p+0x464)!=0){
        if(page==owner_page)clear();
        return;
    }
    void *ui=*(void **)(image+0x34e43b8),*car=NULL;
    if(!ui)return;
    ((void **(*)(void *,void **,int))(image+0xda1560))(ui,&car,0x5a);
    if(!car){if(page==owner_page)clear();return;}
    void *physics=*(void **)((unsigned char *)car+0x4c08);
    if(car==owned_car && page==owner_page && own_callback() && physics==owned_physics){prepare_parked(car);hold_garage_mix();gas_prompt(page);release(car);return;}
    if(owned_car)clear_car();
    if(*(void **)car!=image+0x2c5cb80 || !physics || *(void **)physics!=image+0x2eb5e18 || *(void **)((unsigned char *)car+0x5d50)){
        if(last_skipped!=car)log_line("Skipped: unsupported car or existing native callback.");
        last_skipped=car;restore_garage_mix();release(car);return;
    }
    last_skipped=NULL;
    void **bound=((void *(*)(size_t))(image+0x3229c0))(0x28);
    if(!bound){restore_garage_mix();release(car);return;}
    bound[0]=image+0x2dd0c90; // Native bound-member callable, including clone/destroy.
    bound[1]=(void *)throttle;bound[2]=NULL;bound[3]=car;bound[4]=NULL;
    void *source[4]={NULL,NULL,NULL,bound};
    owner_page=page;owned_car=car;owned_physics=physics;parked_prepared=0;
    if(!prepare_parked(car)){owner_page=owned_car=owned_physics=NULL;release(car);((void (*)(void *))(image+0x31c7b0))(bound);restore_garage_mix();log_line("Skipped: preview input frame is not ready.");return;}
    ((void (*)(void *,void *))(image+0x2a6c10))(car,source);
    owned_target=*(void **)((unsigned char *)car+0x5d50);
    InterlockedExchange(&callback_seen,0);InterlockedExchange(&park_rejected,0);InterlockedExchange(&park_sync_seen,0);
    InterlockedExchange(&throttle_seen,0);attached_at=GetTickCount64();warned_no_tick=0;
    hold_garage_mix();gas_prompt(page);
    log_line("Engine Swap: native throttle attached; preview parked; global pause and volume settings untouched.");
}
static void enter(void *page){
    ((Action)(image+0xec2530))(page);attach(page);
}
static void update(void *page){
    ((Action)(image+0xec7170))(page);
    attach(page); // A preview change can replace the native car.
    if(InterlockedCompareExchange(&callback_seen,2,1)==1)log_line("Native car update invoked throttle callback.");
    if(InterlockedCompareExchange(&park_sync_seen,2,1)==1)log_line("Parked inputs synchronized at native preview read.");
    if(InterlockedCompareExchange(&throttle_seen,2,1)==1)log_line("Native throttle accepted RT.");
    if(InterlockedCompareExchange(&park_rejected,2,1)==1)log_line("Throttle blocked: native parked input not present.");
    if(owner_page && !warned_no_tick && GetTickCount64()-attached_at>2000 && !callback_seen){
        warned_no_tick=1;log_line("Native car update has not invoked throttle: preview may be paused.");
    }
}
static void leave(void *page){
    if(page==owner_page)clear();
    ((Action)(image+0xec6c00))(page);
}
static int patch(unsigned rva,void *expected,void *replacement){
    void **slot=(void **)(image+rva);DWORD old;
    if(*slot!=expected)return 0;
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&old))return 0;
    *slot=replacement;DWORD unused;VirtualProtect(slot,8,old,&unused);return 1;
}
__declspec(dllexport) BOOL WINAPI FH2PluginInitialize(const FH2PluginContext *ctx){
    if(!ctx || ctx->size<sizeof(*ctx) || ctx->api_version!=1 || ctx->adapter!=FH2_PLUGIN_AZIMUTH)return FALSE;
    image=ctx->game_image;
    if(!image || ctx->game_image_size!=0x3a18000)return FALSE;
    // Validate every slot before changing any; no chain-overwrite of other hooks.
    if(*(void **)(image+0x2e5dba0)!=image+0xec2530 ||
       *(void **)(image+0x2e5dbe0)!=image+0xec7170 ||
       *(void **)(image+0x2e5dc10)!=image+0xec6c00 ||
       *(void **)(image+0x2eb5f68)!=image+0x12c62f0)return FALSE;
    native_input=(InputGetter)(image+0x12c62f0);
    wchar_t path[1024];size_t i=0;
    if(ctx->plugins_directory){
        while(ctx->plugins_directory[i] && i<900){path[i]=ctx->plugins_directory[i];++i;}
        if(ctx->plugins_directory[i])return FALSE;
        const wchar_t ini_suffix[]=L"\\EO_GarageEngineRev.ini";
        for(size_t j=0;j<sizeof(ini_suffix)/sizeof(*ini_suffix);++j)path[i+j]=ini_suffix[j];
        wchar_t enabled[8];
        GetPrivateProfileStringW(L"Logging",L"Enabled",L"0",enabled,8,path);
        if(enabled[0]==L'1' && !enabled[1]){
            const wchar_t log_suffix[]=L"\\EO_GarageEngineRev.log";
            for(size_t j=0;j<sizeof(log_suffix)/sizeof(*log_suffix);++j)path[i+j]=log_suffix[j];
            log_handle=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        }
    }
    if(!patch(0x2e5dba0,image+0xec2530,enter))return FALSE;
    if(!patch(0x2e5dbe0,image+0xec7170,update)){
        patch(0x2e5dba0,enter,image+0xec2530);return FALSE;
    }
    if(!patch(0x2e5dc10,image+0xec6c00,leave)){
        patch(0x2e5dbe0,update,image+0xec7170);
        patch(0x2e5dba0,enter,image+0xec2530);return FALSE;
    }
    if(!patch(0x2eb5f68,image+0x12c62f0,input_for_preview)){
        patch(0x2e5dc10,leave,image+0xec6c00);
        patch(0x2e5dbe0,update,image+0xec7170);
        patch(0x2e5dba0,enter,image+0xec2530);return FALSE;
    }
    log_line("EO Garage Engine Rev 1.0.3 loaded. Engine Swap only. Native RT prompt and temporary music/effects ducking.");return TRUE;
}
#ifdef GARAGE_REV_TEST
__declspec(dllexport) void RevTestSetImage(void *base){image=base;}
__declspec(dllexport) void RevTestAttach(void *page){attach(page);}
__declspec(dllexport) void RevTestClear(void){clear();}
__declspec(dllexport) void RevTestFocus(int focus){test_focus=focus;}
__declspec(dllexport) void *RevTestOwner(void){return owned_car;}
#endif
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved){
    (void)instance;(void)reason;(void)reserved;return TRUE;
}
