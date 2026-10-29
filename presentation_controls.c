#include "presentation_controls.h"
#include "support.h"
#include "video_settings.h"

static unsigned char *image;

typedef void **(*Narrative)(void *, void **, const void *);
typedef void **(*GarageNarrative)(void *, void **, const void *, int);

// These call sites return a temporary shared_ptr whose caller immediately
// releases it. An empty result is valid and avoids scheduling the optional line.
static void **photo_repeat(void *manager, void **result, const void *name) {
    if (video_setting_get(BEN_PHOTO_DIALOGUE)) {
        return ((Narrative)(image + 0x5118a0))(manager, result, name);
    }
    result[0] = result[1] = NULL;
    return result;
}

static void **garage_greeting(void *manager, void **result, const void *name, int priority) {
    if (video_setting_get(ASHLEY_GARAGE_DIALOGUE)) {
        return ((GarageNarrative)(image + 0x5117b0))(manager, result, name, priority);
    }
    result[0] = result[1] = NULL;
    return result;
}

static void checkpoint_audio(void *manager, void *event) {
    if (video_setting_get(CHECKPOINT_SOUND)) {
        ((void (*)(void *, void *))(*(void ***)manager)[1])(manager, event);
    }
}

static void wheelspin_notification(void *notification, const void *heading, const void *message) {
    if (video_setting_get(WHEELSPIN_NOTIFICATION)) {
        ((void (*)(void *, const void *, const void *))(image + 0x755230))(notification, heading,
                                                                           message);
    }
}

static void skill_notification(void *notification) {
    if (video_setting_get(SKILL_POINT_NOTIFICATION))
        ((void (*)(void *))(image + 0x750db0))(notification);
}
static void skill_available_voice(void *component) {
    if (video_setting_get(SKILL_POINT_NOTIFICATION))
        ((void (*)(void *))(image + 0x59d570))(component);
}
static void skill_earned_voice(void *component) {
    if (video_setting_get(SKILL_POINT_NOTIFICATION))
        ((void (*)(void *))(image + 0x59d670))(component);
}
static void **wheelspin_voice(void *manager, void **result, const void *name, int priority) {
    if (video_setting_get(WHEELSPIN_NOTIFICATION))
        return ((GarageNarrative)(image + 0x5117b0))(manager, result, name, priority);
    result[0] = result[1] = NULL;
    return result;
}
static void **wheelspin_intro_voice(void *manager, void **result, const void *name) {
    if (video_setting_get(WHEELSPIN_NOTIFICATION))
        return ((Narrative)(image + 0x5118a0))(manager, result, name);
    result[0] = result[1] = NULL;
    return result;
}

// Only NoiseOffsetAngle/X/Y used by HUDSkillControl's TopBounds transform.
// Native noise generation and score state stay intact for immediate re-enabling.
static void **skill_counter_noise(void **out, const float *value) {
    const float zero = 0.0f;
    return ((void **(*)(void **, const float *))(image + 0x3f5990))(
        out, video_setting_get(SKILL_COUNTER_SHAKE) ? value : &zero);
}

static void **skill_counter_scale(void **out, const float *value) {
    const float normal = 1.0f;
    return ((void **(*)(void **, const float *))(image + 0x3f5990))(
        out, video_setting_get(SKILL_COUNTER_SHAKE) ? value : &normal);
}

// Only SpeedZoneActivationComponent's activation and fade-weight updates. Zero weight
// restores the underlying world preset; the zone's timers/state remain native.
static void speed_zone_vignette(void *manager, const void *name, float weight) {
    ((void (*)(void *, const void *, float))(image + 0x11b35b0))(
        manager, name, video_setting_get(SPEED_ZONE_VIGNETTE) ? weight : 0.0f);
}

// This typed constant-buffer upload contains the world's main vignette, not the
// UI button-bar gradient. Its upper/lower alpha values interpolate vertically.
// Preserve authored parameters: only the GPU upload sees zero strength when Off.
static void vignette_upload(void *buffer,void *graphics,void *parameters) {
    typedef void (*Upload)(void *,void *,void *);
    Upload native=(Upload)(image+0x18b7c50);
    if(video_setting_get(VIGNETTE_EFFECT)){native(buffer,graphics,parameters);return;}
    unsigned char *data=parameters;float upper,lower;
    if(!data || !memory_read(data+0xac,&upper,4) || !memory_read(data+0xdc,&lower,4) ||
       !memory_is_writable(data+0xac,4) || !memory_is_writable(data+0xdc,4)){
        native(buffer,graphics,parameters);return;
    }
    const float zero=0;
    memory_copy(data+0xac,&zero,4);memory_copy(data+0xdc,&zero,4);
    native(buffer,graphics,parameters);
    memory_copy(data+0xac,&upper,4);memory_copy(data+0xdc,&lower,4);
}

PatchDefinition presentation_controls_hook_patches[35];
static unsigned char originals[35][13], replacements[35][13];
static const unsigned char *accepted[35][2];

