#include "fh2/designs.h"
#include "shared/logging.h"
#include "shared/support.h"

static unsigned char *image;
static void *active;
static void **failed;
static unsigned failed_count,failed_capacity;
static int waiting,dirty,abandon,bound_once;
static ULONGLONG started;

static void report(unsigned car,unsigned total,unsigned matching,int prefix,int force){
    char message[160],*out=message;
    const char *labels[]={"DESIGNS: car ID=","; headers=","; matching=","; new slot="};
    unsigned values[]={car,total,matching,(unsigned)prefix};
    for(unsigned i=0;i<ARRAY_COUNT(values);i++){
        const char *label=labels[i];while(*label)*out++=*label++;
        char digits[10];unsigned n=0,value=values[i];
        do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
        while(n)*out++=digits[--n];
    }
    const char *end=force?"; incomplete headers retained.\r\n":".\r\n";
    while(*end)*out++=*end++;
    *out=0;log_append(message);
}

static void reset(void){
    if(failed)HeapFree(GetProcessHeap(),0,failed);
    failed=NULL;failed_count=failed_capacity=0;
    active=NULL;waiting=dirty=abandon=bound_once=0;
}

static int header_failed(void *item){
    for(unsigned i=0;i<failed_count;i++)if(failed[i]==item)return 1;
    return 0;
}

static void header_complete(void *item){
    ((void (*)(void *))(image+0xe07ec0))(item);
    if(!active)return;
    dirty=1;
    /* The native callback runs for failed headers too, but only successful
       reads set the item's IsLoaded byte. Remember failures so a damaged
       design cannot keep the catalogue waiting forever. */
    unsigned char loaded=0;
    if(!memory_read((unsigned char *)item+0xac,&loaded,1) || loaded || header_failed(item))return;
    if(failed_count==failed_capacity){
        unsigned capacity=failed_capacity?failed_capacity*2:16;
        void **next=HeapAlloc(GetProcessHeap(),0,(size_t)capacity*sizeof(void *));
        if(!next){abandon=1;return;}
        if(failed){memory_copy(next,failed,failed_count*sizeof(void *));HeapFree(GetProcessHeap(),0,failed);}
        failed=next;failed_capacity=capacity;
    }
    failed[failed_count++]=item;
}

static int current_car(void){
    void *car=NULL,*record=NULL,*vt=NULL,*get=NULL,*release=NULL;
    int ordinal=-1;
    ((void *(*)(void **))(image+0xced1c0))(&car);
    if(car && memory_read_pointer(car,&vt) && vt &&
       memory_read_pointer((unsigned char *)vt+0x100,&get) && get &&
       memory_read_pointer((unsigned char *)vt+0x18,&release) && release){
        record=((void *(*)(void *))get)(car);
        /* +0x260 is the owned garage-entry ID. Native BAB540 instead uses
           virtual +0x188 and reads the first integer from CarUniqueId for
           its Data_Car.Id query. Follow that same model-ID path. */
        void *record_vt=NULL,*get_ordinal=NULL,*value=NULL;
        if(record && memory_read_pointer(record,&record_vt) && record_vt &&
           memory_read_pointer((unsigned char *)record_vt+0x188,&get_ordinal) && get_ordinal){
            value=((void *(*)(void *))get_ordinal)(record);
            if(value)memory_read(value,&ordinal,sizeof(ordinal));
        }
        ((void (*)(void *))release)(car);
    }
    return ordinal;
}

/* PopupLoadSaveLivery calls slot 0x648 after enumerating the local files,
   before attaching its collection to ItemList. Partition the owned item
   pointers at this boundary: thumbnails, SavedFile handles and metadata stay
   together, and no file/texture work is added to the UI's update loop. */
