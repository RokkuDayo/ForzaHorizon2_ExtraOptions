// fh2-monitor.dll: FPS counter and fh2-perf.csv.
//
// Loaded by the mod (features/monitor_bootstrap.c) right after the game creates
// its D3D12 device, only when FH2_ExtraOptions.ini [Monitor] ShowFps or PerfLog asks for
// it. It hooks exactly four things:
//   - IDXGIFactory2::CreateSwapChainForHwnd (to learn the swap chain and queue),
//   - IDXGISwapChain::Present / IDXGISwapChain1::Present1 (frame times, overlay),
//   - IDXGISwapChain3::SetColorSpace1 (overlay brightness on HDR),
//   - ID3D12CommandQueue::ExecuteCommandLists (opens the GPU span timestamp).
// The counter is one alpha-blended quad drawn into the back buffer just before
// Present on the presenting queue. The GPU frame span comes from two timestamp
// queries: one before the first command list after a Present, one at the next
// Present. GPU load and video memory come from PDH on a worker thread. Any GPU
// failure disables the overlay and GPU time and keeps the CPU measurements.
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <stdint.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

#define MON_EXPORT extern "C" __declspec(dllexport)

namespace {
SRWLOCK mutex=SRWLOCK_INIT;
struct Hold { Hold(){AcquireSRWLockExclusive(&mutex);} ~Hold(){ReleaseSRWLockExclusive(&mutex);} };
// Set while we submit our own command lists, so our ExecuteCommandLists hook skips them.
thread_local bool internal;
struct Internal { bool saved=internal; Internal(){internal=true;} ~Internal(){internal=saved;} };

template<class T> bool hook(void** table,UINT index,void* callback,T& original){
    if(table[index]==callback)return original!=nullptr;
    DWORD old;if(!VirtualProtect(table+index,sizeof(void*),PAGE_READWRITE,&old))return false;
    original=reinterpret_cast<T>(table[index]);InterlockedExchangePointer(table+index,callback);
    DWORD ignored;VirtualProtect(table+index,sizeof(void*),old,&ignored);return true;
}

std::atomic<unsigned> overlay{0};
bool attached,csvEnabled;
HANDLE csv=INVALID_HANDLE_VALUE;
LARGE_INTEGER frequency;

// Frame history: present time and frame time, newest at next-1.
constexpr unsigned HISTORY=2048;
LONGLONG presentTimes[HISTORY];float frameMs[HISTORY];unsigned historyNext,historyCount;
LONGLONG lastPresent,lastText,lastCsv,sessionStart;
char session[20];
// GPU spans (ms) with their completion time, same scheme.
constexpr unsigned GPU_HISTORY=256;
LONGLONG gpuTimes[GPU_HISTORY];float gpuMs[GPU_HISTORY];unsigned gpuNext,gpuCount;

// Swap chains seen at creation and their D3D12 queues.
struct Chain { void* primary; void* third; ID3D12CommandQueue* queue; UINT colorSpace; };
Chain chains[8];unsigned chainCount;
using PresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);
using MakeChainFn=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,HWND,const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
using ColorSpaceFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*,DXGI_COLOR_SPACE_TYPE);
using ExecuteFn=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList*const*);
PresentFn originalPresent;Present1Fn originalPresent1;MakeChainFn originalMakeChain;
ColorSpaceFn originalColorSpace;ExecuteFn originalExecute;
// Chains found without our factory hook, asked once each for their queue.
void* askedChains[8];unsigned askedCount;
bool asked_device(void* chain){
    for(unsigned i=0;i<askedCount;++i)if(askedChains[i]==chain)return true;
    if(askedCount==8)return true;
    askedChains[askedCount++]=chain;return false;
}
// The chain (index in chains) whose frames are measured and drawn on.
constexpr unsigned NONE=~0u;
unsigned bound=NONE;LONGLONG boundPresent;

// GPU side, created on the first Present that needs it.
constexpr unsigned RING=4;
struct Slot { ID3D12CommandAllocator* allocator{}; ID3D12GraphicsCommandList* list{}; ID3D12GraphicsCommandList* start{};
    UINT64 fence{}; bool started{},resolved{}; };
Slot slots[RING];unsigned slot;bool frameOpen;
ID3D12Device* device;ID3D12CommandQueue* gpuQueue;ID3D12Fence* fence;UINT64 fenceValue;HANDLE fenceEvent;
ID3D12QueryHeap* queries;ID3D12Resource* readback;const UINT64* stamps;UINT64 stampFrequency;
ID3D12DescriptorHeap* rtvHeap;UINT rtvStride;ID3D12RootSignature* root;ID3DBlob *vs,*ps;
struct Pso { DXGI_FORMAT format; ID3D12PipelineState* pipeline; };
Pso psos[4];unsigned psoCount;
bool gpuReady,gpuFailed;

