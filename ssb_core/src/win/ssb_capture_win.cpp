/* Captura de audio en Windows: WASAPI.
 *
 * Tres tipos de fuente, los tres verificados en probes/multi.cpp con las cuatro
 * corriendo a la vez:
 *   - salida de un dispositivo   AUDCLNT_STREAMFLAGS_LOOPBACK
 *   - una aplicacion concreta    ActivateAudioInterfaceAsync + PROCESS_LOOPBACK
 *   - un dispositivo de entrada  captura normal
 *
 * Este es el unico fichero C++ del motor, y lo es por COM.
 */
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <audioclientactivationparams.h>
#include <functiondiscoverykeys_devpkey.h>
#include <endpointvolume.h>
#include <mmreg.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern "C" {
#include "ssb.h"
#include "ssb_internal.h"
}

static const GUID SSB_SUBTYPE_FLOAT = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID SSB_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

#define FMT_FLOAT32 0
#define FMT_INT16 1
#define FMT_INT32 2
#define FMT_BAD (-1)

static void i_w2a(const wchar_t *w, char *out, size_t cap)
{
    if (w == NULL)
    {
        if (cap > 0)
            out[0] = 0;
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
    out[cap - 1] = 0;
}

static int i_fmt_kind(const WAVEFORMATEX *f)
{
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return (f->wBitsPerSample == 32) ? FMT_FLOAT32 : FMT_BAD;
    if (f->wFormatTag == WAVE_FORMAT_PCM)
        return (f->wBitsPerSample == 16) ? FMT_INT16 : ((f->wBitsPerSample == 32) ? FMT_INT32 : FMT_BAD);
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE *e = (const WAVEFORMATEXTENSIBLE *)f;
        if (IsEqualGUID(e->SubFormat, SSB_SUBTYPE_FLOAT))
            return (f->wBitsPerSample == 32) ? FMT_FLOAT32 : FMT_BAD;
        if (IsEqualGUID(e->SubFormat, SSB_SUBTYPE_PCM))
            return (f->wBitsPerSample == 16) ? FMT_INT16 : ((f->wBitsPerSample == 32) ? FMT_INT32 : FMT_BAD);
    }
    return FMT_BAD;
}

/* ------------------------------------------------------------ enumeracion */

static IMMDeviceEnumerator *i_enumerator(void)
{
    IMMDeviceEnumerator *en = NULL;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void **)&en)))
        return NULL;
    return en;
}

static void i_device_name(IMMDevice *dev, char *out, size_t cap)
{
    IPropertyStore *ps = NULL;
    PROPVARIANT v;
    out[0] = 0;
    if (FAILED(dev->OpenPropertyStore(STGM_READ, &ps)))
        return;
    PropVariantInit(&v);
    if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
        i_w2a(v.pwszVal, out, cap);
    PropVariantClear(&v);
    ps->Release();
}

static void i_proc_name(DWORD pid, char *out, size_t cap)
{
    HANDLE h;
    out[0] = 0;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h == NULL)
        return;
    {
        wchar_t buf[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, buf, &n))
        {
            wchar_t *slash = wcsrchr(buf, L'\\');
            i_w2a(slash ? slash + 1 : buf, out, cap);
        }
    }
    CloseHandle(h);
}

/* Enumera las sesiones de audio de los dispositivos de salida: es la lista de
   aplicaciones que de verdad estan produciendo audio. */
