#include "fm5/fm5_ui.h"
#include "fm5/fm5_vehicle.h"
#include "shared/support.h"

static unsigned char *image;
static unsigned keyboard=VK_F1,pad;
static int held,blocked=1;
static volatile LONG hidden;
static HWND (WINAPI *foreground_window)(void);
static DWORD (WINAPI *window_process)(HWND,DWORD *);
static SHORT (WINAPI *key_state)(int);

typedef struct {const wchar_t *name;unsigned value;} Name;
static const Name buttons[]={
    {L"None",0},{L"DPadUp",1},{L"DPadDown",2},{L"DPadLeft",4},{L"DPadRight",8},
    {L"Menu",0x10},{L"Start",0x10},{L"View",0x20},{L"Back",0x20},
    {L"LeftStick",0x40},{L"LS",0x40},{L"RightStick",0x80},{L"RS",0x80},
    {L"LB",0x100},{L"RB",0x200},{L"A",0x1000},{L"B",0x2000},{L"X",0x4000},{L"Y",0x8000}};
static const Name keys[]={
    {L"None",0},{L"Escape",VK_ESCAPE},{L"Enter",VK_RETURN},{L"Space",VK_SPACE},
    {L"Tab",VK_TAB},{L"Backspace",VK_BACK},{L"Insert",VK_INSERT},{L"Delete",VK_DELETE},
    {L"Home",VK_HOME},{L"End",VK_END},{L"PageUp",VK_PRIOR},{L"PageDown",VK_NEXT},
    {L"Up",VK_UP},{L"Down",VK_DOWN},{L"Left",VK_LEFT},{L"Right",VK_RIGHT},
    {L"Shift",VK_SHIFT},{L"Ctrl",VK_CONTROL},{L"Alt",VK_MENU}};
static int equal(const wchar_t *a,const wchar_t *b){
    while(*a && *b){wchar_t x=*a++,y=*b++;if(x>=L'a'&&x<=L'z')x-=32;if(y>=L'a'&&y<=L'z')y-=32;if(x!=y)return 0;}
    return *a==*b;
}
static int number(const wchar_t *s,unsigned *out){
    unsigned n=0,base=10,digits=0;
    if(s[0]==L'0' && (s[1]==L'x'||s[1]==L'X')){base=16;s+=2;}
    for(;*s;++s){unsigned c=*s;if(c>=L'a'&&c<=L'f')c-=32;
        unsigned d=c>=L'0'&&c<=L'9'?c-L'0':c>=L'A'&&c<=L'F'?c-L'A'+10:99;
        if(d>=base || n>(65535-d)/base)return 0;n=n*base+d;++digits;}
    if(!digits)return 0;*out=n;return 1;
}
static int parse(const wchar_t *s,int controller,unsigned *out){
    const Name *names=controller?buttons:keys;unsigned count=controller?ARRAY_COUNT(buttons):ARRAY_COUNT(keys);
    for(unsigned i=0;i<count;++i)if(equal(s,names[i].name)){*out=names[i].value;return 1;}
    if(!controller){
        if(s[0] && !s[1]){unsigned c=s[0];if(c>=L'a'&&c<=L'z')c-=32;
            if(c>=L'A'&&c<=L'Z'){*out=c;return 1;}}
        unsigned n;if((s[0]==L'F'||s[0]==L'f') && number(s+1,&n) && n>=1 && n<=24){*out=VK_F1+n-1;return 1;}
    }
    return number(s,out) && (controller?*out<=65535:*out<=255);
}
unsigned fm5_read_binding(const wchar_t *ini, const wchar_t *key, int controller,
                          const wchar_t *fallback) {
    unsigned value = 0;
    wchar_t text[64];
    if (ini) {
        GetPrivateProfileStringW(L"Bindings", key, fallback, text, ARRAY_COUNT(text), ini);
        if (parse(text, controller, &value)) return value;
    }
    parse(fallback, controller, &value);
    return value;
}

