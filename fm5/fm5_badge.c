#include "fm5/fm5_badge.h"
#include "shared/support.h"
#include "shared/logging.h"

/* A regular native badge, not a replacement for the player's other badges.
   A connection-local TEMP view shadows the merged table. No changes are
   written to the original game database or its persistent mod cache. */
static unsigned char *image;
static wchar_t picture[PATH_CAPACITY];
static int attempted,available;
static int mounted;

/* Player-card snapshots store the badge as uint16_t (native 0x2a5b4f).
   Keep the low word of the original EO ID so existing snapshots resolve too.
   The old full-width ID remains a lookup-only alias for equipped profiles. */
#define GAMERPIC_BADGE_ID 64888u
#define LEGACY_GAMERPIC_BADGE_ID 2147483000u
#define GAMERPIC_BADGE_TEXTURE "'GAME:\\Media\\UI\\Textures\\Data_Bound\\Achievement_Badges\\Player_Status\\__eo_gamerpic__.swatchbin'"

/* A loose, manifest-free volume is necessary: an alias at the Windows I/O
   boundary alone cannot make a new filename visible in the stock root volume. */
void fm5_badge_files(void *filesystem){
    if(mounted || !filesystem)return;
    typedef struct {const char *data;uintptr_t reserved;size_t length,capacity;} Path;
    static const char src[]="game:\\__eo_badge_files__\\";
    static const char dst[]="game:\\media\\ui\\textures\\data_bound\\achievement_badges\\player_status\\";
    Path source={src,0,sizeof(src)-1,31},destination={dst,0,sizeof(dst)-1,sizeof(dst)};
    void *storage=((void *(*)(size_t))(image+0x1cdbb0))(0x50);
    void *volume=storage?((void *(*)(void *,const Path *,unsigned char))(image+0x794a50))(storage,&source,1):NULL;
    if(!volume)return;
    void **vt=*(void ***)volume;((void (*)(void *))vt[1])(volume);
    mounted=((unsigned char (*)(void *,const Path *,void **,unsigned,unsigned char,unsigned char,const Path *))(*(void ***)filesystem)[0x1a8/8])
        (filesystem,&destination,&volume,0x7ffffffb,0,0,NULL);
    ((void (*)(void *))vt[2])(volume);
    if(mounted)((void (*)(void *))(*(void ***)filesystem)[0x1b8/8])(filesystem);
}

/* The translation layer accepts PNG contents through the normal .swatchbin
   asset path. Alias that virtual path directly to its configured picture:
   no resampling, texture conversion, temporary copy or cache is needed. */
