/* Mide el nivel que llega de una o varias fuentes, sin tocar NADA del sistema.
 *
 *     nivel <segundos> <fuente> [fuente...]
 *     nivel 8 app:WhatsApp app:msedgewebview2 output
 *
 * Admite VARIAS a la vez, que es como se responde de verdad a "¿por donde sale
 * el audio de esta app?": se capturan todas en la misma ventana de tiempo y se
 * comparan. Con una sola fuente nunca sabes si el silencio es de la fuente o es
 * que en ese momento no estaba sonando nada.
 *
 * Distingue los casos que se confunden entre si cuando una pista sale vacia:
 *   - no llega ningun paquete           -> la fuente no entrega nada
 *   - llegan marcados SILENT            -> hay flujo, el sistema dice silencio
 *   - llegan con los datos a cero       -> hay flujo, pero el audio no va ahi
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern "C" {
#include "ssb.h"
#include "ssb_internal.h"
}

struct medida
{
    double sum2;
    double peak;
    unsigned long long frames;
    unsigned long long silent_frames;
    unsigned long packets;
    unsigned long silent_packets;
    unsigned channels;
};

static void on_audio(void *ctx, const float *pcm, uint32_t frames,
                     ssb_time t, int silent, int disc)
{
    medida *m = (medida *)ctx;
    uint32_t i;
    (void)t; (void)disc;
    m->packets++;
    m->frames += frames;
    if (silent || pcm == NULL)
    {
        m->silent_packets++;
        m->silent_frames += frames;
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
}

int main(int argc, char **argv)
{
    enum { MAXSRC = 8 };
    ssb_source src[MAXSRC];
    ssb_capture *cap[MAXSRC];
    medida m[MAXSRC];
    const char *spec[MAXSRC];
    int n = 0, i;
    double secs;

    if (argc < 3)
    {
        printf("uso: nivel <segundos> <fuente> [fuente...]\n");
        printf("     nivel 8 app:WhatsApp app:msedgewebview2 output\n");
        return 2;
    }
    secs = atof(argv[1]);
    if (secs <= 0.0)
        secs = 5.0;

    for (i = 2; i < argc && n < MAXSRC; ++i)
    {
        uint32_t ch = 0, rt = 0;
        spec[n] = argv[i];
        cap[n] = NULL;
        memset(&m[n], 0, sizeof(m[n]));
        m[n].channels = 2;
        if (ssb_source_parse(spec[n], &src[n]) != ssb_ok)
        {
            printf("  %-28s NO SE ENCUENTRA\n", spec[n]);
            continue;
        }
        if (ssb_capture_open(&src[n], on_audio, &m[n], &ch, &rt, &cap[n]) != ssb_ok)
        {
            printf("  %-28s NO SE PUDO ABRIR (%s)\n", spec[n], src[n].name);
            continue;
        }
        m[n].channels = ch;
        printf("  %-28s abierta a %u Hz: %s\n", spec[n], rt, src[n].name);
        n++;
    }
    if (n == 0)
    {
        printf("\nninguna fuente abierta\n");
        return 1;
    }

    printf("\nmidiendo %.0f s. HAZ SONAR AHORA LO QUE QUIERAS COMPROBAR.\n", secs);
    fflush(stdout);
    ssb_sleep_ms((uint32_t)(secs * 1000.0));

    printf("\n%-26s %9s %8s %10s %10s   %s\n",
           "fuente", "paquetes", "SILENT", "rms dBFS", "pico dBFS", "veredicto");
    for (i = 0; i < n; ++i)
    {
        double rms = 0.0;
        const char *v;
        if (cap[i] != NULL)
            ssb_capture_close(&cap[i]);
        if (m[i].frames > m[i].silent_frames)
        {
            double cnt = (double)(m[i].frames - m[i].silent_frames) * (double)m[i].channels;
            rms = sqrt(m[i].sum2 / (cnt > 0 ? cnt : 1));
        }
        if (m[i].packets == 0)
            v = "NO LLEGA NADA";
        else if (m[i].silent_packets == m[i].packets)
            v = "todo marcado SILENT";
        else if (m[i].peak <= 0.0)
            v = "paquetes con datos a CERO";
        else if (m[i].peak < 0.0005)
            v = "practicamente silencio";
        else
            v = "HAY AUDIO";
        printf("%-26s %9lu %8lu %10.2f %10.2f   %s\n", spec[i],
               m[i].packets, m[i].silent_packets,
               (rms > 0) ? 20.0 * log10(rms) : -999.0,
               (m[i].peak > 0) ? 20.0 * log10(m[i].peak) : -999.0, v);
    }
    printf("\nSi una app suena, su fila dice CERO y la de `output` dice HAY AUDIO,\n");
    printf("entonces su audio no sale por el arbol de procesos de ese pid.\n");
    return 0;
}
