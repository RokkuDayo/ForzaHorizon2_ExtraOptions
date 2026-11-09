#include "fm5/fm5_vehicle.h"
#include "fm5/fm5_ui.h"
#include "shared/support.h"

static unsigned char *image;
static int hotkey_enabled,held,manual=-1;
static unsigned headlights_pad, headlights_key;
static void *driving_car,*driving_presentation;
static struct {void *page,*car,*proxy,*actual;float light;unsigned char driver;int values[2];} photo;
/* Published to the render thread. Old queued commands must obey the newest
   selection, including Default's captured amount, until Photo Mode exits. */
static void *volatile light_target;
static volatile LONG light_amount_bits;
static int light_pending;
static volatile LONG light_revision,light_rendered;
static float photo_light_amount(void){return photo.values[1]?photo.values[1]==2?1.0f:0.0f:photo.light;}
static void render_light(void *actual,float amount){
    if(InterlockedCompareExchangePointer(&light_target,NULL,NULL)==actual){
        LONG bits=InterlockedCompareExchange(&light_amount_bits,0,0);
        if(InterlockedCompareExchangePointer(&light_target,NULL,NULL)==actual)memory_copy(&amount,&bits,4);
    }
    ((void (*)(void *,float))(image+0x86f3f0))(actual,amount);
}

/* A setter receipt only updates 108C. Materials are published later by this
   native render update, which Photo Mode can otherwise have stopped sampling. */
static void render_update(void *actual,void *context){
    LONG revision=InterlockedCompareExchange(&light_revision,0,0);
    int selected=InterlockedCompareExchangePointer(&light_target,NULL,NULL)==actual;
    if(selected){
        LONG bits=InterlockedCompareExchange(&light_amount_bits,0,0);float amount;
        memory_copy(&amount,&bits,4);
        ((void (*)(void *,float))(image+0x86f3f0))(actual,amount);
    }
    ((void (*)(void *,void *))(image+0x874c90))(actual,context);
    if(selected && revision==InterlockedCompareExchange(&light_revision,0,0) &&
       InterlockedCompareExchangePointer(&light_target,NULL,NULL)==actual)
        InterlockedExchange(&light_rendered,revision);
}

