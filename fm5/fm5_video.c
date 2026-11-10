#include "fm5/fm5_video.h"
#include "fm5/fm5_camera.h"
#include "fm5/fm5_settings.h"
#include "shared/support.h"
#include "shared/video_options_text.h"
#include "fm5/fm5_video_layout.h"

typedef struct {void **vtable;unsigned char header[0x20];void *get,*set,*unused;const char *name;void *type;uint64_t flags;} Property;
typedef struct Link {Property *property;struct Link *next;} Link;
typedef struct {void **vtable;void *page,*unused;} Callback;
typedef struct {Callback storage;void *target;} Function;
_Static_assert(sizeof(Property)==0x58,"native property ABI");
static unsigned char *image;
static void *(*allocate)(size_t);
enum { QUALITY_CHOICES=2, CALIBRATION, COCKPIT_BLUR, BLUR_CHOICES, PROPERTY_COUNT };
static Property props[PROPERTY_COUNT];static Link links[PROPERTY_COUNT];
static const char *const names[]={"EoLivery4K","EoLiveryMSAA","EoLiveryChoices","EoCalibration","EoCockpitBlur","EoBlurChoices"};
static int registered,calibration=1;
#define values (fm5_settings_values()+FM5_SAVED_LIVERY_4K)
static void *choices,*blur_choices,*profile_page;
static Function saved_back;
static void clear_back(void){
    void *target=saved_back.target;saved_back.target=NULL;
    if(target)((void (*)(void *,unsigned char))(*(void ***)target)[4])(target,1);
}
static void remember_back(void *page){
    clear_back();
    unsigned char *head=*(unsigned char **)((unsigned char *)page+0x290);
    if(!head)return;
    unsigned char *node=*(unsigned char **)head;
    for(unsigned i=0;node && node!=head && i<256;++i,node=*(unsigned char **)node){
        if(*(int *)(node+0x10)!=0x2d || !node[0x14])continue;
        Function *function=(Function *)(node+0x18);void *target=function->target;
        if(target)saved_back.target=((void *(*)(void *,void *))(*(void ***)target)[0])(target,NULL);
        break;
    }
}
static void (*log_message)(const char *);
static void release(void *object){if(object && --*(unsigned *)((unsigned char *)object+0x10)==0)((void (*)(void *))(image+0x137710))(object);}
static const VideoOptionsText *labels(void){
    void *manager=NULL;unsigned char code[32]={0};
    if(memory_read_pointer(image+0x25e7ae8,&manager) && manager)memory_read((unsigned char *)manager+0x30,code,32);
    return video_options_text_code(code[24]==15 && code[16]>=2 && code[16]<=3?(char *)code:NULL);
}
static void notify(void *page,unsigned i){((void (*)(void *,void *))(image+0x3c02a0))(page,props+i);}
static void save(void){fm5_settings_save();}
static void **get(void **out,Property *property,void *page){
    (void)page;unsigned i=(unsigned)(property-props);*out=NULL;
    if(i==QUALITY_CHOICES || i==BLUR_CHOICES){*out=i==QUALITY_CHOICES?choices:blur_choices;if(*out)++*(unsigned *)((unsigned char *)*out+0x10);}
    else if(i<PROPERTY_COUNT){int value=i==CALIBRATION?calibration:i==COCKPIT_BLUR?fm5_camera_get(FM5_COCKPIT_BLUR):values[i];((void **(*)(void **,const int *))(image+0x296b00))(out,&value);}
    return out;
}
static void set(Property *property,void *page,void *boxed){
    unsigned i=(unsigned)(property-props);int value;
    if((i>=2 && i!=COCKPIT_BLUR) || !boxed || !memory_read((unsigned char *)boxed+0x28,&value,4) || value<0 || value>1)return;
    if(i==COCKPIT_BLUR){if(fm5_camera_set(FM5_COCKPIT_BLUR,value))notify(page,i);return;}
    if(value==values[i])return;
    values[i]=value;save();notify(page,i);
}
static int register_properties(void){
    if(registered)return 1;
    const VideoOptionsText *text=labels();
    for(unsigned group=0;group<2;++group){
        void **collection=group?&blur_choices:&choices;
        *collection=allocate(0x58);if(!*collection)return 0;
        ((void (*)(void *))(image+0x33d020))(*collection);
        for(int i=0;i<2;++i){
            void *label=allocate(0x40);if(!label)return 0;
            const wchar_t *word=i?text->choices[0]:group?text->choices[2]:text->default_choice;wchar_t upper[256];
            if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,word,-1,upper,256,NULL,NULL,0))word=upper;
            ((void *(*)(void *,const wchar_t *,int))(image+0xab1770))(label,word,-1);
            ((void (*)(void *,int,void *))(*(void ***)*collection)[8])(*collection,i,label);release(label);
        }
    }
    Link **head=(Link **)(image+0x246c130);
    for(unsigned i=0;i<PROPERTY_COUNT;++i){
        Property *original=((Property *(*)(void))(image+((i==QUALITY_CHOICES || i==BLUR_CHOICES)?0x357260:0x357080)))();
        ((void (*)(void *))(image+0x9045f0))(props+i);props[i].vtable=original->vtable;
        memory_copy((unsigned char *)(props+i)+0x28,(unsigned char *)original+0x28,0x30);
        props[i].name=names[i];props[i].get=(void *)get;props[i].set=(i<2 || i==COCKPIT_BLUR)?(void *)set:NULL;
        links[i]=(Link){props+i,*head};*head=links+i;
    }
    registered=1;return 1;
}
static void *clone(Callback *self,void *out){if(!out)out=allocate(sizeof(*self));if(out)memory_copy(out,self,sizeof(*self));return out;}
static void destroy(Callback *self,unsigned char owned){if(owned)((void (*)(void *))(image+0x1cdc30))(self);}
static void *callback_type(void *self){(void)self;return NULL;}
static unsigned char back(Callback *self){
    calibration=0;notify(self->page,CALIBRATION);
    /* Remove only our temporary Back override. The native page's normal Back
       behavior then remains responsible for leaving Video options. */
    if(saved_back.target)((void (*)(void *,int,Function *))(image+0x8e2f30))(self->page,0x2d,&saved_back);
    else ((void (*)(void *,int))(image+0x8e3090))(self->page,0x2d);
    return 1;
}
static void show_brightness(void *page);
static unsigned char accept(Callback *self){
    if(calibration)return 1;
    void *button=((void *(*)(void *,const char *))(image+0x2ec3e0))(self->page,"EoBrightness");
    if(button){
        void *focused=NULL;
        /* UIElement.IsKeyboardFocusWithin through the native dependency getter. */
        ((void **(*)(void *,void **,void *))(image+0x9fe290))(button,&focused,*(void **)(image+0x24830b8));
        int is_focused=focused && *((unsigned char *)focused+0x28);release(focused);
        if(is_focused){show_brightness(self->page);return 1;}
    }
    save();
    /* Same native page-completion call used by HUD Accept/Cancel. */
    void *result=NULL;
    ((void (*)(void *,void **))(*(void ***)self->page)[0x510/8])(self->page,&result);
    release(result);return 1;
}
static unsigned char reset(Callback *self){if(calibration)return 0;for(unsigned i=0;i<2;++i){values[i]=0;notify(self->page,i);}fm5_camera_set(FM5_COCKPIT_BLUR,1);notify(self->page,COCKPIT_BLUR);save();return 1;}
static void *back_table[]={(void *)clone,(void *)clone,(void *)back,(void *)callback_type,(void *)destroy};
static void *accept_table[]={(void *)clone,(void *)clone,(void *)accept,(void *)callback_type,(void *)destroy};
static void *reset_table[]={(void *)clone,(void *)clone,(void *)reset,(void *)callback_type,(void *)destroy};
static void bind(void *page,int action,void **table){Function f={{table,page,NULL},NULL};f.target=&f.storage;((void (*)(void *,int,Function *))(image+0x8e2f30))(page,action,&f);}
static void show_brightness(void *page){remember_back(page);calibration=1;notify(page,CALIBRATION);bind(page,0x2d,back_table);}
static void profile_input(void *page,int action,Function *function){
    profile_page=page;((void (*)(void *,int,Function *))(image+0x8e2f30))(page,action,function);
}
static void initialize_page(void *page){
    clear_back();profile_page=NULL;calibration=1;
    ((void (*)(void *))(image+0x4aae00))(page);
    if(profile_page!=page)return; /* Preserve initial-experience calibration. */
    calibration=0;notify(page,CALIBRATION);bind(page,0x44,reset_table);bind(page,0x12,accept_table);
    void *button=((void *(*)(void *,const char *))(image+0x2ec3e0))(page,"EoBrightness");
    if(!button)return;
    ((void (*)(void *))(image+0x29acc0))(button);
    _Alignas(16) struct {void (*method)(void *);uint64_t adjustment;} method={show_brightness,0};
    void *delegate=NULL;((void (*)(void **,void *,void *))(image+0x3a98d0))(&delegate,page,&method);
    if(delegate){((void (*)(void *,void *))(image+0x25e500))(button,(unsigned char *)delegate+0x28);release(delegate);}
}
static void close_page(void *page){
    clear_back();profile_page=NULL;calibration=1;
    ((void (*)(void *))(image+0x4ab040))(page);
}
static void put(char *out,size_t capacity,size_t *used,const char *src,size_t count){
    if(*used>=capacity || count>=capacity-*used){*used=capacity;return;}
    memory_copy(out+*used,src,count);*used+=count;out[*used]=0;
}
static void escaped(char *out,size_t capacity,size_t *used,const char *src){
    for(;*src;++src){const char *s=*src=='&'?"&amp;":*src=='\"'?"&quot;":*src=='<'?"&lt;":*src=='>'?"&gt;":NULL;
        put(out,capacity,used,s?s:src,s?text_length(s):1);}
}
static int starts(const char *s,const char *prefix){while(*prefix)if(*s++!=*prefix++)return 0;return 1;}
size_t fm5_video_xaml(const char *source,size_t length,char *out,size_t capacity){
    if(!text_find(source,length,"x:Class=\"ForzaUI.F866_VideoOptions\"") || !register_properties())return 0;
    const VideoOptionsText *text=labels();size_t used=0;put(out,capacity,&used,"\xef\xbb\xbf",3);
    for(const char *s=video_layout;*s;){
        if(starts(s,"@DESCRIPTION_TRIGGERS@")){
            const char *a="<Page.Triggers>";put(out,capacity,&used,a,text_length(a));
            for(unsigned i=0;i<4;++i){
                const char *p="<EventTrigger RoutedEvent=\"Keyboard.GotKeyboardFocus\" SourceName=\"";put(out,capacity,&used,p,text_length(p));
                p=i==0?"EoBrightness":i==1?names[COCKPIT_BLUR]:names[i-2];put(out,capacity,&used,p,text_length(p));
                p="\"><BeginStoryboard><Storyboard><StringAnimationUsingKeyFrames Storyboard.TargetName=\"TextBlock_FocusedDescription\" Storyboard.TargetProperty=\"Text\"><DiscreteStringKeyFrame KeyTime=\"0\" Value=\"";put(out,capacity,&used,p,text_length(p));
                if(i==1)escaped(out,capacity,&used,"Enable motion blur in the cockpit view.");
                else if(i>1)escaped(out,capacity,&used,text->settings_descriptions[i+7]);
                else {p="{local:Loc GameOptions2.IDS_BrightnessAdjustment_Desc2}";put(out,capacity,&used,p,text_length(p));}
                p="\"/></StringAnimationUsingKeyFrames></Storyboard></BeginStoryboard></EventTrigger>";put(out,capacity,&used,p,text_length(p));
            }
            a="</Page.Triggers>";put(out,capacity,&used,a,text_length(a));s+=text_length("@DESCRIPTION_TRIGGERS@");
        }else if(starts(s,"@LABEL") && s[6]>='0' && s[6]<='1' && s[7]=='@'){
            wchar_t wide[256],upper[256];char utf8[1024];const char *label=text->settings_names[9+s[6]-'0'];
            if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,label,-1,wide,256) && LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,wide,-1,upper,256,NULL,NULL,0) && WideCharToMultiByte(CP_UTF8,0,upper,-1,utf8,sizeof(utf8),NULL,NULL))label=utf8;
            escaped(out,capacity,&used,label);s+=8;
        }else {put(out,capacity,&used,s,1);++s;}
        if(used>=capacity)return 0;
    }
    return used;
}
/* Fixed, verified native render-target edits. No polling or per-frame work. */
static const float livery_inverse __attribute__((used))=.25f;
__attribute__((naked)) static void livery_extents(void){
    __asm__ volatile(
        "vcvtdq2ps %xmm0, %xmm0\n"
        "vmulss livery_inverse(%rip), %xmm0, %xmm4\n"
        "vmovd %edi, %xmm1\n"
        "vcvtdq2ps %xmm1, %xmm1\n"
        "vmulss livery_inverse(%rip), %xmm1, %xmm5\n"
        "ret\n");
}
static unsigned quality_sites(Fm5Site *sites){
    unsigned count=0;
    if(values[0]){
        sites[count++]=(Fm5Site){.rva=0x7aa671,.size=5,.original={0xba,0,8,0,0},.replacement={0xba,0,0x10,0,0}};
        sites[count++]=(Fm5Site){.rva=0x7aa6a6,.size=5,.original={0xba,0,4,0,0},.replacement={0xba,0,0x10,0,0}};
    }
    if(values[1]){
        /* The native retail path requests eight samples. Azimuth can lower this
           value; enabling enhancement never downgrades an existing 8x target. */
        unsigned char current[8];memory_copy(current,image+0x7ba4ad,8);
        if(current[0]==0x48 && current[1]==0xc7 && current[2]==0x40 && current[3]==8 &&
           (current[4]==1 || current[4]==2 || current[4]==4 || current[4]==8) && !current[5] && !current[6] && !current[7]){
            Fm5Site *s=sites+count++;*s=(Fm5Site){.rva=0x7ba4ad,.size=8};memory_copy(s->original,current,8);memory_copy(s->replacement,current,8);
            if(s->replacement[4]<4)s->replacement[4]=4;
        }else sites[count++]=(Fm5Site){.rva=0x7ba4ad,.size=8,.original={0x48,0xc7,0x40,8,8,0,0,0},.replacement={0x48,0xc7,0x40,8,8,0,0,0}};
    }
    if(values[0]){
        unsigned shift=2;
        /* Original adds double EBP/R14D. A leaf in the adjacent padding scales
           both dimensions without touching live integer/vector registers. */
        Fm5Site *s=sites+count++;*s=(Fm5Site){.rva=0x7ba707,.size=9,.replacement={0xc1,0xe5,(unsigned char)shift,0x41,0xc1,0xe6,(unsigned char)shift,0xc3,0x90}};
        for(unsigned i=0;i<9;++i)s->original[i]=0xcc;
        s=sites+count++;*s=(Fm5Site){.rva=0x7ba536,.size=5,.original={3,0xed,0x45,3,0xf6},.replacement={0xe8}};
        int32_t delta=0x7ba707-0x7ba53b;memory_copy(s->replacement+1,&delta,4);
        /* Correct only the two render-space extents. XMM7 remains 0.5 for the
           separate region-centering math later in the same function. */
        s=sites+count++;*s=(Fm5Site){.rva=0x7b9c3c,.size=16,
          .original={0xc5,0xf8,0x5b,0xc0,0xc5,0xfa,0x59,0xe7,0xc5,0xf9,0x6e,0xcf,0xc5,0xf8,0x5b,0xc9},
          .replacement={0xe8,0,0,0,0,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90}};
        void *extent=(void *)livery_extents;
        static const unsigned char call[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x90,0x90,0x90,0x90};
        memory_copy(s->replacement,call,16);memory_copy(s->replacement+2,&extent,8);
        s=sites+count++;*s=(Fm5Site){.rva=0x7b9c4c,.size=4,.original={0xc5,0xf2,0x59,0xef},.replacement={0x90,0x90,0x90,0x90}};
    }
    return count;
}
unsigned fm5_video_prepare(unsigned char *game,Fm5Site *sites,const wchar_t *directory,void (*log)(const char *)){
    image=game;log_message=log;registered=0;choices=blur_choices=NULL;calibration=1;profile_page=NULL;
    allocate=(void *(*)(size_t))(image+0x1cdbb0);(void)directory;unsigned count=quality_sites(sites);
    Fm5Site *s=sites+count++;*s=(Fm5Site){.rva=0x226b0f8,.size=8};
    void *native=image+0x4aae00,*callback=(void *)initialize_page;memory_copy(s->original,&native,8);memory_copy(s->replacement,&callback,8);
    s=sites+count++;*s=(Fm5Site){.rva=0x226b158,.size=8};native=image+0x4ab040;callback=(void *)close_page;
    memory_copy(s->original,&native,8);memory_copy(s->replacement,&callback,8);
    s=sites+count++;*s=(Fm5Site){.rva=0x4a0ae4,.size=12,.replacement={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0}};
    for(unsigned i=0;i<12;++i)s->original[i]=0xcc;callback=(void *)profile_input;memory_copy(s->replacement+2,&callback,8);
    s=sites+count++;*s=(Fm5Site){.rva=0x4aaf4b,.size=5,.original={0xe8,0xe0,0x7f,0x43,0},.replacement={0xe8}};
    int32_t delta=0x4a0ae4-0x4aaf50;memory_copy(s->replacement+1,&delta,4);
    if(log_message)log_message("VIDEO: native options page, original brightness calibration and restart-applied livery quality controls enabled.\r\n");
    return count;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) size_t Fm5TestVideoXaml(const char *s,size_t n,char *out,size_t cap){return fm5_video_xaml(s,n,out,cap);}
__declspec(dllexport) void *Fm5TestVideoProperty(unsigned i){return i<PROPERTY_COUNT?props+i:NULL;}
__declspec(dllexport) unsigned Fm5TestVideoPrepare(void *game,Fm5Site *sites,const wchar_t *directory){return fm5_video_prepare(game,sites,directory,NULL);}
#endif