// Overlay text, refreshed every half second.
constexpr unsigned COLUMNS=24,LINES=4;   // all 96 glyphs the constants hold
char text[LINES][COLUMNS+1];unsigned lineCount;

// GPU load and video memory, sampled once a second on a worker thread while the
// counter is on. Load: PDH "GPU Engine(*engtype_3D)\
// Utilization Percentage" summed over this adapter's 3D engines in every process
// (Task Manager's 3D graph). Memory: PDH "GPU Adapter Memory(*)\Dedicated Usage"
// for this adapter, else DXGI QueryVideoMemoryInfo (this process). Total:
// DXGI_ADAPTER_DESC1.DedicatedVideoMemory. All optional.
std::atomic<int> gpuLoadTenths{-1};
std::atomic<UINT64> vramUsed{0},vramTotal{0};
bool monitorStarted;LUID adapterLuid;

const char glyphs[]=" 0123456789.%:/-ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char overlayShader[]=R"HLSL(
cbuffer Overlay : register(b0) { float4 Rect; float4 Target; float4 Color; uint4 Layout; uint4 Text[6]; };
struct V { float4 position : SV_Position; };
V vs(uint id : SV_VertexID) {
    float2 pixel = Rect.xy + float2(id & 1, id >> 1) * Rect.zw;
    V o; o.position = float4(pixel.x / Target.x * 2 - 1, 1 - pixel.y / Target.y * 2, 0, 1); return o;
}
// 5x7 glyphs: rows 0-5 in x (5 bits each, leftmost column high), row 6 in y.
static const uint2 Font[42] = {
    {0x00000000,0x00},{0x239ace2e,0x0e},{0x08421184,0x0e},{0x1041062e,0x1f},{0x2211105f,0x0e},{0x05f928c2,0x02},
    {0x2210fa1f,0x0e},{0x231f4106,0x0e},{0x1082083f,0x08},{0x2317462e,0x0e},{0x0417c62e,0x0c},{0x18000000,0x0c},
    {0x26820b38,0x03},{0x18c03180,0x00},{0x20820820,0x00},{0x000f8000,0x00},{0x23f8c62e,0x11},{0x231f463e,0x1e},
    {0x2308422e,0x0e},{0x2518c65c,0x1c},{0x210f421f,0x1f},{0x210f421f,0x10},{0x231bc22e,0x0f},{0x231fc631,0x11},
    {0x0842108e,0x0e},{0x24210847,0x0c},{0x254c5251,0x11},{0x21084210,0x1f},{0x231ad771,0x11},{0x233ae631,0x11},
    {0x2318c62e,0x0e},{0x210f463e,0x10},{0x2558c62e,0x0d},{0x254f463e,0x11},{0x0217420f,0x1e},{0x0842109f,0x04},
    {0x2318c631,0x0e},{0x1518c631,0x04},{0x2b5ac631,0x0a},{0x22a22a31,0x11},{0x08454631,0x04},{0x2082083f,0x1f}};
float4 ps(V input) : SV_Target {
    float4 background = float4(0, 0, 0, Color.w);
    float2 p = input.position.xy - Rect.xy - Target.w;
    if (p.x < 0 || p.y < 0) return background;
    uint2 cell = uint2(p / Target.z);
    uint column = cell.x / 6, row = cell.y / 9, gx = cell.x % 6, gy = cell.y % 9;
    if (column >= Layout.x || row >= Layout.y || gx >= 5 || gy >= 7) return background;
    uint index = row * Layout.x + column;
    // Static indices only: dynamically indexed root constants misread on AMD.
    uint q = index / 16;
    uint4 packed = q == 0 ? Text[0] : q == 1 ? Text[1] : q == 2 ? Text[2] : q == 3 ? Text[3] : q == 4 ? Text[4] : Text[5];
    uint part = (index / 4) % 4;
    uint word = part == 0 ? packed.x : part == 1 ? packed.y : part == 2 ? packed.z : packed.w;
    uint glyph = min((word >> ((index % 4) * 8)) & 255, 41);
    uint2 bits = Font[glyph];
    uint on = gy < 6 ? (bits.x >> (gy * 5 + 4 - gx)) & 1 : (bits.y >> (4 - gx)) & 1;
    return on ? float4(Color.rgb, 1) : background;
}
)HLSL";

double ms_between(LONGLONG a,LONGLONG b){return double(b-a)*1000.0/double(frequency.QuadPart);}
void gpu_fail(const char* why){gpuFailed=true;OutputDebugStringA("fh2-monitor: GPU part disabled: ");OutputDebugStringA(why);OutputDebugStringA("\n");}
bool gpu_ok(HRESULT hr,const char* what){if(SUCCEEDED(hr))return true;gpu_fail(what);return false;}

