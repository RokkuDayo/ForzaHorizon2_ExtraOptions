#include "manufacturer_jump.h"
#include "speed_zone.h"
#include "support.h"
#include "logging.h"
#include "bindings.h"
#include "autodrive_game.h"
#include "video_options_text.h"

typedef void (*Action)(void *);

typedef struct {
    void **vtable;
    unsigned char header[0x20];
    void *getter, *setter, *unused;
    const char *name;
    void *type;
    uint64_t flags;
} NativeProperty;

typedef struct {
    NativeProperty *property;
    void *next;
} PropertyLink;

typedef struct {
    void *vtable, *page, *dialog;
} PickCallback;

typedef struct {
    PickCallback storage;
    void *target;
} NativeFunction;

typedef struct {
    int make;
    void *item, *car, *logo;
    unsigned rim_index;
    int aftermarket;
    char rim_name[256];
} MakeEntry;

typedef struct {
    void *page, *controller;
    int pressed;
} PageInput;

static unsigned char *image;
static NativeProperty properties[6];
static PropertyLink links[6];
static int registered;
static MakeEntry entries[1024];
static unsigned entry_count;
static void *parent_page, *parent_behavior, *dialog, *buttons;
static int pending_make;
static PageInput inputs[5];
static int parent_rims, parent_category;
static void *parent_collection;

static void release_ui(void *value) {
    if (value && InterlockedDecrement((volatile LONG *)((unsigned char *)value + 0x10)) == 0) {
        ((Action)(image + 0x257730))(value);
    }
}

static void release_text(void *value) {
    if (value) {
        ((Action)(*(void ***)value)[3])(value);
    }
}

static void clear_picker(void) {
    unsigned count = entry_count;
    void *owned_buttons = buttons;
    // Invalidate getters/callbacks before releasing native objects, whose
    // destructors can notify UI bindings synchronously.
    buttons = NULL;
    entry_count = 0;
    parent_page = parent_behavior = dialog = NULL;
    pending_make = 0;
    parent_rims=parent_category=0;parent_collection=NULL;
    for (unsigned i = 0; i < count; ++i) {
        release_ui(entries[i].car);
        release_ui(entries[i].logo);
        entries[i].logo=NULL;
        entries[i].car = entries[i].item = NULL;
    }
    release_ui(owned_buttons);
}

static void **get_property(void **out, NativeProperty *property, void *object) {
    *out = NULL;
    if (property == &properties[0]) {
        unsigned char active = object == dialog && dialog != NULL;
        return ((void **(*)(void **, const unsigned char *))(image + 0x4702f0))(out, &active);
    }
    for (unsigned i = 0; i < entry_count; ++i) {
        if (entries[i].item == object) {
            if(property==&properties[4] || property==&properties[5]){
                unsigned char rims=(unsigned char)(property==&properties[5]?entries[i].aftermarket:parent_rims);
                return ((void **(*)(void **,const unsigned char *))(image+0x4702f0))(out,&rims);
            }
            *out = property == &properties[1] ? entries[i].car
                 : property == &properties[3] ? entries[i].logo
                 : *(void **)((unsigned char *)object + 0x70);
            if (*out) {
                InterlockedIncrement((volatile LONG *)((unsigned char *)*out + 0x10));
            }
            break;
        }
    }
    return out;
}

static int register_properties(void) {
    if (registered) {
        return 1;
    }
    // PopupVerticalMenu uses the PopupMenu type (3534c90), not the
    // similarly named PopupLoadSavePhoto type (3535c50).
    // Initialize the original descriptors first; their lazy registration resets
    // the type's property links and must not discard our added properties.
    ((Action)(image + 0xbcf4d0))(NULL);
    ((Action)(image + 0xe15db0))(NULL);
    static const unsigned factories[] = {0xe249d0, 0xe25e70, 0xe25e70, 0xe25e70, 0xe249d0, 0xe249d0};
    static const unsigned types[] = {0x3534c90, 0x34f1720, 0x34f1720, 0x34f1720, 0x34f1720, 0x34f1720};
    static const char *const names[] = {"EOMakeGrid", "EOMakeCar", "EOMakeLabel", "EOMakeLogo", "EOMakeRim", "EOMakeAftermarket"};
    for (unsigned i = 0; i < ARRAY_COUNT(properties); ++i) {
        NativeProperty *source = ((NativeProperty * (*)(void))(image + factories[i]))();
        if (!source) {
            return 0;
        }
        ((Action)(image + 0x143eb80))(&properties[i]);
        properties[i].vtable = source->vtable;
        memory_copy((unsigned char *)&properties[i] + 0x28, (unsigned char *)source + 0x28, 0x30);
        // Keep the native car record for image binding. ManufacturerIcon is a
        // binding; avoid converting its texture source to localized display text.
        if (i == 1) {
            properties[i].type = image + 0x3501660; // CarRecord
        }
        properties[i].getter = (void *)get_property;
        properties[i].setter = NULL;
        properties[i].name = names[i];
        links[i].property = &properties[i];
        ((void (*)(void *, PropertyLink *))(image + 0x155ee30))(image + types[i], &links[i]);
    }
    registered = 1;
    return 1;
}

