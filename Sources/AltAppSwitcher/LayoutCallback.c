#define COBJMACROS
#include <windows.h>
#include <msctf.h>
#include <stdio.h>

typedef struct ProfileSink {
    ITfInputProcessorProfileActivationSink iface;
    LONG refCount;
} ProfileSink;

/*----------------------------------------------------------
    IUnknown
----------------------------------------------------------*/

HRESULT STDMETHODCALLTYPE Sink_QueryInterface(
    ITfInputProcessorProfileActivationSink* This,
    REFIID riid,
    void** ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ITfInputProcessorProfileActivationSink)) {
        *ppv = This;
        ITfInputProcessorProfileActivationSink_AddRef(This);
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Sink_AddRef(
    ITfInputProcessorProfileActivationSink* This)
{
    ProfileSink* self = (ProfileSink*)This;
    return InterlockedIncrement(&self->refCount);
}

ULONG STDMETHODCALLTYPE Sink_Release(
    ITfInputProcessorProfileActivationSink* This)
{
    ProfileSink* self = (ProfileSink*)This;

    LONG r = InterlockedDecrement(&self->refCount);

    if (r == 0)
        HeapFree(GetProcessHeap(), 0, self);

    return r;
}

HRESULT STDMETHODCALLTYPE Sink_OnActivated(
    ITfInputProcessorProfileActivationSink* This,
    DWORD dwProfileType,
    LANGID langid,
    REFCLSID clsid,
    REFGUID catid,
    REFGUID guidProfile,
    HKL hkl,
    DWORD flags)
{
    (void)This;
    (void)dwProfileType;
    (void)langid;
    (void)clsid;
    (void)catid;
    (void)guidProfile;
    (void)hkl;
    (void)flags;
    printf("Keyboard layout changed\n");
    printf("LANGID = %04X\n", langid);
    printf("HKL    = %p\n", hkl);

    return S_OK;
}

static ITfInputProcessorProfileActivationSinkVtbl SinkVtbl = {
    Sink_QueryInterface,
    Sink_AddRef,
    Sink_Release,
    Sink_OnActivated
};

ProfileSink* CreateSink(void)
{
    ProfileSink* sink = HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY,
        sizeof(ProfileSink));

    sink->iface.lpVtbl = &SinkVtbl;
    sink->refCount = 1;

    return sink;
}

static ProfileSink* sink;
static DWORD cookie;
static ITfSource* source;
static ITfThreadMgr* threadMgr;

void InitSink()
{
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return;

    hr = CoCreateInstance(
        &CLSID_TF_ThreadMgr,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_ITfThreadMgr,
        (void**)&threadMgr);

    if (FAILED(hr))
        return;

    TfClientId clientId;
    ITfThreadMgr_Activate(threadMgr, &clientId);

    ITfThreadMgr_QueryInterface(
        threadMgr,
        &IID_ITfSource,
        (void**)&source);

    sink = CreateSink();

    ITfSource_AdviseSink(
        source,
        &IID_ITfInputProcessorProfileActivationSink,
        (IUnknown*)&sink->iface,
        &cookie);

    printf("Waiting for keyboard layout changes...\n");

    return;
}

void DeinitSink()
{
    ITfSource_UnadviseSink(source, cookie);
    ITfSource_Release(source);
    ITfThreadMgr_Deactivate(threadMgr);
    ITfThreadMgr_Release(threadMgr);
    ITfInputProcessorProfileActivationSink_Release(&sink->iface);
    CoUninitialize();
}