// Statistics over frames presented after `since`.
struct Stats { unsigned frames; double avgMs,p99Ms,maxMs,low1Fps; };
Stats frame_stats(LONGLONG since){
    static float sorted[HISTORY];Stats s{};double sum=0;
    for(unsigned i=0;i<historyCount;++i){unsigned k=(historyNext+HISTORY-1-i)%HISTORY;if(presentTimes[k]<=since)break;
        sorted[s.frames++]=frameMs[k];sum+=frameMs[k];}
    if(!s.frames)return s;
    std::sort(sorted,sorted+s.frames);s.avgMs=sum/s.frames;s.maxMs=sorted[s.frames-1];
    unsigned p99=unsigned(std::ceil(0.99*s.frames))-1;s.p99Ms=sorted[std::min(p99,s.frames-1)];
    // 1% low: average frame rate of the slowest 1% of frames (at least one).
    unsigned worst=std::max(1u,s.frames/100);double worstSum=0;
    for(unsigned i=0;i<worst;++i)worstSum+=sorted[s.frames-1-i];
    s.low1Fps=worstSum>0?1000.0*worst/worstSum:0;return s;
}
bool gpu_average(LONGLONG since,double& average,unsigned& count){
    double sum=0;count=0;
    for(unsigned i=0;i<gpuCount;++i){unsigned k=(gpuNext+GPU_HISTORY-1-i)%GPU_HISTORY;if(gpuTimes[k]<=since)break;sum+=gpuMs[k];++count;}
    average=count?sum/count:0;return count!=0;
}

// "GPU 87%" / "GPU --%" and "VRAM 5.2/16 GB" (empty when unknown).
void load_text(char* out,size_t size){
    const int load=gpuLoadTenths.load();
    if(load>=0)snprintf(out,size,"GPU %d%%",(load+5)/10);else snprintf(out,size,"GPU --%%");
}
void vram_text(char* out,size_t size){
    const double gb=1024.0*1024.0*1024.0,used=double(vramUsed.load())/gb,total=double(vramTotal.load())/gb;
    if(used<=0){out[0]=0;return;}
    if(total>0)snprintf(out,size,"VRAM %.1f/%.0f GB",used,total);else snprintf(out,size,"VRAM %.1f GB",used);
}

void refresh_text(LONGLONG now){
    const LONGLONG half=frequency.QuadPart/2;
    Stats recent=frame_stats(now-half);
    const double fps=recent.frames&&recent.avgMs>0?1000.0/recent.avgMs:0;
    memset(text,0,sizeof(text));
    char load[16],vram[24];load_text(load,sizeof(load));vram_text(vram,sizeof(vram));
    if(overlay.load()==1){snprintf(text[0],sizeof(text[0]),"%.0f FPS %s",fps,load);
        lineCount=1;if(vram[0]){snprintf(text[1],sizeof(text[1]),"%s",vram);lineCount=2;}return;}
    Stats longer=frame_stats(now-frequency.QuadPart*10);
    double gpu=0;unsigned samples=0;gpu_average(now-half,gpu,samples);
    snprintf(text[0],sizeof(text[0]),"%.1f FPS %.1f MS",fps,recent.avgMs);
    if(samples)snprintf(text[1],sizeof(text[1]),"1%% LOW %.0f GPU %.1f MS",longer.low1Fps,gpu);
    else snprintf(text[1],sizeof(text[1]),"1%% LOW %.0f GPU --",longer.low1Fps);
    if(vram[0])snprintf(text[2],sizeof(text[2]),"%s %s",load,vram);else snprintf(text[2],sizeof(text[2]),"%s",load);
    lineCount=3;
}

// PDH, loaded at run time (no pdh import library).
using PdhOpenQueryFn=PDH_STATUS(WINAPI*)(LPCWSTR,DWORD_PTR,PDH_HQUERY*);
using PdhAddCounterFn=PDH_STATUS(WINAPI*)(PDH_HQUERY,LPCWSTR,DWORD_PTR,PDH_HCOUNTER*);
using PdhCollectFn=PDH_STATUS(WINAPI*)(PDH_HQUERY);
using PdhArrayFn=PDH_STATUS(WINAPI*)(PDH_HCOUNTER,DWORD,LPDWORD,LPDWORD,PPDH_FMT_COUNTERVALUE_ITEM_W);
struct Pdh { PdhCollectFn collect; PdhArrayFn array; };