static int ready(void *page) {
    unsigned char *behavior = *(void **)((unsigned char *)page + 0x310);
    return image && behavior && behavior[0x78] && !*(void **)((unsigned char *)page + 0x2a0);
}

static int is_rim_page(void *page) {
    if(!image || !page)return 0;
    void *table=*(void **)page;
    // WheelStyles derives from UpgradeCarPartsBase but has its own vtable.
    return table==image+0x2e5e958 || table==image+0x2d820d8;
}
typedef struct {char data[16];size_t length,capacity;} RimString;
static void *rim_collection(void *page){return *(void **)((unsigned char *)page+0x420);}
static unsigned rim_count(void *page){
    unsigned char *collection=rim_collection(page);
    if(!collection)return 0;
    void *list=collection+0x30;
    int count=((int (*)(void *))(*(void ***)list)[9])(list);
    return count>=0 && count<=32768?(unsigned)count:0;
}
static const RimString *rim_field(void *page,unsigned index,const char *name){
    unsigned char *collection=rim_collection(page);
    void *rows=collection?*(void **)(collection+0xa0):NULL;
    if(!rows)return NULL;
    RimString field={0};size_t length=text_length(name);
    field.length=length;field.capacity=length<16?15:length;
    if(length<16)memory_copy(field.data,name,length+1);
    else memory_copy(field.data,&name,sizeof(name));
    return ((const RimString *(*)(void *,int,const RimString *,int))(*(void ***)rows)[16])
        (rows,(int)index,&field,0);
}
static const char *rim_text(const RimString *s){return s?(s->capacity<16?s->data:*(const char *const *)s->data):NULL;}
static int rim_name(void *page,unsigned index,char out[256]){
    const RimString *s=rim_field(page,index,"ManufacturerName");const char *name=rim_text(s);
    if(!name || !s->length || s->length>=256)return 0;
    memory_copy(out,name,s->length);out[s->length]=0;return 1;
}
static int same_wide(const wchar_t *a,const wchar_t *b){
    if(!a || !b)return a==b;
    while(*a && *a==*b){++a;++b;}return *a==*b;
}
static int append_rim_make(void *page,unsigned index,const char *raw){
    void *localized=NULL,*item=NULL,*logo=NULL;
    // The literal-string constructor does not resolve database _& references.
    // Use the recordset's localized field accessor, as native upgrade UI does.
    unsigned char *collection=rim_collection(page);
    void *rows=collection?*(void **)(collection+0xa0):NULL;
    if(!rows)return 0;
    const char *column="ManufacturerName";
    RimString field={0};field.length=16;field.capacity=16;
    memory_copy(field.data,&column,sizeof(column));
    if(raw[0]=='_' && raw[1]=='&'){
        ((void **(*)(void *,void **,int,const RimString *,unsigned char))(*(void ***)rows)[18])
            (rows,&localized,(int)index,&field,0);
    }else{
        // Synthetic stock-rim names are literal text, not localization keys.
        ((void **(*)(void **,const char *,int))(image+0x323310))(&localized,raw,-1);
    }
    if(!localized)return 0;
    int value=(int)entry_count;
    ((void **(*)(void **,const int *,void **))(image+0xc65ab0))(&item,&value,&localized);
    release_text(localized);
    if(!item)return 0;
    const RimString *path=rim_field(page,index,"LogoPath");
    if(path && path->length && path->length<1024){
        wchar_t wide[1024];
        int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,rim_text(path),(int)path->length,wide,1023);
        if(n>0){wide[n]=0;((void **(*)(void **,const wchar_t *))(image+0x13fe8e0))(&logo,wide);}
    }
    MakeEntry *entry=&entries[entry_count++];
    entry->aftermarket=(raw[0]=='_' && raw[1]=='&') || (path && (text_find(rim_text(path),path->length,"Aftermarket") || text_find(rim_text(path),path->length,"aftermarket")));
    entry->make=value+1;entry->item=item;entry->car=NULL;entry->logo=logo;entry->rim_index=index;
    memory_copy(entry->rim_name,raw,text_length(raw)+1);
    ((void (*)(void *,int,void *))(*(void ***)buttons)[8])(buttons,value,item);
    release_ui(item);return 1;
}
static void *page_selector(void *page){
    return *(void **)((unsigned char *)page+(is_rim_page(page)?0x438:0xa08));
}