static uint32_t i_enum_sessions(ssb_source *out, uint32_t cap, uint32_t written)
{
    IMMDeviceEnumerator *en = i_enumerator();
    IMMDeviceCollection *col = NULL;
    UINT n = 0, i;
    uint32_t total = 0;
    DWORD seen[256];
    uint32_t nseen = 0;

    if (en == NULL)
        return 0;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
    {
        en->Release();
        return 0;
    }
    col->GetCount(&n);
    for (i = 0; i < n; ++i)
    {
        IMMDevice *dev = NULL;
        IAudioSessionManager2 *mgr = NULL;
        IAudioSessionEnumerator *se = NULL;
        int count = 0, j;

        char devname[SSB_NAME_MAX];
        if (FAILED(col->Item(i, &dev)))
            continue;
        i_device_name(dev, devname, sizeof(devname));
        if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void **)&mgr)) &&
            SUCCEEDED(mgr->GetSessionEnumerator(&se)) &&
            SUCCEEDED(se->GetCount(&count)))
        {
            for (j = 0; j < count; ++j)
            {
                IAudioSessionControl *sc = NULL;
                IAudioSessionControl2 *sc2 = NULL;
                DWORD pid = 0;
                uint32_t k;
                int dup = 0;

                if (FAILED(se->GetSession(j, &sc)))
                    continue;
                if (SUCCEEDED(sc->QueryInterface(__uuidof(IAudioSessionControl2), (void **)&sc2)))
                {
                    if (SUCCEEDED(sc2->GetProcessId(&pid)) && pid != 0)
                    {
                        for (k = 0; k < nseen; ++k)
                            if (seen[k] == pid)
                                dup = 1;
                        if (!dup && nseen < 256)
                        {
                            seen[nseen++] = pid;
                            if (out != NULL && written + total < cap)
                            {
                                ssb_source *s = &out[written + total];
                                AudioSessionState st = AudioSessionStateInactive;
                                memset(s, 0, sizeof(*s));
                                s->kind = ssb_src_process;
                                s->pid = (uint32_t)pid;
                                i_proc_name(pid, s->name, SSB_NAME_MAX);
                                snprintf(s->id, SSB_NAME_MAX, "%u", (unsigned)pid);
                                /* Por que salida esta sonando, y si suena ahora. */
                                snprintf(s->endpoint, SSB_NAME_MAX, "%s", devname);
                                if (SUCCEEDED(sc->GetState(&st)))
                                    s->active = (st == AudioSessionStateActive) ? 1 : 0;
                            }
                            total++;
                        }
                    }
                    sc2->Release();
                }
                sc->Release();
            }
        }
        if (se != NULL)
            se->Release();
        if (mgr != NULL)
            mgr->Release();
        dev->Release();
    }
    col->Release();
    en->Release();
    return total;
}

static uint32_t i_enum_devices(EDataFlow flow, ssb_src_kind kind, ssb_source *out,
                               uint32_t cap, uint32_t written)
{
    IMMDeviceEnumerator *en = i_enumerator();
    IMMDeviceCollection *col = NULL;
    UINT n = 0, i;

    if (en == NULL)
        return 0;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)))
    {
        en->Release();
        return 0;
    }
    col->GetCount(&n);
    for (i = 0; i < n; ++i)
    {
        IMMDevice *dev = NULL;
        if (FAILED(col->Item(i, &dev)))
            continue;
        if (out != NULL && written + i < cap)
        {
            ssb_source *s = &out[written + i];
            LPWSTR id = NULL;
            memset(s, 0, sizeof(*s));
            s->kind = kind;
            i_device_name(dev, s->name, SSB_NAME_MAX);
            if (SUCCEEDED(dev->GetId(&id)))
            {
                i_w2a(id, s->id, SSB_NAME_MAX);
                CoTaskMemFree(id);
            }
        }
        dev->Release();
    }
    col->Release();
    en->Release();
    return (uint32_t)n;
}

extern "C" uint32_t ssb_enumerate(ssb_source *out, uint32_t cap)
{
    uint32_t n = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    n += i_enum_devices(eRender, ssb_src_output_device, out, cap, n);
    n += i_enum_devices(eCapture, ssb_src_input_device, out, cap, n);
    n += i_enum_sessions(out, cap, n);
    return n;
}

/* Busca la raiz del arbol de procesos cuyo nombre contiene `needle`. Importa:
   WhatsApp corre como WhatsApp.exe bajo WhatsApp.Root.exe, y el loopback por
   proceso con INCLUDE_TARGET_PROCESS_TREE hay que pedirlo sobre la raiz. */
static uint32_t i_find_process(const char *needle)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe;
    DWORD match[256];
    DWORD parent[256];
    char nameb[SSB_NAME_MAX];
    uint32_t nmatch = 0, i, j;
    char low[SSB_NAME_MAX];
    size_t k;

    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    snprintf(low, sizeof(low), "%s", needle);
    for (k = 0; low[k] != 0; ++k)
        low[k] = (char)tolower((unsigned char)low[k]);

    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            size_t m;
            i_w2a(pe.szExeFile, nameb, sizeof(nameb));
            for (m = 0; nameb[m] != 0; ++m)
                nameb[m] = (char)tolower((unsigned char)nameb[m]);
            if (strstr(nameb, low) != NULL && nmatch < 256)
            {
                match[nmatch] = pe.th32ProcessID;
                parent[nmatch] = pe.th32ParentProcessID;
                nmatch++;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    /* De todos los que coinciden, el que manda es el que no tiene un padre que
       tambien coincida. */
    for (i = 0; i < nmatch; ++i)
    {
        int parent_matches = 0;
        for (j = 0; j < nmatch; ++j)
            if (match[j] == parent[i])
                parent_matches = 1;
        if (!parent_matches)
            return (uint32_t)match[i];
    }
    return (nmatch > 0) ? (uint32_t)match[0] : 0;
}