bool instance_on_adapter(const wchar_t* name,const wchar_t* luid){
    if(!name)return false;
    const size_t n=wcslen(luid);
    for(;*name;++name){size_t i=0;
        while(i<n&&name[i]&&towlower(name[i])==luid[i])++i;
        if(i==n)return true;}
    return false;
}
// Sums one wildcard counter over the instances of this adapter.
bool sum_counter(const Pdh& pdh,PDH_HCOUNTER counter,DWORD format,const wchar_t* luid,double& sum){
    DWORD bytes=0,count=0;
    if(pdh.array(counter,format,&bytes,&count,nullptr)!=(PDH_STATUS)PDH_MORE_DATA||!bytes)return false;
    std::vector<unsigned char> buffer(bytes);auto items=(PPDH_FMT_COUNTERVALUE_ITEM_W)buffer.data();
    if(pdh.array(counter,format,&bytes,&count,items)!=ERROR_SUCCESS)return false;
    bool any=false;sum=0;
    for(DWORD i=0;i<count;++i){const auto& v=items[i].FmtValue;
        if((v.CStatus!=PDH_CSTATUS_VALID_DATA&&v.CStatus!=PDH_CSTATUS_NEW_DATA)||!instance_on_adapter(items[i].szName,luid))continue;
        sum+=format==PDH_FMT_LARGE?double(v.largeValue):v.doubleValue;any=true;}
    return any;
}

DWORD WINAPI monitor_thread(void*){
    IDXGIAdapter3* adapter{};
    {IDXGIFactory4* factory{};
        if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))){factory->EnumAdapterByLuid(adapterLuid,IID_PPV_ARGS(&adapter));factory->Release();}}
    if(adapter){DXGI_ADAPTER_DESC1 d{};if(SUCCEEDED(adapter->GetDesc1(&d)))vramTotal=d.DedicatedVideoMemory;}
    wchar_t luid[48];swprintf(luid,48,L"luid_0x%08lx_0x%08lx",(unsigned long)adapterLuid.HighPart,(unsigned long)adapterLuid.LowPart);
    Pdh pdh{};PDH_HQUERY query{};PDH_HCOUNTER engines{},memory{};
    if(HMODULE module=LoadLibraryW(L"pdh.dll")){
        auto open=(PdhOpenQueryFn)(void*)GetProcAddress(module,"PdhOpenQueryW");
        auto add=(PdhAddCounterFn)(void*)GetProcAddress(module,"PdhAddEnglishCounterW");
        pdh.collect=(PdhCollectFn)(void*)GetProcAddress(module,"PdhCollectQueryData");
        pdh.array=(PdhArrayFn)(void*)GetProcAddress(module,"PdhGetFormattedCounterArrayW");
        if(open&&add&&pdh.collect&&pdh.array&&open(nullptr,0,&query)==ERROR_SUCCESS){
            if(add(query,L"\\GPU Engine(*engtype_3D)\\Utilization Percentage",0,&engines)!=ERROR_SUCCESS)engines=nullptr;
            if(add(query,L"\\GPU Adapter Memory(*)\\Dedicated Usage",0,&memory)!=ERROR_SUCCESS)memory=nullptr;
            if(!engines&&!memory)query=nullptr;
        }else query=nullptr;
    }
    for(;;){
        Sleep(1000);
        if(!overlay.load())continue;
        bool loadOk=false,memoryOk=false;double value=0;
        if(query&&pdh.collect(query)==ERROR_SUCCESS){
            if(engines&&sum_counter(pdh,engines,PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,luid,value)){
                gpuLoadTenths=int(std::min(100.0,std::max(0.0,value))*10.0+0.5);loadOk=true;}
            if(memory&&sum_counter(pdh,memory,PDH_FMT_LARGE,luid,value)&&value>0){vramUsed=UINT64(value);memoryOk=true;}
        }
        if(!memoryOk&&adapter){DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if(SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))){vramUsed=info.CurrentUsage;memoryOk=true;}}
        if(!loadOk)gpuLoadTenths=-1;
    }
}

// Starts the worker once, for the adapter of the queue that presents.
void start_monitor(ID3D12CommandQueue* queue){
    if(monitorStarted||!queue)return;
    monitorStarted=true;
    ID3D12Device* owner{};
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&owner)))||!owner)return;
    adapterLuid=owner->GetAdapterLuid();owner->Release();
    if(HANDLE thread=CreateThread(nullptr,0,monitor_thread,nullptr,0,nullptr))CloseHandle(thread);
}

