/* ¿La captura depende del volumen de salida del sistema?
 *
 * Es la pregunta que decide todo el diseno de la calidad. Si el loopback trae
 * el audio DESPUES del control de volumen, grabar a volumen bajo pierde bits de
 * verdad y no hay forma de recuperarlos luego. Si lo trae antes, el volumen del
 * sistema es irrelevante para lo que guardamos.
 *
 * No se razona: se mide. Se pone el volumen a varios valores, se captura la
 * misma senal y se compara el nivel.
 *
 * CUIDADO: esto CAMBIA EL VOLUMEN MAESTRO de la maquina.
 *
 * Por eso hay que pedirlo a proposito con --cambiar-volumen. Sin esa bandera no
 * toca nada y solo mide al volumen actual. La primera version no la tenia,
 * murio a mitad de una ejecucion y dejo el volumen del usuario donde estuviera:
 * una herramienta que toca estado global de la maquina no puede depender de
 * llegar viva hasta su ultima linea.
 *
 * El valor original se restaura tambien desde atexit, no solo al final de main.
 * Y si algo sale mal, `vol.exe master <n>` lo pone donde uno quiera.
 *
 *     volumen <pid-de-la-app> [--cambiar-volumen]
 */
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

extern "C" {
#include "ssb.h"
#include "ssb_internal.h"
}

#pragma comment(lib, "ole32.lib")

struct medida
{
    double sum2;
    double peak;
    unsigned long long frames;
    unsigned channels;
};

static void on_audio(void *ctx, const float *pcm, uint32_t frames,
                     ssb_time t, int silent, int disc)
{
    medida *m = (medida *)ctx;
    uint32_t i;
    (void)t; (void)disc;
    if (silent || pcm == NULL)
    {
        m->frames += frames;
        return;
    }
    for (i = 0; i < frames * m->channels; ++i)
    {
        double v = (double)pcm[i];
        double a = (v < 0) ? -v : v;
        m->sum2 += v * v;
        if (a > m->peak)
            m->peak = a;
    }
    m->frames += frames;
}

static IAudioEndpointVolume *abrir_volumen(void)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioEndpointVolume *vol = NULL;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void **)&en)))
        return NULL;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)))
    {
        dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void **)&vol);
        dev->Release();
    }
    en->Release();
    return vol;
}

/* Captura `secs` de la fuente y devuelve nivel eficaz y pico, en dBFS. */
static int medir(const char *spec, double secs, double *rms_db, double *peak_db, unsigned *rate)
{
    ssb_source src;
    ssb_capture *cap = NULL;
    medida m;
    uint32_t ch = 0, rt = 0;

    if (ssb_source_parse(spec, &src) != ssb_ok)
        return 0;
    memset(&m, 0, sizeof(m));
    m.channels = 2;
    if (ssb_capture_open(&src, on_audio, &m, &ch, &rt, &cap) != ssb_ok)
        return 0;
    m.channels = ch;
    ssb_sleep_ms((uint32_t)(secs * 1000.0));
    ssb_capture_close(&cap);
    if (m.frames == 0)
        return 0;
    {
        double n = (double)m.frames * (double)ch;
        double rms = sqrt(m.sum2 / (n > 0 ? n : 1));
        *rms_db = (rms > 0) ? 20.0 * log10(rms) : -999.0;
        *peak_db = (m.peak > 0) ? 20.0 * log10(m.peak) : -999.0;
        *rate = rt;
    }
    return 1;
}

/* Restauracion tambien por atexit: si algo llama a exit() o se sale por un
   camino de error, el volumen del usuario vuelve igual. */
static IAudioEndpointVolume *g_vol = NULL;
static float g_original = -1.0f;

static void i_restaurar(void)
{
    if (g_vol != NULL && g_original >= 0.0f)
    {
        g_vol->SetMasterVolumeLevelScalar(g_original, NULL);
        g_original = -1.0f;
    }
}

int main(int argc, char **argv)
{
    IAudioEndpointVolume *vol = NULL;
    float original = -1.0f;
    int permiso = 0;
    int a;
    const double niveles[3] = { 1.0, 0.50, 0.20 };
    char appspec[64];
    int i;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    vol = abrir_volumen();
    if (vol == NULL)
    {
        printf("no puedo abrir el control de volumen\n");
        return 1;
    }
    for (a = 1; a < argc; ++a)
        if (strcmp(argv[a], "--cambiar-volumen") == 0)
            permiso = 1;

    vol->GetMasterVolumeLevelScalar(&original);
    if (permiso == 0)
    {
        printf("volumen actual: %.0f%%\n", original * 100.0);
        printf("\nNo se toca nada. Barrer varios volumenes hay que pedirlo aparte:\n");
        printf("    volumen <pid> --cambiar-volumen\n");
        vol->Release();
        CoUninitialize();
        return 0;
    }
    g_vol = vol;
    g_original = original;
    atexit(i_restaurar);
    printf("volumen original: %.0f%%  (se restaura al salir, tambien por atexit)\n\n",
           original * 100.0);

    if (argc > 1)
        snprintf(appspec, sizeof(appspec), "app:%s", argv[1]);
    else
        appspec[0] = 0;

    printf("%-8s | %-34s | %-34s\n", "volumen", "loopback del DISPOSITIVO", "loopback por PROCESO");
    printf("%-8s | %-34s | %-34s\n", "--------", "----------------------------------", "----------------------------------");

    for (i = 0; i < 3; ++i)
    {
        double rd = 0, pd = 0, rp = 0, pp = 0;
        unsigned r1 = 0, r2 = 0;
        char ca[40], cb[40];

        vol->SetMasterVolumeLevelScalar((float)niveles[i], NULL);
        ssb_sleep_ms(700);   /* que el motor de audio aplique el cambio */

        if (medir("output", 3.0, &rd, &pd, &r1))
            snprintf(ca, sizeof(ca), "rms %7.2f dB  pico %7.2f dB", rd, pd);
        else
            snprintf(ca, sizeof(ca), "%s", "sin datos");

        if (appspec[0] != 0 && medir(appspec, 3.0, &rp, &pp, &r2))
            snprintf(cb, sizeof(cb), "rms %7.2f dB  pico %7.2f dB", rp, pp);
        else
            snprintf(cb, sizeof(cb), "%s", "sin datos");

        printf("%6.0f%%  | %-34s | %-34s\n", niveles[i] * 100.0, ca, cb);
        fflush(stdout);
    }

    i_restaurar();
    printf("\nvolumen restaurado a %.0f%%\n", original * 100.0);
    printf("\nLectura: si una columna cambia con el volumen, esa captura va DESPUES\n");
    printf("del control y grabar bajo pierde resolucion. Si no cambia, es indiferente.\n");
    vol->Release();
    CoUninitialize();
    return 0;
}