int fm5_keyboard_down(unsigned key) {
    if (!key || !key_state || !foreground_window || !window_process) return 0;
    HWND window = foreground_window();
    DWORD process = 0;
    if (!window || !window_process(window, &process) || process != GetCurrentProcessId()) return 0;
    return (key_state((int)key) & 0x8000) != 0;
}
static void toggle_tick(int focused,int gameplay,int down){
    /* Never leave the pause menu or another menu invisible. Releasing the
       binding is required after a focus/context change before it can toggle. */
    if(!focused || !gameplay){InterlockedExchange(&hidden,0);blocked=1;}
    else if(!down)blocked=0;
    if(focused && gameplay && down && !held && !blocked)InterlockedXor(&hidden,1);
    held=down;
}
static void application_update(void *app){
    ((void (*)(void *))(image+0xa59ba0))(app);
    DWORD process=0;HWND window=foreground_window?foreground_window():NULL;
    if(window && window_process)window_process(window,&process);
    int focused=window && process==GetCurrentProcessId();unsigned short buttons_down=0;
    int gameplay=fm5_vehicle_input(&buttons_down,keyboard!=0);
    int down=focused && ((keyboard && key_state && (key_state((int)keyboard)&0x8000)) ||
        (pad && (buttons_down&pad)==pad));
    toggle_tick(focused,gameplay,down);
}
static void present(void *app,void *transform){
    if(!InterlockedCompareExchange(&hidden,0,0))((void (*)(void *,void *))(image+0xa59ce0))(app,transform);
}
static void draw_screen(void *app,void *screen,void *transform,uintptr_t caller){
    unsigned char *root=*(unsigned char **)screen;
    uintptr_t caller_rva=caller-(uintptr_t)image;
    /* UserInterface's shared render loop submits both the gameplay overlay
       (kind 1) and texture-backed screens (kinds 2 and 3) at the same site.
       The loop has already read the screen kind at +0x258 before calling us. */
    int hide=InterlockedCompareExchange(&hidden,0,0)!=0;
    unsigned kind=hide && root && caller_rva==0x8e5dfe ? *(unsigned *)(root+0x258) : 0;
    if(!hide || caller_rva==0x8e57b2 || kind==2 || kind==3 ||
       (root && *(void **)root==image+0x2272bc0)){
        ((void (*)(void *,void *,void *))(image+0xa59d50))(app,screen,transform);return;
    }
    /* The native draw consumes exactly one owned AVUI reference, also when
       drawing is suppressed. Do not suspend UI updates or input handling. */
    if(root && InterlockedDecrement((volatile LONG *)(root+0x10))==0)
        ((void (*)(void *))(image+0x137710))(root);
}
__declspec(noinline) static void draw(void *app,void *screen,void *transform){
    draw_screen(app,screen,transform,(uintptr_t)__builtin_return_address(0));
}
unsigned fm5_ui_prepare(unsigned char *game,Fm5Site *sites,const wchar_t *ini){
    image=game;
    HMODULE user=GetModuleHandleW(L"user32.dll");
    foreground_window=(void *)GetProcAddress(user,"GetForegroundWindow");
    window_process=(void *)GetProcAddress(user,"GetWindowThreadProcessId");
    key_state=(void *)GetProcAddress(user,"GetAsyncKeyState");
    keyboard=fm5_read_binding(ini,L"ToggleUIKeyboard",0,L"F1");
    pad=fm5_read_binding(ini,L"ToggleUIXbox",1,L"None");
    held=0;blocked=1;InterlockedExchange(&hidden,0);
    static const unsigned tables[]={0x23193d0,0x22d8f00};
    static const unsigned offsets[]={0x40,0x48,0x50},originals[]={0xa59ba0,0xa59ce0,0xa59d50};
    void *callbacks[]={(void *)application_update,(void *)present,(void *)draw};
    for(unsigned t=0;t<2;++t)for(unsigned i=0;i<3;++i){
        Fm5Site *s=&sites[t*3+i];*s=(Fm5Site){.rva=tables[t]+offsets[i],.size=8};
        void *original=image+originals[i];memory_copy(s->original,&original,8);memory_copy(s->replacement,&callbacks[i],8);
    }
    return 6;
}
#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) unsigned Fm5TestUiPrepare(unsigned char *game,Fm5Site *sites,const wchar_t *ini){return fm5_ui_prepare(game,sites,ini);}
__declspec(dllexport) int Fm5TestUiParse(const wchar_t *s,int controller,unsigned *out){return parse(s,controller,out);}
__declspec(dllexport) int Fm5TestUiTick(int focus,int gameplay,int down){toggle_tick(focus,gameplay,down);return InterlockedCompareExchange(&hidden,0,0);}
__declspec(dllexport) void Fm5TestUiDraw(void *app,void *screen,void *transform){draw_screen(app,screen,transform,0);}
__declspec(dllexport) void Fm5TestUiDrawFrom(void *app,void *screen,void *transform,unsigned rva){
    draw_screen(app,screen,transform,(uintptr_t)image+rva);
}
__declspec(dllexport) void Fm5TestUiPresent(void *app,void *transform){present(app,transform);}
#endif