void write_csv(LONGLONG now){
    if(csv==INVALID_HANDLE_VALUE)return;
    Stats s=frame_stats(lastCsv);
    double gpu=0;unsigned samples=0;gpu_average(lastCsv,gpu,samples);
    SYSTEMTIME t;GetLocalTime(&t);char gpuText[24]="";if(samples)snprintf(gpuText,sizeof(gpuText),"%.3f",gpu);
    const double seconds=ms_between(lastCsv,now)/1000.0;
    char line[512];int n=snprintf(line,sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u,%s,%.3f,%.3f,%u,%.2f,%.3f,%.3f,%.3f,%.2f,%s,%u,%u\r\n",
        t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,session,
        ms_between(sessionStart,now)/1000.0,seconds,s.frames,s.frames&&seconds>0?s.frames/seconds:0.0,
        s.avgMs,s.p99Ms,s.maxMs,s.low1Fps,gpuText,samples,overlay.load());
    if(n>0&&n<(int)sizeof(line)){DWORD written;WriteFile(csv,line,(DWORD)n,&written,nullptr);}
}

void record_frame(LONGLONG now){
    if(lastPresent){
        const float ms=(float)ms_between(lastPresent,now);
        presentTimes[historyNext]=now;frameMs[historyNext]=ms;
        historyNext=(historyNext+1)%HISTORY;historyCount=std::min(historyCount+1,HISTORY);
    }else{lastText=lastCsv=now;}
    lastPresent=now;
}

bool gpu_initialize(ID3D12CommandQueue* queue){
    if(!gpu_ok(queue->GetDevice(IID_PPV_ARGS(&device)),"queue device"))return false;
    gpuQueue=queue;queue->AddRef();
    if(!gpu_ok(queue->GetTimestampFrequency(&stampFrequency),"timestamp frequency")||!stampFrequency)return false;
    if(!gpu_ok(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence"))return false;
    fenceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!fenceEvent){gpu_fail("event");return false;}
    D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=RING*2;
    if(!gpu_ok(device->CreateQueryHeap(&q,IID_PPV_ARGS(&queries)),"query heap"))return false;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=RING*2*sizeof(UINT64);
    buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(!gpu_ok(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_PPV_ARGS(&readback)),"readback"))return false;
    void* mapped{};if(!gpu_ok(readback->Map(0,nullptr,&mapped),"map readback"))return false;stamps=(const UINT64*)mapped;
    D3D12_DESCRIPTOR_HEAP_DESC rtv{};rtv.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rtv.NumDescriptors=RING;
    if(!gpu_ok(device->CreateDescriptorHeap(&rtv,IID_PPV_ARGS(&rtvHeap)),"rtv heap"))return false;
    rtvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for(unsigned i=0;i<RING;++i){auto& s=slots[i];
        if(!gpu_ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator)),"allocator")||
           !gpu_ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator,nullptr,IID_PPV_ARGS(&s.list)),"list")||
           !gpu_ok(s.list->Close(),"close list"))return false;
        // The start marker never changes: record it once per slot.
        ID3D12CommandAllocator* startAllocator{};
        if(!gpu_ok(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&startAllocator)),"start allocator")||
           !gpu_ok(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,startAllocator,nullptr,IID_PPV_ARGS(&s.start)),"start list"))return false;
        s.start->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,i*2);
        if(!gpu_ok(s.start->Close(),"close start list"))return false;
    }
    ID3DBlob* errors{};
    for(int stage=0;stage<2;++stage){
        HRESULT hr=D3DCompile(overlayShader,strlen(overlayShader),"fh2-monitor",nullptr,nullptr,stage?"ps":"vs",stage?"ps_5_0":"vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3|D3DCOMPILE_ENABLE_STRICTNESS,0,stage?&ps:&vs,&errors);
        if(errors){errors->Release();errors=nullptr;}
        if(!gpu_ok(hr,"overlay shader"))return false;
    }
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.ShaderRegister=0;parameter.Constants.Num32BitValues=40;parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=1;signature.pParameters=&parameter;
    ID3DBlob *bytes{},*error{};HRESULT hr=D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error);
    if(error)error->Release();
    if(!gpu_ok(hr,"serialize overlay root"))return false;
    hr=device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root));bytes->Release();
    return gpu_ok(hr,"overlay root signature");
}

ID3D12PipelineState* pipeline_for(DXGI_FORMAT format){
    for(unsigned i=0;i<psoCount;++i)if(psos[i].format==format)return psos[i].pipeline;
    if(psoCount==4)return nullptr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC g{};g.pRootSignature=root;
    g.VS={vs->GetBufferPointer(),vs->GetBufferSize()};g.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    auto& blend=g.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;
    blend.SrcBlend=D3D12_BLEND_SRC_ALPHA;blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ZERO;blend.DestBlendAlpha=D3D12_BLEND_ONE;blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.LogicOp=D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN|D3D12_COLOR_WRITE_ENABLE_BLUE;
    g.SampleMask=UINT_MAX;g.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;g.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    g.RasterizerState.DepthClipEnable=TRUE;g.DepthStencilState.DepthEnable=FALSE;g.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    g.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;g.NumRenderTargets=1;g.RTVFormats[0]=format;g.SampleDesc.Count=1;
    ID3D12PipelineState* pipeline{};
    if(!gpu_ok(device->CreateGraphicsPipelineState(&g,IID_PPV_ARGS(&pipeline)),"overlay pipeline"))return nullptr;
    psos[psoCount++]={format,pipeline};
    return pipeline;
}