extern "C" ssb_res ssb_source_parse(const char *spec, ssb_source *out)
{
    char kind[32];
    const char *sel;
    const char *colon;
    ssb_source list[256];
    uint32_t n, i, idx = 0;
    int numeric = 1;
    size_t k;

    if (spec == NULL || out == NULL)
        return ssb_err_arg;
    colon = strchr(spec, ':');
    if (colon != NULL)
    {
        size_t len = (size_t)(colon - spec);
        if (len >= sizeof(kind))
            len = sizeof(kind) - 1;
        memcpy(kind, spec, len);
        kind[len] = 0;
        sel = colon + 1;
    }
    else
    {
        snprintf(kind, sizeof(kind), "%s", spec);
        sel = "";
    }

    memset(out, 0, sizeof(*out));
    if (strcmp(kind, "app") == 0)
    {
        out->kind = ssb_src_process;
        for (k = 0; sel[k] != 0; ++k)
            if (sel[k] < '0' || sel[k] > '9')
                numeric = 0;
        if (sel[0] != 0 && numeric)
        {
            out->pid = (uint32_t)strtoul(sel, NULL, 10);
            i_proc_name(out->pid, out->name, SSB_NAME_MAX);
            if (out->name[0] == 0)
                snprintf(out->name, SSB_NAME_MAX, "pid %u", (unsigned)out->pid);
            return ssb_ok;
        }
        out->pid = i_find_process(sel);
        if (out->pid == 0)
            return ssb_err_notfound;
        i_proc_name(out->pid, out->name, SSB_NAME_MAX);
        return ssb_ok;
    }

    if (strcmp(kind, "output") == 0)
        out->kind = ssb_src_output_device;
    else if (strcmp(kind, "input") == 0)
        out->kind = ssb_src_input_device;
    else
        return ssb_err_arg;

    if (sel[0] == 0 || strcmp(sel, "default") == 0)
    {
        /* Se deja el id vacio (que significa "el predeterminado"), pero se copia
           el nombre real del dispositivo: la interfaz ensena "Speakers (Realtek)"
           en vez de una etiqueta generica, y el motor no inventa texto visible. */
        IMMDeviceEnumerator *en = i_enumerator();
        IMMDevice *dev = NULL;
        out->name[0] = 0;
        if (en != NULL)
        {
            if (SUCCEEDED(en->GetDefaultAudioEndpoint(
                    (out->kind == ssb_src_output_device) ? eRender : eCapture, eConsole, &dev)))
            {
                i_device_name(dev, out->name, SSB_NAME_MAX);
                dev->Release();
            }
            en->Release();
        }
        if (out->name[0] == 0)
            snprintf(out->name, SSB_NAME_MAX, "%s",
                     (out->kind == ssb_src_output_device) ? "default output" : "default input");
        return ssb_ok;
    }

    n = ssb_enumerate(list, 256);
    if (n > 256)
        n = 256;
    for (k = 0; sel[k] != 0; ++k)
        if (sel[k] < '0' || sel[k] > '9')
            numeric = 0;
    if (numeric)
        idx = (uint32_t)strtoul(sel, NULL, 10);

    {
        uint32_t seen = 0;
        for (i = 0; i < n; ++i)
        {
            if (list[i].kind != out->kind)
                continue;
            if (numeric)
            {
                if (seen == idx)
                {
                    *out = list[i];
                    return ssb_ok;
                }
                seen++;
            }
            else
            {
                char a[SSB_NAME_MAX], b[SSB_NAME_MAX];
                size_t m;
                snprintf(a, sizeof(a), "%s", list[i].name);
                snprintf(b, sizeof(b), "%s", sel);
                for (m = 0; a[m] != 0; ++m)
                    a[m] = (char)tolower((unsigned char)a[m]);
                for (m = 0; b[m] != 0; ++m)
                    b[m] = (char)tolower((unsigned char)b[m]);
                if (strstr(a, b) != NULL)
                {
                    *out = list[i];
                    return ssb_ok;
                }
            }
        }
    }
    return ssb_err_notfound;
}

