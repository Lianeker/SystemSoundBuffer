/* MP3 en Linux, con LAME cargado en tiempo de ejecucion.
 *
 * En Windows esto lo hace Media Foundation, que viene con el sistema. En Linux
 * no hay codificador de MP3 en la plataforma, asi que hay que usar libmp3lame.
 * Se carga con `dlopen` en vez de enlazarla, y eso no es un rodeo: es lo que
 * mantiene las dos propiedades del proyecto.
 *
 *   - No hace falta libmp3lame-dev para compilar. El binario que se publica se
 *     construye en una maquina que puede no tenerla.
 *   - El binario ARRANCA sin ella. Si no esta, `ssb_encode` devuelve
 *     `ssb_err_platform` y la interfaz se queda con el WAV, que es lo que ya
 *     hacia y lo que dice su mensaje. Enlazarla haria que el programa no
 *     abriese en un sistema sin MP3, que es mucho peor que no tener MP3.
 *   - Y LAME es LGPL: cargarla asi no arrastra sus obligaciones al binario.
 *
 * AAC no se implementa. No hay codificador libre equivalente sin friccion de
 * licencias, y prometer AAC para dar WAV seria peor que decir que no esta.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ lectura de WAV

   Solo hace falta para leer lo que acabamos de escribir, pero `ssb encode`
   tambien acepta ficheros de fuera, asi que se recorren los trozos en vez de
   dar por hecho que `data` esta en el offset 44. */

typedef struct
{
    FILE *f;
    uint32_t channels;
    uint32_t rate;
    uint32_t bytes; /* por muestra: 2 o 3 */
    uint64_t frames;
} i_wav;

static uint32_t i_rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t i_rd16(const uint8_t *b)
{
    return (uint16_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8));
}

static ssb_res i_wav_open(const char *path, i_wav *w)
{
    uint8_t h[12];
    uint8_t ch[8];
    int tiene_fmt = 0;

    memset(w, 0, sizeof(*w));
    w->f = fopen(path, "rb");
    if (w->f == NULL)
        return ssb_err_io;
    if (fread(h, 1, 12, w->f) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0)
    {
        fclose(w->f);
        w->f = NULL;
        return ssb_err_format;
    }

    while (fread(ch, 1, 8, w->f) == 8)
    {
        uint32_t size = i_rd32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0)
        {
            uint8_t fmt[40];
            uint32_t n = (size < sizeof(fmt)) ? size : (uint32_t)sizeof(fmt);
            uint16_t tag, bits;
            if (fread(fmt, 1, n, w->f) != n)
                break;
            tag = i_rd16(fmt);
            w->channels = i_rd16(fmt + 2);
            w->rate = i_rd32(fmt + 4);
            bits = i_rd16(fmt + 14);
            /* 1 es PCM; 0xFFFE es EXTENSIBLE, que para PCM entero se lee
               igual en los campos que aqui se usan. */
            if ((tag != 1 && tag != 0xFFFE) || (bits != 16 && bits != 24))
            {
                fclose(w->f);
                w->f = NULL;
                return ssb_err_format;
            }
            w->bytes = (uint32_t)bits / 8;
            tiene_fmt = 1;
            if (size > n)
                fseek(w->f, (long)(size - n), SEEK_CUR);
        }
        else if (memcmp(ch, "data", 4) == 0)
        {
            if (tiene_fmt == 0 || w->channels == 0)
                break;
            w->frames = (uint64_t)size / ((uint64_t)w->channels * w->bytes);
            return ssb_ok;
        }
        else
        {
            fseek(w->f, (long)size, SEEK_CUR);
        }
        if ((size & 1) != 0)
            fseek(w->f, 1, SEEK_CUR); /* los trozos van a numero par de bytes */
    }

    if (w->f != NULL)
        fclose(w->f);
    w->f = NULL;
    return ssb_err_format;
}

/* ------------------------------------------------------------------ LAME */

typedef struct lame_t lame_t;

typedef struct
{
    void *so;
    lame_t *(*init)(void);
    int (*set_rate)(lame_t *, int);
    int (*set_channels)(lame_t *, int);
    int (*set_brate)(lame_t *, int);
    int (*set_quality)(lame_t *, int);
    int (*init_params)(lame_t *);
    int (*enc_inter)(lame_t *, short *, int, unsigned char *, int);
    int (*enc)(lame_t *, const short *, const short *, int, unsigned char *, int);
    int (*flush)(lame_t *, unsigned char *, int);
    int (*close)(lame_t *);
} i_lame;

static void *i_sym(void *so, const char *name, int *ok)
{
    void *p = dlsym(so, name);
    if (p == NULL)
        *ok = 0;
    return p;
}

