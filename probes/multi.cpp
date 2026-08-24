/* Prueba de captura WASAPI MULTIPISTA: varias fuentes simultaneas, cada una en su hilo.
   Responde: ¿se pueden grabar a la vez la salida del sistema, dos aplicaciones
   distintas y el microfono? ¿Comparten reloj (QPC) para alinear las pistas? */
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

/* Subformatos de WAVEFORMATEXTENSIBLE, definidos a mano para no depender de ksmedia.h */
static const GUID SUBTYPE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

#define KIND_DEVICE_LOOPBACK 0
#define KIND_PROCESS_LOOPBACK 1
#define KIND_INPUT_DEVICE 2

typedef struct _src_t
{
    const char *name;
    int kind;
    DWORD pid;
    double secs;
    /* resultados */
    int setup_ok;
    HRESULT hr;
    const char *hr_where;
    WAVEFORMATEX fmt;
    const char *fmt_note;
    UINT64 frames;
    UINT64 silent;
    UINT64 discont;
    double peak;
    UINT64 qpc_first;
    UINT64 qpc_last;
    UINT64 dev_first;
    UINT64 dev_last;
} Src;

class Handler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    HANDLE done;
    Handler() { done = CreateEventW(NULL, FALSE, FALSE, NULL); }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *op) { (void)op; SetEvent(done); return S_OK; }
    STDMETHOD(QueryInterface)(REFIID riid, void **ppv)
    {
        if (riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) { *ppv = (IActivateAudioInterfaceCompletionHandler *)this; return S_OK; }
        if (riid == __uuidof(IAgileObject) || riid == IID_IUnknown) { *ppv = (IAgileObject *)this; return S_OK; }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() { return 2; }
    STDMETHOD_(ULONG, Release)() { return 1; }
};

static void measure(Src *s, const BYTE *data, UINT32 frames)
{
    UINT32 ns = frames * s->fmt.nChannels, j;
    if (s->fmt.wBitsPerSample == 16)
    {
        const short *p = (const short *)data;
        for (j = 0; j < ns; ++j) { double a = fabs((double)p[j]) / 32768.0; if (a > s->peak) s->peak = a; }
    }
    else if (s->fmt.wBitsPerSample == 32)
    {
        const float *p = (const float *)data;
        for (j = 0; j < ns; ++j) { double a = fabs((double)p[j]); if (a > s->peak) s->peak = a; }
    }
}

static DWORD WINAPI run_source(LPVOID arg)
{
    Src *s = (Src *)arg;
    IAudioClient *ac = NULL;
    IAudioCaptureClient *cap = NULL;
    HANDLE ev = NULL;
    WAVEFORMATEX *mix = NULL;
    ULONGLONG t0;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (s->kind == KIND_PROCESS_LOOPBACK)
    {
        AUDIOCLIENT_ACTIVATION_PARAMS ap;
        PROPVARIANT pv;
        Handler h;
        IActivateAudioInterfaceAsyncOperation *op = NULL;
        HRESULT act = E_FAIL;
        IUnknown *unk = NULL;
        WAVEFORMATEXTENSIBLE wfx;

        ZeroMemory(&ap, sizeof(ap));
        ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        ap.ProcessLoopbackParams.TargetProcessId = s->pid;
        ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PropVariantInit(&pv);
        pv.vt = VT_BLOB;
        pv.blob.cbSize = sizeof(ap);
        pv.blob.pBlobData = (BYTE *)&ap;

        hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, &h, &op);
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "ActivateAudioInterfaceAsync"; goto fin; }
        WaitForSingleObject(h.done, 5000);
        hr = op->GetActivateResult(&act, &unk);
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "GetActivateResult"; goto fin; }
        if (FAILED(act)) { s->hr = act; s->hr_where = "activacion"; goto fin; }
        ac = (IAudioClient *)unk;

        /* Primero pedimos float32 a 48 kHz, el formato natural para mezclar pistas. */
        ZeroMemory(&wfx, sizeof(wfx));
        wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfx.Format.nChannels = 2;
        wfx.Format.nSamplesPerSec = 48000;
        wfx.Format.wBitsPerSample = 32;
        wfx.Format.nBlockAlign = 8;
        wfx.Format.nAvgBytesPerSec = 48000 * 8;
        wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wfx.Samples.wValidBitsPerSample = 32;
        wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        wfx.SubFormat = SUBTYPE_FLOAT;

        ev = CreateEventW(NULL, FALSE, FALSE, NULL);
        hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            2000000, 0, (WAVEFORMATEX *)&wfx, NULL);
        if (SUCCEEDED(hr))
        {
            s->fmt = wfx.Format;
            s->fmt.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            s->fmt_note = "float32 48k aceptado";
        }
        else
        {
            WAVEFORMATEX p16;
            ZeroMemory(&p16, sizeof(p16));
            p16.wFormatTag = WAVE_FORMAT_PCM;
            p16.nChannels = 2;
            p16.nSamplesPerSec = 48000;
            p16.wBitsPerSample = 16;
            p16.nBlockAlign = 4;
            p16.nAvgBytesPerSec = 48000 * 4;
            hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                2000000, 0, &p16, NULL);
            if (FAILED(hr)) { s->hr = hr; s->hr_where = "Initialize"; goto fin; }
            s->fmt = p16;
            s->fmt_note = "float32 rechazado, pcm16 48k";
        }
        hr = ac->SetEventHandle(ev);
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "SetEventHandle"; goto fin; }
    }
    else
    {
        IMMDeviceEnumerator *en = NULL;
        IMMDevice *dev = NULL;
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en);
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "CoCreateInstance"; goto fin; }
        hr = en->GetDefaultAudioEndpoint(s->kind == KIND_DEVICE_LOOPBACK ? eRender : eCapture, eConsole, &dev);
        en->Release();
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "GetDefaultAudioEndpoint"; goto fin; }
        hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&ac);
        dev->Release();
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "Activate"; goto fin; }
        hr = ac->GetMixFormat(&mix);
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "GetMixFormat"; goto fin; }
        s->fmt = *mix;
        s->fmt_note = "formato de mezcla del dispositivo";
        hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            s->kind == KIND_DEVICE_LOOPBACK ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                            2000000, 0, mix, NULL);
        CoTaskMemFree(mix);
        if (FAILED(hr)) { s->hr = hr; s->hr_where = "Initialize"; goto fin; }
    }

    hr = ac->GetService(__uuidof(IAudioCaptureClient), (void **)&cap);
    if (FAILED(hr)) { s->hr = hr; s->hr_where = "GetService"; goto fin; }
    hr = ac->Start();
    if (FAILED(hr)) { s->hr = hr; s->hr_where = "Start"; goto fin; }
    s->setup_ok = 1;

    t0 = GetTickCount64();
    while (GetTickCount64() - t0 < (ULONGLONG)(s->secs * 1000))
    {
        UINT32 packet = 0;
        if (ev != NULL) WaitForSingleObject(ev, 100); else Sleep(5);
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0)
        {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devpos = 0, qpc = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, &devpos, &qpc))) break;
            if (s->frames == 0) { s->qpc_first = qpc; s->dev_first = devpos; }
            s->qpc_last = qpc;
            s->dev_last = devpos;
            s->frames += frames;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) s->silent += frames;
            else measure(s, data, frames);
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) s->discont++;
            cap->ReleaseBuffer(frames);
            packet = 0;
        }
    }
    ac->Stop();

