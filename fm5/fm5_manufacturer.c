#include "fm5/fm5_manufacturer.h"
#include "shared/support.h"
#include "shared/video_options_text.h"

/* The native popup owns focus, input, transition timing and cancellation.
   All native CarSelect variants register View; rim lists register Y.
   No input polling or gameplay work. */
typedef struct {
    void **vtable;unsigned char header[0x20];
    void *getter,*setter,*unused;const char *name;void *type;uint64_t flags;
} Property;
typedef struct Link {Property *property;struct Link *next;} Link;
typedef struct {void **vtable;void *page,*dialog;} Callback;
typedef struct {Callback storage;void *target;} Function;
typedef struct {char data[16];size_t length,capacity;} RimString;
typedef struct {int make;void *car,*label,*item,*logo;int aftermarket;char raw[256];wchar_t name[256];} Entry;
static unsigned char *image;
static void (*log_message)(const char *);
static void *(*allocate)(size_t);
static void (*free_object)(void *);
static Entry entries[512];
static const Entry empty_entry;
#include "fm5/fm5_manufacturer_metrics.h"
static unsigned entry_count;
static void *parent,*dialog,*buttons,*source_collection;
static int pending_make,registered;
static int initial_reveal=-1;
static Property properties[5];
static Link links[5];
static char pending_rim[256];
_Static_assert(sizeof(Property)==0x58,"native property ABI");

/* Primary CarSelect vtables, including the generic selector used by flows
   without a specialized screen type. All inherit the collection at +0x368
   and carousel at +0x948. Keep each variant's native lifecycle calls: Career,
   multiplayer and the demo have additional initialization/update work. */
typedef struct {unsigned type,initialize,update,destroy;int kind;} CarPage;
static const CarPage car_pages[]={
    {0x224a3c0,0x3667a0,0x2fd8c0,0x2fb530,1}, /* Buy Cars */
    {0x2262d58,0x366af0,0x2fd8c0,0x2fc7f0,2}, /* My Cars */
    {0x223e0f0,0x2fcce0,0x2fd8c0,0x2fc7f0,4}, /* Generic / free play */
    {0x22448e0,0x3016a0,0x2fd8c0,0x2fc7f0,4}, /* Designs / paint */
    {0x223e9b8,0x307050,0x2fd8c0,0x2fc7f0,4}, /* Career */
    {0x2245f98,0x304690,0x2fd8c0,0x304570,4}, /* Rivals */
    {0x224d1d0,0x304690,0x2fd8c0,0x304570,4}, /* Lobby */
    {0x224aa00,0x306760,0x306d40,0x3066b0,4}, /* Multiplayer lobby */
    {0x223f5a0,0x304690,0x2fd8c0,0x304570,4}, /* Playground */
    {0x2245980,0x301330,0x2fd8c0,0x2fb530,1}, /* Initial car purchase */
    {0x223fc80,0x300a10,0x300c00,0x300930,4}, /* ForzaVista demo */
    {0x22567d0,0x2fcce0,0x2fd8c0,0x3a8940,4}  /* Marketplace */
};
static const CarPage *car_page(void *page){
    if(page)for(unsigned i=0;i<ARRAY_COUNT(car_pages);++i)
        if(*(void **)page==image+car_pages[i].type)return car_pages+i;
    return NULL;
}

