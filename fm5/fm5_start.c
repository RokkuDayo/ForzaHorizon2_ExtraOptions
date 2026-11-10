#include "fm5/fm5_start.h"
#include "shared/support.h"
#include "fm5/fm5_start_layout.h"

static int enabled;
void fm5_start_configure(int value){enabled=value!=0;}
int fm5_start_resource(const char *source,size_t length){
    return enabled && source && text_find(source,length,
        "x:Class=\"ForzaUI.Scenes.WPF.F000_Splash\"")!=NULL;
}
size_t fm5_start_xaml(const char *source,size_t length,char *out,size_t capacity){
    if(!fm5_start_resource(source,length) || !out || capacity<sizeof(e3_start_layout))return 0;
    /* Reuse F002_DemoSplash's authored page and storyboards, with the retail
       F000 class and its failed-sign-in cleanup. The native Splash controller,
       profile loading and subsequent retail transitions stay in charge. */
    memory_copy(out,e3_start_layout,sizeof(e3_start_layout));
    return sizeof(e3_start_layout)-1;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) size_t Fm5TestStartXaml(const char *src,size_t length,char *out,size_t capacity,int value){
    fm5_start_configure(value);return fm5_start_xaml(src,length,out,capacity);
}
#endif