void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}

void record_overlay(ID3D12GraphicsCommandList* list,IDXGISwapChain3* chain,UINT colorSpace,unsigned s){
    if(!lineCount)return;
    ID3D12Resource* buffer{};
    if(FAILED(chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer))))return;
    const auto desc=buffer->GetDesc();
    ID3D12PipelineState* pipeline=pipeline_for(desc.Format);
    if(!pipeline||desc.Width<64||desc.Height<64){buffer->Release();return;}
    D3D12_CPU_DESCRIPTOR_HANDLE rtv=rtvHeap->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=SIZE_T(s)*rtvStride;
    device->CreateRenderTargetView(buffer,nullptr,rtv);
    struct Constants { float rect[4],target[4],color[4]; UINT layout[4]; UINT text[24]; } c{};
    unsigned columns=0;for(unsigned l=0;l<lineCount;++l)columns=std::max<unsigned>(columns,(unsigned)strlen(text[l]));
    if(!columns){buffer->Release();return;}
    for(unsigned l=0;l<lineCount;++l)for(unsigned i=0;i<columns;++i){
        const char ch=text[l][i];const char* found=ch?strchr(glyphs,ch>='a'&&ch<='z'?ch-32:ch):nullptr;
        const UINT glyph=found&&*found?UINT(found-glyphs):0;const unsigned index=l*columns+i;
        c.text[index/4]|=glyph<<((index%4)*8);}
    const float scale=std::max(1.f,std::floor(float(desc.Height)/540.f)),pad=2*scale,margin=8*scale;
    const float width=columns*6*scale-scale+2*pad,height=lineCount*9*scale-2*scale+2*pad;
    c.rect[0]=float(desc.Width)-margin-width;c.rect[1]=margin;c.rect[2]=width;c.rect[3]=height;
    c.target[0]=float(desc.Width);c.target[1]=float(desc.Height);c.target[2]=scale;c.target[3]=pad;
    // Yellow at about 200 nits on HDR: PQ (BT.2100) and scRGB need different values.
    const float brightness=colorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020?0.58f:
        colorSpace==DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709?2.5f:1.f;
    c.color[0]=1.f*brightness;c.color[1]=0.9f*brightness;c.color[2]=0.25f*brightness;c.color[3]=0.6f;
    c.layout[0]=columns;c.layout[1]=lineCount;
    transition(list,buffer,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};
    D3D12_RECT scissor{0,0,LONG(desc.Width),LONG(desc.Height)};
    list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);
    list->SetGraphicsRootSignature(root);list->SetGraphicsRoot32BitConstants(0,40,&c,0);list->SetPipelineState(pipeline);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);list->DrawInstanced(4,1,0,0);
    transition(list,buffer,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
    buffer->Release();
}

bool slot_free(unsigned s,DWORD wait){
    const UINT64 done=fence->GetCompletedValue();
    if(done==UINT64_MAX){gpu_fail("device removed");return false;}
    if(done>=slots[s].fence)return true;
    if(!wait||FAILED(fence->SetEventOnCompletion(slots[s].fence,fenceEvent)))return false;
    return WaitForSingleObject(fenceEvent,wait)==WAIT_OBJECT_0;
}

template<class T> void release(T*& object){if(object){object->Release();object=nullptr;}}

// Drops the GPU side once its last submission is done, for another queue.
void gpu_release(){
    if(fence&&fenceEvent&&fence->GetCompletedValue()<fenceValue&&
       SUCCEEDED(fence->SetEventOnCompletion(fenceValue,fenceEvent)))WaitForSingleObject(fenceEvent,1000);
    for(auto& s:slots){release(s.list);release(s.start);release(s.allocator);s=Slot{};}
    for(unsigned i=0;i<psoCount;++i)release(psos[i].pipeline);
    psoCount=0;
    if(readback)readback->Unmap(0,nullptr);
    stamps=nullptr;
    release(readback);release(queries);release(rtvHeap);release(root);release(vs);release(ps);
    release(fence);release(device);release(gpuQueue);
    if(fenceEvent){CloseHandle(fenceEvent);fenceEvent=nullptr;}
    fenceValue=0;slot=0;frameOpen=false;gpuReady=false;gpuFailed=false;
}