static int i_lame_open(i_lame *l)
{
    /* Los nombres que usan las distribuciones. El primero es el habitual. */
    static const char *k_sonames[] = { "libmp3lame.so.0", "libmp3lame.so" };
    uint32_t i;
    int ok = 1;

    memset(l, 0, sizeof(*l));
    for (i = 0; i < sizeof(k_sonames) / sizeof(k_sonames[0]); ++i)
    {
        l->so = dlopen(k_sonames[i], RTLD_LAZY | RTLD_LOCAL);
        if (l->so != NULL)
            break;
    }
    if (l->so == NULL)
        return 0;

    /* Los punteros a funcion no se pueden convertir desde void* en C90 sin
       avisos; se pasa por un objeto del tamano adecuado. */
    *(void **)&l->init = i_sym(l->so, "lame_init", &ok);
    *(void **)&l->set_rate = i_sym(l->so, "lame_set_in_samplerate", &ok);
    *(void **)&l->set_channels = i_sym(l->so, "lame_set_num_channels", &ok);
    *(void **)&l->set_brate = i_sym(l->so, "lame_set_brate", &ok);
    *(void **)&l->set_quality = i_sym(l->so, "lame_set_quality", &ok);
    *(void **)&l->init_params = i_sym(l->so, "lame_init_params", &ok);
    *(void **)&l->enc_inter = i_sym(l->so, "lame_encode_buffer_interleaved", &ok);
    *(void **)&l->enc = i_sym(l->so, "lame_encode_buffer", &ok);
    *(void **)&l->flush = i_sym(l->so, "lame_encode_flush", &ok);
    *(void **)&l->close = i_sym(l->so, "lame_close", &ok);

    if (ok == 0)
    {
        dlclose(l->so);
        l->so = NULL;
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ salida */

#define I_BLOQUE 8192u

const char *ssb_format_ext(ssb_format fmt)
{
    switch (fmt)
    {
    case ssb_fmt_mp3:
        return "mp3";
    case ssb_fmt_m4a:
        return "m4a";
    case ssb_fmt_wav:
    default:
        return "wav";
    }
}

ssb_res ssb_encode(const char *wav_path, const char *out_path,
                   ssb_format fmt, uint32_t target_kbps)
{
    i_wav w;
    i_lame l;
    lame_t *gf;
    FILE *out;
    short *pcm = NULL;
    unsigned char *mp3 = NULL;
    uint32_t mp3cap;
    ssb_res res = ssb_ok;

    if (wav_path == NULL || out_path == NULL)
        return ssb_err_arg;
    if (fmt != ssb_fmt_mp3)
        return ssb_err_platform; /* AAC no, y WAV no pasa por aqui */
    if (target_kbps == 0)
        target_kbps = 192;

    if (i_lame_open(&l) == 0)
        return ssb_err_platform;

    if (i_wav_open(wav_path, &w) != ssb_ok)
    {
        dlclose(l.so);
        return ssb_err_format;
    }
    if (w.channels < 1 || w.channels > 2)
    {
        fclose(w.f);
        dlclose(l.so);
        return ssb_err_format;
    }

    out = fopen(out_path, "wb");
    if (out == NULL)
    {
        fclose(w.f);
        dlclose(l.so);
        return ssb_err_io;
    }

    gf = l.init();
    if (gf == NULL)
    {
        fclose(out);
        fclose(w.f);
        dlclose(l.so);
        return ssb_err_mem;
    }
    l.set_rate(gf, (int)w.rate);
    l.set_channels(gf, (int)w.channels);
    l.set_brate(gf, (int)target_kbps);
    l.set_quality(gf, 2); /* 2 es el "casi lo mejor" que recomienda LAME */
    if (l.init_params(gf) < 0)
    {
        l.close(gf);
        fclose(out);
        fclose(w.f);
        dlclose(l.so);
        return ssb_err_format;
    }

    /* 1.25 * muestras + 7200 es el techo que documenta LAME. */
    mp3cap = (uint32_t)(1.25 * I_BLOQUE) + 7200u;
    pcm = (short *)malloc((size_t)I_BLOQUE * w.channels * sizeof(short));
    mp3 = (unsigned char *)malloc(mp3cap);
    if (pcm == NULL || mp3 == NULL)
        res = ssb_err_mem;

    while (res == ssb_ok)
    {
        uint8_t crudo[I_BLOQUE * 2 * 3];
        size_t quiere = (size_t)I_BLOQUE * w.channels * w.bytes;
        size_t leidos = fread(crudo, 1, quiere, w.f);
        uint32_t muestras, i;
        int n;

        if (leidos == 0)
            break;
        muestras = (uint32_t)(leidos / w.bytes);
        for (i = 0; i < muestras; ++i)
        {
            const uint8_t *p = crudo + (size_t)i * w.bytes;
            if (w.bytes == 2)
            {
                pcm[i] = (short)(int16_t)i_rd16(p);
            }
            else
            {
                /* De 24 a 16 bits: se queda con los dos bytes altos, que es lo
                   mismo que dividir por 256 truncando. */
                int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
                if ((v & 0x800000) != 0)
                    v -= (1 << 24);
                pcm[i] = (short)(v >> 8);
            }
        }

        if (w.channels == 2)
            n = l.enc_inter(gf, pcm, (int)(muestras / 2), mp3, (int)mp3cap);
        else
            n = l.enc(gf, pcm, pcm, (int)muestras, mp3, (int)mp3cap);

        if (n < 0)
        {
            res = ssb_err_format;
            break;
        }
        if (n > 0 && fwrite(mp3, 1, (size_t)n, out) != (size_t)n)
        {
            res = ssb_err_io;
            break;
        }
    }

    if (res == ssb_ok)
    {
        int n = l.flush(gf, mp3, (int)mp3cap);
        if (n > 0 && fwrite(mp3, 1, (size_t)n, out) != (size_t)n)
            res = ssb_err_io;
    }

    free(pcm);
    free(mp3);
    l.close(gf);
    fclose(out);
    fclose(w.f);
    dlclose(l.so);

    if (res != ssb_ok)
        remove(out_path);
    return res;
}
