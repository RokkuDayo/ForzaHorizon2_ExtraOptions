#include "shared/boot_diagnostics.h"
#include "shared/addon_cache.h"
#include "shared/addon_cache_io.h"
#include "shared/sha256.h"
#include "shared/support.h"
#include "shared/logging.h"

typedef struct {
    unsigned char magic[8],key[32],database_hash[32];
    unsigned char header_hash[32];
    unsigned count,plan_bytes;
    unsigned char active[128],excluded[128];
} CacheHeader;
static const unsigned char magic[8]={'E','O','D','B',5,0,0,0};
static int hash_file(const wchar_t *path,Sha256Context *hash){
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    if(file==INVALID_HANDLE_VALUE)return 0;
    unsigned char *buffer=HeapAlloc(GetProcessHeap(),0,65536);DWORD bytes;int ok=buffer!=NULL;
    while(ok){ok=ReadFile(file,buffer,65536,&bytes,NULL)!=0;if(!ok || !bytes)break;
        sha256_update(hash,buffer,bytes);}
    if(buffer)HeapFree(GetProcessHeap(),0,buffer);CloseHandle(file);return ok;
}
static char *utf8(const wchar_t *path){
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,NULL,0,NULL,NULL);
    char *out=n?HeapAlloc(GetProcessHeap(),0,n):NULL;
    if(out && !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,out,n,NULL,NULL)){
        HeapFree(GetProcessHeap(),0,out);out=NULL;}
    return out;
}
static void *open_file(const wchar_t *path){
    void *native=addon_cache_io_open(path);
    if(native)return native;
#ifndef FH2_TWEAKS_TEST
    return NULL;
#endif
    char *name=utf8(path);void *db=NULL;const AddonSqlite *api=addon_database_api();
    if(!name)return NULL;
    int failed=api->open(name,&db);HeapFree(GetProcessHeap(),0,name);
    if(failed){if(db)api->close(db);return NULL;}return db;
}
static int backup(void *destination,void *source){
    const AddonSqlite *api=addon_database_api();
    void *copy=api->backup_init(destination,"main",source,"main");
    if(!copy)return 0;
    int result=api->backup_step(copy,-1),finish=api->backup_finish(copy);
    return result==101 && !finish;
}
static int temporary(const wchar_t *directory,wchar_t *path){
    return GetTempFileNameW(directory,L"eod",0,path)!=0;
}
static int snapshot(void *source,const wchar_t *path){
    const AddonSqlite *api=addon_database_api();void *db=open_file(path);
    if(!db)return 0;int ok=backup(db,source);if(api->close(db))ok=0;return ok;
}
// File metadata is cheap even for large texture packs. Hash JSON content as
// well, so database/manifest changes are noticed even if timestamps are kept.
static int hash_tree(Sha256Context *hash,const wchar_t *root,unsigned depth,unsigned *files){
    if(depth>32 || *files>100000)return 0;
    wchar_t *paths=HeapAlloc(GetProcessHeap(),0,2*PATH_CAPACITY*sizeof(wchar_t));
    if(!paths)return 0;wchar_t *pattern=paths,*path=paths+PATH_CAPACITY;
    if(!path_join(pattern,root,L"*")){HeapFree(GetProcessHeap(),0,paths);return 0;}
    WIN32_FIND_DATAW data;HANDLE scan=FindFirstFileW(pattern,&data);int ok=1;
    if(scan==INVALID_HANDLE_VALUE){DWORD e=GetLastError();HeapFree(GetProcessHeap(),0,paths);
        return e==ERROR_FILE_NOT_FOUND;}
    do{
        if(!lstrcmpiW(data.cFileName,L".") || !lstrcmpiW(data.cFileName,L".."))continue;
        /* Plugins load independently of the database cache. Their INIs and
           log files (including .txt logs) may change on every boot/frame.
           None is an input to the loader's database merge/ID planning. */
        if(depth==0 && (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) &&
           !lstrcmpiW(data.cFileName,L"plugins"))continue;
        size_t extension=(size_t)lstrlenW(data.cFileName);
        if(extension>=4 && !lstrcmpiW(data.cFileName+extension-4,L".log"))continue;
        if(++*files>100000 || !path_join(path,root,data.cFileName) ||
           (data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){ok=0;break;}
        sha256_update(hash,(const unsigned char *)data.cFileName,
                      ((size_t)lstrlenW(data.cFileName)+1)*sizeof(wchar_t));
        sha256_update(hash,(const unsigned char *)&data.dwFileAttributes,sizeof(DWORD));
        if(!(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
            sha256_update(hash,(const unsigned char *)&data.ftLastWriteTime,sizeof(FILETIME));
        sha256_update(hash,(const unsigned char *)&data.nFileSizeHigh,2*sizeof(DWORD));
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
            if(!hash_tree(hash,path,depth+1,files)){ok=0;break;}
        }else{
            size_t n=(size_t)lstrlenW(data.cFileName);
            if(n>=5 && !lstrcmpiW(data.cFileName+n-5,L".json") && !hash_file(path,hash)){ok=0;break;}
        }
    }while(FindNextFileW(scan,&data));
    if(ok && GetLastError()!=ERROR_NO_MORE_FILES)ok=0;
    FindClose(scan);HeapFree(GetProcessHeap(),0,paths);return ok;
}
static void hash_json(Sha256Context *hash,const JsonValue *value){
    for(;value;value=value->next){
        sha256_update(hash,(const unsigned char *)&value->kind,sizeof(value->kind));
        sha256_update(hash,(const unsigned char *)&value->length,sizeof(value->length));
        if(value->text)sha256_update(hash,(const unsigned char *)value->text,value->length);
        if(value->name)sha256_update(hash,(const unsigned char *)value->name,text_length(value->name)+1);
        if(value->child)hash_json(hash,value->child);
        static const unsigned char end=255;sha256_update(hash,&end,1);
    }
}
int addon_cache_key(void *source,const wchar_t *directory,unsigned settings,unsigned char key[32]){
    if(!directory || !*directory || !CreateDirectoryW(directory,NULL)){
        if(!directory || !*directory || GetLastError()!=ERROR_ALREADY_EXISTS)return 0;
    }
    wchar_t *path=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,PATH_CAPACITY*sizeof(wchar_t));
    if(!path)return 0;
    uint64_t begin=boot_diag_now();
    int ok=temporary(directory,path) && snapshot(source,path);
    boot_diag_end("cache.snapshot_base_database",NULL,begin);
    Sha256Context hash;sha256_initialize(&hash);
    sha256_update(&hash,magic,sizeof(magic));
    sha256_update(&hash,(const unsigned char *)&settings,sizeof(settings));
    sha256_update(&hash,(const unsigned char *)MOD_VERSION,sizeof(MOD_VERSION));
    begin=boot_diag_now();if(ok)ok=hash_file(path,&hash);
    boot_diag_end("cache.hash_base_snapshot",NULL,begin);
    if(path[0])DeleteFileW(path);
    unsigned total=addons_count(),files=0;
    sha256_update(&hash,(const unsigned char *)&total,sizeof(total));
    for(unsigned i=0;ok && i<total;++i){
        const wchar_t *folder=addons_directory(i);
        unsigned valid=folder!=NULL;sha256_update(&hash,(const unsigned char *)&valid,sizeof(valid));
        hash_json(&hash,addons_manifest_document(i));
        hash_json(&hash,addons_database_document(i));
        hash_json(&hash,addons_strings_document(i));
        if(folder){sha256_update(&hash,(const unsigned char *)folder,((size_t)lstrlenW(folder)+1)*2);
            begin=boot_diag_now();ok=hash_tree(&hash,folder,0,&files);
            boot_diag_end_w("cache.scan_mod_tree",folder,begin);}
    }
    HeapFree(GetProcessHeap(),0,path);
    if(ok)sha256_finalize(&hash,key);return ok;
}
static int paths(const wchar_t *directory,wchar_t *db,wchar_t *header){
    return path_join(db,directory,L"merged.sqlite") && path_join(header,directory,L"merged.cache");
}
void *addon_cache_load(const wchar_t *directory,const unsigned char key[32],unsigned count,
                      unsigned char active[128],unsigned char excluded[128]){
    wchar_t *names=HeapAlloc(GetProcessHeap(),0,2*PATH_CAPACITY*sizeof(wchar_t));
    if(!names)return NULL;void *result=NULL,*plan=NULL;static const CacheHeader empty;CacheHeader header;memory_copy(&header,&empty,sizeof(header));DWORD read;
    if(!paths(directory,names,names+PATH_CAPACITY))goto done;
    HANDLE file=CreateFileW(names+PATH_CAPACITY,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE)goto done;
    LARGE_INTEGER size;int ok=GetFileSizeEx(file,&size) && size.QuadPart>=(LONGLONG)sizeof(header) &&
        ReadFile(file,&header,sizeof(header),&read,NULL) && read==sizeof(header);
    ok=ok && header.plan_bytes>=132 && header.plan_bytes<=ADDON_ID_CACHE_LIMIT && size.QuadPart==(LONGLONG)(sizeof(header)+header.plan_bytes);
    if(ok){plan=HeapAlloc(GetProcessHeap(),0,header.plan_bytes);
        ok=plan && ReadFile(file,plan,header.plan_bytes,&read,NULL) && read==header.plan_bytes;}
    CloseHandle(file);
    if(!ok)goto done;
    unsigned char expected_header[32],actual_header[32];memory_copy(expected_header,header.header_hash,32);
    for(unsigned i=0;i<32;i++)header.header_hash[i]=0;
    Sha256Context header_sha;sha256_initialize(&header_sha);sha256_update(&header_sha,(const unsigned char *)&header,sizeof(header));
    sha256_update(&header_sha,plan,header.plan_bytes);sha256_finalize(&header_sha,actual_header);
    if(!ok || !memory_equal(expected_header,actual_header,32) || !memory_equal(header.magic,magic,8) || !memory_equal(header.key,key,32) || header.count!=count)goto done;
    for(unsigned i=0;i<128;++i)if(header.active[i]>1 || header.excluded[i]>1 ||
        (i>=count && (header.active[i] || header.excluded[i])))goto done;
    Sha256Context hash;unsigned char digest[32];sha256_initialize(&hash);
    if(!hash_file(names,&hash))goto done;sha256_finalize(&hash,digest);
    if(!memory_equal(digest,header.database_hash,32))goto done;
    uint64_t begin=boot_diag_now();
    void *cached=open_file(names);
    if(cached){result=addon_database_clone(cached);addon_database_api()->close(cached);}
    boot_diag_end("cache.open_clone_cached_database",NULL,begin);
    if(result){
        begin=boot_diag_now();ok=addon_ids_cache_import(plan,header.plan_bytes,count);
        boot_diag_end("cache.restore_ID_plan",ok?"restored":"invalid",begin);
        if(ok){memory_copy(active,header.active,128);memory_copy(excluded,header.excluded,128);}
        else{addon_database_api()->close(result);result=NULL;}
    }
done:if(plan)HeapFree(GetProcessHeap(),0,plan);HeapFree(GetProcessHeap(),0,names);return result;
}
static void addon_cache_store_impl(const wchar_t *directory,const unsigned char key[32],void *merged,unsigned count,
                       const unsigned char active[128],const unsigned char excluded[128]){
    wchar_t *names=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,4*PATH_CAPACITY*sizeof(wchar_t));
    if(!names)return;wchar_t *db=names,*meta=names+PATH_CAPACITY,*temp=names+2*PATH_CAPACITY,*head=names+3*PATH_CAPACITY;
    void *plan=NULL;unsigned plan_bytes=0;
    static const CacheHeader empty;CacheHeader header;memory_copy(&header,&empty,sizeof(header));
    int ok=addon_ids_cache_export(&plan,&plan_bytes) && paths(directory,db,meta) && temporary(directory,temp) && snapshot(merged,temp);
    Sha256Context hash;sha256_initialize(&hash);
    if(ok)ok=hash_file(temp,&hash);
    if(ok){sha256_finalize(&hash,header.database_hash);memory_copy(header.magic,magic,8);
        memory_copy(header.key,key,32);header.count=count;header.plan_bytes=plan_bytes;memory_copy(header.active,active,128);memory_copy(header.excluded,excluded,128);
        Sha256Context header_sha;sha256_initialize(&header_sha);sha256_update(&header_sha,(const unsigned char *)&header,sizeof(header));
        sha256_update(&header_sha,plan,plan_bytes);sha256_finalize(&header_sha,header.header_hash);
        ok=temporary(directory,head);}
    if(ok){HANDLE file=CreateFileW(head,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);DWORD n;
        ok=file!=INVALID_HANDLE_VALUE && WriteFile(file,&header,sizeof(header),&n,NULL) && n==sizeof(header) &&
            WriteFile(file,plan,plan_bytes,&n,NULL) && n==plan_bytes && FlushFileBuffers(file);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    // Header last: interrupted/mismatched pairs are rejected by the payload hash.
    if(ok)ok=MoveFileExW(temp,db,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(ok)ok=MoveFileExW(head,meta,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(temp[0])DeleteFileW(temp);if(head[0])DeleteFileW(head);
    log_append(ok?"MODS: merged database cache saved.\r\n":"MODS: database cache write failed; in-memory merge retained.\r\n");
    if(plan)HeapFree(GetProcessHeap(),0,plan);HeapFree(GetProcessHeap(),0,names);
}

void addon_cache_store(const wchar_t *directory,const unsigned char key[32],void *merged,unsigned count,
                       const unsigned char active[128],const unsigned char excluded[128]){
    uint64_t begin=boot_diag_now();addon_cache_store_impl(directory,key,merged,count,active,excluded);
    boot_diag_end("cache.persist_merged_database",NULL,begin);
}