static int proxy_presentation(void *proxy,void **actual){
    void *vt=NULL;
    return memory_read_pointer(proxy,&vt) && (vt==image+0x22cdc78 || vt==image+0x22cfbd0) &&
        memory_read_pointer((unsigned char *)proxy+8,actual) && memory_read_pointer(*actual,&vt) &&
        (vt==image+0x22d49e8 || vt==image+0x22d6c08);
}
static int presentation(void *car,void **proxy,void **actual){
    return car && memory_read_pointer((unsigned char *)car+0x4c18,proxy) && proxy_presentation(*proxy,actual);
}
static void set_light(void *proxy,float amount){((void (*)(void *,float))(image+0x83de00))(proxy,amount);}
static void set_driver(void *proxy,unsigned char show){((void (*)(void *,unsigned char))(image+0x83fd60))(proxy,show);}
static int photo_target(void *page,void **car,void **proxy,void **actual){
    void *target=NULL,*vt=NULL;
    *car=*proxy=*actual=NULL;
    for(unsigned i=0;i<3;++i){
        void *camera=NULL;
        if(!memory_read_pointer((unsigned char *)page+0xb70+i*0x10,&camera) || !camera ||
           !memory_read_pointer(camera,&vt) ||
           !memory_read_pointer((unsigned char *)camera+8,&target) || !target)continue;
        if(vt==image+0x22b15a8){
            /* Showroom Photo Mode owns its preview through the native UI
               renderer, rather than a simulated ForzaCar camera target. */
            void *renderer=NULL;
            if(memory_read_pointer(target,&vt) && (vt==image+0x22b13b8 || vt==image+0x22b1298) &&
               memory_read_pointer((unsigned char *)target+0x18,&renderer) &&
               memory_read_pointer(renderer,&vt) && (vt==image+0x22b17b0 || vt==image+0x22b16a0) &&
               memory_read_pointer((unsigned char *)renderer+0x10,proxy) && proxy_presentation(*proxy,actual))return 1;
            continue;
        }
        if(vt!=image+0x22b1af0)continue;
        if(!memory_read_pointer(target,&vt))continue;
        if(vt==image+0x22bcff8){
            if(!memory_read_pointer((unsigned char *)target+8,&target) || !memory_read_pointer(target,&vt))continue;
        }
        /* Only native polymorphic targets have an RTTI complete-object locator.
           UI camera state can contain non-target payloads in this position. */
        if((uintptr_t)vt<(uintptr_t)image+0x1d00000 || (uintptr_t)vt>=(uintptr_t)image+0x2340000)continue;
        *car=((void *(*)(void *,int,void *,void *,int))(image+0xe79f0c))
            (target,0,image+0x234e528,image+0x234d548,0);
        if(presentation(*car,proxy,actual))return 1;
    }
    *car=*proxy=*actual=NULL;return 0;
}
static void restore(void){
    int had_light=InterlockedExchangePointer(&light_target,NULL)!=NULL;light_pending=0;
    void *proxy=NULL,*actual=NULL;
    /* Retain one native car reference until its overrides are restored.
       Verify presentation identity before using a possibly replaced proxy. */
    void *car=NULL;
    int valid=photo.car?presentation(photo.car,&proxy,&actual):photo.page?photo_target(photo.page,&car,&proxy,&actual):0;
    if(valid &&
       proxy==photo.proxy && actual==photo.actual){
        if(photo.values[0])set_driver(proxy,photo.driver);
        if(had_light)set_light(proxy,photo.light);
    }
    if(photo.car)((void (*)(void *))(image+0x166110))(photo.car);
    photo.car=photo.proxy=photo.actual=NULL;photo.values[0]=photo.values[1]=0;
}
void fm5_vehicle_photo_begin(void *page){
    if(photo.page)restore();
    photo.page=page;photo.values[0]=photo.values[1]=0;
}
void fm5_vehicle_photo_end(void *page){if(photo.page==page){restore();photo.page=NULL;}}
int fm5_vehicle_photo_get(unsigned i){return i<2?photo.values[i]:0;}
int fm5_vehicle_photo_tick(void *page){
    if(page!=photo.page || !light_pending || !photo.actual ||
       InterlockedCompareExchange(&light_rendered,0,0)!=InterlockedCompareExchange(&light_revision,0,0))return 0;
    light_pending=0;return 1;
}
int fm5_vehicle_photo_set(void *page,unsigned i,int value){
    if(i>1 || value<0 || value>(i?2:1) || page!=photo.page || value==photo.values[i])return 0;
    void *car=NULL,*proxy=NULL,*actual=NULL;
    if(!photo_target(page,&car,&proxy,&actual))return 0;
    if(car!=photo.car || proxy!=photo.proxy || actual!=photo.actual){
        restore();if(car)((void (*)(void *))(image+0x166100))(car);photo.car=car;photo.proxy=proxy;photo.actual=actual;
        if(!memory_read((unsigned char *)actual+0x4f5,&photo.driver,1) ||
           !memory_read((unsigned char *)actual+0x108c,&photo.light,4) || !float_is_finite(photo.light)){
            if(car)((void (*)(void *))(image+0x166110))(car);photo.car=photo.proxy=photo.actual=NULL;return 0;
        }
    }
    photo.values[i]=value;
    if(i){
        float amount=photo_light_amount();LONG bits;memory_copy(&bits,&amount,4);
        InterlockedExchange(&light_amount_bits,bits);
        InterlockedExchangePointer(&light_target,actual);
        InterlockedIncrement(&light_revision);light_pending=1;
        set_light(proxy,amount);
    }else set_driver(proxy,value?0:photo.driver);
    return 1;
}
int fm5_vehicle_local_car(void *car){
    unsigned role=~0u;void *owner=NULL,*vt=NULL;
    if(!memory_read((unsigned char *)car+0x570,&role,4) || (role&~2u) ||
       !memory_read((unsigned char *)car+0x4bf8,&owner,sizeof(owner)))return 0;
    if(!owner)return 1;
    if(!memory_read_pointer(owner,&vt))return 0;
    uintptr_t fn=0;if(!memory_read((unsigned char *)vt+0x50,&fn,8) ||
        fn<(uintptr_t)image+0x1000 || fn>=(uintptr_t)image+0x1010000)return 0;
    return ((int (*)(void *))fn)(owner)==0;
}
int fm5_vehicle_input(unsigned short *buttons, int allow_keyboard){
    void *manager=NULL,*controller=NULL,*vt=NULL;unsigned char connected=0;
    *buttons=0;
    if(!memory_read_pointer(image+0x25e7100,&manager) || !memory_read_pointer(manager,&vt) || vt!=image+0x22bd730 ||
       !memory_read_pointer((unsigned char *)manager+0xab28,&controller))return 0;
    uintptr_t first=(uintptr_t)manager+0x10,selected=(uintptr_t)controller;
    if(selected<first || selected>=first+8*0x1300 || (selected-first)%0x1300 ||
       !memory_read((unsigned char *)controller+4,&connected,1))return 0;
    if(connected==1){
        if(!memory_read((unsigned char *)controller+0xc0,buttons,2))return 0;
    }else if(!allow_keyboard)return 0;
    struct {void *proxy,**blocks;size_t capacity,start,count;} contexts;
    if(!memory_read((unsigned char *)controller+0x38,&contexts,sizeof(contexts)) ||
       !contexts.count || !contexts.blocks || !contexts.capacity || contexts.capacity>4096 ||
       (contexts.capacity&(contexts.capacity-1)) || contexts.count>contexts.capacity*2 || contexts.start>SIZE_MAX-contexts.count)return 0;
    void *block=NULL;unsigned permissions=0;size_t slot=(contexts.start/2)&(contexts.capacity-1);
    return memory_read_pointer(contexts.blocks+slot,&block) &&
        memory_read((unsigned char *)block+(contexts.start%2)*8+4,&permissions,4) && (permissions&1);
}
/* Called at ForzaCar's ordinary light publication, not in an input polling
   thread. R8 is supplied by the verified thunk's preserved RDI car argument. */