static int prioritize(void *page,int force){
    unsigned char *list=NULL,*storage=NULL,*array=NULL;
    void **items=NULL;
    int count=0,selected=-1,ordinal=current_car();
    if(ordinal<0 || !memory_read_pointer((unsigned char *)page+0x490,(void **)&list) || !list ||
       !memory_read_pointer(list+0x50,(void **)&storage) || !storage ||
       !memory_read(storage+0x30,&count,sizeof(count)) || count<2 ||
       !memory_read_pointer(storage+0x28,(void **)&array) || !array ||
       !memory_read_pointer(array+0x28,(void **)&items) || !items)return 1;
    size_t bytes=(size_t)count*sizeof(void *);
    if(!memory_is_writable(items,bytes))return 1;
    void *begin=NULL,*end=NULL;
    memory_read_pointer((unsigned char *)page+0x588,&begin);
    memory_read_pointer((unsigned char *)page+0x590,&end);
    /* Native enumeration inserts one synthetic New Design item in save
       mode. Keep it first, and only wait for/sort real header-backed files. */
    int prefix=begin!=end?1:0;
    if(!force)for(int i=prefix;i<count;i++){
        unsigned char loaded=0;
        if(!items[i])continue;
        memory_read((unsigned char *)items[i]+0xac,&loaded,1);
        if(!loaded && !header_failed(items[i]))return 0;
    }
    unsigned char *scratch=HeapAlloc(GetProcessHeap(),0,bytes+(size_t)count);
    if(!scratch)return 1;
    void **ordered=(void **)scratch;
    unsigned char *matches=scratch+bytes;
    unsigned matching=0;
    for(int i=prefix;i<count;i++){
        void *metadata=NULL;int car=-1;
        unsigned char loaded=0;
        if(items[i])memory_read((unsigned char *)items[i]+0xac,&loaded,1);
        if(loaded && memory_read_pointer((unsigned char *)items[i]+0xf0,&metadata) && metadata)
            memory_read((unsigned char *)metadata+0xa4,&car,sizeof(car));
        matches[i]=car==ordinal;
        matching+=matches[i];
    }
    if(matching && matching<(unsigned)(count-prefix)){
        if(bound_once)memory_read((unsigned char *)page+0x6a8,&selected,sizeof(selected));
        if(prefix)ordered[0]=items[0];
        unsigned first=(unsigned)prefix,rest=prefix+matching;
        for(int i=prefix;i<count;i++){
            unsigned position=matches[i]?first++:rest++;
            ordered[position]=items[i];
            if(i==selected){
                int index=(int)position;
                if(memory_is_writable((unsigned char *)page+0x6a8,sizeof(index)))
                    memory_copy((unsigned char *)page+0x6a8,&index,sizeof(index));
            }
        }
        memory_copy(items,ordered,bytes);
        /* A fresh visit opens at the prioritized group, rather than scrolling
           down to a remembered alphabetical slot from the original order. */
        if(!bound_once && memory_is_writable((unsigned char *)page+0x6a8,sizeof(int))){
            int first=0;memory_copy((unsigned char *)page+0x6a8,&first,sizeof(first));
        }
    }
    report((unsigned)ordinal,(unsigned)(count-prefix),matching,prefix,force);
    HeapFree(GetProcessHeap(),0,scratch);
    return 1;
}

static void finish(void *page,int force){
    dirty=0;
    if(!prioritize(page,force))return;
    waiting=0;bound_once=1;
    if(failed)HeapFree(GetProcessHeap(),0,failed);
    failed=NULL;failed_count=failed_capacity=0;
    ((void (*)(void *))(image+0xe12b60))(page);
}
static void list_ready(void *page){
    if(active!=page){reset();active=page;}
    waiting=1;started=GetTickCount64();finish(page,0);
}
static void initialize(void *page){
    reset();active=page;
    ((void (*)(void *))(image+0xe0ad10))(page);
}
static void update(void *page){
    ((void (*)(void *))(image+0xe10b00))(page);
    if(active==page && waiting){
        int force=abandon || GetTickCount64()-started>=5000;
        if(dirty || force)finish(page,force);
    }
}
static void *destroy(void *page,unsigned flags){
    if(active==page)reset();
    return ((void *(*)(void *,unsigned))(image+0xdfe3c0))(page,flags);
}

PatchDefinition designs_hook_patches[5];
static unsigned char originals[5][8],replacements[5][8];
static const unsigned char *accepted[5][2];
void designs_prepare_hooks(unsigned char *game){
    reset();image=game;
    /* Scope to local liveries; PopupLoadSaveLiveryGroup stays native. */
    static const unsigned offsets[]={0x2e28478+0x648,0x2e28478+0x598,
        0x2e28478+0x4c8,0x2e28478,0x2e28398+0xa0};
    static const unsigned native[]={0xe12b60,0xe0ad10,0xe10b00,0xdfe3c0,0xe07ec0};
    static void *callbacks[]={(void *)list_ready,(void *)initialize,(void *)update,
        (void *)destroy,(void *)header_complete};
    for(unsigned i=0;i<ARRAY_COUNT(offsets);i++){
        void *original=game+native[i];
        memory_copy(originals[i],&original,8);
        memory_copy(replacements[i],callbacks+i,8);
        accepted[i][0]=originals[i];accepted[i][1]=replacements[i];
        designs_hook_patches[i]=(PatchDefinition){offsets[i],0,8,GROUP_VIDEO_OPTIONS,
            "local_design_order",originals[i],replacements[i],replacements[i],accepted[i],2};
    }
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) const PatchDefinition *Fh2TestDesignsPrepare(unsigned char *g){
    designs_prepare_hooks(g);return designs_hook_patches;
}
#endif