static void retain(void *p){if(p)InterlockedIncrement((volatile LONG *)((unsigned char *)p+0x10));}
static void release(void *p){
    if(p && !InterlockedDecrement((volatile LONG *)((unsigned char *)p+0x10)))
        ((void (*)(void *))(image+0x137710))(p);
}
static void clear_entries(void){
    unsigned count=entry_count;void *owned=buttons;
    entry_count=0;buttons=NULL;dialog=NULL;initial_reveal=-1;
    for(unsigned i=0;i<count;++i){
        release(entries[i].car);release(entries[i].label);release(entries[i].item);release(entries[i].logo);
        entries[i].car=entries[i].label=entries[i].item=entries[i].logo=NULL;
    }
    release(owned);
}
static void reset(void){parent=source_collection=NULL;pending_make=0;pending_rim[0]=0;clear_entries();}
static int page_kind(void *page){
    if(!page)return 0;const CarPage *car=car_page(page);if(car)return car->kind;
    void *type=*(void **)page;
    if(type==image+0x22aa2f0 || type==image+0x2250d18)return 3; /* Wheel Styles */
    return 0;
}
static void *selector(void *page){return *(void **)((unsigned char *)page+(page_kind(page)==3?0x380:0x948));}
static void *collection(void *page){return *(void **)((unsigned char *)page+(page_kind(page)==3?0x360:0x368));}
static unsigned rim_count(void *page){
    unsigned char *list=collection(page);if(!list)return 0;
    void *values=list+0x30;
    int count=((int (*)(void *))(*(void ***)values)[9])(values);
    return count>=0 && count<=32768?(unsigned)count:0;
}
static const RimString *rim_field(void *page,unsigned index,const char *column){
    unsigned char *list=collection(page);void *rows=list?*(void **)(list+0xa0):NULL;
    if(!rows)return NULL;
    RimString field={0};field.length=text_length(column);field.capacity=field.length<16?15:field.length;
    if(field.length<16)memory_copy(field.data,column,field.length+1);
    else memory_copy(field.data,&column,sizeof(column));
    /* FM5's raw by-name string accessor is 0x70, localized text is 0x80. */
    return ((const RimString *(*)(void *,int,const RimString *,int))(*(void ***)rows)[0x70/8])(rows,(int)index,&field,0);
}
static const char *rim_text(const RimString *s){return s?(s->capacity<16?s->data:*(const char *const *)s->data):NULL;}
static int rim_name(void *page,unsigned index,char out[256]){
    const RimString *s=rim_field(page,index,"ManufacturerName");const char *text=rim_text(s);
    if(!text || !s->length || s->length>=256)return 0;
    memory_copy(out,text,s->length);out[s->length]=0;return 1;
}
static void *wide_string(const wchar_t *text){
    void *s=allocate(0x40);if(s)((void *(*)(void *,const wchar_t *,int))(image+0xab1770))(s,text,-1);return s;
}
static void *literal(const char *text,size_t length){
    wchar_t wide[1024];
    if(length>=1024)return NULL;
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,(int)length,wide,1023);
    if(n<=0)return NULL;wide[n]=0;return wide_string(wide);
}
static const wchar_t *jump_label(void){
    void *manager=NULL;unsigned char code[32]={0};
    if(memory_read_pointer(image+0x25e7ae8,&manager) && manager)memory_read((unsigned char *)manager+0x30,code,32);
    const char *language=code[24]==15 && code[16]>=2 && code[16]<=3?(char *)code:NULL;
    return video_options_text_code(language)->manufacturer_jump;
}
static void *car_at(void *page,unsigned i,unsigned *count){
    unsigned char *list=*(void **)((unsigned char *)page+0x368);
    unsigned char *storage=list?*(void **)(list+0x50):NULL;
    *count=storage?*(unsigned *)(storage+0x30):0;
    if(*count>32768 || i>=*count)return NULL;
    unsigned char *array=*(void **)(storage+0x28);
    void **data=array?*(void ***)(array+0x28):NULL;
    return data?data[i]:NULL;
}
static int make_id(void *car){
    return car?((int (*)(void *,const char *))(*(void ***)car)[7])(car,"MakeId"):0;
}
static void *selected(void *control){
    void *item=NULL;
    if(control)((void **(*)(void *,void **))(image+0x2ae320))(control,&item);
    return item;
}
static void **get_property(void **out,Property *property,void *object){
    *out=NULL;
    if(property==properties || property==properties+2){
        int active=property==properties ? object==dialog && dialog!=NULL :
            car_page(object)!=NULL;
        return ((void **(*)(void **,const int *))(image+0x296b00))(out,&active);
    }
    for(unsigned i=0;i<entry_count;++i)if(entries[i].item==object){
        if(property==properties+4){
            int active=entries[i].aftermarket;
            return ((void **(*)(void **,const int *))(image+0x296b00))(out,&active);
        }
        *out=property==properties+3?entries[i].logo:entries[i].car;retain(*out);break;
    }
    return out;
}
static int register_properties(void){
    if(registered)return 1;
    /* Register native properties before prepending our bindings. PopupMenu
       has no own property initializer; its title comes from PopupBase.
       The adjacent IsA helpers require a type argument and cannot be used here. */
    const unsigned initializers[]={0x404970,0x2fc080};
    for(unsigned i=0;i<ARRAY_COUNT(initializers);++i)((void (*)(void))(image+initializers[i]))();
    const unsigned types[]={0x2467b10,0x2463f90,0x2460910,0x2463f90,0x2463f90};
    const char *names[]={"EoMakeGrid","EoMakeCar","EoMakeJump","EoMakeLogo","EoMakeAftermarket"};
    for(unsigned i=0;i<ARRAY_COUNT(properties);++i){
        Property *original=((Property *(*)(void))(image+(i==1?0x29cbf0:i==3?0x2c7e40:0x357080)))();
        if(!original)return 0;
        Property *p=properties+i;
        ((void (*)(void *))(image+0x9045f0))(p);p->vtable=original->vtable;
        memory_copy((unsigned char *)p+0x28,(unsigned char *)original+0x28,0x30);
        p->getter=(void *)get_property;p->setter=NULL;p->name=names[i];
        if(i==1)p->type=image+0x245ec90; /* Preserve native image-source binding. */
        Link **head=(Link **)(image+types[i]+0x40);
        links[i]=(Link){p,*head};*head=links+i;
    }
    registered=1;return 1;
}
static int append_rim(void *page,unsigned index,const char *raw){
    Entry *entry=entries+entry_count;void *label=NULL;
    if(raw[0]=='_' && raw[1]=='&'){
        void *rows=*(void **)((unsigned char *)collection(page)+0xa0),*localized=NULL;
        const char *column="ManufacturerName";RimString field={.length=16,.capacity=16};
        memory_copy(field.data,&column,sizeof(column));
        ((void **(*)(void *,void **,int,const RimString *,int))(*(void ***)rows)[0x80/8])(rows,&localized,(int)index,&field,0);
        if(localized){
            ((void **(*)(void **,void **))(image+0x8e37d0))(&label,&localized);
            ((void (*)(void *))(*(void ***)localized)[2])(localized);
        }
    }else label=literal(raw,text_length(raw));
    if(!label)return 0;
    memory_copy(entry,&empty_entry,sizeof(*entry));entry->make=(int)entry_count+1;entry->label=label;
    memory_copy(entry->raw,raw,text_length(raw)+1);
    const wchar_t *text=*(const wchar_t **)((unsigned char *)label+0x28);unsigned n=0;
    while(text && text[n] && n<255){entry->name[n]=text[n];++n;}entry->name[n]=0;
    const RimString *path=rim_field(page,index,"LogoPath");
    if(path && path->length && path->length<1024){
        const char *p=rim_text(path);entry->logo=literal(p,path->length);
        entry->aftermarket=text_find(p,path->length,"Aftermarket") || text_find(p,path->length,"aftermarket");
    }
    entry->aftermarket|=raw[0]=='_' && raw[1]=='&';
    ++entry_count;return 1;
}
static int collect(void *page){
    unsigned count=0;
    if(page_kind(page)==3){
        count=rim_count(page);
        for(unsigned i=0;i<count;++i){
            char raw[256];if(!rim_name(page,i,raw))continue;int found=0;
            for(unsigned j=0;j<entry_count && !found;++j)found=text_equal_ignore_case(entries[j].raw,raw);
            if(found)continue;
            if(entry_count==ARRAY_COUNT(entries) || !append_rim(page,i,raw))return 0;
        }
    }else
    for(unsigned i=0;;++i){
        void *car=car_at(page,i,&count);
        if(count>32768)return 0;
        if(i>=count)break;
        int make=make_id(car),found=make<=0;
        for(unsigned j=0;j<entry_count && !found;++j)found=entries[j].make==make;
        if(found)continue;
        if(entry_count==ARRAY_COUNT(entries))return 0;
        void *label=NULL,*localized=NULL;
        /* CarRecord exposes localized database strings through slot 0x60;
           convert that interface to AVUI's String, as native bindings do. */
        ((void **(*)(void *,void **,const char *))(*(void ***)car)[0x60/8])(car,&localized,"MakeName");
        if(!localized)return 0;
        ((void **(*)(void **,void **))(image+0x8e37d0))(&label,&localized);
        ((void (*)(void *))(*(void ***)localized)[2])(localized);
        if(!label)return 0;
        const wchar_t *text=*(const wchar_t **)((unsigned char *)label+0x28);
        Entry *entry=entries+entry_count;
        unsigned n=0;
        while(n<255 && text && text[n])++n;
        if(!n){release(label);return 0;}
        memory_copy(entry,&empty_entry,sizeof(*entry));entry->make=make;entry->car=car;entry->label=label;retain(car);
        for(unsigned k=0;k<=n;++k)entry->name[k]=text[k];
        ++entry_count;
    }
    /* Alphabetical picker order is independent of the carousel's sort/filter. */
    for(unsigned i=1;i<entry_count;++i){
        Entry entry;memory_copy(&entry,entries+i,sizeof(entry));unsigned j=i;
        while(j && CompareStringOrdinal(entries[j-1].name,-1,entry.name,-1,TRUE)==CSTR_GREATER_THAN){
            memory_copy(entries+j,entries+j-1,sizeof(entry));--j;
        }
        memory_copy(entries+j,&entry,sizeof(entry));
    }
    return entry_count!=0;
}
static void *clone(Callback *self,void *out){
    if(!out)out=allocate(sizeof(*self));
    if(out)memory_copy(out,self,sizeof(*self));return out;
}
static void destroy_callback(Callback *self,unsigned char owned){if(owned)free_object(self);}
static void *callback_type(void *self){(void)self;return NULL;}
static void accept(Callback *self){
    if(self->page!=parent || self->dialog!=dialog || !dialog)return;
    void *item=selected(*(void **)((unsigned char *)dialog+0x3e0));
    for(unsigned i=0;i<entry_count;++i)if(entries[i].item==item){pending_make=entries[i].make;memory_copy(pending_rim,entries[i].raw,sizeof(pending_rim));break;}
    release(item);
}
static void *accept_table[]={(void *)clone,(void *)clone,(void *)accept,(void *)callback_type,(void *)destroy_callback};
static void open_picker(void *page){
    if(!image || parent || !page || !page_kind(page) ||
       !selector(page) || !register_properties())return;
    if(!collect(page)){reset();return;}
    buttons=allocate(0x58);
    if(!buttons){reset();return;}
    ((void (*)(void *))(image+0x33d020))(buttons);
    for(unsigned i=0;i<entry_count;++i){
        void *item=((void *(*)(size_t,void *))(image+0x9047d0))(0x90,image+0x2463f90);
        if(!item){reset();return;}
        wchar_t display[256];trim_make_name(entries[i].name,display);
        unsigned length=0;while(display[length] && display[length]==entries[i].name[length])++length;
        Function empty={0};void *name;
        if(!display[length] && !entries[i].name[length]){name=entries[i].label;retain(name);}
        else name=wide_string(display);
        if(!name){release(item);reset();return;}
        ((void *(*)(void *,void **,Function *,int))(image+0x404a00))(item,&name,&empty,(int)i);
        entries[i].item=item;
        ((void (*)(void *,int,void *))(*(void ***)buttons)[8])(buttons,(int)i,item);
    }
    void *current=selected(selector(page));int make=0,initial=0;
    if(page_kind(page)==3){
        /* Wheel collection selects a boxed row index, not a CarRecord. */
        int index=current?*(int *)((unsigned char *)current+0x28):-1;char raw[256];
        if(index>=0 && (unsigned)index<rim_count(page) && rim_name(page,(unsigned)index,raw))
            for(unsigned i=0;i<entry_count;++i)if(text_equal_ignore_case(entries[i].raw,raw)){make=entries[i].make;break;}
    }else make=make_id(current);
    release(current);
    for(unsigned i=0;i<entry_count;++i)if(entries[i].make==make){initial=(int)i;break;}
    void *popup=NULL;
    ((void **(*)(void **))(image+0x394e10))(&popup);
    if(!popup){reset();return;}
    parent=page;source_collection=collection(page);dialog=popup;
    *(int *)((unsigned char *)popup+0x3f0)=initial;initial_reveal=initial;
    void **title=(void **)((unsigned char *)popup+0x3a0);release(*title);
    void *manager=NULL;unsigned char code[32]={0};
    if(memory_read_pointer(image+0x25e7ae8,&manager) && manager)memory_read((unsigned char *)manager+0x30,code,32);
    const char *language=code[24]==15 && code[16]>=2 && code[16]<=3 ? (char *)code:NULL;
    void *heading=allocate(0x40);
    if(!heading){release(popup);reset();return;}
    wchar_t upper[256];const wchar_t *label=video_options_text_code(language)->manufacturer_jump;
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,label,-1,upper,256,NULL,NULL,0))upper[0]=0;
    ((void *(*)(void *,const wchar_t *,int))(image+0xab1770))(heading,upper,-1);*title=heading;
    ((void (*)(void *,void *))(image+0x442190))(popup,(unsigned char *)buttons+0x38);
    *(int *)((unsigned char *)popup+0x360)=6;
    ((void (*)(void *,int))(image+0x405bf0))(popup,6);
    Function function={{accept_table,page,popup},NULL};function.target=&function.storage;
    ((void (*)(void *,int,Function *,unsigned char))(image+0x405810))(popup,0,&function,1);
    /* Configure before attaching: XAML can be requested during attachment. */
    void *ui=*(void **)(image+0x25f88a0);
    ((void (*)(void *,void *,void *))(*(void ***)ui)[0x120/8])(ui,page,popup);
    release(popup);
    
}
static unsigned char view(Callback *self){open_picker(self->page);return 1;}
static void *view_table[]={(void *)clone,(void *)clone,(void *)view,(void *)callback_type,(void *)destroy_callback};
static void bind_picker(void *page,int action){
    Function function={{view_table,page,NULL},NULL};function.target=&function.storage;
    ((void (*)(void *,int,Function *))(image+0x8e2f30))(page,action,&function);
}
static void initialize_cars(void *page){
    const CarPage *type=car_page(page);if(!type)return;
    ((void (*)(void *))(image+type->initialize))(page);bind_picker(page,0x0d);
}
static void initialize_rims(void *page){((void (*)(void *))(image+0x49b930))(page);bind_picker(page,0x44);}
static void finish_jump(void *page){
    if(parent!=page || dialog)return;
    int make=pending_make;void *list=source_collection;char raw[256];memory_copy(raw,pending_rim,sizeof(raw));
    parent=source_collection=NULL;pending_make=0;pending_rim[0]=0;
    if(!make || list!=collection(page) || !selector(page))return;
    if(page_kind(page)==3){
        unsigned count=rim_count(page);
        for(unsigned i=0;i<count;++i){
            char name[256];if(!rim_name(page,i,name) || !text_equal_ignore_case(name,raw))continue;
            ((void (*)(void *,int))(image+0x2ae280))(selector(page),(int)i);
            ((void (*)(void *,int))(image+0x8fd920))(selector(page),(int)i);break;
        }
        return;
    }
    unsigned count=0,target=0;void *chosen=NULL;
    for(unsigned i=0;;++i){
        void *car=car_at(page,i,&count);if(count>32768 || i>=count)break;
        if(!car || make_id(car)!=make)continue;
        if(*((unsigned char *)car+0x64))continue; /* Recommended prefix, not the regular brand group. */
        chosen=car;target=i;
        /* My Cars pins the current vehicle ahead of its manufacturer group.
           Prefer the regular group, falling back to it for a one-car brand. */
        if(page_kind(page)!=2 || !((int (*)(void *,const char *))(*(void ***)car)[7])(car,"IsCurrent"))break;
    }
    if(chosen){
        ((void (*)(void *,int))(image+0x2ae280))(selector(page),(int)target);
        ((void (*)(void *,void *))(image+0x8fd860))(selector(page),chosen);
    }
}
static void update_cars(void *page){
    const CarPage *type=car_page(page);if(!type)return;
    ((void (*)(void *))(image+type->update))(page);finish_jump(page);
}
static void update_rims(void *page){((void (*)(void *))(image+0x495ca0))(page);finish_jump(page);}
/* Native Wheel Styles rebuilds its footer when the highlighted part changes.
   Update its existing Y prompt after each rebuild rather than appending twice. */