static void *car_at(void *page, unsigned index, unsigned *count) {
    unsigned char *collection = *(void **)((unsigned char *)page + 0x428);
    unsigned char *storage = collection ? *(void **)(collection + 0x50) : NULL;
    unsigned n = storage ? *(unsigned *)(storage + 0x30) : 0;
    *count = n;
    if (n > 32768 || index >= n) {
        return NULL;
    }
    unsigned char *array = *(void **)(storage + 0x28);
    void **data = array ? *(void ***)(array + 0x28) : NULL;
    return data ? data[index] : NULL;
}

static int car_int(void *car, const char *column) {
    return car ? ((int (*)(void *, const char *))(*(void ***)car)[0x38 / 8])(car, column) : 0;
}

static int make_id(void *car) {
    return car_int(car, "MakeId");
}

static void *car_text(void *car, const char *column) {
    void *text = NULL;
    ((void **(*)(void *, void **, const char *))(*(void ***)car)[0x60 / 8])(car, &text, column);
    return text;
}

static int append_make(void *car, int make) {
    void *name = car_text(car, "ManufacturerMakeName"), *item = NULL;
    if (!name) {
        return 0;
    }
    int value = (int)entry_count;
    ((void **(*)(void **, const int *, void **))(image + 0xc65ab0))(&item, &value, &name);
    release_text(name);
    if (!item) {
        return 0;
    }
    InterlockedIncrement((volatile LONG *)((unsigned char *)car + 0x10));
    MakeEntry *entry=&entries[entry_count++];
    entry->aftermarket=0;entry->make=make;entry->item=item;entry->car=car;entry->logo=NULL;entry->rim_index=0;entry->rim_name[0]=0;
    ((void (*)(void *, int, void *))(*(void ***)buttons)[8])(buttons, value, item);
    release_ui(item);
    return 1;
}

static void *clone_callback(PickCallback *source, void *destination) {
    if (!destination) {
        destination = ((void *(*)(size_t))(image + 0x3229c0))(sizeof(*source));
    }
    if (destination) {
        memory_copy(destination, source, sizeof(*source));
    }
    return destination;
}

static void destroy_callback(PickCallback *self, unsigned char free_object) {
    if (free_object) {
        ((Action)(image + 0x31c7b0))(self);
    }
}

static void *callback_type(void *self) {
    (void)self;
    return NULL;
}

static void accept_make(PickCallback *self) {
    if (self->page != parent_page || self->dialog != dialog || !dialog) {
        return;
    }
    void *item = ((void *(*)(void *))(image + 0xe01a60))(dialog);
    for (unsigned i = 0; i < entry_count; ++i) {
        if (item == entries[i].item) {
            pending_make = entries[i].make;
            log_append_value("MANUFACTURERS: chosen make", pending_make);
            return;
        }
    }
}

static void *callback_vtable[] = {(void *)clone_callback, (void *)clone_callback,
                                  (void *)accept_make, (void *)callback_type,
                                  (void *)destroy_callback};

static int selected_index(void *selector) {
    void *value = NULL;
    if (!selector) {
        return -1;
    }
    ((void **(*)(void *, void **, void *))(image + 0x15e0f10))(selector, &value,
                                                               *(void **)(image + 0x36d1dd8));
    int index = value ? *(int *)((unsigned char *)value + 0x28) : -1;
    release_ui(value);
    return index;
}