void gpu_frame(Chain& chain,IDXGISwapChain3* third,LONGLONG now){
    if(!chain.queue)return;
    if(gpuReady&&chain.queue!=gpuQueue)gpu_release();   // the bound chain moved to another queue
    if(gpuFailed)return;
    if(!gpuReady){if(!gpu_initialize(chain.queue)){gpuFailed=true;return;}gpuReady=true;}
    if(chain.queue!=gpuQueue)return;
    const unsigned s=slot;auto& sl=slots[s];
    if(!slot_free(s,50)){frameOpen=false;sl.started=false;return;}
    if(sl.resolved){const UINT64 a=stamps[s*2],b=stamps[s*2+1];
        if(b>a&&b-a<stampFrequency){const float ms=float(double(b-a)*1000.0/double(stampFrequency));
            gpuTimes[gpuNext]=now;gpuMs[gpuNext]=ms;
            gpuNext=(gpuNext+1)%GPU_HISTORY;gpuCount=std::min(gpuCount+1,GPU_HISTORY);}
        sl.resolved=false;}
    Internal scope;
    if(FAILED(sl.allocator->Reset())||FAILED(sl.list->Reset(sl.allocator,nullptr))){gpu_fail("reset list");return;}
    const bool timed=sl.started&&frameOpen;
    if(timed){sl.list->EndQuery(queries,D3D12_QUERY_TYPE_TIMESTAMP,s*2+1);
        sl.list->ResolveQueryData(queries,D3D12_QUERY_TYPE_TIMESTAMP,s*2,2,readback,UINT64(s)*2*sizeof(UINT64));}
    if(overlay.load()&&third)record_overlay(sl.list,third,chain.colorSpace,s);
    if(!gpu_ok(sl.list->Close(),"close overlay list"))return;
    ID3D12CommandList* lists[]={sl.list};gpuQueue->ExecuteCommandLists(1,lists);
    if(!gpu_ok(gpuQueue->Signal(fence,++fenceValue),"signal"))return;
    sl.fence=fenceValue;sl.resolved=timed;sl.started=false;frameOpen=false;slot=(s+1)%RING;
}

// Before each ExecuteCommandLists: open the frame's GPU span on the present queue.
void STDMETHODCALLTYPE execute(ID3D12CommandQueue* self,UINT count,ID3D12CommandList*const* submitted){
    if(!internal&&gpuReady&&self==gpuQueue){
        Hold hold;
        if(!gpuFailed&&!frameOpen&&(overlay.load()||csvEnabled)&&slot_free(slot,0)){
            Internal scope;ID3D12CommandList* lists[]={slots[slot].start};originalExecute(self,1,lists);
            slots[slot].started=true;frameOpen=true;
        }
    }
    originalExecute(self,count,submitted);
}
void hook_queue(ID3D12CommandQueue* queue){
    if(queue)hook(*(void***)queue,10,(void*)execute,originalExecute);
}

Chain* find_chain(void* self){
    for(unsigned i=0;i<chainCount;++i)if(chains[i].primary==self||chains[i].third==self)return &chains[i];
    return nullptr;
}

void on_present(IDXGISwapChain* self,UINT flags){
    if(flags&DXGI_PRESENT_TEST)return;
    LARGE_INTEGER now;QueryPerformanceCounter(&now);
    Hold hold;
    Chain* chain=find_chain(self);
    if(!chain&&chainCount<8&&!asked_device(self)){   // created before our factory hook: ask DXGI for its queue
        ID3D12CommandQueue* queue{};
        if(SUCCEEDED(self->GetDevice(IID_PPV_ARGS(&queue)))){hook_queue(queue);
            chain=&chains[chainCount++];*chain={self,nullptr,queue,DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};}
    }
    // Azimuth 1.x presents its own menu and the game on separate swap chains.
    // The newest chain that presents is the one measured and drawn on; an
    // older one takes over again once the newer one is idle for a second.
    if(chain){
        const unsigned index=unsigned(chain-chains);
        if(index!=bound){
            if(bound!=NONE&&index<bound&&now.QuadPart-boundPresent<frequency.QuadPart)return;
            bound=index;lastPresent=0;historyCount=0;gpuCount=0;
            if(gpuFailed)gpu_release();   // a new chain gets its own attempt
        }
        boundPresent=now.QuadPart;
    }else if(bound!=NONE&&now.QuadPart-boundPresent<frequency.QuadPart)return;
    record_frame(now.QuadPart);
    const unsigned mode=overlay.load();
    if(mode&&now.QuadPart-lastText>=frequency.QuadPart/2){lastText=now.QuadPart;refresh_text(now.QuadPart);}
    if(!mode)lineCount=0;
    if(csvEnabled&&now.QuadPart-lastCsv>=frequency.QuadPart){write_csv(now.QuadPart);lastCsv=now.QuadPart;}
    if(!mode&&!csvEnabled){frameOpen=false;slots[slot].started=false;return;}
    if(!chain)return;
    if(mode)start_monitor(chain->queue);
    IDXGISwapChain3* third{};
    if(FAILED(self->QueryInterface(IID_PPV_ARGS(&third))))third=nullptr;
    gpu_frame(*chain,third,now.QuadPart);
    if(third)third->Release();
}

