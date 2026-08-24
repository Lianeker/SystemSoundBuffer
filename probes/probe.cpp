/* Prueba de captura WASAPI: enumeracion, loopback de dispositivo y loopback por proceso. */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define CHK(hr, what) if (FAILED(hr)) { printf("  FALLO %s hr=0x%08X\n", what, (unsigned)(hr)); return (int)(hr); }

static void print_fmt(const char *tag, WAVEFORMATEX *f)
{
    printf("  %s: tag=%u canales=%u rate=%u bits=%u block=%u\n", tag,
           (unsigned)f->wFormatTag, (unsigned)f->nChannels, (unsigned)f->nSamplesPerSec,
           (unsigned)f->wBitsPerSample, (unsigned)f->nBlockAlign);
}

static int list_devices(void)
{
    IMMDeviceEnumerator *en = NULL;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void **)&en);
    EDataFlow flows[2];
    const char *names[2];
    int k;
    CHK(hr, "CoCreateInstance(MMDeviceEnumerator)");
    flows[0] = eRender; flows[1] = eCapture;
    names[0] = "RENDER (salida)"; names[1] = "CAPTURE (entrada)";
    for (k = 0; k < 2; ++k)
    {
        IMMDeviceCollection *col = NULL;
        UINT n = 0, i;
        hr = en->EnumAudioEndpoints(flows[k], DEVICE_STATE_ACTIVE, &col);
        CHK(hr, "EnumAudioEndpoints");
        col->GetCount(&n);
        printf("%s: %u dispositivo(s)\n", names[k], (unsigned)n);
        for (i = 0; i < n; ++i)
        {
            IMMDevice *dev = NULL;
            IPropertyStore *ps = NULL;
            PROPVARIANT v;
            col->Item(i, &dev);
            dev->OpenPropertyStore(STGM_READ, &ps);
            PropVariantInit(&v);
            ps->GetValue(PKEY_Device_FriendlyName, &v);
            printf("  [%u] %ls\n", (unsigned)i, v.pwszVal);
            PropVariantClear(&v);
            ps->Release();
            dev->Release();
        }
        col->Release();
    }
    en->Release();
    return 0;
}

/* Mide pico y cuenta frames de un cliente de captura ya inicializado. */
static int drain(IAudioClient *ac, WAVEFORMATEX *fmt, double secs, HANDLE ev)
{
    IAudioCaptureClient *cap = NULL;
    ULONGLONG t0;
    UINT64 total = 0, silent = 0;
    double peak = 0.0;
    HRESULT hr = ac->GetService(__uuidof(IAudioCaptureClient), (void **)&cap);
    CHK(hr, "GetService(IAudioCaptureClient)");
    hr = ac->Start();
    CHK(hr, "IAudioClient::Start");
    t0 = GetTickCount64();
    while (GetTickCount64() - t0 < (ULONGLONG)(secs * 1000))
    {
        UINT32 packet = 0;
        if (ev != NULL) WaitForSingleObject(ev, 200); else Sleep(10);
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0)
        {
            BYTE *data = NULL;
            UINT32 frames = 0, j, ns;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, NULL, NULL))) break;
            total += frames;
            ns = frames * fmt->nChannels;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
            {
                silent += frames;
            }
            else if (fmt->wBitsPerSample == 16)
            {
                const short *s = (const short *)data;
                for (j = 0; j < ns; ++j) { double a = fabs((double)s[j]) / 32768.0; if (a > peak) peak = a; }
            }
            else if (fmt->wBitsPerSample == 32)
            {
                const float *s = (const float *)data;
                for (j = 0; j < ns; ++j) { double a = fabs((double)s[j]); if (a > peak) peak = a; }
            }
            cap->ReleaseBuffer(frames);
            packet = 0;
        }
    }
    ac->Stop();
    cap->Release();
    printf("  frames=%llu (%.2f s de audio) silencio=%llu pico=%.4f\n",
           (unsigned long long)total, (double)total / (double)fmt->nSamplesPerSec,
           (unsigned long long)silent, peak);
    printf("  VEREDICTO: %s\n", (total > 0 && peak > 0.0001) ? "AUDIO REAL CAPTURADO"
           : (total > 0 ? "solo silencio (la fuente no emitia)" : "cero frames"));
    return 0;
}

