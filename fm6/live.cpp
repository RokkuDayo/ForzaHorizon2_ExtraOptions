#include "live.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "patch.hpp"
#include "pe.hpp"
#include "hook.hpp"
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <cmath>
#include <mutex>

namespace {
uint8_t *image;
wchar_t path[MAX_PATH];
bool active=false, probe=false;
std::atomic<int> blur{100}, fovs[5]{};
constexpr const char *keys[]={"FOVDriver","FOVHood","FOVBumper","FOVChase","FOVFarChase"};
constexpr const char *halfkeys[]={"FOVDriverHalf","FOVHoodHalf","FOVBumperHalf","FOVChaseHalf","FOVFarChaseHalf"};
volatile LONG *cockpit_switch=nullptr;
std::atomic<ULONGLONG> next_poll{0};
int last_request=0;
using Factory=void *(*)();
Factory factories[3]{};
constexpr uint32_t sites[]={0x5aeb58,0x5aeb94,0x525f3d};
constexpr uint32_t targets[]={0x4de1b0,0x4f26dc,0x526704};
constexpr const char *sigs[]={"e8 53 f6 f2 ff 48 89 05 ac a6 83 01","e8 43 3b f4 ff 48 89 05 e0 ac 83 01","e8 c2 07 00 00 48 89 05 47 1d 8c 01"};
constexpr const char *names[]={"CameraEffects","CameraEffectsOptions","MasterVolume"};
constexpr uint32_t objects[]={0x1dccf60,0x1dc96f0,0x1dc8190};
template<class T> bool safe_read(const void *p,T &v) {
    SIZE_T n=0; return ReadProcessMemory(GetCurrentProcess(),p,&v,sizeof v,&n) && n==sizeof v;
}
void descriptor(const char *name, const void *p) {
    uint64_t d[12]{}; if (!safe_read(p,d)) {core::log::write("FM6 UI: %s descriptor unreadable",name);return;}
    core::log::write("FM6 UI: %s descriptor=%p vt=%llx getter=%llx setter=%llx nameptr=%llx type=%llx owner=%llx flags=%llx",name,p,(unsigned long long)d[0],(unsigned long long)d[5],(unsigned long long)d[6],(unsigned long long)d[8],(unsigned long long)d[9],(unsigned long long)d[10],(unsigned long long)d[11]);
}
template<size_t I> void *factory_hook(){
    void *p=factories[I](); descriptor(names[I],p);return p;
}
constexpr Factory callbacks[]={factory_hook<0>,factory_hook<1>,factory_hook<2>};
void absjump(uint8_t *p,const void *target){p[0]=0xff;p[1]=0x25;std::memset(p+2,0,4);std::memcpy(p+6,&target,8);}
void install_probe(){
    for(size_t i=0;i<3;i++){
        auto *site=image+sites[i];
        if(!core::pe::readable(site,12)||core::patch::match(site,12,sigs[i],nullptr)<0){core::log::write("FM6 UI: %s signature mismatch",names[i]);continue;}
        auto *stub=static_cast<uint8_t*>(core::hook::alloc_near(site+5,14,PAGE_EXECUTE_READWRITE));
        if(!stub)continue;
        void *cb=reinterpret_cast<void*>(callbacks[i]);absjump(stub,cb);
        FlushInstructionCache(GetCurrentProcess(),stub,14);
        int32_t delta=static_cast<int32_t>(reinterpret_cast<intptr_t>(stub)-reinterpret_cast<intptr_t>(site+5));
        uint8_t call[5]={0xe8};std::memcpy(call+1,&delta,4);factories[i]=reinterpret_cast<Factory>(image+targets[i]);
        if(!core::patch::write_code(site,call,5)){VirtualFree(stub,0,MEM_RELEASE);continue;}
        core::log::write("FM6 UI: %s factory trace installed",names[i]);
    }
}
void snapshot(){
    for(size_t i=0;i<3;i++)descriptor(names[i],image+objects[i]);
    for(uint32_t rva:{0x1d6ac20u,0x1d68ec0u}){
        uintptr_t node=0;safe_read(image+rva,node);
        core::log::write("FM6 UI: registry head RVA=%x ptr=%llx",rva,(unsigned long long)node);
        uintptr_t seen[256]{};
        unsigned count=0;
        for(unsigned i=0;node&&i<256;i++){
            if(std::find(seen,seen+i,node)!=seen+i){core::log::write("FM6 UI: registry cycle at %u",i);break;}
            seen[i]=node; ++count;
            uintptr_t pair[2]{};if(!safe_read(reinterpret_cast<void*>(node),pair))break;
            char name[97]{};uintptr_t np=0;bool ok=false;
            if(pair[0]&&safe_read(reinterpret_cast<void*>(pair[0]+0x40),np)&&np){
                for(unsigned j=0;j<96;j++){char c=0;if(!safe_read(reinterpret_cast<void*>(np+j),c))break;if(!c){ok=j>0;break;}if(c<32||c>126)break;name[j]=c;}
            }
            core::log::write("FM6 UI: link[%u] node=%llx property=%llx name=%s next=%llx",i,(unsigned long long)node,(unsigned long long)pair[0],ok?name:"?",(unsigned long long)pair[1]);
            if(ok && (!std::strcmp(name,"CameraEffects") || !std::strcmp(name,"CameraEffectsOptions") || !std::strcmp(name,"MasterVolume") || !std::strcmp(name,"MasterVolumeText"))) descriptor(name,reinterpret_cast<void*>(pair[0]));
            if(pair[1]==node) break;
            node=pair[1];
        }
        core::log::write("FM6 UI: registry entries=%u remaining=%llx",count,(unsigned long long)node);
    }
}

// Experimental HUD extension. Keep the original native resource object and size.
// Only modify an exact HUD XML match in writable memory; no archive writes.
struct NativeProperty { void* vtable; unsigned char header[0x20]; void* getter; void* setter; void* unused; const char* name; void* type; void* owner; uint64_t flags; };
static_assert(sizeof(NativeProperty)==0x60);
struct PropertyLink { NativeProperty* property; PropertyLink* next; };
NativeProperty hud_properties[7]{};
PropertyLink hud_links[7]{};
bool hud_registered=false;
std::mutex hud_mutex;
bool native_hud=false;
constexpr char hud_class[]="x:Class=\"ForzaUI.Scenes.WPF.HudOptions\"";
constexpr char style_xml[]=R"EO(<Style x:Key="EoFovSliderStyle" TargetType="{x:Type Slider}"><Setter Property="local:Audio.PlayFocusSound" Value="True"/><Setter Property="Focusable" Value="True"/><Setter Property="Margin" Value="0,2,0,0"/><Setter Property="Height" Value="2"/><Setter Property="Maximum" Value="67"/><Setter Property="SmallChange" Value="0.5"/><Setter Property="LargeChange" Value="0"/><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="{x:Type Slider}"><Grid x:Name="Container"><Track x:Name="PART_Track" Margin="15,0,0,0" Width="355" Height="2" HorizontalAlignment="Left"><Track.DecreaseRepeatButton><RepeatButton x:Name="Decrease" Style="{StaticResource OptionsDecreaseSliderButtonStyle}"/></Track.DecreaseRepeatButton><Track.Thumb><Thumb x:Name="Thumb" Style="{StaticResource OptionsAdjustSliderThumbStyle}"/></Track.Thumb><Track.IncreaseRepeatButton><RepeatButton x:Name="Increase" Style="{StaticResource OptionsIncreaseSliderButtonStyle}"/></Track.IncreaseRepeatButton></Track></Grid><ControlTemplate.Triggers><Trigger Property="IsFocused" Value="True"><Setter TargetName="Decrease" Property="Background" Value="{StaticResource ForzaFocusBrush}"/><Setter TargetName="Thumb" Property="Background" Value="{StaticResource ForzaFocusAmbientBrush}"/></Trigger></ControlTemplate.Triggers></ControlTemplate></Setter.Value></Setter></Style>)EO";
constexpr const char* hud_names[]={"EoFovDriver","EoFovHood","EoFovBumper","EoFovChase","EoFovFarChase","EoBlur","EoCockpit"};
constexpr const char* labels[]={"FOV - DRIVER","FOV - HOOD","FOV - BUMPER","FOV - CHASE","FOV - FAR CHASE","MOTION BLUR (OFF / SHORT / LONG)","COCKPIT BLUR (OFF / ON)"};
constexpr float stock_fov[]={48,66,48,48.5f,58};
template<size_t I> void** hud_get(void** out,NativeProperty*,void*) {
    float value=0;
    if constexpr(I<5){value=fovs[I].load()*.5f;if(value==0)value=std::clamp(stock_fov[I],50.0f,67.0f);}
    else if constexpr(I==5){int b=blur.load();value=b==0?0:b<=100?1:2;}
    else {value=cockpit_switch?float(InterlockedCompareExchange(cockpit_switch,0,0)):0;}
    *out=nullptr;
    reinterpret_cast<void**(*)(void**,const float*)>(image+0x255414)(out,&value);
    return out;
}
template<size_t I> void hud_set(NativeProperty* prop,void* page,void* boxed){
    float value=0;if(!boxed||!safe_read(static_cast<uint8_t*>(boxed)+0x28,value)||!std::isfinite(value))return;
    int v=0; const char* key=nullptr;
    if constexpr(I<5){if(value<50||value>67)return;v=static_cast<int>(std::lround(value*2));key=halfkeys[I];}
    else if constexpr(I==5){if(value<0||value>2)return;constexpr int presets[]={0,100,300};v=presets[static_cast<int>(std::lround(value))];key="MotionBlurIntensity";}
    else {if(value<0||value>1)return;v=static_cast<int>(std::lround(value));key="CockpitMotionBlur";}
    wchar_t val[16],wkey[64];swprintf(val,16,L"%d",v);MultiByteToWideChar(CP_ACP,0,key,-1,wkey,64);
    if(!WritePrivateProfileStringW(L"FM6",wkey,val,path))return;
    bool changed=false;
    if constexpr(I<5)changed=fovs[I].exchange(v)!=v;
    else if constexpr(I==5)changed=blur.exchange(v)!=v;
    else if(cockpit_switch)changed=InterlockedExchange(cockpit_switch,v)!=v;
    if(changed){core::log::write("FM6 HUD: %s=%d",key,v);if(page)reinterpret_cast<void(*)(void*,void*)>(image+0x2e200c)(page,prop);}
}
using HudGetter=void**(*)(void**,NativeProperty*,void*);
using HudSetter=void(*)(NativeProperty*,void*,void*);
constexpr HudGetter hud_getters[]={hud_get<0>,hud_get<1>,hud_get<2>,hud_get<3>,hud_get<4>,hud_get<5>,hud_get<6>};
constexpr HudSetter hud_setters[]={hud_set<0>,hud_set<1>,hud_set<2>,hud_set<3>,hud_set<4>,hud_set<5>,hud_set<6>};
bool register_hud(){
    if(hud_registered)return true;
    NativeProperty camera{},volume{};
    if(!safe_read(image+0x1dccf60,camera)||!safe_read(image+0x1dc8190,volume))return false;
    if(camera.vtable!=image+0x1998728||volume.vtable!=camera.vtable||camera.owner!=image+0x1d6abe0||volume.type!=image+0x1d967b0)return false;
    PropertyLink* head=nullptr;if(!safe_read(image+0x1d6ac20,head)||!head)return false;
    using Ctor=void*(*)(NativeProperty*,void*,const char*,void*,void*,void*,void*);
    for(size_t i=0;i<7;i++){
        auto& p=hud_properties[i];auto g=reinterpret_cast<void*>(hud_getters[i]);auto t=reinterpret_cast<void*>(hud_setters[i]);
        reinterpret_cast<Ctor>(image+0x2e19cc)(&p,camera.owner,hud_names[i],volume.type,nullptr,g,t);
        if(p.vtable!=camera.vtable||p.owner!=camera.owner||p.type!=volume.type||p.getter!=g||p.setter!=t)return false;
        core::log::write("FM6 HUD: %s descriptor validated",hud_names[i]);
    }
    for(size_t i=0;i<7;i++){hud_links[i]={&hud_properties[i],head};head=&hud_links[i];}
    *reinterpret_cast<PropertyLink**>(image+0x1d6ac20)=head;
    hud_registered=true;return true;
}
std::string rows_xml(){
    std::string rows;
    for(size_t i=0;i<7;i++){
        std::string name=hud_names[i];
        rows+=R"EO(<ContentControl Style="{StaticResource AssistsOptionsControlStyle}"><Grid><Grid.ColumnDefinitions><ColumnDefinition Width="*"/><ColumnDefinition Width="*"/></Grid.ColumnDefinitions><ContentControl Content=")EO";
        rows+=labels[i];rows+=R"EO(" Style="{StaticResource OptionsCategoryNameControlStyle}" Tag="{Binding IsFocused, ElementName=)EO"+name+R"EO(}"/><Slider Grid.Column="1" x:Name=")EO"+name+R"EO(" Value="{Binding )EO"+name+R"EO(, Mode=TwoWay}" Minimum=")EO";
        rows+=i<5?"50":"0";rows+=R"EO(" Maximum=")EO";rows+=i<5?"67":i==5?"2":"1";rows+=R"EO(" SmallChange=")EO";rows+=i<5?"0.5":"1";rows+=R"EO(" Style="{StaticResource EoFovSliderStyle}"/>)EO";
        if(i<5 && GetPrivateProfileIntW(L"FM6",L"NativeHUDReadouts",1,path)) {
            rows+=R"EO(<TextBlock Grid.Column="1" Width="85" HorizontalAlignment="Right" TextAlignment="Right" IsHitTestVisible="False" Style="{StaticResource OptionNameTextBlockStyle}" Text="{Binding )EO"+name+R"EO(}" Tag="{Binding IsFocused, ElementName=)EO"+name+R"EO(}"/>)EO";
        }
        if(i>=5 && GetPrivateProfileIntW(L"FM6",L"NativeHUDPresetText",1,path)) {
            rows+=R"EO(<TextBlock Grid.Column="1" Width="85" HorizontalAlignment="Right" TextAlignment="Right" IsHitTestVisible="False" Tag="{Binding IsFocused, ElementName=)EO"+name+R"EO(}"><TextBlock.Style><Style TargetType="{x:Type TextBlock}" BasedOn="{StaticResource OptionNameTextBlockStyle}"><Setter Property="Text" Value="Off"/><Style.Triggers><DataTrigger Binding="{Binding )EO"+name+R"EO(}" Value="1"><Setter Property="Text" Value=")EO"+(i==5?"Short":"On")+R"EO("/></DataTrigger>)EO";
            if(i==5)rows+=R"EO(<DataTrigger Binding="{Binding EoBlur}" Value="2"><Setter Property="Text" Value="Long"/></DataTrigger>)EO";
            rows+="</Style.Triggers></Style></TextBlock.Style></TextBlock>";
        }
        rows+="</Grid></ContentControl>";
    }
    return rows;
}
// Remove XML comments only; keep quoted values and all other text intact.
std::string strip_comments(const std::string& src){
    std::string dst; dst.reserve(src.size());
    bool in_tag=false; char quote=0;
    for(size_t i=0;i<src.size();++i){
        char c=src[i];
        if(!quote && !in_tag && src.compare(i,4,"<!--")==0){
            auto end=src.find("-->",i+4); if(end==std::string::npos)return src;
            i=end+2;continue;
        }
        dst+=c;
        if(quote){if(c==quote)quote=0;}
        else if(in_tag && (c=='\''||c=='"'))quote=c;
        else if(c=='<')in_tag=true;
        else if(c=='>')in_tag=false;
    }
    return dst;
}
bool writable_range(void* ptr,size_t length){
    auto p=reinterpret_cast<uintptr_t>(ptr); if(length>UINTPTR_MAX-p)return false;auto end=p+length;
    while(p<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof m)||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        auto prot=m.Protect&0xff;if(prot!=PAGE_READWRITE&&prot!=PAGE_WRITECOPY&&prot!=PAGE_EXECUTE_READWRITE&&prot!=PAGE_EXECUTE_WRITECOPY)return false;
        auto next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;if(next<=p)return false;p=next;
    }return true;
}
void inspect_hud(void* obj){
    if(!obj)return;
    struct Data{char* bytes;int length;int capacity;} data{};
    if(!safe_read(static_cast<uint8_t*>(obj)+0x28,data)||!data.bytes||data.length<100||data.length>196608)return;
    char prefix[4096]{};SIZE_T n=0;size_t limit=std::min<size_t>(sizeof prefix,static_cast<size_t>(data.length));
    if(!ReadProcessMemory(GetCurrentProcess(),data.bytes,prefix,limit,&n)||n!=limit)return;
    if(std::string_view(prefix,limit).find(hud_class)==std::string_view::npos)return;
    std::lock_guard<std::mutex> lock(hud_mutex);
    core::log::write("FM6 HUD: native resource matched length=%d capacity=%d",data.length,data.capacity);
    if(!native_hud)return;
    std::string src(static_cast<size_t>(data.length),'\0');
    if(!ReadProcessMemory(GetCurrentProcess(),data.bytes,src.data(),src.size(),&n)||n!=src.size())return;
    if(src.find("EoFovDriver")!=std::string::npos)return;
    // Remove only inter-tag indentation; preserve text and attribute contents.
    std::string dst;dst.reserve(src.size());
    for(size_t i=0;i<src.size();++i){dst+=src[i];if(src[i]=='>'){size_t j=i+1;while(j<src.size()&&(src[j]==' '||src[j]=='\t'||src[j]=='\r'||src[j]=='\n'))++j;if(j<src.size()&&src[j]=='<')i=j-1;}}
    const char* marker="<Page.Resources>";auto p=dst.find(marker);if(p==std::string::npos)return;dst.insert(p+strlen(marker),style_xml);
    p=dst.find("<ContentControl x:Name=\"Control_Damage\"");if(p==std::string::npos)return;dst.insert(p,rows_xml());
    if(dst.size()>src.size()){
        auto compact=strip_comments(dst);dst.clear();
        bool in_tag=false;char quote=0;
        for(size_t i=0;i<compact.size();++i){
            char c=compact[i];
            if(!quote && (c==' '||c=='\t'||c=='\r'||c=='\n')){
                size_t j=i+1;while(j<compact.size()&&(compact[j]==' '||compact[j]=='\t'||compact[j]=='\r'||compact[j]=='\n'))++j;
                if(in_tag)dst+=' ';
                else if(!( !dst.empty() && dst.back()=='>' && j<compact.size() && compact[j]=='<'))dst.append(compact,i,j-i);
                i=j-1;continue;
            }
            dst+=c;
            if(quote){if(c==quote)quote=0;}
            else if(in_tag&&(c=='\''||c=='"'))quote=c;
            else if(c=='<')in_tag=true;
            else if(c=='>')in_tag=false;
        }
    }
    core::log::write("FM6 HUD: markup bytes=%zu limit=%zu",dst.size(),src.size());
    if(dst.size()>src.size()||!writable_range(data.bytes,src.size())){core::log::write("FM6 HUD: original retained (size or writable range)");return;}
    if(!register_hud()){core::log::write("FM6 HUD: original retained (property registration)");return;}
    dst.resize(src.size(),' ');std::memcpy(data.bytes,dst.data(),dst.size());
    core::log::write("FM6 HUD: 7 native sliders inserted in memory");
}
void** resource_get(void* reader,void** out){
    // Exact original 19-byte getter: copy pointer, increment ref count, return out.
    void* obj=*reinterpret_cast<void**>(static_cast<uint8_t*>(reader)+0x38);*out=obj;
    if(obj)++*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(obj)+0x10);
    inspect_hud(obj);
    return out;
}
void install_native_hud(){
    auto* site=image+0x1768ec;
    constexpr char sig[]="48 8b 41 38 48 89 02 48 85 c0 74 03 ff 40 10 48 8b c2 c3";
    if(!core::pe::readable(site,19)||core::patch::match(site,19,sig,nullptr)<0){core::log::write("FM6 HUD: resource getter signature mismatch");return;}
    uint8_t jump[14]{};absjump(jump,reinterpret_cast<void*>(resource_get));
    if(core::patch::write_code(site,jump,sizeof jump))core::log::write("FM6 HUD: resource getter installed (mutation=%d)",native_hud);
}

