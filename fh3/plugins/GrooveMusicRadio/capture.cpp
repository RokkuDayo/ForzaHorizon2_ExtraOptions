// Process loopback capture, as in Microsoft's ApplicationLoopback sample: activate the virtual
// "VAD\Process_Loopback" device for one process tree, then read it like any loopback capture.
#include "capture.hpp"

#include <audioclient.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace {

// audioclientactivationparams.h (not in every SDK).
enum CaptureActivationType { kActivationDefault = 0, kActivationProcessLoopback = 1 };
enum CaptureLoopbackMode { kIncludeTargetProcessTree = 0, kExcludeTargetProcessTree = 1 };
struct ProcessLoopbackParams {
    DWORD TargetProcessId;
    CaptureLoopbackMode ProcessLoopbackMode;
};
struct ActivationParams {
    CaptureActivationType ActivationType;
    ProcessLoopbackParams ProcessLoopbackParams;
};
const wchar_t kProcessLoopbackDevice[] = L"VAD\\Process_Loopback";

const IID kIID_IAudioClient         = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
const IID kIID_IAudioCaptureClient  = {0xC8ADBD64, 0xE71E, 0x48A0, {0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17}};
const IID kIID_ICompletionHandler   = {0x41D949AB, 0x9862, 0x444A, {0x80, 0xF6, 0xC2, 0x61, 0x33, 0x4D, 0xA5, 0xEB}};
const IID kIID_IAgileObject         = {0x94EA2B94, 0xE9CC, 0x49E0, {0xC0, 0xFF, 0xEE, 0x64, 0xCA, 0x8F, 0x5B, 0x90}};
const CLSID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID kIID_IMMDeviceEnumerator  = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID kIID_IAudioSessionManager2 = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
const IID kIID_IAudioSessionControl2 = {0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};
const IID kIID_ISimpleAudioVolume   = {0x87CE5498, 0x68D6, 0x44E5, {0x92, 0x15, 0x6D, 0xA4, 0x7E, 0xF8, 0x83, 0xD8}};

// Called by Windows (on any thread: hence agile) when the activation is done.
class Activation final : public IActivateAudioInterfaceCompletionHandler {
public:
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Activation() { CloseHandle(done); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kIID_ICompletionHandler) || IsEqualIID(riid, kIID_IAgileObject)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        SetEvent(done);
        return S_OK;
    }

private:
    std::atomic<ULONG> refs_{1};
};

void logf(const CaptureLog& log, const char* fmt, ...) {
    if (!log) return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    log(line);
}

IAudioClient* activate(DWORD pid, const CaptureLog& log) {
    ActivationParams p{};
    p.ActivationType                            = kActivationProcessLoopback;
    p.ProcessLoopbackParams.TargetProcessId     = pid;
    p.ProcessLoopbackParams.ProcessLoopbackMode = kIncludeTargetProcessTree;
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt             = VT_BLOB;
    pv.blob.cbSize    = sizeof p;
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&p);

    auto* handler = new Activation;
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(kProcessLoopbackDevice, kIID_IAudioClient, &pv, handler, &op);
    IAudioClient* client = nullptr;
    if (SUCCEEDED(hr) && WaitForSingleObject(handler->done, 5000) == WAIT_OBJECT_0) {
        HRESULT result = E_FAIL;
        IUnknown* unk  = nullptr;
        hr = op->GetActivateResult(&result, &unk);
        if (SUCCEEDED(hr)) hr = result;
        if (SUCCEEDED(hr) && unk) hr = unk->QueryInterface(kIID_IAudioClient, reinterpret_cast<void**>(&client));
        if (unk) unk->Release();
    } else if (SUCCEEDED(hr)) {
        hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (op) op->Release();
    handler->Release();
    if (!client) logf(log, "capture: couldn't open the process loopback device (0x%08lX; needs Windows 10 2004 or later)", hr);
    return client;
}

struct Job {
    DWORD pid;
    const std::atomic<float>* gain;
    CaptureSink sink;
    CaptureLog log;
    const volatile bool* stop;
};

// Float samples (with the app's volume already applied) -> S16 times `gain`.
void to_s16(const float* in, size_t frames, float gain, std::vector<int16_t>& out) {
    out.resize(frames * 2);
    for (size_t i = 0; i < frames * 2; ++i) {
        float v = in[i] * gain * 32767.0f;
        out[i]  = static_cast<int16_t>(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v);
    }
}