extern "C" int ssb_output_muted(const ssb_source *src)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioEndpointVolume *vol = NULL;
    BOOL muted = FALSE;
    float level = 1.0f;
    int res = 0;
    int couninit = 0;

    if (src == NULL || src->kind != ssb_src_output_device)
        return 0;

    /* Ojo con el equilibrio: esta funcion se llamaba una vez por segundo y por
       pista, y hacia CoInitializeEx SIN su CoUninitialize. Cada llamada dejaba
       la cuenta de COM del hilo un punto mas arriba, para siempre. */
    couninit = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED)) ? 1 : 0;
    en = i_enumerator();
    if (en == NULL)
    {
        if (couninit)
            CoUninitialize();
        return 0;
    }
    if (src->id[0] == 0)
    {
        en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    }
    else
    {
        wchar_t wid[SSB_NAME_MAX * 2];
        MultiByteToWideChar(CP_UTF8, 0, src->id, -1, wid, SSB_NAME_MAX * 2);
        en->GetDevice(wid, &dev);
    }
    if (dev != NULL)
    {
        if (SUCCEEDED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void **)&vol)))
        {
            vol->GetMute(&muted);
            vol->GetMasterVolumeLevelScalar(&level);
            res = (muted || level <= 0.0001f) ? 1 : 0;
            vol->Release();
        }
        dev->Release();
    }
    en->Release();
    if (couninit)
        CoUninitialize();
    return res;
}

/* ---------------------------------------------------- activacion por proceso */

class ActHandler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
{
public:
    HANDLE done;
    ActHandler() { done = CreateEventW(NULL, FALSE, FALSE, NULL); }
    ~ActHandler()
    {
        if (done != NULL)
            CloseHandle(done);
    }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *op)
    {
        (void)op;
        SetEvent(done);
        return S_OK;
    }
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

/* ------------------------------------------------------------------ captura */

struct ssb_capture_t
{
    ssb_source src;
    ssb_audio_fn fn;
    void *ctx;

    IAudioClient *ac;
    IAudioCaptureClient *cap;
    HANDLE ev;

    WAVEFORMATEX *mix; /* propiedad nuestra si vino de GetMixFormat */
    WAVEFORMATEXTENSIBLE own;
    int kind;
    uint32_t src_channels;
    uint32_t out_channels;
    uint32_t rate;

    float *buf;
    uint32_t buf_frames;

    /* Ultima marca de tiempo buena, para poder continuar desde ella cuando
       WASAPI avisa de que la del paquete no vale. Ver i_stamp. */
    UINT64 last_stamp;
    UINT32 last_frames;
    int have_stamp;

    volatile LONG stop;
    ssb_thread *th;
};

static ssb_res i_open_device(ssb_capture *c, const ssb_source *src)
{
    IMMDeviceEnumerator *en = i_enumerator();
    IMMDevice *dev = NULL;
    HRESULT hr;
    int loopback = (src->kind == ssb_src_output_device);

    if (en == NULL)
        return ssb_err_platform;
    if (src->id[0] == 0)
    {
        hr = en->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &dev);
    }
    else
    {
        wchar_t wid[SSB_NAME_MAX * 2];
        MultiByteToWideChar(CP_UTF8, 0, src->id, -1, wid, SSB_NAME_MAX * 2);
        hr = en->GetDevice(wid, &dev);
    }
    en->Release();
    if (FAILED(hr))
        return ssb_err_notfound;

    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&c->ac);
    dev->Release();
    if (FAILED(hr))
        return ssb_err_platform;

    if (FAILED(c->ac->GetMixFormat(&c->mix)))
        return ssb_err_platform;
    c->kind = i_fmt_kind(c->mix);
    if (c->kind == FMT_BAD)
        return ssb_err_platform;
    c->src_channels = c->mix->nChannels;
    c->rate = c->mix->nSamplesPerSec;

    hr = c->ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                           loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                           2000000, 0, c->mix, NULL);
    if (FAILED(hr))
        return ssb_err_platform;
    return ssb_ok;
}