static void publish_light(void *proxy,float native,void *car){
    if(hotkey_enabled && fm5_vehicle_local_car(car)){
        unsigned short buttons=0;
        int allowed=fm5_vehicle_input(&buttons,headlights_key!=0);
        int down=(headlights_pad && (buttons & headlights_pad)==headlights_pad) ||
                 fm5_keyboard_down(headlights_key);
        if(car!=driving_car || proxy!=driving_presentation){
            driving_car=car;driving_presentation=proxy;manual=-1;held=down;
        }else{
            if(allowed && !photo.page && down && !held)manual=!(manual<0?native>0:manual);
            held=down;
        }
        if(manual>=0)native=(float)manual;
    }
    if(photo.page && proxy==photo.proxy && photo.values[1])native=photo.values[1]==2?1.0f:0.0f;
    ((void (*)(void *,float))(*(void ***)proxy)[0x220/8])(proxy,native);
}
static void publish_driver(void *proxy,unsigned char show){
    if(photo.page && proxy==photo.proxy && photo.values[0])show=0;
    set_driver(proxy,show);
}
unsigned fm5_vehicle_prepare(unsigned char *game,Fm5Site *sites,int hotkey,const wchar_t *ini){
    image=game;hotkey_enabled=hotkey;held=0;manual=-1;driving_car=driving_presentation=NULL;
    headlights_pad=fm5_read_binding(ini,L"HeadlightsXbox",1,L"RightStick");
    headlights_key=fm5_read_binding(ini,L"HeadlightsKeyboard",0,L"F3");
    photo.page=photo.car=photo.proxy=photo.actual=NULL;photo.values[0]=photo.values[1]=0;
    InterlockedExchangePointer(&light_target,NULL);light_pending=0;
    void *original=image+0x83fd60,*driver=(void *)publish_driver;
    /* The factory constructs TRefCountedObjectThreadSafe derivatives. Their
       interface layout/setters match the base, but their vtables differ. */
    for(unsigned i=0;i<2;++i){
        sites[i]=(Fm5Site){.rva=i?0x22cfef8:0x22cdfa0,.size=8};
        memory_copy(sites[i].original,&original,8);memory_copy(sites[i].replacement,&driver,8);
    }
    original=image+0x86f3f0;void *light=(void *)render_light;
    for(unsigned i=0;i<2;++i){
        sites[2+i]=(Fm5Site){.rva=i?0x22d6e28:0x22d4c08,.size=8};
        memory_copy(sites[2+i].original,&original,8);memory_copy(sites[2+i].replacement,&light,8);
    }
    original=image+0x874c90;void *update=(void *)render_update;
    for(unsigned i=0;i<2;++i){
        sites[4+i]=(Fm5Site){.rva=i?0x22d7000:0x22d4de0,.size=8};
        memory_copy(sites[4+i].original,&original,8);memory_copy(sites[4+i].replacement,&update,8);
    }
    if(!hotkey)return 6;
    sites+=6;
    sites[0]=(Fm5Site){.rva=0x5e6f61,.size=15,.replacement={0x49,0x89,0xf8,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    for(unsigned i=0;i<15;++i)sites[0].original[i]=0xcc;
    void *callback=(void *)publish_light;memory_copy(sites[0].replacement+5,&callback,8);
    sites[1]=(Fm5Site){.rva=0x5e3956,.size=6,.original={0xff,0x90,0x20,2,0,0},.replacement={0xe8,0,0,0,0,0x90}};
    int32_t jump=0x5e6f61-0x5e395b;memory_copy(sites[1].replacement+1,&jump,4);
    return 8;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fm5TestVehicleInput(unsigned short *buttons,int allow_keyboard){
    return fm5_vehicle_input(buttons,allow_keyboard);
}
__declspec(dllexport) void Fm5TestVehicleRenderUpdate(void *actual,void *context){render_update(actual,context);}
__declspec(dllexport) unsigned Fm5TestVehiclePrepare(unsigned char *game,Fm5Site *sites,int hotkey){return fm5_vehicle_prepare(game,sites,hotkey,NULL);}
__declspec(dllexport) void Fm5TestVehicleLight(void *proxy,float native,void *car){publish_light(proxy,native,car);}
__declspec(dllexport) void Fm5TestVehiclePhotoBegin(void *p){fm5_vehicle_photo_begin(p);}
__declspec(dllexport) void Fm5TestVehiclePhotoEnd(void *p){fm5_vehicle_photo_end(p);}
__declspec(dllexport) int Fm5TestVehiclePhotoSet(void *p,unsigned i,int v){return fm5_vehicle_photo_set(p,i,v);}
__declspec(dllexport) int Fm5TestVehiclePhotoGet(unsigned i){return fm5_vehicle_photo_get(i);}
__declspec(dllexport) int Fm5TestVehiclePhotoTick(void *p){return fm5_vehicle_photo_tick(p);}
#endif
