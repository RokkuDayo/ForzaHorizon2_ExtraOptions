#include "headlight_beams.h"
#include "support.h"

// Daylight activation belongs to each light preset, separately from the car's
// emissive amount. Override only the player's HeadlightAmount (13) presets while
// the native renderer uploads them. Never change the global night-light factor.
static unsigned char *image;
static void *volatile renderer;
typedef struct { unsigned char *address; float saved[2]; } Override;
typedef struct { unsigned char *address; float original[2]; } Key;
static Key previous[32];
static unsigned previous_count;
static void *previous_system;
PatchDefinition headlight_beam_patches[4];
static unsigned char originals[4][12], replacements[4][12];
static const unsigned char *accepted[4][2];

void headlight_beams_set_car(void *car, int enabled) {
    void *proxy=NULL,*actual=NULL,*vtable=NULL;
    if(enabled && car && memory_read_pointer((unsigned char *)car+0x4d38,&proxy) &&
       memory_read_pointer((unsigned char *)proxy+8,&actual) && memory_read_pointer(actual,&vtable) &&
       (vtable==image+0x2ec1450 || vtable==image+0x2ec2b70)) {
        InterlockedExchangePointer(&renderer,actual);
    } else InterlockedExchangePointer(&renderer,NULL);
}

static unsigned char *record(unsigned char *owner, unsigned offset, unsigned index,
                             size_t stride, unsigned limit) {
    uintptr_t begin=0,end=0;
    if(!owner || !memory_read(owner+offset,&begin,8) || !memory_read(owner+offset+8,&end,8) ||
       !begin || end<begin || (end-begin)%stride || (end-begin)/stride>limit ||
       index>=(end-begin)/stride)return NULL;
    return (unsigned char *)(begin+index*stride);
}

static unsigned collect(void *system, int glow, Override out[32]) {
    unsigned char *car=InterlockedCompareExchangePointer(&renderer,NULL,NULL);
    void *vtable=NULL;unsigned char *context=NULL,*manager=NULL,*backing=NULL,*asset=NULL;
    uintptr_t begin=0,end=0;
    if(!car || !memory_read_pointer(car,&vtable) ||
       (vtable!=image+0x2ec1450 && vtable!=image+0x2ec2b70) ||
       !memory_read_pointer(car+0x10,(void **)&context) ||
       !memory_read_pointer(context+0x380,(void **)&manager) ||
       !memory_read_pointer(manager+(glow?0x18:8),(void **)&backing) || backing!=system ||
       !memory_read_pointer(car+0x1278,(void **)&asset) ||
       !memory_read(asset+0x4be0,&begin,8) || !memory_read(asset+0x4be8,&end,8) ||
       !begin || end<begin || (end-begin)%8 || (end-begin)/8>512)return 0;
    unsigned count=0;
    for(uintptr_t at=begin;at<end && count<32;at+=8) {
        unsigned char *instance=NULL, data[0x54];
        if(!memory_read_pointer((void *)at,(void **)&instance) ||
           !memory_read(instance,data,sizeof(data)) || *(unsigned short *)(data+4)!=13)continue;
        unsigned char *mapping=record(manager,0x28,*(unsigned *)(data+0x50),24,100000);
        unsigned index=~0u;
        if(!mapping || !memory_read(mapping+(glow?4:0),&index,4))continue;
        unsigned char *light=record(backing,glow?0x678:0,index,64,100000);
        unsigned preset=~0u;
        if(!light || !memory_read(light+(glow?0xc:0x30),&preset,4))continue;
        if(glow)preset&=0xffff;
        unsigned char *definition=record(backing,glow?0x6b0:0x38,preset,glow?0x120:0xc0,100000);
        unsigned char *threshold=definition?definition+(glow?0x44:0x9c):NULL;
        if(!threshold || !memory_is_writable(threshold,8))continue;
        unsigned i=0;for(;i<count;++i)if(out[i].address==threshold)break;
        if(i<count)continue;
        float values[2];
        if(!memory_read(threshold,values,8) || !float_is_finite(values[0]) ||
           !float_is_finite(values[1]))continue;
        out[count].address=threshold;memory_copy(out[count].saved,values,8);++count;
    }
    return count;
}

static void upload(void *system,void *graphics,int glow) {
    Override overrides[32];unsigned count=collect(system,glow,overrides);
    if(!glow) {
        int changed=system!=previous_system || count!=previous_count;
        for(unsigned i=0;!changed && i<count;++i)
            changed=previous[i].address!=overrides[i].address ||
                    !memory_equal(previous[i].original,overrides[i].saved,8);
        // ForwardPlus caches its preset GPU buffer. Invalidate only on a change
        // of ownership/preset values, including On -> Default/Off and car swaps.
        if(changed && (count || previous_count)) {
            unsigned char *dirty=(unsigned char *)system+0x100;
            if(memory_is_writable(dirty,1))*dirty=1;
        }
        previous_system=system;previous_count=count;
        for(unsigned i=0;i<count;++i) {
            previous[i].address=overrides[i].address;
            memory_copy(previous[i].original,overrides[i].saved,8);
        }
    }
    const float always_on[2]={0,0};
    for(unsigned i=0;i<count;++i)memory_copy(overrides[i].address,always_on,8);
    ((void (*)(void *,void *))(image+(glow?0x10f6970:0x10f6c40)))(system,graphics);
    for(unsigned i=0;i<count;++i)memory_copy(overrides[i].address,overrides[i].saved,8);
}
static void upload_beams(void *system,void *graphics){upload(system,graphics,0);}
static void upload_glows(void *system,void *graphics){upload(system,graphics,1);}

void headlight_beams_bind(unsigned char *game_image) {
    if(image!=game_image){renderer=NULL;previous_count=0;previous_system=NULL;}
    image=game_image;
    const unsigned sites[2]={0x130839e,0x13083cd};
    const unsigned pads[2]={0x13084a1,0x13084e1};
    const unsigned targets[2]={0x10f6c40,0x10f6970};
    const uintptr_t hooks[2]={(uintptr_t)upload_beams,(uintptr_t)upload_glows};
    const char *names[4]={"player_headlight_upload_bridge","player_headlight_upload",
                          "player_headlight_glow_bridge","player_headlight_glow"};
    for(unsigned i=0;i<2;++i) {
        unsigned p=i*2,c=p+1;
        for(unsigned j=0;j<12;++j)originals[p][j]=0xcc;
        replacements[p][0]=0x48;replacements[p][1]=0xb8;
        memory_copy(replacements[p]+2,&hooks[i],8);
        replacements[p][10]=0xff;replacements[p][11]=0xe0;
        originals[c][0]=replacements[c][0]=0xe8;
        int32_t delta=targets[i]-(sites[i]+5);memory_copy(originals[c]+1,&delta,4);
        delta=pads[i]-(sites[i]+5);memory_copy(replacements[c]+1,&delta,4);
        for(unsigned j=p;j<=c;++j){
            accepted[j][0]=originals[j];accepted[j][1]=replacements[j];
            headlight_beam_patches[j]=(PatchDefinition){j==p?pads[i]:sites[i],0,j==p?12:5,
                GROUP_VEHICLE_CONTROLS,names[j],originals[j],replacements[j],replacements[j],accepted[j],2};
        }
    }
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestHeadlightBind(void *value){headlight_beams_bind(value);}
__declspec(dllexport) void Fh2TestHeadlightRenderer(void *value){InterlockedExchangePointer(&renderer,value);}
__declspec(dllexport) void Fh2TestHeadlightUpload(void *system,void *graphics,int glow){upload(system,graphics,glow);}
#endif
