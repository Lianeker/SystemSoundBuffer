/* Reproduccion de un WAV con PulseAudio, con pausa y salto.
 *
 * Equivale a win/ssb_play_win.cpp: reproduce EXACTAMENTE el fichero que produce
 * la exportacion, no una version paralela leida del anillo. Si sonara distinto,
 * seria un error que ninguna prueba de exportacion podria detectar.
 *
 * Un hilo propio. La pausa no cierra el flujo, solo deja de alimentarlo, para
 * que reanudar sea instantaneo.
 *
 * Se usa la API simple de libpulse y no la asincrona: aqui no hace falta un
 * mainloop propio porque no hay que reaccionar a nada del servidor, solo
 * empujar bytes. El formato que se le pide es el DEL FICHERO —16 o 24 bits—,
 * asi que reproducir es copiar bytes y no hay conversion que pueda estropear
 * nada por el camino.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <pulse/simple.h>
#include <pulse/error.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define I_CHUNK_FRAMES 2048

struct ssb_play_t
{
    pa_simple *pa;
    FILE *f;
    long data_start;

    uint32_t channels;
    uint32_t rate;
    uint32_t bytes; /* por muestra en el fichero: 2 o 3 */
    uint64_t frames;
    uint64_t done;

    ssb_mutex *mtx;
    /* Salto pendiente, en frames + 1 (0 = ninguno). Lo pide la interfaz y lo
       aplica el hilo de reproduccion antes de leer: mover el fichero desde
       fuera mientras el otro hilo lo esta leyendo es pedir una carrera. */
    uint64_t seek_to;
    int paused;
    int stop;
    int finished;

    ssb_thread *th;
    uint8_t *buf;
};

/* ------------------------------------------------------------- lectura WAV */

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

/* Solo lee lo que escribe ssb_wav.c: RIFF/PCM entero, 16 o 24 bits, cabecera
   canonica de 44 bytes. No es un lector de WAV general y no pretende serlo. */
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
    p->data_start = ftell(p->f);
    return (p->frames > 0) ? 1 : 0;
}

/* ------------------------------------------------------------------- el hilo */

static int i_flag(ssb_play *p, const int *field)
{
    int v;
    ssb_mutex_lock(p->mtx);
    v = *field;
    ssb_mutex_unlock(p->mtx);
    return v;
}

static void i_set_flag(ssb_play *p, int *field, int v)
{
    ssb_mutex_lock(p->mtx);
    *field = v;
    ssb_mutex_unlock(p->mtx);
}

static void i_play_thread(void *ctx)
{
    ssb_play *p = (ssb_play *)ctx;
    size_t frame_bytes = (size_t)p->channels * p->bytes;

    for (;;)
    {
        uint64_t seek;
        size_t want, got;
        int err = 0;

        if (i_flag(p, &p->stop) != 0)
            break;

        ssb_mutex_lock(p->mtx);
        seek = p->seek_to;
        p->seek_to = 0;
        ssb_mutex_unlock(p->mtx);

        if (seek != 0)
        {
            uint64_t fr = seek - 1;
            fseek(p->f, p->data_start + (long)(fr * frame_bytes), SEEK_SET);
            ssb_mutex_lock(p->mtx);
            p->done = fr;
            p->finished = 0;
            ssb_mutex_unlock(p->mtx);
            /* Lo que ya estaba en el buffer del servidor pertenece al tramo
               anterior: si no se tira, se oye el sitio del que se venia. */
            pa_simple_flush(p->pa, &err);
        }

        if (i_flag(p, &p->paused) != 0)
        {
            ssb_sleep_ms(15);
            continue;
        }

        if (p->done >= p->frames)
        {
            pa_simple_drain(p->pa, &err);
            i_set_flag(p, &p->finished, 1);
            ssb_sleep_ms(30);
            continue;
        }

        want = I_CHUNK_FRAMES;
        if ((uint64_t)want > p->frames - p->done)
            want = (size_t)(p->frames - p->done);

        got = fread(p->buf, frame_bytes, want, p->f);
        if (got == 0)
        {
            i_set_flag(p, &p->finished, 1);
            ssb_sleep_ms(30);
            continue;
        }

        if (pa_simple_write(p->pa, p->buf, got * frame_bytes, &err) < 0)
        {
            i_set_flag(p, &p->finished, 1);
            break;
        }

        ssb_mutex_lock(p->mtx);
        p->done += got;
        ssb_mutex_unlock(p->mtx);
    }
}

