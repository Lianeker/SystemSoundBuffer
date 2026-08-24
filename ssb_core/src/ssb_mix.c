/* Mezcla de varios WAV ya exportados en uno solo.
 *
 * Las entradas NO tienen por que traer los mismos canales: un microfono es mono
 * y una salida es estereo, y grabar los dos a la vez es lo normal. La mono se
 * reparte por igual entre los canales de la salida. Antes esto se negaba, y el
 * resultado era que en cuanto anadias un microfono no se podia ni exportar en
 * un fichero ni reproducir nada.
 *
 * Va DESPUES de la exportacion normal, no en paralelo a ella: cada pista se
 * escribe con el mismo camino de siempre —ventana comun, huecos, recorte,
 * silenciadas— y esto solo suma los resultados. Asi la mezcla no puede diferir
 * de lo que se oye al exportar por separado, que es el error facil.
 *
 * Solo lee lo que escribe ssb_wav.c: RIFF/PCM entero, 16 o 24 bits, cabecera
 * canonica de 44 bytes. No es un lector de WAV general y no pretende serlo.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    FILE *f;
    uint32_t channels;
    uint32_t rate;
    uint32_t bytes;
    uint64_t frames;
} i_wavr;

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

static int i_wavr_open(const char *path, i_wavr *w)
{
    char tag[4];
    uint32_t sz = 0, rate = 0, brate = 0, dbytes = 0;
    uint16_t fmt = 0, ch = 0, align = 0, bits = 0;

    memset(w, 0, sizeof(*w));
    w->f = fopen(path, "rb");
    if (w->f == NULL)
        return 0;
    if (fread(tag, 1, 4, w->f) != 4 || memcmp(tag, "RIFF", 4) != 0) goto bad;
    if (!i_rd_u32(w->f, &sz)) goto bad;
    if (fread(tag, 1, 4, w->f) != 4 || memcmp(tag, "WAVE", 4) != 0) goto bad;
    if (fread(tag, 1, 4, w->f) != 4 || memcmp(tag, "fmt ", 4) != 0) goto bad;
    if (!i_rd_u32(w->f, &sz) || sz != 16) goto bad;
    if (!i_rd_u16(w->f, &fmt) || fmt != 1) goto bad;
    if (!i_rd_u16(w->f, &ch) || ch == 0) goto bad;
    if (!i_rd_u32(w->f, &rate) || rate == 0) goto bad;
    if (!i_rd_u32(w->f, &brate)) goto bad;
    if (!i_rd_u16(w->f, &align)) goto bad;
    if (!i_rd_u16(w->f, &bits) || (bits != 16 && bits != 24)) goto bad;
    if (fread(tag, 1, 4, w->f) != 4 || memcmp(tag, "data", 4) != 0) goto bad;
    if (!i_rd_u32(w->f, &dbytes)) goto bad;

    w->channels = ch;
    w->rate = rate;
    w->bytes = bits / 8u;
    w->frames = (uint64_t)dbytes / ((uint64_t)ch * w->bytes);
    return 1;
bad:
    fclose(w->f);
    w->f = NULL;
    return 0;
}

/* Lee hasta `frames` a int32, en la escala de SU propia profundidad. */
static uint32_t i_wavr_read(i_wavr *w, int32_t *out, uint32_t frames)
{
    uint32_t got = 0;
    uint32_t i;
    size_t n = (size_t)frames * w->channels;
    if (w->bytes == 2)
    {
        for (i = 0; i < n; ++i)
        {
            uint8_t b[2];
            if (fread(b, 1, 2, w->f) != 2)
                break;
            out[i] = (int16_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8));
            got++;
        }
    }
    else
    {
        for (i = 0; i < n; ++i)
        {
            uint8_t b[3];
            int32_t v;
            if (fread(b, 1, 3, w->f) != 3)
                break;
            v = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16));
            if (v & 0x800000)
                v -= 0x1000000;
            out[i] = v;
            got++;
        }
    }
    return got / w->channels;
}