// Keep the popup's native accept/cancel and key repeat, but navigate our fixed
// four-row grid explicitly (shoulders skip three columns). CommonListBox's text-menu page commands
// otherwise treat bumpers as top/bottom movement even when its items host is a grid.
static void grid_key_down(void *selector, void *event) {
    int key = *(int *)((unsigned char *)event + 0x64);
    int own = dialog && parent_page && *(void **)((unsigned char *)parent_page + 0x2a0) == dialog &&
              *(void **)((unsigned char *)dialog + 0x4a0) == selector;
    if (!own || !((key >= 23 && key <= 26) || key == 19 || key == 20)) {
        ((void (*)(void *, void *))(image + 0x1417e20))(selector, event);
        return;
    }
    if (*((unsigned char *)event + 0x32)) {
        return;
    }
    int index = selected_index(selector), target = index;
    if (index >= 0 && (unsigned)index < entry_count) {
        if (key == 19 || key == 20) {
            int column = index / 4 + (key == 19 ? -3 : 3);
            int last_column = ((int)entry_count - 1) / 4;
            if (column < 0) {
                column = 0;
            } else if (column > last_column) {
                column = last_column;
            }
            target = column * 4 + index % 4;
            if ((unsigned)target >= entry_count) {
                target = (int)entry_count - 1;
            }
        } else if (key == 23 && index >= 4) {
            target -= 4;
        } else if (key == 25 && (unsigned)((index / 4 + 1) * 4) < entry_count) {
            target += 4;
            if ((unsigned)target >= entry_count) {
                target = (int)entry_count - 1;
            }
        } else if (key == 24 && index % 4) {
            --target;
        } else if (key == 26 && index % 4 != 3 && (unsigned)(index + 1) < entry_count) {
            ++target;
        }
        if (target != index) {
            void *boxed = NULL;
            ((void **(*)(void **, const int *))(image + 0x336a10))(&boxed, &target);
            if (boxed) {
                ((void (*)(void *, void *, void *))(image + 0x15f8a70))(
                    selector, *(void **)(image + 0x36d1dd8), boxed);
                ((void (*)(void *, int))(image + 0x140caf0))(selector, target);
                ((void (*)(void *, void *))(image + 0x14232f0))(selector, entries[target].item);
            }
            release_ui(boxed);
        }
    }
    *((unsigned char *)event + 0x32) = 1;
}

static int sort_manufacturers(void){
    // All pickers use the same localized, case-insensitive alphabetical order.
    // The carousel order (recommended/current cars included) does not participate.
    for(unsigned i=1;i<entry_count;++i){
        MakeEntry entry; memory_copy(&entry,&entries[i],sizeof(entry));unsigned at=i;
        void *label=*(void **)((unsigned char *)entry.item+0x70);
        const wchar_t *name=label?*(const wchar_t **)((unsigned char *)label+0x28):L"";
        while(at){
            void *other=*(void **)((unsigned char *)entries[at-1].item+0x70);
            const wchar_t *previous=other?*(const wchar_t **)((unsigned char *)other+0x28):L"";
            if(CompareStringOrdinal(previous,-1,name,-1,TRUE)!=CSTR_GREATER_THAN)break;
            memory_copy(&entries[at],&entries[at-1],sizeof(entry));--at;
        }
        memory_copy(&entries[at],&entry,sizeof(entry));
    }
    void *sorted=NULL;
    ((void **(*)(void **))(image+0xc65230))(&sorted);
    if(!sorted)return 0;
    for(unsigned i=0;i<entry_count;++i){
        *(int *)((unsigned char *)entries[i].item+0x68)=(int)i;
        ((void (*)(void *,int,void *))(*(void ***)sorted)[8])(sorted,(int)i,entries[i].item);
    }
    release_ui(buttons);buttons=sorted;return 1;
}