/* --------------------------------------------------------------------- API */

ssb_res ssb_play_open(const char *path, ssb_play **out)
{
    ssb_play *p;
    pa_sample_spec spec;
    pa_buffer_attr attr;
    int err = 0;

    if (path == NULL || out == NULL)
        return ssb_err_arg;
    *out = NULL;

    p = (ssb_play *)calloc(1, sizeof(ssb_play));
    if (p == NULL)
        return ssb_err_mem;

    if (!i_open_wav(p, path))
    {
        ssb_play_close(&p);
        return ssb_err_io;
    }

    p->mtx = ssb_mutex_create();
    p->buf = (uint8_t *)malloc((size_t)I_CHUNK_FRAMES * p->channels * p->bytes);
    if (p->mtx == NULL || p->buf == NULL)
    {
        ssb_play_close(&p);
        return ssb_err_mem;
    }

    spec.format = (p->bytes == 3) ? PA_SAMPLE_S24LE : PA_SAMPLE_S16LE;
    spec.rate = p->rate;
    spec.channels = (uint8_t)p->channels;

    /* Un buffer corto: la barra de reproduccion se dibuja con `done`, que cuenta
       lo ENTREGADO al servidor, no lo ya sonado. Cuanto mas hondo el buffer, mas
       se adelanta la barra a lo que se oye. 200 ms es un compromiso entre eso y
       no quedarse corto de datos. */
    memset(&attr, 0xff, sizeof(attr));
    attr.tlength = (uint32_t)(pa_usec_to_bytes(200000, &spec));

    p->pa = pa_simple_new(NULL, "SystemSoundBuffer", PA_STREAM_PLAYBACK, NULL,
                          "playback", &spec, NULL, &attr, &err);
    if (p->pa == NULL)
    {
        ssb_play_close(&p);
        return ssb_err_platform;
    }

    p->th = ssb_thread_start(i_play_thread, p);
    if (p->th == NULL)
    {
        ssb_play_close(&p);
        return ssb_err_platform;
    }

    *out = p;
    return ssb_ok;
}

void ssb_play_seek(ssb_play *p, double seconds)
{
    uint64_t fr;
    if (p == NULL || p->rate == 0)
        return;
    if (seconds < 0.0)
        seconds = 0.0;
    fr = (uint64_t)(seconds * (double)p->rate);
    if (fr > p->frames)
        fr = p->frames;
    ssb_mutex_lock(p->mtx);
    p->seek_to = fr + 1;
    ssb_mutex_unlock(p->mtx);
}

void ssb_play_pause(ssb_play *p, int paused)
{
    if (p != NULL)
        i_set_flag(p, &p->paused, (paused != 0) ? 1 : 0);
}

int ssb_play_paused(const ssb_play *p)
{
    return (p != NULL && p->paused != 0) ? 1 : 0;
}

int ssb_play_done(const ssb_play *p)
{
    return (p == NULL || p->finished != 0) ? 1 : 0;
}

double ssb_play_position(const ssb_play *p)
{
    if (p == NULL || p->rate == 0)
        return 0.0;
    return (double)p->done / (double)p->rate;
}

double ssb_play_duration(const ssb_play *p)
{
    if (p == NULL || p->rate == 0)
        return 0.0;
    return (double)p->frames / (double)p->rate;
}

void ssb_play_close(ssb_play **pp)
{
    ssb_play *p;
    int err = 0;

    if (pp == NULL || *pp == NULL)
        return;
    p = *pp;

    if (p->th != NULL)
    {
        i_set_flag(p, &p->stop, 1);
        ssb_thread_join(&p->th);
    }
    if (p->pa != NULL)
    {
        pa_simple_flush(p->pa, &err);
        pa_simple_free(p->pa);
        p->pa = NULL;
    }
    if (p->f != NULL)
        fclose(p->f);
    if (p->mtx != NULL)
        ssb_mutex_destroy(&p->mtx);
    free(p->buf);
    free(p);
    *pp = NULL;
}