ssb_res ssb_mix_wavs(const char **paths, uint32_t n, const char *out, double *gain_used)
{
    i_wavr *in = NULL;
    int32_t *buf = NULL;
    int64_t *acc = NULL;
    ssb_wav *w = NULL;
    uint32_t i, ch = 0, rate = 0, bits = 0;
    uint64_t frames = 0;
    int64_t peak = 0;
    double gain = 1.0;
    double fs = 32767.0;
    ssb_res res = ssb_ok;
    int pass;
    const uint32_t CHUNK = SSB_BLOCK_FRAMES;

    if (paths == NULL || out == NULL || n == 0)
        return ssb_err_arg;

    in = (i_wavr *)calloc(n, sizeof(i_wavr));
    if (in == NULL)
        return ssb_err_mem;

    for (i = 0; i < n; ++i)
    {
        if (!i_wavr_open(paths[i], &in[i]))
        {
            res = ssb_err_io;
            goto done;
        }
        if (i == 0)
        {
            ch = in[i].channels;
            rate = in[i].rate;
            bits = in[i].bytes * 8u;
            frames = in[i].frames;
        }
        else
        {
            /* La frecuencia SI tiene que coincidir: juntar 44100 con 48000 sin
               remuestrear pondria una de las dos a otra velocidad. Se dice con
               un error propio para que quien llame pueda explicarlo. */
            if (in[i].rate != rate)
            {
                res = ssb_err_format;
                goto done;
            }
            /* Los canales no: la salida lleva los del que mas tenga, y una
               entrada mono se reparte entre todos. */
            if (in[i].channels > ch)
                ch = in[i].channels;
            /* La profundidad de la mezcla es la MAYOR de las entradas: bajarla
               tiraria bits que alguien pidio expresamente conservar. */
            if (in[i].bytes * 8u > bits)
                bits = in[i].bytes * 8u;
            if (in[i].frames < frames)
                frames = in[i].frames;
        }
    }

    /* Lo unico que se sabe repartir es mono -> N. Cualquier otra combinacion
       (estereo dentro de una mezcla de 4, por ejemplo) necesitaria decidir que
       canal va donde, y eso no se adivina. */
    for (i = 0; i < n; ++i)
    {
        if (in[i].channels != ch && in[i].channels != 1)
        {
            res = ssb_err_format;
            goto done;
        }
    }

    fs = (bits > 16) ? 8388607.0 : 32767.0;
    /* Un hueco de lectura POR ENTRADA. Se leen todas antes de sumar ninguna:
       leyendo y sumando a la vez, la primera entrada que se quedaba corta
       encogia el tramo para las siguientes, pero las anteriores ya habian
       avanzado su fichero de mas y a partir de ahi iban desplazadas. */
    buf = (int32_t *)malloc((size_t)n * CHUNK * SSB_MAX_CHANNELS * sizeof(int32_t));
    acc = (int64_t *)malloc((size_t)CHUNK * ch * sizeof(int64_t));
    if (buf == NULL || acc == NULL)
    {
        res = ssb_err_mem;
        goto done;
    }

    /* Dos pasadas: la primera SOLO mide el pico de la suma, la segunda escribe
       ya con la ganancia que haga falta. Recortar al vuelo distorsionaria, y
       fijar la ganancia de antemano (dividir por el numero de pistas, por
       ejemplo) bajaria el volumen aunque no hiciera ninguna falta. */
    for (pass = 0; pass < 2; ++pass)
    {
        uint64_t done_frames = 0;

        for (i = 0; i < n; ++i)
        {
            if (fseek(in[i].f, 44, SEEK_SET) != 0)
            {
                res = ssb_err_io;
                goto done;
            }
        }
        if (pass == 1)
        {
            res = ssb_wav_open_ex(out, ch, rate, bits, &w);
            if (res != ssb_ok)
                goto done;
        }

        while (done_frames < frames)
        {
            uint32_t want = (uint32_t)((frames - done_frames > (uint64_t)CHUNK)
                                       ? CHUNK : (uint32_t)(frames - done_frames));
            uint32_t use = want;
            uint32_t k, c, fr;
            size_t total;

            /* Primero leer TODAS, luego sumar el tramo que todas cubren. */
            for (i = 0; i < n; ++i)
            {
                uint32_t got = i_wavr_read(&in[i], buf + (size_t)i * CHUNK * SSB_MAX_CHANNELS, want);
                if (got < use)
                    use = got;
            }
            if (use == 0)
                break;

            memset(acc, 0, (size_t)use * ch * sizeof(int64_t));
            for (i = 0; i < n; ++i)
            {
                const int32_t *src = buf + (size_t)i * CHUNK * SSB_MAX_CHANNELS;
                double scale = fs / ((in[i].bytes > 2) ? 8388607.0 : 32767.0);
                uint32_t sch = in[i].channels;
                for (fr = 0; fr < use; ++fr)
                {
                    for (c = 0; c < ch; ++c)
                    {
                        /* Mono: la misma muestra a todos los canales. */
                        int32_t v = src[(size_t)fr * sch + (sch == 1 ? 0 : c)];
                        acc[(size_t)fr * ch + c] += (int64_t)((double)v * scale);
                    }
                }
            }
            want = use;
            total = (size_t)want * ch;

            if (pass == 0)
            {
                for (k = 0; k < total; ++k)
                {
                    int64_t v = (acc[k] < 0) ? -acc[k] : acc[k];
                    if (v > peak)
                        peak = v;
                }
            }
            else
            {
                for (k = 0; k < total; ++k)
                    buf[k] = (int32_t)((double)acc[k] * gain);
                res = ssb_wav_write32(w, buf, want);
                if (res != ssb_ok)
                    goto done;
            }
            done_frames += want;
        }

        if (pass == 0 && (double)peak > fs)
            gain = fs / (double)peak;
    }

done:
    if (w != NULL)
        ssb_wav_close(&w);
    if (in != NULL)
    {
        for (i = 0; i < n; ++i)
            if (in[i].f != NULL)
                fclose(in[i].f);
    }
    free(in);
    free(buf);
    free(acc);
    if (gain_used != NULL)
        *gain_used = gain;
    return res;
}