static int prepare_texture(void){
    if(attempted)return available;attempted=1;
    HANDLE f=CreateFileW(picture,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    unsigned char signature[8];DWORD read=0;
    if(f!=INVALID_HANDLE_VALUE){
        available=ReadFile(f,signature,sizeof(signature),&read,NULL) && read==sizeof(signature) &&
                  memory_equal(signature,"\x89PNG\r\n\x1a\n",8);
        CloseHandle(f);
    }
    log_append(available?"FM5 BADGE: unlocked gamerpic badge uses Azimuth data/player.png directly.\r\n":
                         "FM5 BADGE: data/player.png unavailable or not a PNG; original badge list retained.\r\n");
    return available;
}
wchar_t *fm5_badge_alias(const wchar_t *path){
    if(!available || !path)return NULL;
    const wchar_t *leaf=path;
    for(const wchar_t *p=path;*p;p++)if(*p==L'/' || *p==L'\\')leaf=p+1;
    if(lstrcmpiW(leaf,L"__eo_gamerpic__.swatchbin"))return NULL;
    size_t n=((size_t)lstrlenW(picture)+1)*sizeof(wchar_t);
    wchar_t *out=HeapAlloc(GetProcessHeap(),0,n);if(out)memory_copy(out,picture,n);return out;
}
void fm5_badge_database(void *db){
    if(!prepare_texture())return;
    static const char sql[]="CREATE TEMP VIEW IF NOT EXISTS List_BadgeAndTitleUnlocks AS "
        "SELECT * FROM main.List_BadgeAndTitleUnlocks UNION ALL "
        "SELECT 64888,1,0,'EO_Gamerpic','Your gamerpic.',"
        GAMERPIC_BADGE_TEXTURE ",'','',1,0,-1,0 "
        "WHERE NOT EXISTS(SELECT 1 FROM main.List_BadgeAndTitleUnlocks WHERE UnlockId=64888) "
        "UNION ALL SELECT 2147483000,1,0,'EO_Gamerpic_Legacy','Your gamerpic.',"
        GAMERPIC_BADGE_TEXTURE ",'','',0,0,-1,0 "
        "WHERE NOT EXISTS(SELECT 1 FROM main.List_BadgeAndTitleUnlocks WHERE UnlockId=2147483000)";
    void *stmt=NULL;
    if(!((int (*)(void *,const char *,int,void **,const char **))(image+0x3d700))(db,sql,-1,&stmt,NULL)){
        int result=((int (*)(void *))(image+0x1eed0))(stmt);
        ((int (*)(void *))(image+0x1ab30))(stmt);
        if(result!=101)log_append("FM5 BADGE: could not register gamerpic badge.\r\n");
    }
}
static void description(void *item,void *property,void *value,void *metadata,
                        uintptr_t a5,uintptr_t a6,uintptr_t a7,uintptr_t a8){
    void *literal=NULL;
    if(item && *(void **)item==image+0x2221758 &&
       (*(unsigned *)((unsigned char *)item+0x60)==GAMERPIC_BADGE_ID ||
        *(unsigned *)((unsigned char *)item+0x60)==LEGACY_GAMERPIC_BADGE_ID)){
        literal=((void *(*)(size_t))(image+0x1cdbb0))(0x40);
        if(literal){((void *(*)(void *,const wchar_t *,int))(image+0xab1770))(literal,L"Your gamerpic.",-1);value=literal;}
    }
    ((void (*)(void *,void *,void *,void *,uintptr_t,uintptr_t,uintptr_t,uintptr_t))(image+0xa02a20))
        (item,property,value,metadata,a5,a6,a7,a8);
    if(literal && !InterlockedDecrement((volatile LONG *)((unsigned char *)literal+0x10)))
        ((void (*)(void *))(image+0x137710))(literal);
}
static int badge_query(char *out,const char *format,const char *field,const char *prefix,
                       const char *prefix2,const char *filter,int category,const char *prefix3){
    static const char query[]="SELECT L_BTU.UnlockId AS Id, L_BTU.UnlockBadgeTexture AS Badge, L_BTU.UnlockTitleString AS Title, L_BTU.UnlockDescription AS Description, COALESCE(BTU.%s, 0) AS NewItem, "
        "((L_BTU.UnlockId=64888 AND L_BTU.UnlockName='EO_Gamerpic') OR (SELECT Count(1) FROM %sBadgeTitleUnlocks BTU WHERE BTU.UnlockId=L_BTU.UnlockId)) AS IsUnlocked "
        "FROM List_BadgeAndTitleUnlocks L_BTU LEFT JOIN %sBadgeTitleUnlocks BTU ON L_BTU.UnlockId=BTU.UnlockId WHERE %s AND L_BTU.UnlockCategory=%d AND "
        "(L_BTU.IsSecret=0 OR (SELECT Count(1) FROM %sBadgeTitleUnlocks AS PlayerUnlocks WHERE Id=PlayerUnlocks.UnlockID)>0) ORDER BY L_BTU.SortOrder, L_BTU.UnlockId";
    return ((int (*)(char *,const char *,const char *,const char *,const char *,const char *,int,const char *))(image+0x1988c0))
        (out,available?query:format,field,prefix,prefix2,filter,category,prefix3);
}
unsigned fm5_badge_prepare(unsigned char *game,Fm5Site *sites,const wchar_t *directory){
    image=game;attempted=available=mounted=0;
    path_join(picture,directory,L"data\\player.png");
    /* Use the formatter's exact native argument list. The SQL buffer remains
       native-owned; unrelated unlock queries and categories are untouched. */
    sites[0]=(Fm5Site){.rva=0x2887b4,.size=12,.replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    for(unsigned i=0;i<12;i++)sites[0].original[i]=0xcc;
    void *fn=(void *)badge_query;memory_copy(sites[0].replacement+2,&fn,8);
    sites[1]=(Fm5Site){.rva=0x28868b,.size=5,.original={0xe8},.replacement={0xe8}};
    int32_t before=0x1988c0-0x28868b-5,after=0x2887b4-0x28868b-5;
    memory_copy(sites[1].original+1,&before,4);memory_copy(sites[1].replacement+1,&after,4);
    sites[2]=(Fm5Site){.rva=0x287f04,.size=12,.replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    for(unsigned i=0;i<12;i++)sites[2].original[i]=0xcc;
    fn=(void *)description;memory_copy(sites[2].replacement+2,&fn,8);
    sites[3]=(Fm5Site){.rva=0x288d8a,.size=5,.original={0xe8},.replacement={0xe8}};
    before=0xa02a20-0x288d8a-5;after=0x287f04-0x288d8a-5;
    memory_copy(sites[3].original+1,&before,4);memory_copy(sites[3].replacement+1,&after,4);
    return 4;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fm5TestBadgePrepare(unsigned char *g,Fm5Site *s,const wchar_t *d){return fm5_badge_prepare(g,s,d);}
__declspec(dllexport) void Fm5TestBadgeDatabase(void *d){fm5_badge_database(d);}
__declspec(dllexport) wchar_t *Fm5TestBadgeAlias(const wchar_t *p){return fm5_badge_alias(p);}
__declspec(dllexport) void Fm5TestBadgeFiles(void *f){fm5_badge_files(f);}
#endif