static ssb_res i_open_process(ssb_capture *c, const ssb_source *src)
{
    AUDIOCLIENT_ACTIVATION_PARAMS ap;
    PROPVARIANT pv;
    ActHandler h;
    IActivateAudioInterfaceAsyncOperation *op = NULL;
    HRESULT hr, act = E_FAIL;
    IUnknown *unk = NULL;

    memset(&ap, 0, sizeof(ap));
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = (DWORD)src->pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = (BYTE *)&ap;

    hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                     __uuidof(IAudioClient), &pv, &h, &op);
    if (FAILED(hr))
        return ssb_err_platform;
    WaitForSingleObject(h.done, 5000);
    hr = op->GetActivateResult(&act, &unk);
    op->Release();
    if (FAILED(hr) || FAILED(act) || unk == NULL)
        return ssb_err_platform;
    c->ac = (IAudioClient *)unk;

    /* Este cliente no soporta GetMixFormat: hay que dar el formato. float32 a
       48 kHz esta verificado en probes/multi.cpp ("float32 48k aceptado"). */
    memset(&c->own, 0, sizeof(c->own));
    c->own.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    c->own.Format.nChannels = 2;
    c->own.Format.nSamplesPerSec = 48000;
    c->own.Format.wBitsPerSample = 32;
    c->own.Format.nBlockAlign = 8;
    c->own.Format.nAvgBytesPerSec = 48000 * 8;
    c->own.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    c->own.Samples.wValidBitsPerSample = 32;
    c->own.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    c->own.SubFormat = SSB_SUBTYPE_FLOAT;

    c->ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    hr = c->ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                           AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                           2000000, 0, (WAVEFORMATEX *)&c->own, NULL);
    if (FAILED(hr))
        return ssb_err_platform;
    if (FAILED(c->ac->SetEventHandle(c->ev)))
        return ssb_err_platform;

    c->kind = FMT_FLOAT32;
    c->src_channels = 2;
    c->rate = 48000;
    return ssb_ok;
}

static void i_convert(ssb_capture *c, const BYTE *data, uint32_t frames)
{
    uint32_t i, ch;
    for (i = 0; i < frames; ++i)
    {
        for (ch = 0; ch < c->out_channels; ++ch)
        {
            size_t si = (size_t)i * c->src_channels + ch;
            float v = 0.0f;
            if (c->kind == FMT_FLOAT32)
                v = ((const float *)data)[si];
            else if (c->kind == FMT_INT16)
                v = (float)((const int16_t *)data)[si] / 32768.0f;
            else
                v = (float)((const int32_t *)data)[si] / 2147483648.0f;
            c->buf[(size_t)i * c->out_channels + ch] = v;
        }
    }
}

/* Sustituye la marca de tiempo de un paquete cuando WASAPI avisa de que no vale.
 *
 * Con AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR puesta, el qpcPosition que acompana al
 * paquete NO es de fiar. Pasarlo tal cual aguas arriba es peligroso: la pista
 * usa las marcas para decidir si falta audio, y una disparatada le haria rehacer
 * su linea de tiempo y meter un hueco de silencio donde no falta nada.
 *
 * Aqui no se sabe nada de esa linea de tiempo, asi que se hace lo unico que es
 * local y siempre seguro: continuar desde el paquete anterior contando los
 * frames que trajo. Si esa continuacion se desviase de la realidad, la pista lo
 * veria como una desviacion sostenida y se re-anclaria por su cuenta; lo que se
 * evita es el salto de un solo paquete.
 *
 * En esta maquina la bandera no aparecio ni una vez en 2499 paquetes
 * (probes/devpos.cpp), asi que esto es proteccion para otro hardware, no el
 * arreglo de algo observado. */