static void show_picker(void *page) {
    if (!ready(page) || parent_page || !register_properties()) {
        return;
    }
    clear_picker();
    ((void **(*)(void **))(image + 0xc65230))(&buttons);
    if (!buttons) {
        return;
    }
    unsigned count = 0;
    int complete = 1;
    int rims=is_rim_page(page);
    if(rims){
        count=rim_count(page);
        for(unsigned i=0;i<count && complete;++i){
            char name[256];if(!rim_name(page,i,name))continue;
            int found=0;
            for(unsigned j=0;j<entry_count && !found;++j)found=text_equal_ignore_case(entries[j].rim_name,name);
            if(!found && (entry_count==ARRAY_COUNT(entries) || !append_rim_make(page,i,name)))complete=0;
        }
    }
    if(complete && !rims) {
        for (unsigned i = 0;; ++i) {
            void *car = car_at(page, i, &count);
            if (i >= count || count > 32768) {
                break;
            }
            int make = make_id(car), found = make <= 0;
            for (unsigned j = 0; j < entry_count && !found; ++j) {
                found = entries[j].make == make;
            }
            if (!found && (entry_count == ARRAY_COUNT(entries) || !append_make(car, make))) {
                complete = 0;
                break;
            }
        }
    }
    if (!entry_count || !complete || count > 32768 || !sort_manufacturers()) {
        clear_picker();
        return;
    }
    unsigned selected_count = 0;
    int current = selected_index(page_selector(page));
    void *current_car = !rims && current >= 0 ? car_at(page, (unsigned)current, &selected_count) : NULL;
    int current_make = current_car && (unsigned)current < selected_count ? make_id(current_car) : 0;
    char current_rim[256];current_rim[0]=0;
    if(rims && current>=0 && (unsigned)current<count)rim_name(page,(unsigned)current,current_rim);
    int initial = 0;
    for (unsigned i = 0; i < entry_count; ++i) {
        if ((rims && text_equal_ignore_case(entries[i].rim_name,current_rim)) || (!rims && entries[i].make == current_make)) {
            initial = (int)i;
            break;
        }
    }
    parent_rims=rims;
    parent_category=rims?*(int *)((unsigned char *)page+0x500):0;
    parent_collection=rims?rim_collection(page):NULL;
    parent_page = page;
    parent_behavior = *(void **)((unsigned char *)page + 0x310);
    dialog = ((void *(*)(void *))(image + 0xce2ca0))(page);
    if (!dialog) {
        clear_picker();
        return;
    }
    // PopupMenu's native initialize routine consumes this field after the XAML
    // Menu has been constructed. Set it before assigning the collection.
    *(int *)((unsigned char *)dialog + 0x4b0) = initial;
    void **title = (void **)((unsigned char *)dialog + 0x460);
    void **body = (void **)((unsigned char *)dialog + 0x468);
    release_ui(*title);
    release_ui(*body);
    *title = *body = NULL;
    ((void **(*)(void **, const wchar_t *))(image + 0x13fe8e0))(
        title, video_options_text(image)->manufacturer_jump);
    ((void (*)(void *, void *))(image + 0xe1b5a0))(dialog, (unsigned char *)buttons + 0x38);
    ((void (*)(void *, void *))(image + 0xcf9c60))(dialog, &properties[0]);
    ((void (*)(void *, int))(image + 0xdd8820))(dialog, 6);
    NativeFunction function = {{callback_vtable, page, dialog}, NULL};
    function.target = &function.storage;
    ((void (*)(void *, int, NativeFunction *, unsigned char))(image + 0xdd8340))(dialog, 0,
                                                                                 &function, 1);
    log_append_value("MANUFACTURERS: native tile picker entries", entry_count);
}