fin:
    if (cap != NULL) cap->Release();
    if (ac != NULL) ac->Release();
    if (ev != NULL) CloseHandle(ev);
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    DWORD pid_a = argc > 1 ? (DWORD)atoi(argv[1]) : 0;
    DWORD pid_b = argc > 2 ? (DWORD)atoi(argv[2]) : 0;
    double secs = argc > 3 ? atof(argv[3]) : 4.0;
    Src src[4];
    HANDLE th[4];
    int n = 0, i;
    UINT64 qpc_min = 0;
    LARGE_INTEGER freq;

    QueryPerformanceFrequency(&freq);
    ZeroMemory(src, sizeof(src));

    src[n].name = "salida del sistema (loopback de dispositivo)";
    src[n].kind = KIND_DEVICE_LOOPBACK;
    src[n].secs = secs;
    n++;

    if (pid_a != 0)
    {
        src[n].name = "app A (loopback por proceso)";
        src[n].kind = KIND_PROCESS_LOOPBACK;
        src[n].pid = pid_a;
        src[n].secs = secs;
        n++;
    }
    if (pid_b != 0)
    {
        src[n].name = "app B (loopback por proceso)";
        src[n].kind = KIND_PROCESS_LOOPBACK;
        src[n].pid = pid_b;
        src[n].secs = secs;
        n++;
    }

    src[n].name = "microfono (dispositivo de entrada)";
    src[n].kind = KIND_INPUT_DEVICE;
    src[n].secs = secs;
    n++;

    printf("Abriendo %d fuentes SIMULTANEAS durante %.1f s...\n\n", n, secs);
    for (i = 0; i < n; ++i)
        th[i] = CreateThread(NULL, 0, run_source, &src[i], 0, NULL);
    WaitForMultipleObjects((DWORD)n, th, TRUE, INFINITE);
    for (i = 0; i < n; ++i)
        CloseHandle(th[i]);

    for (i = 0; i < n; ++i)
        if (src[i].setup_ok && (qpc_min == 0 || src[i].qpc_first < qpc_min)) qpc_min = src[i].qpc_first;

    for (i = 0; i < n; ++i)
    {
        Src *s = &src[i];
        printf("[%d] %s\n", i, s->name);
        if (!s->setup_ok)
        {
            printf("    FALLO en %s: hr=0x%08X\n\n", s->hr_where ? s->hr_where : "?", (unsigned)s->hr);
            continue;
        }
        printf("    formato : %u ch, %u Hz, %u bits  (%s)\n",
               (unsigned)s->fmt.nChannels, (unsigned)s->fmt.nSamplesPerSec,
               (unsigned)s->fmt.wBitsPerSample, s->fmt_note ? s->fmt_note : "");
        printf("    frames  : %llu = %.3f s de audio | silencio %llu | discontinuidades %llu\n",
               (unsigned long long)s->frames,
               (double)s->frames / (double)s->fmt.nSamplesPerSec,
               (unsigned long long)s->silent, (unsigned long long)s->discont);
        printf("    pico    : %.4f  -> %s\n", s->peak,
               (s->frames > 0 && s->peak > 0.0001) ? "AUDIO REAL" : (s->frames > 0 ? "silencio" : "SIN DATOS"));
        /* qpcPosition viene en unidades de 100 ns segun la documentacion de WASAPI. */
        printf("    reloj   : primer qpc = +%.3f ms respecto de la fuente mas temprana | span %.3f s\n",
               (double)(s->qpc_first - qpc_min) / 10000.0,
               (double)(s->qpc_last - s->qpc_first) / 10000000.0);
        printf("\n");
    }

    printf("Si todas las fuentes traen frames y sus qpc arrancan a pocos ms unas de otras,\n");
    printf("las pistas se pueden alinear en una linea de tiempo comun sin inventar nada.\n");
    return 0;
}