DWORD WINAPI capture_thread(LPVOID param) {
    Job* job = static_cast<Job*>(param);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IAudioClient* client = activate(job->pid, job->log);
    IAudioCaptureClient* cap = nullptr;
    HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (client) {
        // Float, so a program turned far down in the volume mixer can be turned back up without losing detail.
        WAVEFORMATEX fmt{};
        fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
        fmt.nChannels       = 2;
        fmt.nSamplesPerSec  = 44100;
        fmt.wBitsPerSample  = 32;
        fmt.nBlockAlign     = 8;
        fmt.nAvgBytesPerSec = 44100 * 8;
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                        2000000, 0, &fmt, nullptr);   // 200 ms
        if (SUCCEEDED(hr)) hr = client->SetEventHandle(ready);
        if (SUCCEEDED(hr)) hr = client->GetService(kIID_IAudioCaptureClient, reinterpret_cast<void**>(&cap));
        if (SUCCEEDED(hr)) hr = client->Start();
        if (FAILED(hr)) {
            logf(job->log, "capture: couldn't start recording (0x%08lX)", hr);
            if (cap) cap->Release();
            cap = nullptr;
        }
    }
    if (cap) {
        logf(job->log, "capture: recording process %lu", job->pid);
        std::vector<int16_t> pcm;
        while (!*job->stop) {
            WaitForSingleObject(ready, 100);
            UINT32 packet = 0;
            while (!*job->stop && SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet) {
                BYTE* data   = nullptr;
                UINT32 n     = 0;
                DWORD flags  = 0;
                if (FAILED(cap->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    job->sink(nullptr, n);
                } else {
                    to_s16(reinterpret_cast<const float*>(data), n, job->gain->load(std::memory_order_relaxed), pcm);
                    job->sink(pcm.data(), n);
                }
                cap->ReleaseBuffer(n);
            }
        }
        client->Stop();
        cap->Release();
    }
    if (client) client->Release();
    CloseHandle(ready);
    CoUninitialize();
    delete job;
    return 0;
}

} // namespace

bool capture_process(DWORD pid, const std::atomic<float>* gain, CaptureSink sink, CaptureLog log, const volatile bool* stop) {
    auto* job = new Job{pid, gain, std::move(sink), std::move(log), stop};
    HANDLE t  = CreateThread(nullptr, 0, capture_thread, job, 0, nullptr);
    if (!t) {
        delete job;
        return false;
    }
    CloseHandle(t);
    return true;
}

namespace {

// Calls `fn` for each sound session on the default output device (what the volume mixer lists).
void for_each_output_session(const std::function<void(DWORD pid, bool active, ISimpleAudioVolume* vol)>& fn) {
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    IMMDevice* dev          = nullptr;
    IAudioSessionManager2* mgr = nullptr;
    IAudioSessionEnumerator* sessions = nullptr;
    if (SUCCEEDED(CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, reinterpret_cast<void**>(&en))) &&
        SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
        SUCCEEDED(dev->Activate(kIID_IAudioSessionManager2, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) &&
        SUCCEEDED(mgr->GetSessionEnumerator(&sessions))) {
        int count = 0;
        sessions->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* ctl   = nullptr;
            IAudioSessionControl2* ctl2 = nullptr;
            ISimpleAudioVolume* vol     = nullptr;
            DWORD owner                 = 0;
            AudioSessionState state     = AudioSessionStateInactive;
            if (SUCCEEDED(sessions->GetSession(i, &ctl)) &&
                SUCCEEDED(ctl->QueryInterface(kIID_IAudioSessionControl2, reinterpret_cast<void**>(&ctl2))) &&
                SUCCEEDED(ctl2->GetProcessId(&owner)) && owner != 0 && SUCCEEDED(ctl->GetState(&state)) &&
                SUCCEEDED(ctl->QueryInterface(kIID_ISimpleAudioVolume, reinterpret_cast<void**>(&vol))))
                fn(owner, state == AudioSessionStateActive, vol);
            if (vol) vol->Release();
            if (ctl2) ctl2->Release();
            if (ctl) ctl->Release();
        }
    }
    if (sessions) sessions->Release();
    if (mgr) mgr->Release();
    if (dev) dev->Release();
    if (en) en->Release();
    if (SUCCEEDED(init)) CoUninitialize();
}

} // namespace

bool set_process_output_volume(DWORD pid, float level) {
    bool done = false;
    for_each_output_session([&](DWORD owner, bool, ISimpleAudioVolume* vol) {
        if (owner == pid && SUCCEEDED(vol->SetMute(FALSE, nullptr)) && SUCCEEDED(vol->SetMasterVolume(level, nullptr))) done = true;
    });
    return done;
}

std::vector<OutputSession> list_output_sessions() {
    std::vector<OutputSession> out;
    for_each_output_session([&](DWORD owner, bool active, ISimpleAudioVolume* vol) {
        OutputSession o{owner, active, 1.0f, false};
        BOOL muted = FALSE;
        vol->GetMasterVolume(&o.level);
        vol->GetMute(&muted);
        o.muted = muted != FALSE;
        out.push_back(o);
    });
    return out;
}

int set_output_volume(const std::function<bool(DWORD pid)>& which, float level, bool muted) {
    int done = 0;
    for_each_output_session([&](DWORD owner, bool, ISimpleAudioVolume* vol) {
        if (which(owner) && SUCCEEDED(vol->SetMute(muted, nullptr)) && SUCCEEDED(vol->SetMasterVolume(level, nullptr))) done++;
    });
    return done;
}
