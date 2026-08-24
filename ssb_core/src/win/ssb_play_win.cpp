/* Reproduccion de un WAV por WASAPI, con pausa.
 *
 * Reproduce EXACTAMENTE el fichero que produce la exportacion, no una version
 * paralela leida del anillo. Asi lo que se oye al dar a Reproducir es, por
 * construccion, lo mismo que se guardaria: si sonara distinto, seria un error
 * que ninguna prueba de exportacion podria detectar.
 *
 * Un hilo propio, como la captura. La pausa no cierra el flujo: solo deja de
 * alimentarlo, para que reanudar sea instantaneo.
 */
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "ssb.h"
#include "ssb_internal.h"
}

struct ssb_play_t
{
    IAudioClient *ac;
    IAudioRenderClient *rc;
    HANDLE ev;
    WAVEFORMATEX *mix;

    FILE *f;
    uint32_t channels;   /* del fichero */
    uint32_t rate;
    uint32_t bytes;      /* por muestra en el fichero: 2 o 3 */
    uint64_t frames;     /* totales del fichero */
    uint64_t done;       /* ya entregados al dispositivo */

    uint32_t out_channels;
    /* Salto pendiente, en frames + 1 (0 = ninguno). Lo pide la interfaz y lo
       aplica el hilo de reproduccion antes de leer: mover el fichero desde
       fuera mientras el otro hilo lo esta leyendo es pedir una carrera. */
    volatile LONG64 seek_to;
    volatile LONG paused;
    volatile LONG stop;
    volatile LONG finished;
    ssb_thread *th;
};

static int i_rd_u32(FILE *f, uint32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4)
        return 0;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int i_rd_u16(FILE *f, uint16_t *v)
{
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2)
        return 0;
    *v = (uint16_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8));
    return 1;
}

static int i_open_wav(ssb_play *p, const char *path)
{
    char tag[4];
    uint32_t sz = 0, rate = 0, brate = 0, dbytes = 0;
    uint16_t fmt = 0, ch = 0, align = 0, bits = 0;

    p->f = fopen(path, "rb");
    if (p->f == NULL)
        return 0;
    if (fread(tag, 1, 4, p->f) != 4 || memcmp(tag, "RIFF", 4) != 0) return 0;
    if (!i_rd_u32(p->f, &sz)) return 0;
    if (fread(tag, 1, 4, p->f) != 4 || memcmp(tag, "WAVE", 4) != 0) return 0;
    if (fread(tag, 1, 4, p->f) != 4 || memcmp(tag, "fmt ", 4) != 0) return 0;
    if (!i_rd_u32(p->f, &sz) || sz != 16) return 0;
    if (!i_rd_u16(p->f, &fmt) || fmt != 1) return 0;
    if (!i_rd_u16(p->f, &ch) || ch == 0) return 0;
    if (!i_rd_u32(p->f, &rate) || rate == 0) return 0;
    if (!i_rd_u32(p->f, &brate)) return 0;
    if (!i_rd_u16(p->f, &align)) return 0;
    if (!i_rd_u16(p->f, &bits) || (bits != 16 && bits != 24)) return 0;
    if (fread(tag, 1, 4, p->f) != 4 || memcmp(tag, "data", 4) != 0) return 0;
    if (!i_rd_u32(p->f, &dbytes)) return 0;

    p->channels = ch;
    p->rate = rate;
    p->bytes = bits / 8u;
    p->frames = (uint64_t)dbytes / ((uint64_t)ch * p->bytes);
    return 1;
}

/* Convierte del fichero al formato de mezcla del dispositivo (float32). */
static void i_fill(ssb_play *p, BYTE *dst, uint32_t frames)
{
    float *out = (float *)dst;
    uint32_t i, c;
    const double fs = (p->bytes > 2) ? 8388608.0 : 32768.0;

    for (i = 0; i < frames; ++i)
    {
        float smp[SSB_MAX_CHANNELS];
        for (c = 0; c < p->channels; ++c)
        {
            int32_t v = 0;
            if (p->bytes == 2)
            {
                uint8_t b[2];
                if (fread(b, 1, 2, p->f) != 2) { smp[c] = 0.0f; continue; }
                v = (int16_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8));
            }
            else
            {
                uint8_t b[3];
                if (fread(b, 1, 3, p->f) != 3) { smp[c] = 0.0f; continue; }
                v = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16));
                if (v & 0x800000)
                    v -= 0x1000000;
            }
            smp[c] = (float)((double)v / fs);
        }
        /* El dispositivo puede pedir mas canales de los que tiene el fichero:
           se replica el ultimo en vez de dejar silencio en los que sobran. */
        for (c = 0; c < p->out_channels; ++c)
            out[(size_t)i * p->out_channels + c] = smp[(c < p->channels) ? c : (p->channels - 1)];
    }
}