static UINT64 i_stamp(ssb_capture *c, UINT64 qpc, DWORD flags, UINT32 frames)
{
    if ((flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0 && c->have_stamp != 0)
        qpc = c->last_stamp + (UINT64)c->last_frames * 10000000ull / (UINT64)c->rate;
    c->last_stamp = qpc;
    c->last_frames = frames;
    c->have_stamp = 1;
    return qpc;
}

static void i_capture_thread(void *ctx)
{
    ssb_capture *c = (ssb_capture *)ctx;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    c->ac->Start();
    while (InterlockedCompareExchange(&c->stop, 0, 0) == 0)
    {
        UINT32 packet = 0;
        if (c->ev != NULL)
            WaitForSingleObject(c->ev, 100);
        else
            Sleep(5);
        while (SUCCEEDED(c->cap->GetNextPacketSize(&packet)) && packet > 0)
        {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devpos = 0, qpc = 0;
            if (FAILED(c->cap->GetBuffer(&data, &frames, &flags, &devpos, &qpc)))
                break;
            if (frames > c->buf_frames)
            {
                float *nb = (float *)realloc(c->buf, (size_t)frames * c->out_channels * sizeof(float));
                if (nb != NULL)
                {
                    c->buf = nb;
                    c->buf_frames = frames;
                }
                else
                {
                    frames = c->buf_frames;
                }
            }
            if (frames > 0)
            {
                int silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 1 : 0;
                int disc = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ? 1 : 0;
                UINT64 stamp = i_stamp(c, qpc, flags, frames);
                if (!silent)
                    i_convert(c, data, frames);
                c->fn(c->ctx, c->buf, frames, (ssb_time)stamp, silent, disc);
            }
            c->cap->ReleaseBuffer(frames);
            packet = 0;
        }
    }

    /* Vaciado final antes de parar: el cliente todavia tiene hasta 200 ms de
       audio ya capturado esperando a que lo recojamos. Salir sin leerlo pierde
       el final de la grabacion, y con sonidos cortos se nota. */
    {
        UINT32 packet = 0;
        while (SUCCEEDED(c->cap->GetNextPacketSize(&packet)) && packet > 0)
        {
            BYTE *data = NULL;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devpos = 0, qpc = 0;
            if (FAILED(c->cap->GetBuffer(&data, &frames, &flags, &devpos, &qpc)))
                break;
            if (frames > 0 && frames <= c->buf_frames)
            {
                /* Tambien aqui la discontinuidad es real y hay que propagarla:
                   pasar 0 fijo perdia la unica senal de que la fuente habia
                   perdido audio justo antes de parar. */
                int silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 1 : 0;
                int disc = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ? 1 : 0;
                UINT64 stamp = i_stamp(c, qpc, flags, frames);
                if (!silent)
                    i_convert(c, data, frames);
                c->fn(c->ctx, c->buf, frames, (ssb_time)stamp, silent, disc);
            }
            c->cap->ReleaseBuffer(frames);
            packet = 0;
        }
    }

    c->ac->Stop();
    CoUninitialize();
}

extern "C" ssb_res ssb_capture_open(const ssb_source *src, ssb_audio_fn fn, void *ctx,
                                    uint32_t *channels, uint32_t *rate, ssb_capture **out)
{
    ssb_capture *c;
    ssb_res res;

    if (src == NULL || fn == NULL || out == NULL || channels == NULL || rate == NULL)
        return ssb_err_arg;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    c = (ssb_capture *)calloc(1, sizeof(ssb_capture));
    if (c == NULL)
        return ssb_err_mem;
    c->src = *src;
    c->fn = fn;
    c->ctx = ctx;

    res = (src->kind == ssb_src_process) ? i_open_process(c, src) : i_open_device(c, src);
    if (res != ssb_ok)
    {
        ssb_capture_close(&c);
        return res;
    }

    c->out_channels = (c->src_channels > SSB_MAX_CHANNELS) ? SSB_MAX_CHANNELS : c->src_channels;
    if (FAILED(c->ac->GetService(__uuidof(IAudioCaptureClient), (void **)&c->cap)))
    {
        ssb_capture_close(&c);
        return ssb_err_platform;
    }

    {
        UINT32 bufframes = 0;
        c->ac->GetBufferSize(&bufframes);
        c->buf_frames = (bufframes > 0) ? bufframes : c->rate;
        c->buf = (float *)calloc((size_t)c->buf_frames * c->out_channels, sizeof(float));
        if (c->buf == NULL)
        {
            ssb_capture_close(&c);
            return ssb_err_mem;
        }
    }

    *channels = c->out_channels;
    *rate = c->rate;

    c->th = ssb_thread_start(i_capture_thread, c);
    if (c->th == NULL)
    {
        ssb_capture_close(&c);
        return ssb_err_platform;
    }
    *out = c;
    return ssb_ok;
}

extern "C" void ssb_capture_close(ssb_capture **cp)
{
    ssb_capture *c;
    if (cp == NULL || *cp == NULL)
        return;
    c = *cp;
    if (c->th != NULL)
    {
        InterlockedExchange(&c->stop, 1);
        if (c->ev != NULL)
            SetEvent(c->ev);
        ssb_thread_join(&c->th);
    }
    if (c->cap != NULL)
        c->cap->Release();
    if (c->ac != NULL)
        c->ac->Release();
    if (c->ev != NULL)
        CloseHandle(c->ev);
    if (c->mix != NULL)
        CoTaskMemFree(c->mix);
    free(c->buf);
    free(c);
    *cp = NULL;
}