void install_cockpit(){
    auto *site=image+0x153829;
    if(!core::pe::readable(image+0x153822,13)||core::patch::match(image+0x153822,13,"80 3d 3f 63 c2 01 00 0f 85 b9 00 00 00|80 3d 3f 63 c2 01 00 66 0f 1f 44 00 00",nullptr)<0){core::log::write("FM6 live: cockpit gate mismatch; restart option retained");return;}
    auto *stub=static_cast<uint8_t*>(core::hook::alloc_near(site+6,64,PAGE_EXECUTE_READWRITE));if(!stub)return;
    // Original JNE enters here only in cockpit. Preserve registers and flags on both paths.
    uint8_t code[48]={0x9c,0x83,0x3d,40,0,0,0,0,0x75,15,0x9d};
    absjump(code+11,image+0x1538e8);code[25]=0x9d;absjump(code+26,image+0x15382f);
    std::memcpy(stub,code,sizeof code);auto *flag=reinterpret_cast<volatile LONG*>(stub+48);
    *flag=core::ini::get_int(path,"FM6","CockpitMotionBlur",0)!=0;
    FlushInstructionCache(GetCurrentProcess(),stub,48);
    int32_t delta=static_cast<int32_t>(reinterpret_cast<intptr_t>(stub)-reinterpret_cast<intptr_t>(site+6));
    uint8_t branch[6]={0x0f,0x85};std::memcpy(branch+2,&delta,4);
    if(!core::patch::write_code(site,branch,6)){VirtualFree(stub,0,MEM_RELEASE);return;}
    cockpit_switch=flag;core::log::write("FM6 live: cockpit switch installed");
}
void load_values(){
    int b=std::clamp(core::ini::get_int(path,"FM6","MotionBlurIntensity",100),0,400);
    if(blur.exchange(b)!=b)core::log::write("FM6 live: blur=%d%%",b);
    for(size_t i=0;i<5;i++){
        int h=core::ini::get_int(path,"FM6",halfkeys[i],-1);
        if(h==-1){int d=core::ini::get_int(path,"FM6",keys[i],0);h=d>0?std::clamp(d,50,67)*2:0;}
        if(h<=0)h=0;else h=std::clamp(h,100,134);
        if(fovs[i].exchange(h)!=h)core::log::write("FM6 live: %s=%.1f",keys[i],h/2.0);
    }
    if(cockpit_switch){LONG c=core::ini::get_int(path,"FM6","CockpitMotionBlur",0)!=0;if(InterlockedExchange(cockpit_switch,c)!=c)core::log::write("FM6 live: cockpit=%ld",c);}
    int request=core::ini::get_int(path,"FM6","UIProbeRequest",0);
    if(probe&&request!=last_request){last_request=request;snapshot();}
}
}
namespace fm6live {
void initialize(uint8_t *base,const wchar_t *ini){
    active=core::ini::get_int(ini,"FM6","LiveControls",0)!=0;probe=core::ini::get_int(ini,"FM6","UIProbe",0)!=0;
    if(!active&&!probe)return;
    if(wcslen(ini)>=MAX_PATH){active=probe=false;return;}wcscpy(path,ini);image=base;
    last_request=core::ini::get_int(path,"FM6","UIProbeRequest",0);
    if(active) install_cockpit();
    if(probe) install_probe();
    load_values();
    native_hud=active&&core::ini::get_int(path,"FM6","NativeHUD",0)!=0;
    if(native_hud||probe)install_native_hud();
    core::log::write("FM6 v0.5b-pre7: live=%d probe=%d",active,probe);
}
bool enabled(){return active||probe;}
int intensity(){return blur.load();}
int fov_half(const char *key){for(size_t i=0;i<5;i++)if(!std::strcmp(keys[i],key))return fovs[i].load();return 0;}
void refresh(){if(!enabled())return;auto now=GetTickCount64();auto prev=next_poll.load();if(now<prev||!next_poll.compare_exchange_strong(prev,now+750))return;load_values();}
}
