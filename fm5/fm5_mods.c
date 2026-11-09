#include "shared/boot_diagnostics.h"
#include "fm5/fm5_mods.h"
#include "fm5/fm5_driver.h"
#include "fm5/fm5_badge.h"
#include "fm5/fm5_carbin.h"
#include "shared/addons.h"
#include "shared/addon_archives.h"
#include "shared/plugins.h"
#include "shared/addon_strings.h"
#include "shared/addon_cache_io.h"
#include "shared/logging.h"
#include "shared/support.h"

typedef struct {const char *data;uintptr_t reserved;size_t length,capacity;} GameString;
static unsigned char *image;
static void *files;
static DWORD database_thread;
static unsigned mounted[128];
static SRWLOCK database_lock=SRWLOCK_INIT;
static int prepared;
#ifdef FH2_TWEAKS_TEST
static const wchar_t *test_mods,*test_content;
extern void Fh2TestAddonPaths(const wchar_t *,const wchar_t *);
#endif
// Keep the native file system inside game: even when the mod is outside Mount.
// Azimuth's file callbacks are not general-purpose Windows disk callbacks.
wchar_t *fm5_mods_alias_path(const wchar_t *path){
    wchar_t *builtin=fm5_badge_alias(path);if(builtin)return builtin;
    static const wchar_t prefix[]=L"game:\\__fm5_mods__\\";
    const unsigned start=ARRAY_COUNT(prefix)-1;
    if(!path || (size_t)lstrlenW(path)<start+5 || CompareStringOrdinal(path,start,prefix,start,TRUE)!=CSTR_EQUAL)return NULL;
    unsigned index=0;
    for(unsigned i=0;i<3;i++){if(path[start+i]<L'0'||path[start+i]>L'9')return NULL;index=index*10+path[start+i]-L'0';}
    if(path[start+3]!=L'\\' || !addons_media_active(index))return NULL;
    const wchar_t *relative=path+start+4,*folder=addons_media_folder(index);
    if(!folder)return NULL;
    unsigned component=0;
    for(unsigned i=0;;i++){
        wchar_t ch=relative[i];
        if(!ch || ch==L'\\' || ch==L'/'){
            if(i==component || (i-component==1&&relative[component]==L'.') || (i-component==2&&relative[component]==L'.'&&relative[component+1]==L'.'))return NULL;
            component=i+1;if(!ch)break;
        }else if(ch<32 || ch==L':' || ch==L'*' || ch==L'?' || ch==L'<' || ch==L'>' || ch==L'"' || ch==L'|')return NULL;
    }
    wchar_t *result=HeapAlloc(GetProcessHeap(),0,PATH_CAPACITY*sizeof(wchar_t));
    if(result && !path_join(result,folder,relative)){HeapFree(GetProcessHeap(),0,result);result=NULL;}return result;
}
typedef BOOL (WINAPI *AttributesA)(LPCSTR,GET_FILEEX_INFO_LEVELS,LPVOID);
static AttributesA original_attributes;
static BOOL WINAPI attributes(LPCSTR path,GET_FILEEX_INFO_LEVELS level,LPVOID data){
    int n=path?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0):0;
    wchar_t *wide=n?HeapAlloc(GetProcessHeap(),0,n*sizeof(wchar_t)):NULL;
    if(wide && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,n)){
        wchar_t *physical=fm5_mods_alias_path(wide);
        if(physical){BOOL result=GetFileAttributesExW(physical,level,data);HeapFree(GetProcessHeap(),0,physical);HeapFree(GetProcessHeap(),0,wide);return result;}
    }
    if(wide && addons_physical_path(wide)){
        BOOL result=GetFileAttributesExW(wide,level,data);HeapFree(GetProcessHeap(),0,wide);return result;
    }
    if(wide)HeapFree(GetProcessHeap(),0,wide);
    return original_attributes(path,level,data);
}
static void prepare_io(void){
    addon_archives_prepare_io();
    if(!original_attributes){
        void **slot=(void **)(image+0x101bf18);DWORD protection;
        if(*slot && VirtualProtect(slot,8,PAGE_READWRITE,&protection)){
            original_attributes=(AttributesA)*slot;InterlockedExchangePointer(slot,(void *)attributes);
            DWORD ignored;VirtualProtect(slot,8,protection,&ignored);
        }
    }
}
static GameString borrowed(const char *s){return (GameString){s,0,text_length(s),31};}
static void mount_active(void){
    if(!files)return;
    int changed=0;
    for(unsigned i=0;i<addons_count() && i<128;i++){
        const wchar_t *folder=addons_media_folder(i);
        if(mounted[i] || !addons_media_active(i) || !folder)continue;
        char path[]="game:\\__fm5_mods__\\000\\";
        const unsigned digit=sizeof("game:\\__fm5_mods__\\")-1;
        path[digit]=(char)('0'+i/100);path[digit+1]=(char)('0'+i/10%10);path[digit+2]=(char)('0'+i%10);
        GameString source=borrowed(path),destination=borrowed("game:\\media\\");
        void *storage=((void *(*)(size_t))(image+0x1cdbb0))(0x50);
        void *volume=storage?((void *(*)(void *,const GameString *,unsigned char))(image+0x794a50))(storage,&source,1):NULL;
        if(volume){
            void **v=*(void ***)volume;((void (*)(void *))v[1])(volume);
            int ok=((unsigned char (*)(void *,const GameString *,void **,unsigned,unsigned char,unsigned char,const GameString *))(*(void ***)files)[0x1a8/8])(files,&destination,&volume,0x70000001+2*i,0,0,NULL);
            ((void (*)(void *))v[2])(volume);
            if(ok){mounted[i]=1;changed=1;fm5_carbin_owner(volume,i);log_append_value("FM5 MODS: native game-path media mount ready; package index=",(int)i);}
            else log_append("FM5 MODS: native media mount failed.\r\n");
        }
    }
    changed|=addon_archives_mount(files);
    plugins_start();
    if(changed)((void (*)(void *))(*(void ***)files)[0x1b8/8])(files);
}
void fm5_mods_builtin_files(unsigned char *base){image=base;addon_archives_bind_fm5(base);prepare_io();}
void fm5_mods_files(void *filesystem){if(prepared){files=filesystem;prepare_io();mount_active();}}
static int open_database(const char *path,void **out){
    uint64_t begin=boot_diag_now();
    int result=((int (*)(const char *,void **))(image+0x7d6b0))(path,out);
    boot_diag_end("database.native_SQLite_open",path,begin);
    if(!result && out && *out && database_thread==GetCurrentThreadId()){
        addons_merge_database(out);begin=boot_diag_now();
        fm5_badge_database(*out);fm5_driver_database(*out);fm5_rims_database(*out);mount_active();
        boot_diag_end("assets.FM5_badges_drivers_rims_mounts",NULL,begin);boot_diag_summary("FM5 database assets ready");
    }
    return result;
}
static void **factory(void **out,const GameString *path){
    uint64_t begin=boot_diag_now();
    AcquireSRWLockExclusive(&database_lock);boot_diag_end("database.lock_wait",NULL,begin);database_thread=GetCurrentThreadId();
    void **result=((void **(*)(void **,const GameString *))(image+0x68d0c0))(out,path);
    database_thread=0;ReleaseSRWLockExclusive(&database_lock);return result;
}
static unsigned hook(Fm5Site *sites,unsigned slot,unsigned call,unsigned original,uintptr_t callback){
    sites[0]=(Fm5Site){slot,12,{0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc},{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0},0};
    memory_copy(sites[0].replacement+2,&callback,8);
    sites[1]=(Fm5Site){call,5,{0xe8,0,0,0,0},{0xe8,0,0,0,0},0};
    int32_t before=(int32_t)(original-call-5),after=(int32_t)(slot-call-5);
    memory_copy(sites[1].original+1,&before,4);memory_copy(sites[1].replacement+1,&after,4);return 2;
}
unsigned fm5_mods_prepare(unsigned char *base,Fm5Site *sites,const wchar_t *executable,const wchar_t *directory,int cache){
    image=base;prepared=0;files=NULL;for(unsigned i=0;i<128;i++)mounted[i]=0;
    log_set_named(directory,L"FM5_ExtraOptions.log");
    addons_configure_fm5(executable);
#ifdef FH2_TWEAKS_TEST
    if(test_mods)Fh2TestAddonPaths(test_mods,test_content);
#endif
    addons_layout_prepare();addons_layout_log();
    addons_cache_configure(directory,cache,1);
    if(!addons_configured())return 0;
    AddonSqlite api;
    const unsigned offsets[]={0x7d6b0,0x7af10,0x3d700,0x1eed0,0x1ab30,0x203a0,0x20680,0x206d0,0xe730,0xec20,0x18800};
    for(unsigned i=0;i<ARRAY_COUNT(offsets);i++)((void **)&api)[i]=base+offsets[i];
    addon_cache_io_bind(base,1);addons_database_profile(base,&api);addon_strings_bind_fm5(base);
    addon_archives_bind_fm5(base);plugins_configure(base,executable,1);prepared=1;
    unsigned n=hook(sites,0x68d1c4,0x5d0000,0x68d0c0,(uintptr_t)factory);
    n+=hook(sites+n,0x68e8c2,0x68ec29,0x7d6b0,(uintptr_t)open_database);
    n+=hook(sites+n,0x7c0a43,0x7cf2f3,0x7cd000,(uintptr_t)addon_strings_fm5_load);
    log_append("FM5 MODS: mods/Forza Motorsport 5 load order, transactional database edits and string tables enabled.\r\n");
    n+=fm5_carbin_prepare(base,sites+n,directory);
    return n;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fm5TestModsPrepare(unsigned char *base,Fm5Site *sites,const wchar_t *executable,const wchar_t *directory,const wchar_t *mods,const wchar_t *content){
    test_mods=mods;test_content=content;unsigned result=fm5_mods_prepare(base,sites,executable,directory,1);test_mods=test_content=NULL;return result;
}
__declspec(dllexport) void Fm5TestModsFiles(void *filesystem){fm5_mods_files(filesystem);}
#endif