static void rim_help(void *page){
    ((void (*)(void *))(image+(*(void **)page==image+0x2250d18?0x498610:0x494c90)))(page);
    bind_picker(page,0x44);
    void *panel=((void *(*)(void *))(image+0x339780))(page);if(!panel)return;
    void *children=((void *(*)(void *))(image+0x909e50))(panel);if(!children)return;
    unsigned count=*(unsigned *)((unsigned char *)children+0x7c);
    void **items=*(void ***)((unsigned char *)children+0x70);
    if(count>64 || !items)return;
    wchar_t upper[256];
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,jump_label(),-1,upper,256,NULL,NULL,0))return;
    void *label=wide_string(upper);if(!label)return;
    for(unsigned i=0;i<count;++i){
        void *button=items[i];if(!button || *(void **)button!=image+0x2253300)continue;
        void *value=NULL;
        ((void **(*)(void *,void **,void *))(image+0x9fe290))(button,&value,*(void **)(image+0x245f7f8));
        int key=value?*(int *)((unsigned char *)value+0x28):-1;release(value);
        if(key==3){((void (*)(void *,void *))(image+0x35bd30))(button,label);release(label);return;}
    }
    /* This consumes our label reference, as in the native upgrade footer. */
    ((void (*)(void *,int,void **))(image+0x385ea0))(panel,3,&label);
}
/* ScrollIntoView can stop at an unrealized tile. Ask the native tile panel
   for its index rectangle, which works even before that tile has a container. */