static void i_play_thread(void *ctx)
{
    ssb_play *p = (ssb_play *)ctx;
    UINT32 bufsize = 0;
    DWORD task = 0;
    HANDLE mm;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    /* Prioridad de audio: sin esto, un hilo de reproduccion se queda sin turno
       bajo carga y se oyen cortes que no estan en el fichero. */
    mm = AvSetMmThreadCharacteristicsW(L"Audio", &task);
    p->ac->GetBufferSize(&bufsize);
    p->ac->Start();

    while (InterlockedCompareExchange(&p->stop, 0, 0) == 0)
    {
        UINT32 padding = 0, avail;
        BYTE *data = NULL;

        if (WaitForSingleObject(p->ev, 200) != WAIT_OBJECT_0)
            continue;

        /* Salto pedido desde fuera: se aplica aqui, en el unico hilo que toca
           el fichero. */
        {
            LONG64 want = InterlockedExchange64(&p->seek_to, 0);
            if (want > 0)
            {
                uint64_t fr = (uint64_t)(want - 1);
                if (fr > p->frames)
                    fr = p->frames;
                if (fseek(p->f, (long)(44 + fr * p->channels * p->bytes), SEEK_SET) == 0)
                    p->done = fr;
            }
        }
        if (InterlockedCompareExchange(&p->paused, 0, 0) != 0)
            continue;
        if (FAILED(p->ac->GetCurrentPadding(&padding)))
            break;
        avail = bufsize - padding;
        if (avail == 0)
            continue;
        if (p->done >= p->frames)
        {
            /* Se acabo el fichero: se deja vaciar la cola antes de avisar, para
               no cortar la ultima decima de segundo. */
            if (padding == 0)
            {
                InterlockedExchange(&p->finished, 1);
                break;
            }
            continue;
        }
        if ((uint64_t)avail > p->frames - p->done)
            avail = (UINT32)(p->frames - p->done);
        if (FAILED(p->rc->GetBuffer(avail, &data)))
            break;
        i_fill(p, data, avail);
        p->rc->ReleaseBuffer(avail, 0);
        p->done += avail;
    }

    p->ac->Stop();
    if (mm != NULL)
        AvRevertMmThreadCharacteristics(mm);
    CoUninitialize();
}

extern "C" ssb_res ssb_play_open(const char *path, ssb_play **out)
{
    ssb_play *p;
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    HRESULT hr;

    if (path == NULL || out == NULL)
        return ssb_err_arg;
    p = (ssb_play *)calloc(1, sizeof(ssb_play));
    if (p == NULL)
        return ssb_err_mem;

    if (!i_open_wav(p, path))
    {
        if (p->f != NULL) fclose(p->f);
        free(p);
        return ssb_err_io;
    }
    if (p->frames == 0)
    {
        fclose(p->f);
        free(p);
        return ssb_err_empty;
    }

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void **)&en)))
        goto fail;
    hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    en->Release();
    if (FAILED(hr))
        goto fail;
    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&p->ac);
    dev->Release();
    if (FAILED(hr))
        goto fail;
    if (FAILED(p->ac->GetMixFormat(&p->mix)))
        goto fail;

    /* Se reproduce al formato de mezcla del dispositivo. Si su frecuencia no es
       la del fichero, no se remuestrea aqui: se pide a WASAPI que lo haga, que
       para eso tiene el conversor del motor de audio. */
    if (FAILED(p->ac->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                 AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 2000000, 0, p->mix, NULL)))
        goto fail;

    p->out_channels = p->mix->nChannels;
    p->ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (p->ev == NULL)
        goto fail;
    if (FAILED(p->ac->SetEventHandle(p->ev)))
        goto fail;
    if (FAILED(p->ac->GetService(__uuidof(IAudioRenderClient), (void **)&p->rc)))
        goto fail;

    p->th = ssb_thread_start(i_play_thread, p);
    if (p->th == NULL)
        goto fail;

    *out = p;
    return ssb_ok;

fail:
    if (p->rc != NULL) p->rc->Release();
    if (p->ac != NULL) p->ac->Release();
    if (p->ev != NULL) CloseHandle(p->ev);
    if (p->mix != NULL) CoTaskMemFree(p->mix);
    if (p->f != NULL) fclose(p->f);
    free(p);
    return ssb_err_platform;
}

extern "C" void ssb_play_seek(ssb_play *p, double seconds)
{
    uint64_t fr;
    if (p == NULL || p->rate == 0)
        return;
    if (seconds < 0.0)
        seconds = 0.0;
    fr = (uint64_t)(seconds * (double)p->rate);
    if (fr > p->frames)
        fr = p->frames;
    InterlockedExchange64(&p->seek_to, (LONG64)(fr + 1));
}

extern "C" void ssb_play_pause(ssb_play *p, int paused)
{
    if (p != NULL)
        InterlockedExchange(&p->paused, paused ? 1 : 0);
}

extern "C" int ssb_play_paused(const ssb_play *p)
{
    return (p != NULL && p->paused != 0) ? 1 : 0;
}

extern "C" int ssb_play_done(const ssb_play *p)
{
    return (p == NULL || p->finished != 0) ? 1 : 0;
}

extern "C" double ssb_play_position(const ssb_play *p)
{
    if (p == NULL || p->rate == 0)
        return 0.0;
    return (double)p->done / (double)p->rate;
}

extern "C" double ssb_play_duration(const ssb_play *p)
{
    if (p == NULL || p->rate == 0)
        return 0.0;
    return (double)p->frames / (double)p->rate;
}

extern "C" void ssb_play_close(ssb_play **pp)
{
    ssb_play *p;
    if (pp == NULL || *pp == NULL)
        return;
    p = *pp;
    InterlockedExchange(&p->stop, 1);
    if (p->ev != NULL)
        SetEvent(p->ev);
    ssb_thread_join(&p->th);
    if (p->rc != NULL) p->rc->Release();
    if (p->ac != NULL) p->ac->Release();
    if (p->ev != NULL) CloseHandle(p->ev);
    if (p->mix != NULL) CoTaskMemFree(p->mix);
    if (p->f != NULL) fclose(p->f);
    free(p);
    *pp = NULL;
}
