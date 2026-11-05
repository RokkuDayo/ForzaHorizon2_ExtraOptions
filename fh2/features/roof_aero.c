#include "roof_aero.h"
#include "support.h"
#include "logging.h"

// GState/Granny schemas and arrays use four-byte packing, including pointers.
#pragma pack(push,4)
typedef struct Type {
    unsigned kind;const char *name;struct Type *reference;unsigned width;
    unsigned extra[3];void *ignored;
} Type;
typedef struct {Type *type;unsigned char *object;} Variant;
typedef struct {int count;unsigned char *data;} Array;
#pragma pack(pop)
typedef struct Block {struct Block *next;} Block;
typedef struct {Type *type;unsigned char *source,*copy;size_t size;} Copy;
typedef struct Pool {
    struct Pool *next;void *file;Block *blocks;size_t bytes;
    unsigned count,next_uid;int failed;
    Copy copies[768];
} Pool;
static unsigned char *image;
static Pool *pools;
static SRWLOCK lock=SRWLOCK_INIT;
static int equal(const char *a,const char *b){return a&&b&&text_equal_ignore_case(a,b);}
static int starts(const char *a,const char *b){
    while(*b)if(*a++!=*b++)return 0;
    return 1;
}
static int field(Type *type,void *object,const char *name,Type **member,void **address){
    struct {Type *type;void *data;} result={0};
    if(!type||!object||!((unsigned char (*)(Type *,void *,const char *,void *))(image+0x1d9f80))
       (type,object,name,&result))return 0;
    *member=result.type;*address=result.data;return 1;
}
static size_t type_size(Type *type){
    size_t size=0;
    if(!type)return 0;
    for(unsigned i=0;i<128;++i,++type){
        if(!type->kind)return size;
        int part=((int (*)(Type *))(image+0x1e95b0))(type);
        if(part<=0||part>65536||size+(unsigned)part>65536)return 0;
        size+=(unsigned)part;
    }
    return 0;
}
static void *allocate(Pool *pool,size_t bytes){
    if(!bytes||bytes>1048576||pool->bytes+bytes>1048576){pool->failed=1;return NULL;}
    Block *block=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Block)+bytes);
    if(!block){pool->failed=1;return NULL;}
    block->next=pool->blocks;pool->blocks=block;pool->bytes+=bytes;return block+1;
}
static void destroy(Pool *pool){
    while(pool->blocks){Block *b=pool->blocks;pool->blocks=b->next;HeapFree(GetProcessHeap(),0,b);}
    HeapFree(GetProcessHeap(),0,pool);
}
static unsigned char *mapped(Pool *pool,void *source){
    for(unsigned i=0;i<pool->count;++i)if(pool->copies[i].source==source)return pool->copies[i].copy;
    return NULL;
}
static int link_field(const char *name){
    static const char *const names[]={"ParentToken","OtherNode","Owner","OutputNodeToken",
        "StartNode","EndNode","ReferenceNode","SyncedNode","AnimationSlot"};
    for(unsigned i=0;i<ARRAY_COUNT(names);++i)if(equal(name,names[i]))return 1;
    return 0;
}
typedef struct {Type *type;unsigned char *object;} Seen;
static int max_uid(Variant value,Seen *seen,unsigned *count,unsigned *maximum,unsigned depth){
    if(!value.object||!value.type)return 1;
    if(depth>64||*count>=2048)return 0;
    for(unsigned i=0;i<*count;++i)if(seen[i].object==value.object)return 1;
    seen[(*count)++]=(Seen){value.type,value.object};
    size_t offset=0;
    for(Type *m=value.type;m->kind;++m){
        unsigned char *p=value.object+offset;
        if(m->width>1)return 0;
        if(m->kind==20&&starts(m->name,"ContextUID_")){
            unsigned id=*(unsigned *)p;if(id>*maximum)*maximum=id;
        }
        if(!link_field(m->name)){
            if(m->kind==2){
                if(!max_uid((Variant){m->reference,*(unsigned char **)p},seen,count,maximum,depth+1))return 0;
            }else if(m->kind==5){
                if(!max_uid(*(Variant *)p,seen,count,maximum,depth+1))return 0;
            }else if(m->kind==3||m->kind==4){
                Array *a=(Array *)p;size_t stride=m->kind==4?8:type_size(m->reference);
                if(!stride||a->count<0||a->count>2048||(a->count&&!a->data))return 0;
                for(int i=0;i<a->count;++i){
                    unsigned char *item=m->kind==4?((unsigned char **)a->data)[i]:a->data+i*stride;
                    if(!max_uid((Variant){m->reference,item},seen,count,maximum,depth+1))return 0;
                }
            }else if(m->kind==1||m->kind==7)return 0;
        }
        offset+=(unsigned)((int (*)(Type *))(image+0x1e95b0))(m);
    }
    return 1;
}
static unsigned char *clone(Pool *,Type *,unsigned char *,unsigned);
static int clone_into(Pool *pool,Type *type,unsigned char *source,unsigned char *copy,unsigned depth){
    size_t bytes=type_size(type);
    if(!bytes||depth>64||pool->count>=ARRAY_COUNT(pool->copies)){pool->failed=1;return 0;}
    pool->copies[pool->count++]=(Copy){type,source,copy,bytes};
    memory_copy(copy,source,bytes);
    size_t offset=0;
    for(Type *m=type;m->kind;++m){
        size_t size=(unsigned)((int (*)(Type *))(image+0x1e95b0))(m);
        unsigned char *s=source+offset,*d=copy+offset;
        if(m->width>1){pool->failed=1;return 0;}
        if(m->kind==20&&starts(m->name,"ContextUID_"))*(unsigned *)d=pool->next_uid++;
        if(!link_field(m->name)){
            if(m->kind==2){
                unsigned char *target=*(unsigned char **)s;
                if(target)*(unsigned char **)d=clone(pool,m->reference,target,depth+1);
            }else if(m->kind==5){
                Variant *v=(Variant *)s;
                if(v->object)((Variant *)d)->object=clone(pool,v->type,v->object,depth+1);
            }else if(m->kind==3||m->kind==4){
                Array *a=(Array *)s,*out=(Array *)d;
                size_t stride=m->kind==4?8:type_size(m->reference);
                if(a->count<0||a->count>2048||!stride||(a->count&&!a->data)){pool->failed=1;return 0;}
                if(a->count){
                    out->data=allocate(pool,(size_t)a->count*stride);
                    if(!out->data)return 0;
                    for(int i=0;i<a->count;++i){
                        if(m->kind==4){
                            unsigned char *target=((unsigned char **)a->data)[i];
                            ((unsigned char **)out->data)[i]=target?clone(pool,m->reference,target,depth+1):NULL;
                        }else if(!clone_into(pool,m->reference,a->data+i*stride,out->data+i*stride,depth+1))return 0;
                    }
                }
            }else if(m->kind==1||m->kind==7){pool->failed=1;return 0;}
        }
        if(pool->failed)return 0;
        offset+=size;
    }
    return 1;
}
static unsigned char *clone(Pool *pool,Type *type,unsigned char *source,unsigned depth){
    unsigned char *existing=mapped(pool,source);if(existing)return existing;
    unsigned char *copy=allocate(pool,type_size(type));
    return copy&&clone_into(pool,type,source,copy,depth)?copy:NULL;
}
static Variant reference(Variant value,const char *name){
    Type *m;void *p;
    if(!field(value.type,value.object,name,&m,&p))return (Variant){0};
    if(m->kind==2)return (Variant){m->reference,*(unsigned char **)p};
    if(m->kind==5)return *(Variant *)p;
    return (Variant){0};
}
static Variant node_token(Variant value){return reference(value,"nodeToken");}
static const char *node_name(Variant value){
    Variant node=node_token(value);Type *m;void *p;
    return field(node.type,node.object,"Name",&m,&p)&&m->kind==8?*(const char **)p:NULL;
}
static Variant children(Variant value,Array **array,Type **element){
    Variant container=reference(value,"containerToken");Type *m;void *p;
    if(!field(container.type,container.object,"ChildTokens",&m,&p)||m->kind!=3)return (Variant){0};
    *array=p;*element=m->reference;return container;
}
static Variant child(Variant value,const char *name){
    Array *a;Type *t;
    if(!children(value,&a,&t).object||a->count<0||a->count>256)return (Variant){0};
    size_t size=type_size(t);
    if(size!=16)return (Variant){0};
    for(int i=0;i<a->count;++i){
        Variant candidate=reference((Variant){t,a->data+i*size},"Variant");
        if(equal(node_name(candidate),name))return candidate;
    }
    return (Variant){0};
}
static Variant *edge_source(Variant value,const char *edge){
    Variant node=node_token(value);Type *m,*edge_type;void *p,*q;
    if(!field(node.type,node.object,"InputEdges",&m,&p)||m->kind!=3)return NULL;
    Array *a=p;edge_type=m->reference;size_t size=type_size(edge_type);
    if(a->count<0||a->count>64||!size)return NULL;
    for(int i=0;i<a->count;++i){
        unsigned char *item=a->data+i*size;
        if(field(edge_type,item,"EdgeName",&m,&q)&&m->kind==8&&equal(*(const char **)q,edge)&&
           field(edge_type,item,"OtherNode",&m,&q)&&m->kind==5)return q;
    }
    return NULL;
}
static void remap_links(Pool *pool,Variant driving,Variant showroom){
    for(unsigned i=0;i<pool->count;++i){
        Copy *c=&pool->copies[i];size_t offset=0;
        for(Type *m=c->type;m->kind;++m){
            unsigned char *d=c->copy+offset;
            if(link_field(m->name)&&!equal(m->name,"AnimationSlot")){
                unsigned char **target=m->kind==5?&((Variant *)d)->object:m->kind==2?(unsigned char **)d:NULL;
                if(target&&*target){
                    if(m->kind==5&&*target==driving.object)*(Variant *)d=showroom;
                    else {unsigned char *p=mapped(pool,*target);if(p)*target=p;}
                }
            }
            offset+=(unsigned)((int (*)(Type *))(image+0x1e95b0))(m);
        }
    }
}
static int apply(void *file,unsigned char *info){
    if(!info)return 0;
    // Native character-info layout, verified against the vanilla GSF schema.
    const char *hint=*(const char **)(info+0x2c);
    if(!hint)return 0;
    size_t hint_len=0;while(hint_len<1024&&hint[hint_len])++hint_len;
    if(hint_len==1024||!text_find(hint,hint_len,"POR_918Spyder_14"))return 0;
    unsigned count=*(unsigned *)info;
    if(count<1||count>2048)return 0;
    Variant root=*(Variant *)(info+4);
    Variant blend=child(root,"blend graph");
    Variant lod=child(blend,"LODSelect");
    Variant driving=child(lod,"InRace_Focus"),showroom=child(lod,"Autovista");
    Variant aero=child(driving,"ActiveAero"),manual=child(showroom,"active_aero_StateMachine");
    Variant roof=child(showroom,"roof_StateMachine");
    Variant pose=child(showroom,"Subtree Sample");
    Variant *wing=edge_source(pose,"root_boneWing");
    Array *list;Type *element;
    if(!aero.object||!manual.object||!roof.object||!wing||wing->object!=manual.object||
       !edge_source(aero,"speed")||!edge_source(aero,"brake")||
       !children(showroom,&list,&element).object||type_size(element)!=16||list->count<1||list->count>256)return 0;
    Pool *pool=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Pool));if(!pool)return 0;
    pool->file=file;
    Seen *seen=allocate(pool,2048*sizeof(Seen));unsigned seen_count=0,maximum=0;
    if(!seen||!max_uid(root,seen,&seen_count,&maximum,0)||maximum>65535){destroy(pool);return 0;}
    unsigned uid_base=maximum+1;pool->next_uid=uid_base;
    Variant replacement={aero.type,clone(pool,aero.type,aero.object,0)};
    if(!replacement.object||pool->failed){destroy(pool);return 0;}
    remap_links(pool,driving,showroom);
    size_t size=(size_t)(list->count+1)*16;
    unsigned char *new_list=allocate(pool,size);
    if(!new_list){destroy(pool);return 0;}
    memory_copy(new_list,list->data,size-16);
    *(Variant *)(new_list+size-16)=replacement;
    // Commit only after every allocation, pointer fixup and validation succeeds.
    *wing=replacement;list->data=new_list;++list->count;*(unsigned *)info=count+pool->next_uid-uid_base;
    pool->next=pools;pools=pool;
    log_append("ROOF: Porsche 918 showroom graph now retains native automatic aero and airbrake.\r\n");
    return 1;
}
static void *character_info(void *file){
    unsigned char *info=((void *(*)(void *))(image+0x1716e20))(file);
    AcquireSRWLockExclusive(&lock);
    int found=0;for(Pool *p=pools;p;p=p->next)if(p->file==file){found=1;break;}
    if(!found)apply(file,info);
    ReleaseSRWLockExclusive(&lock);return info;
}
static unsigned char release_file(unsigned char *resource){
    void *file=*(void **)(resource+0x150);
    // Keep borrowed source schemas/strings valid through native file destruction.
    unsigned char result=((unsigned char (*)(void *))(image+0x170c390))(resource);
    AcquireSRWLockExclusive(&lock);
    Pool **at=&pools;
    while(*at){Pool *p=*at;if(p->file==file){*at=p->next;destroy(p);break;}at=&p->next;}
    ReleaseSRWLockExclusive(&lock);return result;
}
PatchDefinition roof_aero_hook_patches[3];
static unsigned char call_original[5],call_replacement[5],pad_original[12],pad_replacement[12];
static unsigned char free_original[8],free_replacement[8];
static const unsigned char *call_accepted[]={call_original,call_replacement};
static const unsigned char *pad_accepted[]={pad_original,pad_replacement};
static const unsigned char *free_accepted[]={free_original,free_replacement};
void roof_aero_prepare_hooks(unsigned char *game_image){
    image=game_image;
    int32_t displacement=0x1716e20-(0x170baa9+5);call_original[0]=call_replacement[0]=0xe8;
    memory_copy(call_original+1,&displacement,4);displacement=0x170bb14-(0x170baa9+5);
    memory_copy(call_replacement+1,&displacement,4);
    for(unsigned i=0;i<12;++i)pad_original[i]=0xcc;
    pad_replacement[0]=0x48;pad_replacement[1]=0xb8;uintptr_t ptr=(uintptr_t)character_info;
    memory_copy(pad_replacement+2,&ptr,8);pad_replacement[10]=0xff;pad_replacement[11]=0xe0;
    ptr=(uintptr_t)(image+0x170c390);memory_copy(free_original,&ptr,8);
    ptr=(uintptr_t)release_file;memory_copy(free_replacement,&ptr,8);
    roof_aero_hook_patches[0]=(PatchDefinition){0x170bb14,0,12,GROUP_VEHICLE_CONTROLS,"roof_aero_character_info_bridge",pad_original,pad_replacement,pad_replacement,pad_accepted,2};
    roof_aero_hook_patches[1]=(PatchDefinition){0x170baa9,0,5,GROUP_VEHICLE_CONTROLS,"roof_aero_character_info",call_original,call_replacement,call_replacement,call_accepted,2};
    roof_aero_hook_patches[2]=(PatchDefinition){0x2f32278,0,8,GROUP_VEHICLE_CONTROLS,"roof_aero_file_cleanup",free_original,free_replacement,free_replacement,free_accepted,2};
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestRoofAeroApply(void *file,void *info){return apply(file,info);}
__declspec(dllexport) unsigned Fh2TestRoofAeroPools(void){unsigned n=0;for(Pool*p=pools;p;p=p->next)++n;return n;}
__declspec(dllexport) void *Fh2TestRoofAeroInfoAddress(void){return (void *)character_info;}
#endif