HRESULT STDMETHODCALLTYPE present(IDXGISwapChain* self,UINT sync,UINT flags){
    if(!internal)on_present(self,flags);
    return originalPresent(self,sync,flags);
}
HRESULT STDMETHODCALLTYPE present1(IDXGISwapChain1* self,UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* parameters){
    if(!internal)on_present(self,flags);
    return originalPresent1(self,sync,flags,parameters);
}
HRESULT STDMETHODCALLTYPE color_space(IDXGISwapChain3* self,DXGI_COLOR_SPACE_TYPE space){
    HRESULT hr=originalColorSpace(self,space);
    if(SUCCEEDED(hr)){Hold hold;if(Chain* chain=find_chain(self))chain->colorSpace=space;}
    return hr;
}
void hook_chain(IDXGISwapChain1* chain,IUnknown* device){
    ID3D12CommandQueue* queue{};
    if(!device||FAILED(device->QueryInterface(IID_PPV_ARGS(&queue))))return;   // not a D3D12 swap chain
    hook_queue(queue);
    IDXGISwapChain3* third{};if(FAILED(chain->QueryInterface(IID_PPV_ARGS(&third))))third=nullptr;
    hook(*(void***)chain,8,(void*)present,originalPresent);hook(*(void***)chain,22,(void*)present1,originalPresent1);
    // IDXGISwapChain3 is the same object and vtable in DXGI; anything else is left alone.
    if(third&&*(void***)third==*(void***)chain)hook(*(void***)chain,38,(void*)color_space,originalColorSpace);
    {Hold hold;Chain* known=find_chain(chain);   // a new chain can reuse a destroyed one's address
        if(!known&&chainCount<8)known=&chains[chainCount++];
        if(known){if(known->queue&&known->queue!=gpuQueue)known->queue->Release();
            *known={chain,third,queue,DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};}
        else queue->Release();}
    if(third)third->Release();
}
HRESULT STDMETHODCALLTYPE make_chain(IDXGIFactory2* self,IUnknown* queue,HWND window,const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,IDXGIOutput* output,IDXGISwapChain1** chain){
    HRESULT hr=originalMakeChain(self,queue,window,desc,fullscreen,output,chain);
    if(SUCCEEDED(hr)&&chain&&*chain&&!internal)hook_chain(*chain,queue);
    return hr;
}
} // namespace

// Called once by the mod after the game's D3D12CreateDevice, outside loader
// initialization. `device` is the new device (unused: the GPU part takes the
// device of the presenting queue); `directory` is the game folder.
MON_EXPORT int MonAttach(IUnknown* device,const wchar_t* directory){
    (void)device;
    Hold hold;
    if(attached)return 1;
    if(!directory||wcslen(directory)>32000)return 0;
    // Pin before publishing any callback address. Hooks live until process exit.
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)(uintptr_t)MonAttach,&pinned))return 0;
    attached=true;
    QueryPerformanceFrequency(&frequency);
    static wchar_t path[32768];
    swprintf(path,32768,L"%ls\\FH2_ExtraOptions.ini",directory);
    overlay=std::min<UINT>(2,GetPrivateProfileIntW(L"Monitor",L"ShowFps",0,path));
    csvEnabled=GetPrivateProfileIntW(L"Monitor",L"PerfLog",0,path)!=0;
    LARGE_INTEGER start;QueryPerformanceCounter(&start);sessionStart=start.QuadPart;
    SYSTEMTIME t;GetLocalTime(&t);
    snprintf(session,sizeof(session),"%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    if(csvEnabled){
        swprintf(path,32768,L"%ls\\fh2-perf.csv",directory);
        csv=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,0,nullptr);
        LARGE_INTEGER size{};
        if(csv!=INVALID_HANDLE_VALUE&&GetFileSizeEx(csv,&size)&&!size.QuadPart){
            static const char header[]="time,session,elapsed_s,interval_s,frames,avg_fps,avg_ms,p99_ms,max_ms,low1_fps,"
                "gpu_ms,gpu_samples,overlay\r\n";
            DWORD written;WriteFile(csv,header,sizeof(header)-1,&written,nullptr);
        }
    }
    IDXGIFactory2* factory{};
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 0;
    const bool ok=hook(*(void***)factory,15,(void*)make_chain,originalMakeChain);factory->Release();
    return ok?1:0;
}
