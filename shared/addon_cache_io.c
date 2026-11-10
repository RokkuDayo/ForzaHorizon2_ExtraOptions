#include "shared/addon_cache_io.h"
#include "shared/addon_database.h"
#include "shared/support.h"
// Private SQLite VFS. It is never the default and is used only by cache opens.
// Cache connections run without journals and are closed before atomic publication.
typedef struct Methods Methods;
typedef struct {const Methods *methods;HANDLE handle;int readonly;} File;
struct Methods {
 int version;
 int (*close)(File *);int (*read)(File *,void *,int,int64_t);int (*write)(File *,const void *,int,int64_t);
 int (*truncate)(File *,int64_t);int (*sync)(File *,int);int (*size)(File *,int64_t *);
 int (*lock)(File *,int);int (*unlock)(File *,int);int (*reserved)(File *,int *);
 int (*control)(File *,int,void *);int (*sector)(File *);int (*device)(File *);
};
typedef struct Vfs Vfs;
struct Vfs {
 int version,file_size,path_size;Vfs *next;const char *name;void *app;
 int (*open)(Vfs *,const char *,File *,int,int *);int (*remove)(Vfs *,const char *,int);
 int (*access)(Vfs *,const char *,int,int *);int (*fullpath)(Vfs *,const char *,int,char *);
 void *(*dlopen)(Vfs *,const char *);void (*dlerror)(Vfs *,int,char *);void *(*dlsym)(Vfs *,void *,const char *);void (*dlclose)(Vfs *,void *);
 int (*random)(Vfs *,int,char *);int (*sleep)(Vfs *,int);int (*time)(Vfs *,double *);int (*error)(Vfs *,int,char *);
};
static unsigned char *image;
static int fm5_mode,registered;
static wchar_t *wide(const char *name){
 int n=name?MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,NULL,0):0;
 wchar_t *p=n?HeapAlloc(GetProcessHeap(),0,n*sizeof(wchar_t)):NULL;
 if(p && !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name,-1,p,n)){HeapFree(GetProcessHeap(),0,p);return NULL;}return p;
}
static int close_file(File *f){int ok=CloseHandle(f->handle)!=0;f->methods=NULL;return ok?0:10;}
static int read_file(File *f,void *out,int n,int64_t offset){
 LARGE_INTEGER position;position.QuadPart=offset;DWORD got=0;
 if(n<0 || offset<0 || !SetFilePointerEx(f->handle,position,NULL,FILE_BEGIN) || !ReadFile(f->handle,out,(DWORD)n,&got,NULL))return 266;
 if(got!=(DWORD)n){unsigned char *p=out;for(unsigned i=got;i<(unsigned)n;i++)p[i]=0;return 522;}return 0;
}
static int write_file(File *f,const void *data,int n,int64_t offset){
 LARGE_INTEGER position;position.QuadPart=offset;DWORD written=0;
 return n>=0 && offset>=0 && SetFilePointerEx(f->handle,position,NULL,FILE_BEGIN) && WriteFile(f->handle,data,(DWORD)n,&written,NULL) && written==(DWORD)n?0:778;
}
static int truncate_file(File *f,int64_t size){LARGE_INTEGER p;p.QuadPart=size;return size>=0 && SetFilePointerEx(f->handle,p,NULL,FILE_BEGIN) && SetEndOfFile(f->handle)?0:1546;}
static int sync_file(File *f,int flags){(void)flags;return f->readonly || FlushFileBuffers(f->handle)?0:1034;}
static int size_file(File *f,int64_t *size){LARGE_INTEGER p;if(!GetFileSizeEx(f->handle,&p))return 1802;*size=p.QuadPart;return 0;}
// Each cache connection has exclusive ownership; the game never opens it.
static int lock_file(File *f,int lock){(void)f;(void)lock;return 0;}
static int reserved_file(File *f,int *out){(void)f;*out=0;return 0;}
static int control_file(File *f,int op,void *arg){(void)f;(void)op;(void)arg;return 12;}
static int sector_file(File *f){(void)f;return 4096;}
static int device_file(File *f){(void)f;return 0;}
static const Methods methods={1,close_file,read_file,write_file,truncate_file,sync_file,size_file,lock_file,lock_file,reserved_file,control_file,sector_file,device_file};
static int open_file(Vfs *v,const char *name,File *f,int flags,int *out){
 (void)v;f->methods=NULL;wchar_t *p=wide(name);if(!p)return 14;
 f->readonly=!(flags&2);
 f->handle=CreateFileW(p,f->readonly?GENERIC_READ:(GENERIC_READ|GENERIC_WRITE),0,NULL,(flags&4)?OPEN_ALWAYS:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
 HeapFree(GetProcessHeap(),0,p);if(f->handle==INVALID_HANDLE_VALUE)return 14;
 f->methods=&methods;if(out)*out=flags;return 0;
}
static int remove_file(Vfs *v,const char *name,int sync){(void)v;(void)sync;wchar_t *p=wide(name);if(!p)return 10;int ok=DeleteFileW(p) || GetLastError()==ERROR_FILE_NOT_FOUND;HeapFree(GetProcessHeap(),0,p);return ok?0:10;}
static int access_file(Vfs *v,const char *name,int flags,int *out){(void)v;(void)flags;wchar_t *p=wide(name);*out=p && GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES;if(p)HeapFree(GetProcessHeap(),0,p);return 0;}
static int fullpath(Vfs *v,const char *name,int n,char *out){(void)v;size_t size=text_length(name);if(size>=(size_t)n)return 14;memory_copy(out,name,size+1);return 0;}
static int random_bytes(Vfs *v,int n,char *out){(void)v;uint64_t x=GetTickCount64()^(uintptr_t)out;for(int i=0;i<n;i++){x=x*6364136223846793005ull+1;out[i]=(char)(x>>32);}return n;}
static int sleep_vfs(Vfs *v,int us){(void)v;Sleep((DWORD)((us+999)/1000));return us;}
static int time_vfs(Vfs *v,double *out){(void)v;FILETIME ft;GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER t;t.LowPart=ft.dwLowDateTime;t.HighPart=ft.dwHighDateTime;*out=2305813.5+(double)t.QuadPart/864000000000.0;return 0;}
static int error_vfs(Vfs *v,int n,char *out){(void)v;if(n>0)*out=0;return (int)GetLastError();}
static Vfs vfs={1,sizeof(File),32768,NULL,"eo-cache",NULL,open_file,remove_file,access_file,fullpath,NULL,NULL,NULL,NULL,random_bytes,sleep_vfs,time_vfs,error_vfs};
void addon_cache_io_bind(unsigned char *base,int fm5){if(image!=base || fm5_mode!=fm5)registered=0;image=base;fm5_mode=fm5;}
void *addon_cache_io_open(const wchar_t *path){
 if(!image)return NULL;
 if(!registered){if(((int (*)(Vfs *,int))(image+(fm5_mode?0x9fe0:0x9ff0)))(&vfs,0))return NULL;registered=1;}
 int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,NULL,0,NULL,NULL);
 char *p=n?HeapAlloc(GetProcessHeap(),0,n):NULL;void *db=NULL;
 if(!p || !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,p,n,NULL,NULL)){if(p)HeapFree(GetProcessHeap(),0,p);return NULL;}
 int result=((int (*)(const char *,void **,int,const char *))(image+(fm5_mode?0x7d6f0:0x7d700)))(p,&db,6,"eo-cache");HeapFree(GetProcessHeap(),0,p);
 if(result){if(db)addon_database_api()->close(db);return NULL;}
 const AddonSqlite *api=addon_database_api();const char *pragmas[]={"PRAGMA journal_mode=OFF","PRAGMA synchronous=OFF","PRAGMA temp_store=MEMORY"};
 for(unsigned i=0;i<ARRAY_COUNT(pragmas);i++){void *s=NULL;int error=api->prepare(db,pragmas[i],-1,&s,NULL);if(!error){int rc;do{rc=api->step(s);}while(rc==100);error=rc!=101;api->finalize(s);}if(error){api->close(db);return NULL;}}
 return db;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void *Fh2TestCacheOpen(const wchar_t *path){return addon_cache_io_open(path);}
#endif