static int device_loopback(double secs)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *ac = NULL;
    WAVEFORMATEX *fmt = NULL;
    int r;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void **)&en);
    CHK(hr, "CoCreateInstance");
    hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    CHK(hr, "GetDefaultAudioEndpoint(eRender)");
    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&ac);
    CHK(hr, "Activate(IAudioClient)");
    hr = ac->GetMixFormat(&fmt);
    CHK(hr, "GetMixFormat");
    print_fmt("formato de mezcla", fmt);
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, NULL);
    CHK(hr, "Initialize(LOOPBACK)");
    printf("  Initialize(AUDCLNT_STREAMFLAGS_LOOPBACK) OK\n");
    r = drain(ac, fmt, secs, NULL);
    CoTaskMemFree(fmt);
    ac->Release();
    dev->Release();
    en->Release();
    return r;
}

/* --- loopback por proceso ------------------------------------------------ */
class Handler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    HANDLE done;
    Handler() { done = CreateEventW(NULL, FALSE, FALSE, NULL); }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *op) { (void)op; SetEvent(done); return S_OK; }
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv)
    {
        if (riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
        {
            *ppv = (IActivateAudioInterfaceCompletionHandler *)this;
            return S_OK;
        }
        if (riid == __uuidof(IAgileObject) || riid == IID_IUnknown)
        {
            *ppv = (IAgileObject *)this;
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() { return 2; }
    STDMETHOD_(ULONG, Release)() { return 1; }
};

static int process_loopback(DWORD pid, double secs, bool exclude)
{
    AUDIOCLIENT_ACTIVATION_PARAMS ap;
    PROPVARIANT pv;
    Handler h;
    IActivateAudioInterfaceAsyncOperation *op = NULL;
    HRESULT act = E_FAIL;
    IUnknown *unk = NULL;
    IAudioClient *ac = NULL;
    WAVEFORMATEX fmt;
    HANDLE ev;
    int r;
    HRESULT hr;

    ZeroMemory(&ap, sizeof(ap));
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode =
        exclude ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
                : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = (BYTE *)&ap;

    hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                     __uuidof(IAudioClient), &pv, &h, &op);
    CHK(hr, "ActivateAudioInterfaceAsync");
    WaitForSingleObject(h.done, 5000);
    hr = op->GetActivateResult(&act, &unk);
    CHK(hr, "GetActivateResult(llamada)");
    CHK(act, "GetActivateResult(resultado de activacion)");
    printf("  activacion por proceso OK (pid=%lu modo=%s)\n", (unsigned long)pid,
           exclude ? "EXCLUDE_TREE" : "INCLUDE_TREE");
    ac = (IAudioClient *)unk;

    /* El cliente de loopback por proceso NO soporta GetMixFormat: hay que dar el formato. */
    ZeroMemory(&fmt, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 44100;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    print_fmt("formato pedido", &fmt);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                        AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                        2000000, 0, &fmt, NULL);
    CHK(hr, "Initialize(process loopback)");
    hr = ac->SetEventHandle(ev);
    CHK(hr, "SetEventHandle");
    printf("  Initialize OK\n");
    r = drain(ac, &fmt, secs, ev);
    ac->Release();
    return r;
}

int main(int argc, char **argv)
{
    int r = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (argc < 2 || strcmp(argv[1], "list") == 0)
        r = list_devices();
    else if (strcmp(argv[1], "loop") == 0)
        r = device_loopback(argc > 2 ? atof(argv[2]) : 3.0);
    else if (strcmp(argv[1], "proc") == 0)
        r = process_loopback((DWORD)atoi(argv[2]), argc > 3 ? atof(argv[3]) : 3.0, false);
    else if (strcmp(argv[1], "excl") == 0)
        r = process_loopback((DWORD)atoi(argv[2]), argc > 3 ? atof(argv[3]) : 3.0, true);
    CoUninitialize();
    return r;
}
