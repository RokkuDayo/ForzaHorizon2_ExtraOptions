#include "reader.hpp"

#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <atomic>
#include <cstring>
#include <vector>

namespace {

template <typename Base>
class Unknown : public Base {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_;
        if (!n) delete this;
        return n;
    }

protected:
    virtual ~Unknown() = default;
    HRESULT query(REFIID iid, const IID& own, void** out) {
        if (!out) return E_POINTER;
        if (iid == __uuidof(IUnknown) || iid == own) {
            *out = static_cast<Base*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

private:
    std::atomic<ULONG> refs_{1};
};

class Buffer final : public Unknown<IMFMediaBuffer> {
public:
    explicit Buffer(size_t bytes) : data_(bytes) {}
    BYTE* data() { return data_.data(); }
    void set_length(DWORD n) { length_ = n; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        return query(iid, __uuidof(IMFMediaBuffer), out);
    }
    HRESULT STDMETHODCALLTYPE Lock(BYTE** data, DWORD* max, DWORD* current) override {
        if (!data) return E_POINTER;
        *data = data_.data();
        if (max) *max = (DWORD)data_.size();
        if (current) *current = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentLength(DWORD* n) override {
        if (!n) return E_POINTER;
        *n = length_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCurrentLength(DWORD n) override {
        if (n > data_.size()) return E_INVALIDARG;
        length_ = n;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMaxLength(DWORD* n) override {
        if (!n) return E_POINTER;
        *n = (DWORD)data_.size();
        return S_OK;
    }

private:
    std::vector<BYTE> data_;
    DWORD length_ = 0;
};

// One sample holding one buffer. The attribute half of IMFSample isn't used by the player.
class Sample final : public Unknown<IMFSample> {
public:
    Sample(Buffer* buffer, LONGLONG time, LONGLONG duration) : buffer_(buffer), time_(time), duration_(duration) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IMFAttributes) && out) {
            *out = static_cast<IMFAttributes*>(this);
            AddRef();
            return S_OK;
        }
        return query(iid, __uuidof(IMFSample), out);
    }
    // IMFAttributes
    HRESULT STDMETHODCALLTYPE GetItem(REFGUID, PROPVARIANT*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetItemType(REFGUID, MF_ATTRIBUTE_TYPE*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE CompareItem(REFGUID, REFPROPVARIANT, BOOL* r) override {
        if (r) *r = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compare(IMFAttributes*, MF_ATTRIBUTES_MATCH_TYPE, BOOL* r) override {
        if (r) *r = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetUINT32(REFGUID, UINT32*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetUINT64(REFGUID, UINT64*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetDouble(REFGUID, double*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetGUID(REFGUID, GUID*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetStringLength(REFGUID, UINT32*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetString(REFGUID, LPWSTR, UINT32, UINT32*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetAllocatedString(REFGUID, LPWSTR*, UINT32*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetBlobSize(REFGUID, UINT32*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetBlob(REFGUID, UINT8*, UINT32, UINT32*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetAllocatedBlob(REFGUID, UINT8**, UINT32*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE GetUnknown(REFGUID, REFIID, LPVOID*) override { return MF_E_ATTRIBUTENOTFOUND; }
    HRESULT STDMETHODCALLTYPE SetItem(REFGUID, REFPROPVARIANT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DeleteItem(REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DeleteAllItems() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetUINT32(REFGUID, UINT32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetUINT64(REFGUID, UINT64) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetDouble(REFGUID, double) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetGUID(REFGUID, REFGUID) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetString(REFGUID, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBlob(REFGUID, const UINT8*, UINT32) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetUnknown(REFGUID, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LockStore() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE UnlockStore() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetCount(UINT32* n) override {
        if (n) *n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetItemByIndex(UINT32, GUID*, PROPVARIANT*) override { return E_INVALIDARG; }
    HRESULT STDMETHODCALLTYPE CopyAllItems(IMFAttributes*) override { return S_OK; }
    // IMFSample
    HRESULT STDMETHODCALLTYPE GetSampleFlags(DWORD* f) override {
        if (f) *f = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSampleFlags(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSampleTime(LONGLONG* t) override {
        if (!t) return E_POINTER;
        *t = time_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSampleTime(LONGLONG t) override {
        time_ = t;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSampleDuration(LONGLONG* d) override {
        if (!d) return E_POINTER;
        *d = duration_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSampleDuration(LONGLONG d) override {
        duration_ = d;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBufferCount(DWORD* n) override {
        if (n) *n = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBufferByIndex(DWORD i, IMFMediaBuffer** out) override { return give(i, out); }
    HRESULT STDMETHODCALLTYPE ConvertToContiguousBuffer(IMFMediaBuffer** out) override { return give(0, out); }
    HRESULT STDMETHODCALLTYPE AddBuffer(IMFMediaBuffer*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveBufferByIndex(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveAllBuffers() override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTotalLength(DWORD* n) override { return buffer_->GetCurrentLength(n); }
    HRESULT STDMETHODCALLTYPE CopyToBuffer(IMFMediaBuffer* to) override {
        if (!to) return E_POINTER;
        BYTE *dst, *src;
        DWORD max, length;
        buffer_->Lock(&src, nullptr, &length);
        if (FAILED(to->Lock(&dst, &max, nullptr))) return E_FAIL;
        if (length > max) length = max;
        std::memcpy(dst, src, length);
        to->Unlock();
        return to->SetCurrentLength(length);
    }

private:
    ~Sample() override { buffer_->Release(); }
    HRESULT give(DWORD i, IMFMediaBuffer** out) {
        if (!out) return E_POINTER;
        if (i) return E_INVALIDARG;
        buffer_->AddRef();
        *out = buffer_;
        return S_OK;
    }
    Buffer* buffer_;
    LONGLONG time_, duration_;
};

class Reader final : public Unknown<IMFSourceReader> {
public:
    explicit Reader(PcmSource* source) : source_(source) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        return query(iid, __uuidof(IMFSourceReader), out);
    }
    HRESULT STDMETHODCALLTYPE GetStreamSelection(DWORD, BOOL* selected) override {
        if (selected) *selected = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSelection(DWORD, BOOL) override { return S_OK; }
    // No native types listed: the player then asks for its default, 44.1 kHz 16-bit stereo PCM.
    HRESULT STDMETHODCALLTYPE GetNativeMediaType(DWORD, DWORD, IMFMediaType** type) override {
        if (type) *type = nullptr;
        return MF_E_NO_MORE_TYPES;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentMediaType(DWORD, IMFMediaType** type) override {
        if (type) *type = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetCurrentMediaType(DWORD, DWORD*, IMFMediaType*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCurrentPosition(REFGUID, REFPROPVARIANT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ReadSample(DWORD, DWORD, DWORD* index, DWORD* flags, LONGLONG* timestamp,
                                         IMFSample** sample) override {
        if (index) *index = 0;
        if (flags) *flags = 0;
        if (sample) *sample = nullptr;
        if (!sample) return E_POINTER;
        auto buffer = new Buffer(FT_MAX_OUT * sizeof(Frame));
        size_t frames = ended_ ? 0 : source_->read(reinterpret_cast<Frame*>(buffer->data()), FT_MAX_OUT);
        LONGLONG time = (LONGLONG)(position_ * 10000000ULL / 44100);
        if (timestamp) *timestamp = time;
        if (!frames) {
            ended_ = true;
            buffer->Release();
            if (flags) *flags = MF_SOURCE_READERF_ENDOFSTREAM;
            return S_OK;
        }
        buffer->set_length((DWORD)(frames * sizeof(Frame)));
        position_ += frames;
        *sample = new Sample(buffer, time, (LONGLONG)(frames * 10000000ULL / 44100));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Flush(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetServiceForStream(DWORD, REFGUID, REFIID, LPVOID* out) override {
        if (out) *out = nullptr;
        return MF_E_UNSUPPORTED_SERVICE;
    }
    HRESULT STDMETHODCALLTYPE GetPresentationAttribute(DWORD, REFGUID, PROPVARIANT* value) override {
        if (value) PropVariantInit(value);
        return MF_E_ATTRIBUTENOTFOUND;
    }

private:
    ~Reader() override { delete source_; }
    PcmSource* source_;
    uint64_t position_ = 0;
    bool ended_ = false;
};

}  // namespace

void* make_source_reader(PcmSource* source) { return static_cast<IMFSourceReader*>(new Reader(source)); }
