#include "fm5/fm5_driver.h"
#include "fm5/fm5_vehicle.h"
#include "fm5/fm5_settings.h"
#include "fm5/fm5_badge.h"
#include "shared/support.h"
typedef struct Model {struct Model *next;int id;char media[128];} Model;
static unsigned char *image;
static Model *models;
static unsigned count=1;
static SRWLOCK lock=SRWLOCK_INIT;
static Model *at(unsigned index){Model *m=models;while(m && --index)m=m->next;return m;}
void fm5_driver_database(void *db){
    void *statement=NULL;
    if(((int (*)(void *,const char *,int,void **,const char **))(image+0x3d700))
       (db,"SELECT Id,MediaName FROM DriverModels WHERE Id>0 ORDER BY CASE WHEN lower(MediaName)='stig' THEN 0 ELSE 1 END,Id",-1,&statement,NULL))return;
    Model *first=NULL,**tail=&first;unsigned total=0;
    while(((int (*)(void *))(image+0x1eed0))(statement)==100){
        int64_t id=((int64_t (*)(void *,int))(image+0x20680))(statement,0);
        const char *name=((const char *(*)(void *,int))(image+0x206d0))(statement,1);
        size_t n=name?text_length(name):0;
        if(id<=0 || id>0x7fffffff || !n || n>=128)continue;
        Model *m=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*m));if(!m)break;
        m->id=(int)id;memory_copy(m->media,name,n+1);*tail=m;tail=&m->next;++total;
    }
    ((int (*)(void *))(image+0x1ab30))(statement);
    AcquireSRWLockExclusive(&lock);Model *old=models;models=first;count=total;ReleaseSRWLockExclusive(&lock);
    while(old){Model *next=old->next;HeapFree(GetProcessHeap(),0,old);old=next;}
}
unsigned fm5_driver_count(void){AcquireSRWLockShared(&lock);unsigned n=count+1;ReleaseSRWLockShared(&lock);return n;}
void fm5_driver_label(unsigned index,wchar_t out[128]){
    AcquireSRWLockShared(&lock);Model *m=index?at(index):NULL;
    const char *name=m?m->media:index?"Stig":"Default";
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,out,128))out[0]=0;
    ReleaseSRWLockShared(&lock);
    for(unsigned i=0;out[i];++i){if(out[i]==L'_')out[i]=L' ';if(out[i]>=L'a'&&out[i]<=L'z')out[i]-=32;}
}
int fm5_driver_index(void){
    int id=fm5_settings_values()[FM5_SAVED_PLAYER];if(!id)return 0;
    AcquireSRWLockShared(&lock);unsigned i=1;Model *m=models;
    if(!m){ReleaseSRWLockShared(&lock);return id==1?1:0;}
    while(m && m->id!=id){m=m->next;++i;}int result=m?(int)i:0;ReleaseSRWLockShared(&lock);return result;
}
int fm5_driver_select(unsigned index){
    AcquireSRWLockShared(&lock);Model *m=index?at(index):NULL;
    int id=!index?0:m?m->id:!models&&index==1?1:-1;ReleaseSRWLockShared(&lock);
    if(id<0 || fm5_settings_values()[FM5_SAVED_PLAYER]==id)return 0;
    fm5_settings_values()[FM5_SAVED_PLAYER]=id;fm5_settings_save();return 1;
}
static void build_car(void *car,unsigned char *out,void *request,unsigned char flag){
    ((void (*)(void *,void *,void *,unsigned char))(image+0x5e9760))(car,out,request,flag);
    int id=fm5_settings_values()[FM5_SAVED_PLAYER];
    if(!id || !fm5_vehicle_local_car(car))return;
    char name[128]={0};AcquireSRWLockShared(&lock);
    if(!models && id==1)memory_copy(name,"stig",5);
    else for(Model *m=models;m;m=m->next)if(m->id==id){memory_copy(name,m->media,sizeof(name));break;}
    ReleaseSRWLockShared(&lock);if(!name[0])return;
    /* Override only this local car's transient render request, never its save
       record, the model table, another driver's request, or global asset paths. */
    struct {char data[16];size_t length,capacity;} source={0};
    const char *ptr=name;memory_copy(source.data,&ptr,8);source.length=text_length(name);source.capacity=128;
    ((void *(*)(void *,const void *,size_t,size_t))(image+0x149190))(out+0x20,&source,0,(size_t)-1);
}
static int open_database(const char *path,void **out){
    int result=((int (*)(const char *,void **))(image+0x7d6b0))(path,out);
    if(!result && out && *out){fm5_badge_database(*out);fm5_driver_database(*out);}return result;
}
static unsigned bridge(Fm5Site *sites,unsigned thunk,unsigned call,unsigned native,void *callback){
    sites[0]=(Fm5Site){.rva=thunk,.size=12,.replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    for(unsigned i=0;i<12;++i)sites[0].original[i]=0xcc;memory_copy(sites[0].replacement+2,&callback,8);
    sites[1]=(Fm5Site){.rva=call,.size=5,.original={0xe8},.replacement={0xe8}};
    int32_t before=native-call-5,after=thunk-call-5;
    memory_copy(sites[1].original+1,&before,4);memory_copy(sites[1].replacement+1,&after,4);return 2;
}
unsigned fm5_driver_prepare(unsigned char *game,Fm5Site *sites,int mods_hooked){
    image=game;
    unsigned n=bridge(sites,0x17de14,0x17dd1f,0x5e9760,(void *)build_car);
    if(!mods_hooked)n+=bridge(sites+n,0x68e8c2,0x68ec29,0x7d6b0,(void *)open_database);
    return n;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fm5TestDriverPrepare(unsigned char *g,Fm5Site *s,int m){return fm5_driver_prepare(g,s,m);}
__declspec(dllexport) void Fm5TestDriverDatabase(void *d){fm5_driver_database(d);}
__declspec(dllexport) unsigned Fm5TestDriverCount(void){return fm5_driver_count();}
__declspec(dllexport) int Fm5TestDriverSelect(unsigned i){return fm5_driver_select(i);}
__declspec(dllexport) int Fm5TestDriverIndex(void){return fm5_driver_index();}
__declspec(dllexport) void Fm5TestDriverLabel(unsigned i,wchar_t *o){fm5_driver_label(i,o);}
__declspec(dllexport) void Fm5TestDriverBuild(void *c,void *o,void *r,unsigned char f){build_car(c,o,r,f);}
#endif
