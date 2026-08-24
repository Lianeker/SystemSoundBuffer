/* Escritura de WAV (RIFF/PCM, 16 o 24 bits). Tamanos en little-endian
   explicito, sin depender del endianness de la maquina.

   Las muestras entran SIEMPRE como int32 con el rango de la profundidad
   elegida: +-32767 para 16 bits, +-8388607 para 24. Que el llamante no tenga
   que saber empaquetar es lo que evita que la conversion se duplique en tres
   sitios y diverja en uno. */
#include "ssb.h"
#include "ssb_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ssb_wav_t
{
    FILE *f;
    uint32_t channels;
    uint32_t rate;
    uint32_t bytes;   /* por muestra en el fichero: 2 o 3 */
    uint64_t frames;
};

static void i_u32(FILE *f, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(b, 1, 4, f);
}

static void i_u16(FILE *f, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    fwrite(b, 1, 2, f);
}

ssb_res ssb_wav_open_ex(const char *path, uint32_t channels, uint32_t rate,
                        uint32_t bits, ssb_wav **out)
{
    ssb_wav *w;
    uint32_t bytes;
    if (path == NULL || out == NULL || channels == 0 || rate == 0)
        return ssb_err_arg;
    if (bits != 16 && bits != 24)
        return ssb_err_arg;
    bytes = bits / 8;
    w = (ssb_wav *)calloc(1, sizeof(ssb_wav));
    if (w == NULL)
        return ssb_err_mem;
    w->f = fopen(path, "wb");
    if (w->f == NULL)
    {
        free(w);
        return ssb_err_io;
    }
    w->channels = channels;
    w->rate = rate;
    w->bytes = bytes;

    fwrite("RIFF", 1, 4, w->f);
    i_u32(w->f, 0); /* se parchea al cerrar */
    fwrite("WAVEfmt ", 1, 8, w->f);
    i_u32(w->f, 16);
    i_u16(w->f, 1); /* PCM entero */
    i_u16(w->f, (uint16_t)channels);
    i_u32(w->f, rate);
    i_u32(w->f, rate * channels * bytes);
    i_u16(w->f, (uint16_t)(channels * bytes));
    i_u16(w->f, (uint16_t)bits);
    fwrite("data", 1, 4, w->f);
    i_u32(w->f, 0); /* se parchea al cerrar */

    *out = w;
    return ssb_ok;
}

ssb_res ssb_wav_open(const char *path, uint32_t channels, uint32_t rate, ssb_wav **out)
{
    return ssb_wav_open_ex(path, channels, rate, 16, out);
}

uint32_t ssb_wav_bits(const ssb_wav *w)
{
    return (w != NULL) ? w->bytes * 8 : 0;
}

ssb_res ssb_wav_write32(ssb_wav *w, const int32_t *pcm, uint32_t frames)
{
    size_t n, i;
    if (w == NULL || pcm == NULL)
        return ssb_err_arg;
    if (frames == 0)
        return ssb_ok;
    n = (size_t)frames * w->channels;

    if (w->bytes == 2)
    {
        for (i = 0; i < n; ++i)
        {
            int32_t v = pcm[i];
            uint8_t b[2];
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            b[0] = (uint8_t)(v & 0xFF);
            b[1] = (uint8_t)((v >> 8) & 0xFF);
            if (fwrite(b, 1, 2, w->f) != 2)
                return ssb_err_io;
        }
    }
    else
    {
        /* 24 bits es el unico caso del formato donde una muestra no cabe en un
           tipo nativo: van tres bytes, el mas bajo primero. */
        for (i = 0; i < n; ++i)
        {
            int32_t v = pcm[i];
            uint8_t b[3];
            if (v > 8388607) v = 8388607;
            if (v < -8388608) v = -8388608;
            b[0] = (uint8_t)(v & 0xFF);
            b[1] = (uint8_t)((v >> 8) & 0xFF);
            b[2] = (uint8_t)((v >> 16) & 0xFF);
            if (fwrite(b, 1, 3, w->f) != 3)
                return ssb_err_io;
        }
    }
    w->frames += frames;
    return ssb_ok;
}

ssb_res ssb_wav_write(ssb_wav *w, const int16_t *pcm, uint32_t frames)
{
    size_t n, i;
    int32_t *tmp;
    ssb_res res;
    if (w == NULL || pcm == NULL)
        return ssb_err_arg;
    if (frames == 0)
        return ssb_ok;
    n = (size_t)frames * w->channels;
    tmp = (int32_t *)malloc(n * sizeof(int32_t));
    if (tmp == NULL)
        return ssb_err_mem;
    for (i = 0; i < n; ++i)
        tmp[i] = pcm[i];
    res = ssb_wav_write32(w, tmp, frames);
    free(tmp);
    return res;
}

uint64_t ssb_wav_frames(const ssb_wav *w)
{
    return (w != NULL) ? w->frames : 0;
}

ssb_res ssb_wav_close(ssb_wav **wp)
{
    ssb_wav *w;
    uint32_t data_bytes;
    if (wp == NULL || *wp == NULL)
        return ssb_err_arg;
    w = *wp;
    data_bytes = (uint32_t)(w->frames * w->channels * w->bytes);
    if (fseek(w->f, 4, SEEK_SET) == 0)
        i_u32(w->f, 36 + data_bytes);
    if (fseek(w->f, 40, SEEK_SET) == 0)
        i_u32(w->f, data_bytes);
    fclose(w->f);
    free(w);
    *wp = NULL;
    return ssb_ok;
}