static void finish_picker(void *page) {
    if (parent_page != page) {
        return;
    }
    if (*(void **)((unsigned char *)page + 0x310) != parent_behavior) {
        clear_picker();
        return;
    }
    if (*(void **)((unsigned char *)page + 0x2a0)) {
        return;
    }
    if(parent_rims){
        if(!is_rim_page(page) || rim_collection(page)!=parent_collection ||
           *(int *)((unsigned char *)page+0x500)!=parent_category){clear_picker();return;}
        if(pending_make && ready(page)){
            for(unsigned i=0;i<entry_count;++i)if(entries[i].make==pending_make){
                char name[256];unsigned target=entries[i].rim_index;
                if(target<rim_count(page) && rim_name(page,target,name) && text_equal_ignore_case(name,entries[i].rim_name)){
                    void *selector=page_selector(page),*boxed=NULL,*selected=NULL;
                    int index=(int)target;
                    ((void **(*)(void **,const int *))(image+0x336a10))(&boxed,&index);
                    if(boxed && selector){
                        ((void (*)(void *,void *,void *))(image+0x15f8a70))(selector,*(void **)(image+0x36d1dd8),boxed);
                        ((void **(*)(void *,void **))(image+0xe25a80))(selector,&selected);
                        if(selected)((void (*)(void *,void *))(image+0x14232f0))(selector,selected);
                    }
                    release_ui(selected);release_ui(boxed);
                }
                break;
            }
        }
        clear_picker();return;
    }
    if (pending_make && ready(page)) {
        unsigned count = 0, target = 0;
        void *selected = NULL;
        for (unsigned i = 0;; ++i) {
            void *car = car_at(page, i, &count);
            if (i >= count || count > 32768) {
                break;
            }
            if (make_id(car) != pending_make) {
                continue;
            }
            if (!selected || !car_int(car, "IsCurrent")) {
                selected = car;
                target = i;
            }
            if (!car_int(car, "IsCurrent")) {
                break;
            }
        }
        void *selector = *(void **)((unsigned char *)page + 0xa08), *boxed = NULL;
        if (selected && selector) {
            int index = (int)target;
            ((void **(*)(void **, const int *))(image + 0x336a10))(&boxed, &index);
            if (boxed) {
                ((void (*)(void *, void *, void *))(image + 0x15f8a70))(
                    selector, *(void **)(image + 0x36d1dd8), boxed);
                ((void (*)(void *, int))(image + 0x140caf0))(selector, index);
                ((void (*)(void *, void *))(image + 0x14232f0))(selector, selected);
                log_append_value("MANUFACTURERS: jumped to visible car index", index);
            }
            release_ui(boxed);
        }
    }
    clear_picker();
}

static void prompt(void *page) {
    void *panel = ((void *(*)(void *))(image + 0xced910))(page), *label = NULL;
    if (!panel) {
        return;
    }
    int key = is_rim_page(page) ? 3 : 5; // Native footer IDs: Y / View.
    void *button = ((void *(*)(void *, int))(image + 0xb2aae0))(panel, key);
    if (button) {
        void *attached = NULL, *description = NULL;
        ((void **(*)(void *, void **, void *))(image + 0x15e0f10))(button, &attached,
                                                                   *(void **)(image + 0x34e2ce8));
        int xaml = attached && *((unsigned char *)attached + 0x28);
        release_ui(attached);
        void *property =
            xaml ? ((void *(*)(int))(image + 0xb2e040))(key) : *(void **)(image + 0x34e2c18);
        ((void **(*)(void *, void **, void *))(image + 0x15e0f10))(xaml ? panel : button,
                                                                   &description, property);
        const wchar_t *text =
            description ? *(const wchar_t **)((unsigned char *)description + 0x28) : NULL;
        int visible = text && *text;
        int own = same_wide(text,video_options_text(image)->manufacturer_jump);
        release_ui(description);
        if (visible && (!is_rim_page(page) || own)) {
            return;
        }
    }
    ((void **(*)(void **, const wchar_t *))(image + 0x13fe8e0))(
        &label, video_options_text(image)->manufacturer_jump);
    // Native AddHelpButton consumes this reference, without clearing the pointer.
    if (label) {
        ((void (*)(void *, int, void **))(image + 0xb00010))(panel, key, &label);
    }
}

static void read_input(unsigned index, void *page, int initialize) {
    void *controller = NULL;
    unsigned short buttons_down = 0;
    GameAction action = is_rim_page(page) ? BIND_RIM_MANUFACTURER_JUMP : BIND_MANUFACTURER_JUMP;
    int available = autodrive_game_buttons(&controller, &buttons_down) == 0 ||
                    binding_keyboard(action);
    int pressed = !available || !autodrive_game_window_focused() ||
                  binding_down(action, buttons_down);
    PageInput *input = &inputs[index];
    if (!initialize && input->page == page && input->controller == controller && !input->pressed &&
        pressed && available && autodrive_game_window_focused()) {
        show_picker(page);
    }
    *input = (PageInput){page, controller, pressed};
}

static void initialize(void *page, unsigned native, unsigned index) {
    clear_picker();
    ((Action)(image + native))(page);
    read_input(index, page, 1);
    prompt(page);
}

static void update(void *page, unsigned native, unsigned index) {
    ((Action)(image + native))(page);
    finish_picker(page);
    if (ready(page)) {
        prompt(page);
    }
    read_input(index, page, 0);
}

static void initialize_buy(void *p) {
    initialize(p, 0xcab570, 0);
}