void presentation_controls_prepare_hooks(unsigned char *base) {
    image = base;
    // Verified INT3 padding outside native functions. Keep the audio dispatch
    // separate from checkpoint tracking, placement, timers and race progression.
    static const unsigned thunks[] = {0x511334, 0x511893, 0x5118e3};
    static const unsigned sites[] = {0xdd0391, 0xd38b4e, 0x74d26b};
    static const unsigned callees[] = {0x5118a0, 0x5117b0};
    static const uintptr_t callbacks[] = {(uintptr_t)photo_repeat, (uintptr_t)garage_greeting,
                                          (uintptr_t)checkpoint_audio};
    static const char *const names[] = {"ben_photo_dialogue_thunk", "ashley_garage_dialogue_thunk",
                                        "checkpoint_sound_thunk",   "ben_photo_dialogue",
                                        "ashley_garage_dialogue",   "checkpoint_sound"};
    for (unsigned i = 0; i < 6; ++i) {
        unsigned address, size;
        if (i < 3) {
            address = thunks[i];
            size = 12;
            for (unsigned j = 0; j < size; ++j) {
                originals[i][j] = 0xcc;
            }
            static const unsigned char code[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
            memory_copy(replacements[i], code, size);
            memory_copy(replacements[i] + 2, &callbacks[i], 8);
        } else {
            unsigned index = i - 3;
            address = sites[index];
            if (index < 2) {
                size = 5;
                int32_t native = (int32_t)(callees[index] - address - 5);
                int32_t target = (int32_t)(thunks[index] - address - 5);
                originals[i][0] = replacements[i][0] = 0xe8;
                memory_copy(originals[i] + 1, &native, 4);
                memory_copy(replacements[i] + 1, &target, 4);
            } else {
                size = 13;
                static const unsigned char native[] = {0x4c, 0x8b, 0x03, 0x48, 0x8b, 0xd0, 0x48,
                                                       0x8b, 0xcb, 0x41, 0xff, 0x50, 0x08};
                static const unsigned char code[] = {0x48, 0x8b, 0xd0, 0x48, 0x8b, 0xcb, 0xe8,
                                                     0,    0,    0,    0,    0x90, 0x90};
                int32_t target = (int32_t)(thunks[index] - address - 11);
                memory_copy(originals[i], native, size);
                memory_copy(replacements[i], code, size);
                memory_copy(replacements[i] + 7, &target, 4);
            }
        }
        accepted[i][0] = originals[i];
        accepted[i][1] = replacements[i];
        presentation_controls_hook_patches[i] = (PatchDefinition){address,
                                                                  0,
                                                                  size,
                                                                  GROUP_VIDEO_OPTIONS,
                                                                  names[i],
                                                                  originals[i],
                                                                  replacements[i],
                                                                  replacements[i],
                                                                  accepted[i],
                                                                  2};
    }
    // Only the Tombola notification component's three display calls. Its XP,
    // earned-spin state and post-race rewards are left on their native paths.
    static const unsigned notify_sites[] = {0x7842d9, 0x7844e7, 0x7846e9};
    for (unsigned i = 6; i < 10; ++i) {
        unsigned address = i == 6 ? 0x770e44 : notify_sites[i - 7];
        unsigned size = i == 6 ? 12 : 5;
        if (i == 6) {
            for (unsigned j = 0; j < 12; ++j) {
                originals[i][j] = 0xcc;
            }
            static const unsigned char code[] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
            uintptr_t callback = (uintptr_t)wheelspin_notification;
            memory_copy(replacements[i], code, 12);
            memory_copy(replacements[i] + 2, &callback, 8);
        } else {
            originals[i][0] = replacements[i][0] = 0xe8;
            int32_t before = (int32_t)(0x755230 - address - 5);
            int32_t after = (int32_t)(0x770e44 - address - 5);
            memory_copy(originals[i] + 1, &before, 4);
            memory_copy(replacements[i] + 1, &after, 4);
        }
        accepted[i][0] = originals[i];
        accepted[i][1] = replacements[i];
        presentation_controls_hook_patches[i] = (PatchDefinition){address,
                                                                  0,
                                                                  size,
                                                                  GROUP_VIDEO_OPTIONS,
                                                                  "freeroam_wheelspin_notification",
                                                                  originals[i],
                                                                  replacements[i],
                                                                  replacements[i],
                                                                  accepted[i],
                                                                  2};
    }
    // Presentation and narrative dispatch ONLY. All caller state advances and
    // clears pending flags normally; awards, skill progress and post-race UI
    // keep the original paths. Empty narrative futures are valid: 11ce8e0 is
    // the native null-safe shared-state move used by the intro caller.
    static const unsigned extra_thunks[] = {0x590124, 0x5905b4, 0x590624,
                                            0x590bf4, 0x5913f4};
    static const uintptr_t extra_callbacks[] = {(uintptr_t)skill_notification,
        (uintptr_t)skill_available_voice, (uintptr_t)skill_earned_voice,
        (uintptr_t)wheelspin_voice, (uintptr_t)wheelspin_intro_voice};
    static const unsigned extra_sites[] = {0x5aab7d, 0x5aac78, 0x5aac96,
        0x784bfb, 0x784d57, 0x784f26, 0x784fe9, 0x784c76, 0x784dc8};
    static const unsigned extra_callees[] = {0x750db0, 0x59d570, 0x59d670,
        0x5117b0, 0x5117b0, 0x5117b0, 0x5117b0, 0x5118a0, 0x5118a0};
    for (unsigned i = 10; i < 24; ++i) {
        unsigned n = i - 10, address, size;
        if (n < 5) {
            address = extra_thunks[n];
            size = 12;
            for (unsigned j = 0; j < size; ++j) originals[i][j] = 0xcc;
            static const unsigned char code[] = {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
            memory_copy(replacements[i], code, size);
            memory_copy(replacements[i]+2, &extra_callbacks[n], 8);
        } else {
            n -= 5;
            address = extra_sites[n];
            size = 5;
            unsigned thunk = n < 3 ? n : n < 7 ? 3 : 4;
            int32_t before = (int32_t)(extra_callees[n] - address - 5);
            int32_t after = (int32_t)(extra_thunks[thunk] - address - 5);
            originals[i][0] = replacements[i][0] = 0xe8;
            memory_copy(originals[i]+1, &before, 4);
            memory_copy(replacements[i]+1, &after, 4);
        }
        accepted[i][0] = originals[i];
        accepted[i][1] = replacements[i];
        presentation_controls_hook_patches[i] = (PatchDefinition){address, 0, size,
            GROUP_VIDEO_OPTIONS, "earned_notification_or_voice", originals[i],
            replacements[i], replacements[i], accepted[i], 2};
    }
    // Verified INT3 padding after the Angle getter, plus only the three
    // native float-boxing calls used by Angle/X/Y. No other UI transforms.
    const unsigned shake_thunk = 0xace781;
    static const unsigned shake_sites[] = {0xace71a, 0xace7ca, 0xace87a};
    for (unsigned i = 24; i < 31; ++i) {
        unsigned address = i == 24 ? shake_thunk : i < 28 ? shake_sites[i-25] : i == 28 ? 0xacedb1 : i == 29 ? 0xaced4a : 0xacedfa;
        unsigned size = (i == 24 || i == 28) ? 12 : 5;
        if (i == 24 || i == 28) {
            static const unsigned char code[] = {0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
            uintptr_t callback = i == 24 ? (uintptr_t)skill_counter_noise : (uintptr_t)skill_counter_scale;
            for (unsigned j=0;j<size;j++) originals[i][j]=0xcc;
            memory_copy(replacements[i],code,size);
            memory_copy(replacements[i]+2,&callback,8);
        } else {
            originals[i][0]=replacements[i][0]=0xe8;
            int32_t before=(int32_t)(0x3f5990-address-5);
            int32_t after=(int32_t)((i < 28 ? shake_thunk : 0xacedb1)-address-5);
            memory_copy(originals[i]+1,&before,4);
            memory_copy(replacements[i]+1,&after,4);
        }
        accepted[i][0]=originals[i];accepted[i][1]=replacements[i];
        presentation_controls_hook_patches[i]=(PatchDefinition){address,0,size,GROUP_VIDEO_OPTIONS,
            "skill_counter_shake",originals[i],replacements[i],replacements[i],accepted[i],2};
    }

    // INT3 padding near the native dynamic-post weight setter.
    for (unsigned i=31; i<34; ++i) {
        unsigned address=i==31?0x11b3404:i==32?0x6ba2a4:0x6b971d;
        unsigned size=i==31?12:5;
        if(i==31){
            static const unsigned char code[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
            uintptr_t callback=(uintptr_t)speed_zone_vignette;
            for(unsigned j=0;j<size;++j) originals[i][j]=0xcc;
            memory_copy(replacements[i],code,size);
            memory_copy(replacements[i]+2,&callback,8);
        }else{
            originals[i][0]=replacements[i][0]=0xe8;
            int32_t before=(int32_t)(0x11b35b0-address-5);
            int32_t after=(int32_t)(0x11b3404-address-5);
            memory_copy(originals[i]+1,&before,4);
            memory_copy(replacements[i]+1,&after,4);
        }
        accepted[i][0]=originals[i];accepted[i][1]=replacements[i];
        presentation_controls_hook_patches[i]=(PatchDefinition){address,0,size,GROUP_VIDEO_OPTIONS,
            "speed_zone_vignette",originals[i],replacements[i],replacements[i],accepted[i],2};
    }

    uintptr_t before=(uintptr_t)(image+0x18b7c50),after=(uintptr_t)vignette_upload;
    memory_copy(originals[34],&before,8);memory_copy(replacements[34],&after,8);
    accepted[34][0]=originals[34];accepted[34][1]=replacements[34];
    presentation_controls_hook_patches[34]=(PatchDefinition){0x2e7f410,0,8,GROUP_VIDEO_OPTIONS,
        "main_vignette_upload",originals[34],replacements[34],replacements[34],accepted[34],2};
}