static int reveal_tile(void *panel,int index,const float *size){
    if(!panel || *(void **)panel!=image+0x22dad00 || !size ||
       size[0]<=0 || size[1]<=0 || (unsigned)index>=entry_count)return 0;
    float rectangle[4];
    ((void *(*)(void *,float *,int,const float *))(image+0x8ed080))(panel,rectangle,index,size);
    ((void (*)(void *,const float *))(image+0x8ebc40))(panel,rectangle);
    return 1;
}
/* The stock CommonListBox arrow handler measures only the visible columns.
   Navigate our full column-major collection and let native ScrollIntoView
   realize/scroll each selected tile. Shoulders skip three columns, as in FH2.
   Other list boxes keep their own handler. */
static void grid_key(void *control,void *event){
    int key=event?*(int *)((unsigned char *)event+0x64):0;
    if(!dialog || control!=*(void **)((unsigned char *)dialog+0x3e0) ||
       !((key>=23 && key<=26) || key==19 || key==20) || *((unsigned char *)event+0x32)){
        ((void (*)(void *,void *))(image+0x8f5270))(control,event);return;
    }
    initial_reveal=-1; /* User input takes over the opening position. */
    void *item=selected(control);int index=-1;
    for(unsigned i=0;i<entry_count;++i)if(entries[i].item==item){index=(int)i;break;}
    release(item);
    if(index<0)return;
    int target=index,row=index%4;
    if(key==19 || key==20){
        int column=index/4+(key==19?-3:3),last_column=((int)entry_count-1)/4;
        if(column<0)column=0;
        else if(column>last_column)column=last_column;
        target=column*4+row;
        if((unsigned)target>=entry_count)target=(int)entry_count-1;
    }else if(key==23 && index>=4)target-=4;
    else if(key==24 && row) --target;
    else if(key==25 && (unsigned)(index/4+1)*4<entry_count){
        target+=4;if((unsigned)target>=entry_count)target=(int)entry_count-1;
    }else if(key==26 && row<3 && (unsigned)(index+1)<entry_count)++target;
    /* Consume an edge press too: native spatial navigation must not wrap to
       the first realized tile when there are no more columns in that direction. */
    *((unsigned char *)event+0x32)=1;
    if(target!=index){
        ((void (*)(void *,int))(image+0x2ae280))(control,target);
        ((void (*)(void *,void *))(image+0x8fd860))(control,entries[target].item);
        void *panel=*(void **)((unsigned char *)control+0x1d0);
        /* These are IScrollInfo's committed viewport dimensions. Reuse the
           opening-reveal calculation immediately for multi-column jumps. */
        if(panel && *(void **)panel==image+0x22dad00){
            float size[2];memory_copy(size,(unsigned char *)panel+0x234,sizeof(size));
            if(!reveal_tile(panel,target,size))initial_reveal=target;
        }
    }
}
static void *arrange_tiles(void *panel,void *out,const float *size){
    void *result=((void *(*)(void *,void *,const float *))(image+0x8ec670))(panel,out,size);
    if(initial_reveal<0 || !dialog || !size || size[0]<=0 || size[1]<=0)return result;
    void *control=*(void **)((unsigned char *)dialog+0x3e0);
    if(!control || *(void **)((unsigned char *)control+0x1d0)!=panel)return result;
    int index=initial_reveal;initial_reveal=-1;
    if((unsigned)index>=entry_count)return result;
    ((void (*)(void *,int))(image+0x2ae280))(control,index);
    /* Generic ScrollIntoView may stop at the unrealized container. Use the
       tile panel's own index-to-rectangle and MakeVisible operations, with the
       actual arranged size rather than its not-yet-committed cached size.
       Native MakeVisible updates the scroll offsets and invalidates the viewer.
       Clear the one-shot first: its invalidation causes another layout pass. */
    reveal_tile(panel,index,size);
    return result;
}
static void *destroy_cars(void *page,unsigned flags){
    const CarPage *type=car_page(page);if(!type)return page;
    if(parent==page)reset();return ((void *(*)(void *,unsigned))(image+type->destroy))(page,flags);
}
static void *destroy_rims(void *page,unsigned flags){
    if(parent==page)reset();return ((void *(*)(void *,unsigned))(image+0x387810))(page,flags);
}
static void *destroy_popup(void *popup,unsigned flags){
    if(dialog==popup){clear_entries();if(!pending_make)parent=source_collection=NULL;}
    return ((void *(*)(void *,unsigned))(image+0x442000))(popup,flags);
}
unsigned fm5_manufacturer_prepare(unsigned char *base,Fm5Site *sites,void (*log)(const char *)){
    if(!sites){image=NULL;return 0;}
    image=base;log_message=log;registered=0;parent=dialog=buttons=source_collection=NULL;entry_count=0;pending_make=0;pending_rim[0]=0;initial_reveal=-1;
    allocate=(void *(*)(size_t))(base+0x1cdbb0);free_object=(void (*)(void *))(base+0x1cdc30);
    unsigned count=0;
    for(unsigned i=0;i<ARRAY_COUNT(car_pages);++i){
        const CarPage *page=car_pages+i;
        const unsigned offsets[]={page->type+0x488,page->type+0x4c0,page->type};
        const unsigned originals[]={page->initialize,page->update,page->destroy};
        void *callbacks[]={(void *)initialize_cars,(void *)update_cars,(void *)destroy_cars};
        for(unsigned j=0;j<ARRAY_COUNT(offsets);++j){
            void *original=base+originals[j];Fm5Site *site=sites+count++;
            *site=(Fm5Site){.rva=offsets[j],.size=8};
            memory_copy(site->original,&original,8);memory_copy(site->replacement,callbacks+j,8);
        }
    }
    static const unsigned offsets[]={0x2286198,0x22de468+0x1b8,0x22dad00+0x368,
        0x22aa2f0+0x488,0x22aa2f0+0x4c0,0x22aa2f0,0x22aa2f0+0x5b8,
        0x2250d18+0x488,0x2250d18+0x4c0,0x2250d18,0x2250d18+0x5b8};
    static const unsigned originals[]={0x442000,0x8f5270,0x8ec670,
        0x49b930,0x495ca0,0x387810,0x494c90,0x49b930,0x495ca0,0x387810,0x498610};
    static void *callbacks[]={(void *)destroy_popup,(void *)grid_key,(void *)arrange_tiles,
        (void *)initialize_rims,(void *)update_rims,(void *)destroy_rims,(void *)rim_help,
        (void *)initialize_rims,(void *)update_rims,(void *)destroy_rims,(void *)rim_help};
    for(unsigned i=0;i<ARRAY_COUNT(offsets);++i){
        void *original=base+originals[i];Fm5Site *site=sites+count++;
        *site=(Fm5Site){.rva=offsets[i],.size=8};
        memory_copy(site->original,&original,8);memory_copy(site->replacement,callbacks+i,8);
    }
    return count;
}

#include "fm5/fm5_manufacturer_xaml.h"
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fm5TestManufacturerTrim(const wchar_t *name,wchar_t *out){trim_make_name(name,out);}
__declspec(dllexport) void Fm5TestManufacturerOpen(void *page){open_picker(page);}
__declspec(dllexport) void Fm5TestManufacturerReset(void){reset();}
__declspec(dllexport) void *Fm5TestManufacturerProperty(unsigned i){return i<ARRAY_COUNT(properties)?properties+i:NULL;}
__declspec(dllexport) size_t Fm5TestManufacturerXaml(const char *s,size_t n,char *o,size_t c){return fm5_manufacturer_xaml(s,n,o,c);}
#endif