static void initialize_garage(void *p) {
    speed_zone_car_selection(p);
    initialize(p, 0xccd450, 1);
}

static void update_buy(void *p) {
    update(p, 0xcae2d0, 0);
}

static void update_garage(void *p) {
    update(p, 0xcae2d0, 1);
}

static void initialize_paint_garage(void *p) { initialize(p, 0xccd710, 2); }
static void initialize_paint_all(void *p) { initialize(p, 0xccd6f0, 3); }
static void update_paint_garage(void *p) { update(p, 0xcae2d0, 2); }
static void update_paint_all(void *p) { update(p, 0xcae2d0, 3); }

static void initialize_parts(void *page){
    ((Action)(image+0xec2af0))(page);
    if(is_rim_page(page)){clear_picker();read_input(4,page,1);prompt(page);}
}
static void update_parts(void *page){
    ((Action)(image+0xec7170))(page);
    finish_picker(page);
    if(is_rim_page(page)){
        if(ready(page))prompt(page);
        read_input(4,page,0);
    }
}
// Route the rim-specific binding before native Scene input; View stays native.
int manufacturer_jump_menu_key(void *root,unsigned char up){
    if(up || !is_rim_page(root) || !ready(root) || !autodrive_game_window_focused())return 0;
    void *controller=NULL;unsigned short down=0;
    if(autodrive_game_buttons(&controller,&down)!=0 && !binding_keyboard(BIND_RIM_MANUFACTURER_JUMP))return 0;
    if(!binding_down(BIND_RIM_MANUFACTURER_JUMP,down))return 0;
    show_picker(root);return parent_page==root && dialog!=NULL;
}

#include "manufacturer_jump_xaml.h"

PatchDefinition manufacturer_jump_hook_patches[13];
static unsigned char originals[13][8], replacements[13][8];
static const unsigned char *accepted[13][2];

void manufacturer_jump_prepare_hooks(unsigned char *base) {
    if (image && image != base) {
        clear_picker();
        registered = 0;
    }
    image = base;
    static const unsigned slots[] = {0x2d69258, 0x2d6d018, 0x2d69298, 0x2d6d058, 0x2ec9008,
                                     0x2dd5100, 0x2dd59c8, 0x2dd5140, 0x2dd5a08,
                                     0x2d82560,0x2e5ede0,0x2d825a0,0x2e5ee20};
    static const unsigned native[] = {0xcab570, 0xccd450, 0xcae2d0, 0xcae2d0, 0x1417e20,
                                      0xccd710, 0xccd6f0, 0xcae2d0, 0xcae2d0,
                                      0xec2af0,0xec2af0,0xec7170,0xec7170};
    static const uintptr_t callbacks[] = {(uintptr_t)initialize_buy, (uintptr_t)initialize_garage,
                                          (uintptr_t)update_buy, (uintptr_t)update_garage,
                                          (uintptr_t)grid_key_down,
                                          (uintptr_t)initialize_paint_garage,
                                          (uintptr_t)initialize_paint_all,
                                          (uintptr_t)update_paint_garage,
                                          (uintptr_t)update_paint_all,(uintptr_t)initialize_parts,(uintptr_t)initialize_parts,
                                          (uintptr_t)update_parts,(uintptr_t)update_parts};
    for (unsigned i = 0; i < ARRAY_COUNT(slots); ++i) {
        uintptr_t target = (uintptr_t)(base + native[i]);
        memory_copy(originals[i], &target, 8);
        memory_copy(replacements[i], &callbacks[i], 8);
        accepted[i][0] = originals[i];
        accepted[i][1] = replacements[i];
        manufacturer_jump_hook_patches[i] = (PatchDefinition){slots[i],
                                                              0,
                                                              8,
                                                              GROUP_VIDEO_OPTIONS,
                                                              "manufacturer_jump",
                                                              originals[i],
                                                              replacements[i],
                                                              replacements[i],
                                                              accepted[i],
                                                              2};
    }
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestManufacturerOpen(void *page) {
    show_picker(page);
}

__declspec(dllexport) void Fh2TestManufacturerFinish(void *page) {
    finish_picker(page);
}

__declspec(dllexport) NativeProperty *Fh2TestManufacturerProperty(unsigned i) {
    return i < 6 ? &properties[i] : NULL;
}

__declspec(dllexport) void Fh2TestManufacturerReset(void) {
    clear_picker();
}
#endif
