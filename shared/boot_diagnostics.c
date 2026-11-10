#include "shared/boot_diagnostics.h"
#ifdef EO_BOOT_DIAGNOSTICS
#include "shared/support.h"

/* Diagnostic build only. No worker, polling, database changes or GPU queries.
   One file handle; bounded aggregate storage and event output. Durations are
   inclusive wall time, so nested/parallel spans must never be summed as boot time. */
typedef struct {char stage[64],detail[192];uint64_t total,maximum;unsigned calls;} Timing;
static Timing timings[1024];
static unsigned used,events,dropped;
static uint64_t frequency,origin,logging_ticks;
static HANDLE output=INVALID_HANDLE_VALUE;
static SRWLOCK lock=SRWLOCK_INIT;

uint64_t boot_diag_now(void){LARGE_INTEGER n;return QueryPerformanceCounter(&n)?(uint64_t)n.QuadPart:0;}
static char *text(char *p,const char *s,size_t limit){
    if(s)for(size_t i=0;s[i] && i<limit;++i)*p++=s[i];
    return p;
}
static char *number(char *p,uint64_t value){
    char digits[24];unsigned n=0;do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
    while(n)*p++=digits[--n];return p;
}
static char *milliseconds(char *p,uint64_t ticks){
    uint64_t us=frequency?(ticks/frequency)*1000000+(ticks%frequency)*1000000/frequency:0;
    p=number(p,us/1000);*p++='.';*p++=(char)('0'+us/100%10);
    *p++=(char)('0'+us/10%10);*p++=(char)('0'+us%10);return p;
}
static void write_line(char *line,char *end){DWORD bytes;*end++='\r';*end++='\n';WriteFile(output,line,(DWORD)(end-line),&bytes,NULL);}
void boot_diag_start(const wchar_t *directory,unsigned title,uint64_t start){
    DWORD error=GetLastError();LARGE_INTEGER hz;
    if(!QueryPerformanceFrequency(&hz) || hz.QuadPart<=0){SetLastError(error);return;}
    wchar_t *path=HeapAlloc(GetProcessHeap(),0,PATH_CAPACITY*sizeof(wchar_t));
    if(!path){SetLastError(error);return;}
    AcquireSRWLockExclusive(&lock);
    if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
    output=INVALID_HANDLE_VALUE;used=events=dropped=0;logging_ticks=0;
    frequency=(uint64_t)hz.QuadPart;origin=start?start:boot_diag_now();
    if(path_join(path,directory,title==0x4ce1dfab ? L"FH3_BootPerformance.log" :
                 title==0x7b35d917 ? L"FM5_BootPerformance.log" : L"FH2_BootPerformance.log"))
        output=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(output!=INVALID_HANDLE_VALUE){
        const char *header="EO boot diagnostics " MOD_VERSION "\r\n"
            "Inclusive wall times; nested/parallel spans overlap. Not CPU or total game loading time.\r\n"
            "Events capped at 4096. Summaries rank cumulative time.\r\n"
            "event\tsince_handoff_ms\tduration_ms\tthread\tstage\tdetail\r\n";
        DWORD bytes;WriteFile(output,header,(DWORD)text_length(header),&bytes,NULL);
    }
    ReleaseSRWLockExclusive(&lock);HeapFree(GetProcessHeap(),0,path);SetLastError(error);
}
void boot_diag_end(const char *stage,const char *detail,uint64_t begin){
    DWORD error=GetLastError();uint64_t end=boot_diag_now();
    if(!begin || end<begin){SetLastError(error);return;}
    char name[64]={0},item[192]={0};text(name,stage,63);text(item,detail,191);
    /* Do not split a UTF-8 path/name at the aggregate-key boundary. */
    if(detail && text_length(item)==191 && ((unsigned char)detail[191]&0xc0)==0x80){
        unsigned n=191;while(n && ((unsigned char)item[n-1]&0xc0)==0x80)--n;
        if(n)--n;item[n]=0;
    }
    for(unsigned i=0;name[i];++i)if((unsigned char)name[i]<32)name[i]=' ';
    for(unsigned i=0;item[i];++i)if((unsigned char)item[i]<32)item[i]=' ';
    AcquireSRWLockExclusive(&lock);
    if(output!=INVALID_HANDLE_VALUE){
        unsigned i;for(i=0;i<used;++i)if(text_length(name)==text_length(timings[i].stage) && memory_equal(name,timings[i].stage,text_length(name)) && text_length(item)==text_length(timings[i].detail) && memory_equal(item,timings[i].detail,text_length(item)))break;
        uint64_t duration=end-begin;
        if(i==used && used<ARRAY_COUNT(timings)){
            memory_copy(timings[i].stage,name,sizeof(name));memory_copy(timings[i].detail,item,sizeof(item));
            timings[i].total=timings[i].maximum=0;timings[i].calls=0;++used;
        }
        if(i<used){Timing *t=timings+i;t->total+=duration;++t->calls;if(duration>t->maximum)t->maximum=duration;}
        else ++dropped;
        if(events<4096){
            char line[512],*p=text(line,"span\t",5);p=milliseconds(p,end-origin);*p++='\t';
            p=milliseconds(p,duration);*p++='\t';p=number(p,GetCurrentThreadId());*p++='\t';
            p=text(p,name,63);*p++='\t';p=text(p,item,191);write_line(line,p);++events;
        }
        logging_ticks+=boot_diag_now()-end;
    }
    ReleaseSRWLockExclusive(&lock);SetLastError(error);
}
void boot_diag_end_w(const char *stage,const wchar_t *detail,uint64_t begin){
    DWORD error=GetLastError();char name[768];name[0]=0;
    if(detail){int length=lstrlenW(detail);if(length>191)length=191;
        int bytes=WideCharToMultiByte(CP_UTF8,0,detail,length,name,sizeof(name)-1,NULL,NULL);
        name[bytes>0?bytes:0]=0;}
    boot_diag_end(stage,name,begin);SetLastError(error);
}
void boot_diag_summary(const char *milestone){
    DWORD error=GetLastError();uint64_t begin=boot_diag_now();
    AcquireSRWLockExclusive(&lock);
    if(output!=INVALID_HANDLE_VALUE){
        char line[512],*p=text(line,"summary\t",8);p=text(p,milestone,191);
        p=text(p,"\telapsed_ms=",12);p=milliseconds(p,begin-origin);
        p=text(p,"\tlogger_ms=",11);p=milliseconds(p,logging_ticks);
        p=text(p,"\tdropped_keys=",14);p=number(p,dropped);
        p=text(p,"\tevents=",8);p=number(p,events);write_line(line,p);
        p=text(line,"rank\ttotal_ms\tmax_ms\tcalls\tstage\tdetail",48);write_line(line,p);
        static const unsigned char empty[1024];unsigned char chosen[1024];
        memory_copy(chosen,empty,sizeof(chosen));
        for(unsigned rank=0;rank<30 && rank<used;++rank){
            unsigned best=used;for(unsigned i=0;i<used;++i)if(!chosen[i] && (best==used || timings[i].total>timings[best].total))best=i;
            chosen[best]=1;Timing *t=timings+best;p=number(line,rank+1);*p++='\t';
            p=milliseconds(p,t->total);*p++='\t';p=milliseconds(p,t->maximum);*p++='\t';
            p=number(p,t->calls);*p++='\t';p=text(p,t->stage,63);*p++='\t';p=text(p,t->detail,191);write_line(line,p);
        }
        logging_ticks+=boot_diag_now()-begin;
    }
    ReleaseSRWLockExclusive(&lock);SetLastError(error);
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void EoTestBootStart(const wchar_t *directory,unsigned title){boot_diag_start(directory,title,boot_diag_now());}
__declspec(dllexport) uint64_t EoTestBootNow(void){return boot_diag_now();}
__declspec(dllexport) void EoTestBootEnd(const char *stage,const char *detail,uint64_t begin){boot_diag_end(stage,detail,begin);}
__declspec(dllexport) void EoTestBootSummary(void){boot_diag_summary("private test complete");}
#endif
#endif
