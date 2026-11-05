#include "audio_volume.h"
#include "support.h"
#include "video_settings.h"

static unsigned char *image;
static unsigned char original[2][8], replacement[2][8];
static const unsigned char *accepted[2][2];
PatchDefinition audio_volume_hook_patches[2];

// Native FH2 mixer-strip mute command. This follows the same locked command
// queue used by ParameterInstance::setValue (1a99b60), with the game's existing
// MixerStrip::setFaderLevel command vtable. Never execute the audio-thread handler
// directly or write into a live mixer strip from the UI thread.
static int mute_strip(void *studio,void *strip,int mute){
    void *context=NULL,*scope=NULL,*command=NULL;
    int result=((int (*)(void *,void **))(image+0x1ae90d0))(studio,&context);
    if(result)return result;
    if(!*((unsigned char *)context+0xfd))return 0x65;
    result=((int (*)(void **,void *))(image+0x1ae9240))(&scope,context);
    if(!result){
        unsigned char *queue=*(void **)((unsigned char *)context+0x78);
        command=queue+0x200;
        if(!((unsigned char (*)(void *))(image+0x1aeaf90))(queue))
            result=((int (*)(void *,void **,unsigned))(image+0x1ae9a70))
                (*(void **)(queue+0x1e8),&command,0x20);
        if(!result && command){
            unsigned char *bytes=command;
            *(void **)bytes=image+0x2347198;
            *(uint64_t *)(bytes+8)=0x20;
            *(unsigned *)(bytes+0x10)=(unsigned)(uintptr_t)strip;
            *(float *)(bytes+0x18)=mute?0.0f:1.0f;
            result=((int (*)(void *,void *))(image+0x1aea6e0))(queue,command);
        }
    }
    ((int (*)(void **))(image+0x1ae9290))(&scope);
    return result;
}

// These are the game's five Audio Options VCAs, read from the vanilla strings
// bank. Grouping follows the authored mix, including addon events routed to it.
static const unsigned char category_ids[5][16]={
    {0x63,0xd9,0x65,0x4f,0x9a,0xbb,0xb8,0x49,0x80,0xce,0x16,0xe9,0xf3,0xd7,0x44,0x0a}, // Music
    {0x65,0x9e,0xba,0xc7,0xb0,0xd3,0x8c,0x4c,0x95,0x27,0x65,0xa1,0x2d,0xb6,0x68,0xc0}, // SFX
    {0x45,0x6d,0x36,0x6d,0xfd,0x47,0xb9,0x4d,0xbf,0x31,0x19,0x0b,0x14,0x1d,0x09,0xb5}, // VO
    {0x4b,0xc3,0x1c,0x4b,0xba,0x60,0xa3,0x4d,0x8d,0x4f,0xbf,0x88,0x80,0xd5,0x33,0x54}, // Cars
    {0x98,0xf1,0x89,0x95,0x4b,0xb3,0xf2,0x41,0x97,0x4e,0x86,0x8a,0x2c,0x0a,0x12,0xc5}  // Tires
};
static unsigned desired_mutes,applied_mutes;
static void *mix_engine;
static ULONGLONG next_attempt;
static SRWLOCK mute_lock=SRWLOCK_INIT;
static void refresh_category_mutes(void){
    if(!image)return;
    AcquireSRWLockExclusive(&mute_lock);
    unsigned char *engine=*(void **)(image+0x365d3d8);
    if(engine!=mix_engine){mix_engine=engine;applied_mutes=0;next_attempt=0;}
    unsigned changed=desired_mutes^applied_mutes;
    if(changed && engine && *(void **)engine==image+0x2e73898 && GetTickCount64()>=next_attempt){
        void *studio=*(void **)(engine+0x7f0);
        if(studio){
            for(unsigned i=0;i<5;++i)if(changed&(1u<<i)){
                void *strip=NULL;
                int status=((int (*)(void *,const void *,unsigned,void **))(image+0x1a98300))
                    (studio,category_ids[i],0,&strip);
                if(!status && strip && !mute_strip(studio,strip,(desired_mutes>>i)&1))
                    applied_mutes=(applied_mutes&~(1u<<i))|(desired_mutes&(1u<<i));
            }
        }
        next_attempt=GetTickCount64()+500;
    }
    ReleaseSRWLockExclusive(&mute_lock);
}
static void update_mixer(void *manager){
    refresh_category_mutes();
    ((void (*)(void *))(image+0x1024570))(manager);
}

// Use the final FMOD output gain, independent of the native snapshot mix.
// Resolve the current engine each time; never retain a released engine/channel.
void audio_volume_refresh(void) {
    if (!image) {
        return;
    }
    unsigned char *engine = *(unsigned char **)(image + 0x365d3d8);
    if (!engine || *(void **)engine != image + 0x2e73898) {
        return;
    }
    void *studio = *(void **)(engine + 0x7f0), *core = NULL, *master = NULL;
    typedef int (*GetObject)(void *, void **);
    typedef int (*SetGain)(void *, float);
    if (studio && ((GetObject)(image + 0x1a98080))(studio, &core) == 0 && core &&
        ((GetObject)(image + 0x19954e0))(core, &master) == 0 && master) {
        ((SetGain)(image + 0x1994140))(master, video_setting_get(MASTER_VOLUME) / 100.0f);
    }
}

static void set_audio_options(void *engine, const void *options) {
    ((void (*)(void *, const void *))(image + 0x101fd50))(engine, options);
    unsigned mutes=0;
    const unsigned char *bytes=options;
    for(unsigned i=0;i<5;++i)if(*(const float *)(bytes+0xc+i*4)<=0)mutes|=1u<<i;
    AcquireSRWLockExclusive(&mute_lock);
    desired_mutes=mutes;next_attempt=0;
    ReleaseSRWLockExclusive(&mute_lock);
    refresh_category_mutes();
    audio_volume_refresh();
}

void audio_volume_prepare_hooks(unsigned char *base) {
    if(image!=base){desired_mutes=applied_mutes=0;mix_engine=NULL;next_attempt=0;}
    image = base;
    static const unsigned sites[]={0x2e73918,0x2e764a8};
    static const unsigned native[]={0x101fd50,0x1024570};
    static const uintptr_t callbacks[]={(uintptr_t)set_audio_options,(uintptr_t)update_mixer};
    for(unsigned i=0;i<2;++i){
        uintptr_t target=(uintptr_t)(base+native[i]);
        memory_copy(original[i],&target,8);memory_copy(replacement[i],&callbacks[i],8);
        accepted[i][0]=original[i];accepted[i][1]=replacement[i];
        audio_volume_hook_patches[i]=(PatchDefinition){sites[i],0,8,GROUP_VIDEO_OPTIONS,
            i?"audio_category_zero_mute":"master_volume",original[i],replacement[i],replacement[i],accepted[i],2};
    }
}
