#include "fm5/fm5_carbin.h"
#include "shared/boot_diagnostics.h"
#include "shared/addons.h"
#include "shared/addon_archives.h"
#include "shared/carbin_patch.h"
#include "shared/logging.h"
#include "shared/support.h"
typedef struct {char data[16];size_t length,capacity;} String;
typedef unsigned char (*Resolve)(void *,void **,String *,const String *,unsigned char,unsigned char,unsigned char,void **,unsigned char);
typedef struct Entry {struct Entry *next;unsigned revision;HANDLE keeper;char relative[];} Entry;
static unsigned char *image;
static wchar_t root[PATH_CAPACITY];
static SRWLOCK lock=SRWLOCK_INIT;
static volatile LONG thread;
static Entry *entries;
static unsigned entry_count,owners;
static volatile LONG lookup_reports;
static struct {void *volume;unsigned addon;} owner[128];
static void *cache_volume,*filesystem;
static void *folder_vtable[1+0x1b0/8];
static int absolute(const String *path);

static const char *text(const String *s){return s->capacity>=16?*(const char *const *)s->data:s->data;}
static String borrowed(const char *s){String value={.length=text_length(s),.capacity=31};memory_copy(value.data,&s,sizeof(s));return value;}
static int same(const char *a,size_t n,const char *b){if(text_length(b)!=n)return 0;for(size_t i=0;i<n;i++){char x=a[i],y=b[i];if(x=='/')x='\\';if(y=='/')y='\\';if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;}return 1;}
static int absolute(const String *path){return path && path->length>=12 && same(text(path),12,"game:\\media\\");}
static unsigned char exists(void *volume,const String *path){
    void *target=filesystem && absolute(path)?filesystem:volume;
    void *method=target==volume?*(void **)(image+0x22c7be8+0xc8):(*(void ***)target)[0xc8/8];
    return ((unsigned char (*)(void *,const String *,unsigned char))method)(target,path,1);
}
static void **open_sync(void *volume,void **out,const String *path,unsigned flags,unsigned char cache){
    void *target=filesystem && absolute(path)?filesystem:volume;
    void *method=target==volume?*(void **)(image+0x22c7be8+0x108):(*(void ***)target)[0x108/8];
    return ((void **(*)(void *,void **,const String *,unsigned,unsigned char))method)(target,out,path,flags,cache);
}
static void **open_async(void *volume,void **out,const String *path,unsigned flags,void *device,unsigned a,unsigned b,unsigned char c){
    void *target=filesystem && absolute(path)?filesystem:volume;
    void *method=target==volume?*(void **)(image+0x22c7be8+0x118):(*(void ***)target)[0x118/8];
    return ((void **(*)(void *,void **,const String *,unsigned,void *,unsigned,unsigned,unsigned char))method)(target,out,path,flags,device,a,b,c);
}
int fm5_carbin_physical(const wchar_t *path){size_t n=(size_t)lstrlenW(root);return n && path && (size_t)lstrlenW(path)>n && path[n]==L'\\' && CompareStringOrdinal(path,(int)n,root,(int)n,TRUE)==CSTR_EQUAL;}
void fm5_carbin_owner(void *volume,unsigned addon){if(owners<128){owner[owners].volume=volume;owner[owners++].addon=addon;*(void ***)volume=folder_vtable+1;}}
static int volume_owner(void *volume){for(unsigned i=0;i<owners;i++)if(owner[i].volume==volume)return (int)owner[i].addon;return addon_archives_volume_addon(volume);}
static wchar_t *physical(const wchar_t *folder,const char *relative){
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative,-1,NULL,0);if(!n)return NULL;
 wchar_t *p=HeapAlloc(GetProcessHeap(),0,PATH_CAPACITY*2);if(!p)return NULL;size_t at=(size_t)lstrlenW(folder);
 if(at+(size_t)n+2>=PATH_CAPACITY){HeapFree(GetProcessHeap(),0,p);return NULL;}
 memory_copy(p,folder,at*2);p[at++]=L'\\';MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative,-1,p+at,n);
 for(size_t i=at;i<at+(unsigned)n;i++)if(p[i]==L'/')p[i]=L'\\';return p;
}
static unsigned char *read_patch(const wchar_t *path,size_t *size){HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);if(f==INVALID_HANDLE_VALUE)return NULL;LARGE_INTEGER n;DWORD got;unsigned char *p=NULL;if(GetFileSizeEx(f,&n)&&n.QuadPart>0&&n.QuadPart<=16*1024*1024){p=HeapAlloc(GetProcessHeap(),0,(size_t)n.QuadPart+1);if(p && ReadFile(f,p,(DWORD)n.QuadPart,&got,NULL)&&got==n.QuadPart){p[got]=0;*size=got;}else{if(p)HeapFree(GetProcessHeap(),0,p);p=NULL;}}CloseHandle(f);return p;}
static unsigned char *read_base(void *fs,const String *path,size_t *size,int *source){
 void *volume=NULL;String relative={.capacity=15};Resolve resolve=(Resolve)(image+0x797bf0);unsigned char *data=NULL;
 if(resolve(fs,&volume,&relative,path,0,0,0,NULL,0)&&volume){
  *source=volume_owner(volume);
  String *root_name=(String *)((unsigned char *)volume+0x28);
  if(*source<0 && (*(void ***)volume==(void **)(image+0x22c7be8) || *(void ***)volume==folder_vtable+1) && (same(text(root_name),root_name->length,"game:\\mediaoverride\\") || same(text(root_name),root_name->length,"game:\\mediapc\\")))*source=-2;
  void *file=NULL;
  ((void **(*)(void *,void **,const String *,unsigned,unsigned char))(*(void ***)volume)[0x108/8])(volume,&file,&relative,0x8000001b,1);
  if(file){void **v=*(void ***)file;unsigned n=((unsigned (*)(void *))v[0xa0/8])(file);
   if(n && n<=32*1024*1024){data=HeapAlloc(GetProcessHeap(),0,n);if(data && ((unsigned (*)(void *,void *,unsigned))v[0x60/8])(file,data,n)!=n){HeapFree(GetProcessHeap(),0,data);data=NULL;}if(data)*size=n;}
   ((void (*)(void *))v[2])(file);
  }
  ((void (*)(void *))(*(void ***)volume)[2])(volume);
 }
 if(relative.capacity>=16)((void (*)(void *))(image+0x1cdc30))(*(void **)relative.data);return data;
}
static int publish(void *fs,const char *relative,unsigned char *data,size_t size,Entry *entry){
 wchar_t *path=physical(root,relative);if(!path)return 0;
 CreateDirectoryW(root,NULL);size_t start=(size_t)lstrlenW(root)+1;
 for(size_t i=start;path[i];i++)if(path[i]==L'\\'){path[i]=0;CreateDirectoryW(path,NULL);path[i]=L'\\';}
 HANDLE f=CreateFileW(path,GENERIC_WRITE|GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,NULL);DWORD n;
 int ok=f!=INVALID_HANDLE_VALUE && WriteFile(f,data,(DWORD)size,&n,NULL) && n==size;
 if(ok && !cache_volume){
  int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,root,-1,NULL,0,NULL,NULL);char *name=HeapAlloc(GetProcessHeap(),0,(size_t)bytes+1);
  if(name && WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,root,-1,name,bytes,NULL,NULL)){
   name[bytes-1]='\\';name[bytes]=0;String source=borrowed(name),dest=borrowed("game:\\media\\");
   void *storage=((void *(*)(size_t))(image+0x1cdbb0))(0x50);void *volume=storage?((void *(*)(void *,const String *,unsigned char))(image+0x794a50))(storage,&source,1):NULL;
   if(volume){*(void ***)volume=folder_vtable+1;void **v=*(void ***)volume;((void (*)(void *))v[1])(volume);
    ok=((unsigned char (*)(void *,const String *,void **,unsigned,unsigned char,unsigned char,const String *))(*(void ***)fs)[0x1a8/8])(fs,&dest,&volume,0x7ffffffc,0,0,NULL);
    if(ok){cache_volume=volume;((void (*)(void *))(*(void ***)fs)[0x1b8/8])(fs);}((void (*)(void *))v[2])(volume);
   }else ok=0;
  }else ok=0;
  if(name)HeapFree(GetProcessHeap(),0,name);
 }
 if(ok)entry->keeper=f;else if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);HeapFree(GetProcessHeap(),0,path);return ok;
}
static void prepare(void *fs,const String *path){
 const char *name=text(path);size_t n=path->length;
 if(n<24 || n>1000 || !same(name,12,"game:\\media\\") || !same(name+n-7,7,".carbin") || GetCurrentThreadId()==(DWORD)InterlockedCompareExchange(&thread,0,0))return;
 const char *relative=name+12;
 // Reject traversal and Windows special characters before any disk operation.
 size_t component=12;
 for(size_t i=12;i<=n;i++){
  if(i==n || name[i]=='\\' || name[i]=='/'){
   if(i==component || (i-component==1&&name[component]=='.') || (i-component==2&&name[component]=='.'&&name[component+1]=='.'))return;component=i+1;
  }else if((unsigned char)name[i]<32 || name[i]==':' || name[i]=='*' || name[i]=='?' || name[i]=='<' || name[i]=='>' || name[i]=='"' || name[i]=='|')return;
 }
 AcquireSRWLockExclusive(&lock);unsigned revision=addons_media_revision();
 for(Entry *p=entries;p;p=p->next)if(p->revision==revision && same(relative,n-12,p->relative)){ReleaseSRWLockExclusive(&lock);return;}
 if(entry_count==1024){ReleaseSRWLockExclusive(&lock);return;}
 Entry *entry=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*entry)+n-12+1);
 if(!entry){ReleaseSRWLockExclusive(&lock);return;}
 memory_copy(entry->relative,relative,n-12);entry->revision=revision;entry->next=entries;entries=entry;entry_count++;
 uint64_t diagnostic_begin=boot_diag_now();
 InterlockedExchange(&thread,(LONG)GetCurrentThreadId());
 char patch_name[1010];memory_copy(patch_name,relative,n-12);memory_copy(patch_name+n-12,".json",6);
 wchar_t *patches[128];for(unsigned i=0;i<128;i++)patches[i]=NULL;unsigned total=addons_count();int any=0;
 for(unsigned i=0;i<total;i++)if(addons_media_active(i)){
  const wchar_t *folder=addons_media_folder(i);if(folder){patches[i]=physical(folder,patch_name);if(patches[i] && GetFileAttributesW(patches[i])!=INVALID_FILE_ATTRIBUTES)any=1;else{if(patches[i])HeapFree(GetProcessHeap(),0,patches[i]);patches[i]=NULL;}}
 }
 size_t bytes=0;int source=-1,applied=0;unsigned char *data=any || addon_ids_changed(-1)?read_base(fs,path,&bytes,&source):NULL;
 if(data && source>=0 && addon_ids_changed(source))applied=carbin_remap_ids(data,bytes,addon_ids_carbin_value,(void *)(uintptr_t)(unsigned)source);
 for(unsigned i=0;i<total;i++)if(patches[i]){
  if(data && source!=-2 && (int)i>=source){size_t size=0;unsigned char *raw=read_patch(patches[i],&size);JsonDocument doc={0};const char *error=NULL;void *temporary=NULL;unsigned char *merged=NULL;size_t merged_size=0;
   if(raw && addon_json_parse(&doc,(char *)raw,size) && addon_ids_carbin_json(i,doc.root,&temporary) && carbin_patch_apply(data,bytes,doc.root,&merged,&merged_size,&error)){
    HeapFree(GetProcessHeap(),0,data);data=merged;bytes=merged_size;applied=1;
    if(bytes>=23){unsigned ordinal;memory_copy(&ordinal,data+19,4);unsigned mapped=addon_ids_lookup(i,"Data_Car","Id",ordinal);memory_copy(data+19,&mapped,4);}log_append("FM5 CARBIN: record patch applied.\r\n");
   }else{log_append("FM5 CARBIN: patch rejected; original retained. ");if(error)log_append(error);log_append("\r\n");}
   addon_ids_free_temporary(temporary);addon_json_free(&doc);
  }
  HeapFree(GetProcessHeap(),0,patches[i]);
 }
 if(data){if(applied && !publish(fs,relative,data,bytes,entry))log_append("FM5 CARBIN: temporary asset publish failed.\r\n");HeapFree(GetProcessHeap(),0,data);}
 InterlockedExchange(&thread,0);ReleaseSRWLockExclusive(&lock);
 boot_diag_end("assets.carbin_patch_build",patch_name,diagnostic_begin);
}
static unsigned char resolve(void *fs,void **volume,String *relative,const String *path,unsigned char a,unsigned char b,unsigned char c,void **mount,unsigned char d){
 filesystem=fs;prepare(fs,path);unsigned char found=((Resolve)(image+0x797bf0))(fs,volume,relative,path,a,b,c,mount,d);
 if(path->length>=7 && same(text(path)+path->length-7,7,".carbin") && InterlockedIncrement(&lookup_reports)<=12){
     log_append("FM5 CARBIN: ");log_append(text(path));
     if(found && volume && *volume){int id=volume_owner(*volume);log_append_value("; package index=",id);}
     else log_append("; native file lookup failed.\r\n");
 }
 return found;
}
unsigned fm5_carbin_prepare(unsigned char *base,Fm5Site *sites,const wchar_t *directory){
 image=base;filesystem=NULL;owners=0;
 memory_copy(folder_vtable,base+0x22c7be8-8,sizeof(folder_vtable));
 folder_vtable[1+0xc8/8]=(void *)exists;folder_vtable[1+0x108/8]=(void *)open_sync;folder_vtable[1+0x118/8]=(void *)open_async;
 root[0]=0;(void)directory;addons_cache_folder(root,L"fm5-carbins");
 uintptr_t original=(uintptr_t)(base+0x797bf0),replacement=(uintptr_t)resolve;
 sites[0]=(Fm5Site){0x22b8a70+0x1d0,8,{0},{0},0};memory_copy(sites[0].original,&original,8);memory_copy(sites[0].replacement,&replacement,8);return 1;
}
