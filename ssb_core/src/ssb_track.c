/* Pista: una fuente, un hilo de captura, un buffer circular y un mapa de picos.
 *
 * El hilo de captura hace todo el camino caliente: convertir a int16, acumular
 * un bloque, comprimirlo y escribirlo. Se puede permitir porque el cliente de
 * captura tiene 200 ms de buffer y un bloque son 85 ms de audio que se
 * comprimen en decenas de microsegundos (§5 de docs/01).
 *
 * El mutex protege el indice del ring y el mapa de picos, que es lo unico que
 * lee el hilo de interfaz.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Red de seguridad de la linea de tiempo (ver i_on_audio).
 *
 * El umbral no separa por si solo el temblor del reloj de un hueco real: el
 * temblor medido llega a 4.2 ms de desviacion y el hueco mas pequeno que hemos
 * visto son 12.7 ms, demasiado cerca para fiarlo a un numero. Lo que los separa
 * es la PERSISTENCIA: el temblor vuelve a su sitio en uno o dos paquetes, un
 * hueco real no vuelve nunca. Por eso hacen falta las dos condiciones. */
#define SSB_TIMELINE_SLACK_MS 8
#define SSB_TIMELINE_RUNS     3

struct ssb_track_t
{
    char name[SSB_NAME_MAX];
    char dir[480];
    ssb_source src;
    ssb_track_config cfg;

    ssb_capture *cap;
    uint32_t channels;
    uint32_t rate;

    ssb_mutex *mtx;
    ssb_ring *ring;

    int32_t *acc;       /* acumulador de un bloque, siempre int32 */
    uint32_t bits;         /* 16 o 24: resolucion con la que se guarda */
    uint32_t sample_bytes; /* lo que ocupa una muestra en el modo crudo */
    double full_scale;     /* +-valor maximo de esa resolucion */
    double peak_div;       /* full_scale/32767: para normalizar los picos */
    uint32_t acc_frames;
    ssb_time acc_time;  /* instante del primer frame del acumulador, en la
                           linea de tiempo de colocacion (no el reportado) */
    uint8_t *encbuf;

    int anchored;
    ssb_time anchor;        /* instante reportado del primer paquete */
    ssb_time last_reported; /* instante reportado del ultimo paquete */
    uint64_t frames;        /* frames aceptados en total */
    double drift_ms;

    /* Linea de tiempo de COLOCACION, distinta del ancla de diagnostico de
       arriba. Los frames se situan contando desde `tl_anchor`, no por la marca
       de cada paquete. Ver el comentario largo en i_on_audio. */
    ssb_time tl_anchor;
    uint64_t tl_frames;   /* valor de `frames` cuando se fijo `tl_anchor` */
    uint32_t tl_off_runs; /* paquetes seguidos desviados mas de la cuenta */
    uint32_t reanchors;   /* veces que se rehizo el ancla: huecos reales */
    int tl_reanchor;      /* forzar el salto en el proximo paquete (reanudar) */

    ssb_peak *peaks;
    uint32_t peaks_cap;
    uint32_t peaks_head;
    uint32_t peaks_count;
    uint64_t peaks_first_frame;
    int32_t pk_min;
    int32_t pk_max;
    uint32_t pk_count;
    ssb_time pk_time; /* instante del primer frame del pico en curso */

    ssb_drift_sample *drift;
    uint32_t drift_head;
    uint32_t drift_count;

    uint64_t raw_bytes;
    uint64_t disk_bytes;
    uint64_t filled_frames;
    uint32_t silent_blocks;
    uint32_t discont;
    double peak_level;   /* pico desde que arranco: para estadisticas */
    /* Nivel INSTANTANEO, con caida. El pico historico no sirve para mirar si
       algo esta llegando ahora: en cuanto suena algo fuerte una vez, se queda
       arriba para siempre y deja de informar. */
    double level;

    int frozen;
    /* Hasta que no esta a 1, el callback de captura no toca nada. Ver
       `ssb_track_create` y la guarda al principio de `i_on_audio`. */
    int ready;
    int paused;
    ssb_time pause_at; /* se acepta hasta este instante, no mas */
    ssb_time frozen_at;
};

void ssb_track_config_default(ssb_track_config *cfg)
{
    if (cfg == NULL)
        return;
    cfg->max_seconds = 300;                     /* 5 minutos */
    cfg->max_bytes = 512ull * 1024ull * 1024ull; /* techo de disco por pista */
    cfg->segment_bytes = 4u * 1024u * 1024u;
    cfg->compress = 1;
    cfg->bits = 16;
}

/* ------------------------------------------------------------------- picos */

static void i_peak_push(ssb_track *t, ssb_time when, int32_t mn, int32_t mx)
{
    ssb_peak *p;
    if (t->peaks_count == t->peaks_cap)
    {
        t->peaks_head = (t->peaks_head + 1) % t->peaks_cap;
        t->peaks_count--;
        t->peaks_first_frame += SSB_PEAK_FRAMES;
    }
    p = &t->peaks[(t->peaks_head + t->peaks_count) % t->peaks_cap];
    p->t = when;
    p->min = (int16_t)mn;
    p->max = (int16_t)mx;
    t->peaks_count++;
}

/* ------------------------------------------------------ serie de deriva */

static void i_drift_push(ssb_track *t, ssb_time reported, ssb_time expected,
                         uint32_t packet, int silent)
{
    ssb_drift_sample *d;
    if (t->drift == NULL)
        return;
    if (t->drift_count == SSB_DRIFT_SAMPLES)
    {
        t->drift_head = (t->drift_head + 1) % SSB_DRIFT_SAMPLES;
        t->drift_count--;
    }
    d = &t->drift[(t->drift_head + t->drift_count) % SSB_DRIFT_SAMPLES];
    d->reported = reported;
    d->expected = expected;
    d->frames = t->frames;
    d->packet = packet;
    d->drift_ms = (double)((int64_t)(reported - expected)) / 10000.0;
    d->silent = silent;
    t->drift_count++;
}

uint32_t ssb_track_drift(ssb_track *t, ssb_drift_sample *out, uint32_t cap)
{
    uint32_t n = 0;
    if (t == NULL || out == NULL || cap == 0)
        return 0;
    ssb_mutex_lock(t->mtx);
    while (n < cap && n < t->drift_count)
    {
        out[n] = t->drift[(t->drift_head + n) % SSB_DRIFT_SAMPLES];
        n++;
    }
    ssb_mutex_unlock(t->mtx);
    return n;
}

/* --------------------------------------------------------------- bloques */

static void i_flush_block(ssb_track *t)
{
    size_t n;
    ssb_time bt;
    uint32_t i, total;
    int silent = 1;

    if (t->acc_frames == 0)
        return;

    /* El instante del bloque sale de la linea de tiempo de colocacion, que
       cuenta frames desde un ancla y solo se re-ancla cuando falta audio de
       verdad. Ni por marca de tiempo cruda (tiembla +-5 ms y eso acababa como
       chasquidos y agujeros en el audio) ni por cuenta de frames a secas (el
       loopback por proceso pierde ~12.7 ms en cada apertura o cierre del flujo
       de la app, y contando a secas todo lo posterior se desplaza). Ver el
       comentario largo de i_on_audio y docs/03. */
    bt = t->acc_time;

    total = t->acc_frames * t->channels;
    for (i = 0; i < total; ++i)
    {
        if (t->acc[i] != 0)
        {
            silent = 0;
            break;
        }
    }

    n = ssb_codec_encode32(t->acc, t->acc_frames, t->channels, t->cfg.compress,
                           t->sample_bytes, t->encbuf,
                           ssb_codec_max_size32(SSB_BLOCK_FRAMES, t->channels, t->sample_bytes));
    if (n > 0)
    {
        if (ssb_ring_append(t->ring, bt, t->encbuf, (uint32_t)n, t->acc_frames) == ssb_ok)
        {
            t->disk_bytes += n;
            t->raw_bytes += (uint64_t)t->acc_frames * t->channels * t->sample_bytes;
            if (silent)
                t->silent_blocks++;
        }
    }
    t->acc_frames = 0;
}

/* Acumula `frames` cuyo primer frame ocurrio en `base`. `pcm` a NULL es
   silencio. Se llama con el mutex tomado. */
static void i_push_frames(ssb_track *t, const float *pcm, uint32_t frames, ssb_time base)
{
    uint32_t i, c;
    for (i = 0; i < frames; ++i)
    {
        int32_t *dst = &t->acc[t->acc_frames * t->channels];
        if (t->acc_frames == 0)
            t->acc_time = base + (ssb_time)i * SSB_TICKS_PER_SEC / (ssb_time)t->rate;
        if (t->pk_count == 0)
            t->pk_time = base + (ssb_time)i * SSB_TICKS_PER_SEC / (ssb_time)t->rate;
        for (c = 0; c < t->channels; ++c)
        {
            int32_t s = 0;
            if (pcm != NULL)
            {
                double v = (double)pcm[(size_t)i * t->channels + c];
                double a = (v < 0.0) ? -v : v;
                if (a > t->peak_level)
                    t->peak_level = a;
                if (v > 1.0)
                    v = 1.0;
                if (v < -1.0)
                    v = -1.0;
                /* La unica cuantizacion de toda la cadena. A 24 bits cae por
                   debajo de lo que un float32 de audio puede representar, asi
                   que deja de ser una perdida real. */
                s = (int32_t)(v * t->full_scale);
            }
            dst[c] = s;
            if (s < t->pk_min)
                t->pk_min = s;
            if (s > t->pk_max)
                t->pk_max = s;
        }
        t->acc_frames++;
        t->frames++;

        if (++t->pk_count == SSB_PEAK_FRAMES)
        {
            /* El mapa de picos es para dibujar, no para reproducir: se guarda
               siempre normalizado a int16 y asi la interfaz no tiene que saber
               con que resolucion se esta grabando. */
            i_peak_push(t, t->pk_time,
                        (int32_t)(t->pk_min / t->peak_div),
                        (int32_t)(t->pk_max / t->peak_div));
            t->pk_min = 0x7FFFFFFF;
            t->pk_max = -0x7FFFFFFF;
            t->pk_count = 0;
        }
        if (t->acc_frames == SSB_BLOCK_FRAMES)
            i_flush_block(t);
    }
}

static void i_on_audio(void *ctx, const float *pcm, uint32_t frames,
                       ssb_time tm, int silent, int discont)
{
    ssb_track *t = (ssb_track *)ctx;

    ssb_mutex_lock(t->mtx);
    /* La captura empieza a entregar en cuanto se abre, y en `ssb_track_create`
       la apertura va ANTES de reservar `acc`, el anillo y el mapa de picos: para
       dimensionarlos hay que saber cuantos canales y a que frecuencia, y quien
       lo dice es la propia captura. Sin esta guarda, el primer paquete escribe
       sobre punteros nulos.
       En Windows no saltaba nunca porque WASAPI tarda mas en dar el primer
       paquete. El monitor de PulseAudio entrega en milisegundos, y ahi murio:
       una de cada diez ejecuciones, con SIGSEGV en `i_push_frames`. */
    if (t->ready == 0)
    {
        ssb_mutex_unlock(t->mtx);
        return;
    }
    if (t->paused != 0 && tm > t->pause_at)
    {
        /* Entrada detenida: se tira el paquete y el buffer no se mueve. El
           cliente sigue abierto a proposito, para no pagar el arranque otra
           vez al reanudar (hallazgo H2).
           Ojo al `tm > pause_at`: al parar, el cliente de captura todavia tiene
           unos 200 ms de audio ya capturado esperando a que lo recojamos.
           Cortar por bandera los tiraba, y con sonidos cortos se notaba que
           faltaba el final. Se acepta todo lo anterior al instante de parar. */
        ssb_mutex_unlock(t->mtx);
        return;
    }
    if (!t->anchored)
    {
        t->anchor = tm;
        t->anchored = 1;
        t->tl_anchor = tm;
        t->tl_frames = t->frames;
        t->tl_off_runs = 0;
        i_drift_push(t, tm, tm, frames, silent);
    }
    else
    {
        ssb_time expected = t->anchor + (ssb_time)t->frames * SSB_TICKS_PER_SEC / (ssb_time)t->rate;
        int64_t d = (int64_t)(tm - expected);
        t->drift_ms = (double)d / 10000.0;
        i_drift_push(t, tm, expected, frames, silent);
        /* Antes aqui se inyectaba silencio cuando la cuenta de frames se
           quedaba corta. Era un parche sobre un diagnostico equivocado: lo que
           faltaba no era tiempo, eran frames perdidos en las transiciones del
           flujo de la app. Con marcas de tiempo reales el hueco se representa
           solo, y lo rellena quien exporta. */
    }
    if (discont)
        t->discont++;

    /* Nivel instantaneo de este paquete, con caida exponencial. Un paquete son
       ~10 ms, asi que 0.80 deja una caida de unos 150 ms: lo bastante lenta
       para verla y lo bastante rapida para que el medidor se vacie cuando la
       fuente deja de entregar audio. */
    {
        double pk = 0.0;
        if (!silent && pcm != NULL)
        {
            uint32_t k;
            for (k = 0; k < frames * t->channels; ++k)
            {
                double a = (double)pcm[k];
                if (a < 0.0)
                    a = -a;
                if (a > pk)
                    pk = a;
            }
        }
        t->level *= 0.80;
        if (pk > t->level)
            t->level = pk;
    }

    /* ------------------------------------------------ donde va cada frame
     *
     * NO se coloca por la marca de tiempo del paquete. Medido en esta maquina
     * con probes/devpos.cpp, sobre 2499 paquetes de loopback del dispositivo:
     *
     *   - la posicion de muestra del dispositivo avanza SIEMPRE lo mismo, sin
     *     una sola variacion: no se pierde ni un frame;
     *   - el qpcPosition que acompana a cada paquete se desvia entre -8.5 y
     *     +6.2 ms de lo que dicen los frames, con un patron periodico de tres
     *     paquetes (-4.4, +5.1, -1.1, y vuelta a empezar) que suma cero.
     *
     * O sea: la cuenta de frames es exacta y el reloj de pared es el que
     * tiembla. Colocar por marca de tiempo metia ese temblor en el audio, y
     * quien exporta lo veia como huecos que rellenaba con silencio. De ahi los
     * chasquidos primero y, subida la holgura, los agujeros de ~5 ms despues.
     * Cuidado con la tentacion de arreglar esto subiendo mas la holgura: el
     * temblor y los huecos de verdad se solapan, y solo se puede distinguirlos
     * mirando de donde viene cada cosa, no cuanto mide.
     *
     * Asi que se cuenta desde un ancla, y el ancla se rehace SOLO cuando de
     * verdad falta audio. Dos avisos, ninguno basado en adivinar un umbral:
     *
     *   1. DATA_DISCONTINUITY. Es WASAPI diciendo "aqui he perdido audio".
     *      Aparecio 1 vez en 2499 paquetes, asi que no salta por ruido.
     *   2. Una red de seguridad por si esa bandera no llega en algun camino
     *      (el loopback por proceso pierde ~12.7 ms en cada apertura o cierre
     *      del flujo de la app, ver docs/03): una desviacion sostenida. El
     *      temblor vuelve a su sitio en uno o dos paquetes; un hueco real no
     *      vuelve nunca. Exigir que se mantenga es lo que separa una cosa de la
     *      otra sin tener que acertar un umbral entre 4 y 12 ms.
     */
    {
        ssb_time tl = t->tl_anchor +
                      (ssb_time)(t->frames - t->tl_frames) * SSB_TICKS_PER_SEC / (ssb_time)t->rate;
        int64_t off = (int64_t)(tm - tl);
        int64_t lim = (int64_t)SSB_TIMELINE_SLACK_MS * (int64_t)SSB_TICKS_PER_SEC / 1000;

        /* La racha SOLO cuenta desviaciones hacia adelante, que son las unicas
           que pueden significar audio que falta. Contar tambien las negativas
           dejaba el contador alto durante una deriva negativa —la de esta
           maquina lo es, ver docs/03— y entonces el primer paquete positivo
           fuera de banda re-anclaba sin haber pasado la prueba de persistencia,
           que es justo lo que la prueba existe para impedir. */
        if (off > lim)
            t->tl_off_runs++;
        else
            t->tl_off_runs = 0;

        /* Nunca hacia atras. Mover el ancla en negativo adelanta la linea de
           tiempo, y como esta holgura es mayor que la del exportador, el
           desajuste cae fuera del empalme y alli se recorta: se DESCARTA audio
           bueno. Un reloj que va por detras es deriva, no un hueco. */
        if (t->tl_reanchor != 0 || ((discont != 0 || t->tl_off_runs >= SSB_TIMELINE_RUNS) && off > 0))
        {
            /* Se cierra el bloque en curso antes de saltar. Si no, el hueco no
               aparece donde ocurrio sino en la frontera del bloque siguiente,
               hasta 85 ms mas tarde, porque `acc_time` solo se toma cuando el
               acumulador esta vacio. */
            i_flush_block(t);
            t->tl_anchor = tm;
            t->tl_frames = t->frames;
            t->tl_off_runs = 0;
            t->tl_reanchor = 0;
            /* El primer anclaje al empezar a grabar NO es un hueco: no hay
               audio anterior del que separarse. Contarlo haria que toda
               grabacion naciera diciendo que perdio algo. */
            if (t->frames > 0)
                t->reanchors++;
            tl = tm;
        }
        t->last_reported = tm;
        i_push_frames(t, silent ? NULL : pcm, frames, tl);
    }
    ssb_mutex_unlock(t->mtx);
}

/* ------------------------------------------------------------------ publico */

ssb_res ssb_track_create(const char *name, const char *dir, const ssb_source *src,
                         const ssb_track_config *cfg, ssb_track **out)
{
    ssb_track *t;
    ssb_track_config c;
    ssb_ring_config rc;
    ssb_res res;
    uint32_t seconds;

    if (name == NULL || dir == NULL || src == NULL || out == NULL)
        return ssb_err_arg;
    if (cfg != NULL)
        c = *cfg;
    else
        ssb_track_config_default(&c);
    if (c.max_seconds == 0)
        c.max_seconds = 300;

    t = (ssb_track *)calloc(1, sizeof(ssb_track));
    if (t == NULL)
        return ssb_err_mem;
    snprintf(t->name, sizeof(t->name), "%s", name);
    snprintf(t->dir, sizeof(t->dir), "%s", dir);
    t->src = *src;
    t->cfg = c;
    /* Resolucion de guardado. Solo hay dos: 16, que es lo de siempre, y 24, que
       es todo lo que un float32 de audio puede representar. Cualquier otro valor
       cae a 16 en vez de fallar: es un ajuste, no una orden. */
    t->bits = (c.bits >= 24) ? 24u : 16u;
    t->sample_bytes = (t->bits > 16) ? 4u : 2u;
    t->full_scale = (t->bits > 16) ? 8388607.0 : 32767.0;
    t->peak_div = t->full_scale / 32767.0;
    t->pk_min = 0x7FFFFFFF;
    t->pk_max = -0x7FFFFFFF;

    t->mtx = ssb_mutex_create();
    if (t->mtx == NULL)
    {
        free(t);
        return ssb_err_mem;
    }

    /* Abrir la captura primero: hasta que el sistema no responde no sabemos ni
       cuantos canales ni a que frecuencia vamos a grabar. */
    res = ssb_capture_open(src, i_on_audio, t, &t->channels, &t->rate, &t->cap);
    if (res != ssb_ok)
    {
        ssb_mutex_destroy(&t->mtx);
        free(t);
        return res;
    }
    if (t->channels < 1 || t->channels > SSB_MAX_CHANNELS || t->rate == 0)
    {
        ssb_capture_close(&t->cap);
        ssb_mutex_destroy(&t->mtx);
        free(t);
        return ssb_err_platform;
    }

    memset(&rc, 0, sizeof(rc));
    rc.channels = t->channels;
    rc.rate = t->rate;
    rc.max_bytes = c.max_bytes;
    rc.max_seconds = c.max_seconds;
    rc.segment_bytes = c.segment_bytes;
    rc.bits = t->bits;
    res = ssb_ring_create(dir, &rc, &t->ring);
    if (res != ssb_ok)
    {
        ssb_capture_close(&t->cap);
        ssb_mutex_destroy(&t->mtx);
        free(t);
        return res;
    }

    seconds = c.max_seconds;
    /* Un 25 % de holgura: el anillo de audio guarda hasta max_seconds mas un
       segmento (12.5 %), y si el de picos cubriera menos, la interfaz tendria
       menos picos que audio y la onda vieja se veria bailar. */
    t->peaks_cap = (uint32_t)((uint64_t)seconds * 5u / 4u * t->rate / SSB_PEAK_FRAMES) + 32u;
    t->peaks = (ssb_peak *)calloc(t->peaks_cap, sizeof(ssb_peak));
    t->acc = (int32_t *)calloc((size_t)SSB_BLOCK_FRAMES * t->channels, sizeof(int32_t));
    t->encbuf = (uint8_t *)malloc(ssb_codec_max_size32(SSB_BLOCK_FRAMES, t->channels, t->sample_bytes));
    t->drift = (ssb_drift_sample *)calloc(SSB_DRIFT_SAMPLES, sizeof(ssb_drift_sample));
    if (t->peaks == NULL || t->acc == NULL || t->encbuf == NULL || t->drift == NULL)
    {
        ssb_track_destroy(&t);
        return ssb_err_mem;
    }

    /* Ya esta todo en pie: a partir de aqui el callback puede trabajar. */
    ssb_mutex_lock(t->mtx);
    t->ready = 1;
    ssb_mutex_unlock(t->mtx);

    *out = t;
    return ssb_ok;
}

void ssb_track_destroy(ssb_track **tp)
{
    ssb_track *t;
    if (tp == NULL || *tp == NULL)
        return;
    t = *tp;
    /* Cerrar la captura para el hilo antes de tocar nada que ese hilo use. */
    ssb_capture_close(&t->cap);
    if (t->mtx != NULL)
    {
        ssb_mutex_lock(t->mtx);
        i_flush_block(t);
        ssb_mutex_unlock(t->mtx);
    }
    ssb_ring_destroy(&t->ring);
    ssb_mutex_destroy(&t->mtx);
    free(t->peaks);
    free(t->acc);
    free(t->encbuf);
    free(t->drift);
    free(t);
    *tp = NULL;
}

void ssb_track_stats_get(ssb_track *t, ssb_track_stats *s)
{
    if (t == NULL || s == NULL)
        return;
    memset(s, 0, sizeof(*s));
    ssb_mutex_lock(t->mtx);
    s->frames = t->frames;
    s->raw_bytes = t->raw_bytes;
    s->disk_bytes = t->disk_bytes;
    s->blocks = ssb_ring_blocks(t->ring);
    s->dropped = ssb_ring_dropped(t->ring);
    s->discont = t->discont;
    s->reanchors = t->reanchors;
    s->filled = t->filled_frames;
    s->silent_blocks = t->silent_blocks;
    s->ratio = (t->disk_bytes > 0) ? (double)t->raw_bytes / (double)t->disk_bytes : 0.0;
    s->drift_ms = t->drift_ms;
    s->eff_rate = 0.0;
    if (t->last_reported > t->anchor && t->frames > 0)
        s->eff_rate = (double)t->frames / ssb_time_to_sec(t->last_reported - t->anchor);
    s->peak = t->peak_level;
    s->level = t->level;
    s->channels = t->channels;
    s->rate = t->rate;
    ssb_mutex_unlock(t->mtx);
}

ssb_res ssb_track_span(ssb_track *t, ssb_time *from, ssb_time *to)
{
    ssb_res res;
    if (t == NULL)
        return ssb_err_arg;
    ssb_mutex_lock(t->mtx);
    res = ssb_ring_span(t->ring, from, to);
    ssb_mutex_unlock(t->mtx);
    return res;
}

void ssb_track_pause(ssb_track *t, int paused)
{
    if (t == NULL)
        return;
    ssb_mutex_lock(t->mtx);
    if (t->paused != 0 && paused == 0)
    {
        /* El hueco de la pausa es real y tiene que verse donde ocurrio, asi que
           la linea de tiempo TIENE que saltar. Pero no basta con dejar que lo
           note la prueba de persistencia: durante tres paquetes el audio nuevo
           se colocaria pegado al anterior a la pausa. Se fuerza el salto en el
           primer paquete que llegue, y se cierra el bloque en curso para que el
           corte caiga donde toca y no al final del bloque. */
        i_flush_block(t);
        t->tl_reanchor = 1;
        t->paused = 0;
    }
    else if (paused != 0)
    {
        /* No se cierra el bloque aqui: todavia puede entrar audio anterior al
           instante de parada. Se cierra al guardar, que ya lo hacia. */
        t->pause_at = ssb_now();
        t->paused = 1;
    }
    ssb_mutex_unlock(t->mtx);
}

int ssb_track_paused(const ssb_track *t)
{
    return (t != NULL) ? t->paused : 0;
}

ssb_res ssb_track_set_buffer(ssb_track *t, uint32_t max_seconds)
{
    ssb_res res;
    uint32_t newcap;
    ssb_peak *np;

    if (t == NULL || max_seconds == 0)
        return ssb_err_arg;

    ssb_mutex_lock(t->mtx);
    res = ssb_ring_set_seconds(t->ring, max_seconds);
    if (res == ssb_ok)
    {
        t->cfg.max_seconds = max_seconds;
        /* El mapa de picos tiene que cubrir lo mismo que el audio. Se reasigna
           conservando los mas recientes, que son los que se van a ver. */
        newcap = (uint32_t)((uint64_t)max_seconds * 5u / 4u * t->rate / SSB_PEAK_FRAMES) + 32u;
        np = (ssb_peak *)calloc(newcap, sizeof(ssb_peak));
        if (np != NULL)
        {
            uint32_t keep = (t->peaks_count < newcap) ? t->peaks_count : newcap;
            uint32_t first = t->peaks_count - keep;
            uint32_t i;
            for (i = 0; i < keep; ++i)
                np[i] = t->peaks[(t->peaks_head + first + i) % t->peaks_cap];
            free(t->peaks);
            t->peaks = np;
            t->peaks_cap = newcap;
            t->peaks_head = 0;
            t->peaks_count = keep;
            t->peaks_first_frame += (uint64_t)first * SSB_PEAK_FRAMES;
        }
    }
    ssb_mutex_unlock(t->mtx);
    return res;
}

uint32_t ssb_track_bits(const ssb_track *t)
{
    return (t != NULL) ? t->bits : 16u;
}

uint32_t ssb_track_buffer_seconds(const ssb_track *t)
{
    return (t != NULL) ? t->cfg.max_seconds : 0;
}

void ssb_track_freeze(ssb_track *t, int frozen)
{
    if (t == NULL)
        return;
    ssb_mutex_lock(t->mtx);
    t->frozen = frozen ? 1 : 0;
    if (t->frozen)
    {
        ssb_time to = 0;
        if (ssb_ring_span(t->ring, NULL, &to) == ssb_ok)
            t->frozen_at = to;
    }
    ssb_mutex_unlock(t->mtx);
}

int ssb_track_frozen(const ssb_track *t)
{
    return (t != NULL) ? t->frozen : 0;
}

ssb_time ssb_track_frozen_at(const ssb_track *t)
{
    return (t != NULL) ? t->frozen_at : 0;
}

uint32_t ssb_track_peaks(ssb_track *t, ssb_time from, ssb_time to, ssb_peak *out, uint32_t cap)
{
    uint32_t i, n = 0;

    if (t == NULL || out == NULL || cap == 0 || to <= from)
        return 0;

    ssb_mutex_lock(t->mtx);
    /* Se filtra por el instante que lleva cada pico. Buscar por indice obligaria
       a suponer que no se ha perdido un solo frame, y se pierden (docs/03). */
    for (i = 0; i < t->peaks_count && n < cap; ++i)
    {
        const ssb_peak *p = &t->peaks[(t->peaks_head + i) % t->peaks_cap];
        if (p->t < from)
            continue;
        if (p->t >= to)
            break;
        out[n++] = *p;
    }
    ssb_mutex_unlock(t->mtx);
    return n;
}

/* -------------------------------------------------------- volcado a WAV */

typedef struct
{
    ssb_wav *wav;
    uint32_t channels;
    uint32_t rate;
    ssb_time from;
    ssb_time to;
    ssb_time next;      /* instante donde va la proxima muestra que se escriba */
    uint64_t filled;    /* frames de silencio insertados en huecos reales */
    int32_t *pad;
} i_save_ctx;

/* Holgura al empalmar bloques consecutivos.
 *
 * Las marcas de tiempo vienen del QPC que WASAPI adjunta a cada paquete, y ese
 * reloj no cae exactamente sobre los limites de frame: entre dos bloques
 * seguidos la marca baila unos pocos frames en un sentido o en el otro. Es
 * ruido de medida, no un hueco: las muestras que la tarjeta entrego SON
 * contiguas.
 *
 * Rellenar ese baile con ceros metia rachas de 1 a 4 muestras de silencio en
 * mitad de la onda, casi siempre en el limite de bloque. Medido sobre una
 * grabacion real: 10 inserciones en 1.06 s, 0.4 ms de silencio en total. Nada
 * de audio perdido y, aun asi, diez discontinuidades duras por segundo — que es
 * exactamente lo que se oye como chasquidos.
 *
 * Asi que por debajo de esta holgura el bloque se empalma pegado al anterior y
 * la marca se ignora. Por encima, el hueco es real (una transicion de flujo de
 * la fuente ronda los 12 ms) y hay que taparlo: es lo unico que mantiene las
 * pistas alineadas entre si.
 *
 * 5 ms deja sitio de sobra para el jitter (unos pocos frames) sin acercarse al
 * hueco real mas pequeno que hemos medido. */
#define SSB_SPLICE_SLACK_MS 5

/* Rellena hasta `until` con silencio. Es lo que cubre los frames que la fuente
   perdio en una transicion de flujo: el hueco existio de verdad, y taparlo es
   lo unico que mantiene alineadas las pistas entre si. */
static ssb_res i_pad_until(i_save_ctx *s, ssb_time until)
{
    uint64_t need;
    if (until <= s->next)
        return ssb_ok;
    need = (uint64_t)(until - s->next) * s->rate / SSB_TICKS_PER_SEC;
    while (need > 0)
    {
        uint32_t n = (need > SSB_BLOCK_FRAMES) ? SSB_BLOCK_FRAMES : (uint32_t)need;
        ssb_res res = ssb_wav_write32(s->wav, s->pad, n);
        if (res != ssb_ok)
            return res;
        s->filled += n;
        need -= n;
    }
    s->next = until;
    return ssb_ok;
}

static ssb_res i_on_block(void *ctx, ssb_time bt, const int32_t *pcm, uint32_t frames)
{
    i_save_ctx *s = (i_save_ctx *)ctx;
    uint64_t first = 0;
    uint64_t last = frames;
    ssb_time slack;
    ssb_res res;
    int splice;

    /* Desajuste con el bloque anterior. Por debajo de la holgura es jitter del
       reloj y el bloque se empalma pegado: ni se rellena ni se recorta. */
    slack = (ssb_time)SSB_SPLICE_SLACK_MS * SSB_TICKS_PER_SEC / 1000;
    splice = (bt > s->next) ? ((bt - s->next) <= slack)
                            : ((s->next - bt) <= slack);

    /* Hueco por delante del bloque: el tiempo paso pero no llegaron frames. */
    if (bt > s->next && splice == 0)
    {
        ssb_time cap = (bt < s->to) ? bt : s->to;
        res = i_pad_until(s, cap);
        if (res != ssb_ok)
            return res;
    }
    /* Solape: parte de este bloque ya se escribio (o cae antes del tramo). */
    if (s->next > bt && splice == 0)
    {
        uint64_t skip = (uint64_t)(s->next - bt) * s->rate / SSB_TICKS_PER_SEC;
        first = (skip > frames) ? frames : skip;
    }
    /* Recorte por el final del tramo pedido. Al empalmar, el bloque se escribe
       desde donde iba el cursor, asi que el recorte se mide desde ahi. */
    {
        ssb_time base = (splice == 1) ? s->next : bt;
        if (s->to > base)
        {
            uint64_t keep = (uint64_t)(s->to - base) * s->rate / SSB_TICKS_PER_SEC;
            keep += first;
            if (keep < last)
                last = keep;
        }
        else
        {
            last = first;
        }
    }
    if (last <= first)
        return ssb_ok;

    res = ssb_wav_write32(s->wav, pcm + first * s->channels, (uint32_t)(last - first));
    if (res != ssb_ok)
        return res;
    /* Dentro de un bloque los frames SI son contiguos: el avance por cuenta de
       frames es correcto aqui. Entre bloques manda el reloj, salvo cuando el
       desajuste cabe en la holgura y hemos empalmado.

       Ojo con lo que NO se hace: al empalmar se avanza desde `s->next`, pero el
       cursor se re-ancla a la marca del bloque siguiente, no a una cuenta de
       frames acumulada. Asi el jitter no se suma: cada bloque se mide contra su
       propio reloj y el error se queda en unos pocos frames para siempre, en
       vez de crecer sin techo a lo largo de una grabacion de horas. */
    if (splice == 1)
        s->next = s->next + (ssb_time)(last - first) * SSB_TICKS_PER_SEC / (ssb_time)s->rate;
    else
        s->next = bt + (ssb_time)last * SSB_TICKS_PER_SEC / (ssb_time)s->rate;
    return ssb_ok;
}

ssb_res ssb_ring_save_wav(ssb_ring *r, uint32_t channels, uint32_t rate,
                          ssb_time from, ssb_time to, const char *path,
                          uint64_t *filled_frames)
{
    i_save_ctx s;
    ssb_res res;

    if (r == NULL || path == NULL || rate == 0 || to <= from)
        return ssb_err_arg;

    memset(&s, 0, sizeof(s));
    s.channels = channels;
    s.rate = rate;
    s.from = from;
    s.to = to;
    s.next = from;
    s.pad = (int32_t *)calloc((size_t)SSB_BLOCK_FRAMES * channels, sizeof(int32_t));
    if (s.pad == NULL)
        return ssb_err_mem;

    /* El WAV sale con la MISMA resolucion con la que se grabo. Bajarla aqui
       tiraria en el ultimo paso lo que se ha guardado con cuidado durante toda
       la cadena. */
    res = ssb_wav_open_ex(path, channels, rate, ssb_ring_bits(r), &s.wav);
    if (res != ssb_ok)
    {
        free(s.pad);
        return res;
    }
    res = ssb_ring_read(r, from, to, i_on_block, &s);
    /* Que el anillo no tenga NADA en el tramo no es un error: es una pista que
       en ese rato no estaba grabando. Sale silencio de la duracion exacta, que
       es lo que la deja alineada con las demas. */
    if (res == ssb_err_empty)
        res = ssb_ok;
    /* El tramo pedido se cubre entero: si la fuente se quedo muda al final, eso
       tambien es parte del tramo. */
    if (res == ssb_ok)
        res = i_pad_until(&s, to);
    ssb_wav_close(&s.wav);
    free(s.pad);
    if (filled_frames != NULL)
        *filled_frames = s.filled;
    return res;
}

ssb_res ssb_track_save_wav(ssb_track *t, ssb_time from, ssb_time to, const char *path)
{
    ssb_res res;
    ssb_time sf = 0, st = 0;
    uint64_t filled = 0;

    if (t == NULL || path == NULL)
        return ssb_err_arg;

    ssb_mutex_lock(t->mtx);
    /* Un bloque a medias todavia no esta en el ring; se cierra para que el
       tramo pedido incluya lo ultimo capturado. */
    i_flush_block(t);
    res = ssb_ring_span(t->ring, &sf, &st);
    if (res != ssb_ok)
    {
        ssb_mutex_unlock(t->mtx);
        return res;
    }
    /* NO se recorta al tramo de esta pista.
     *
     * Antes se recortaba, y entonces una pista anadida a mitad de la grabacion
     * producia un fichero mas corto que las demas. Para que salieran alineadas,
     * la interfaz tenia que quedarse con la INTERSECCION de todas: en cuanto
     * anadias una pista, dejaba de poder seleccionarse nada anterior a ella y
     * cualquier seleccion previa pasaba a estar "fuera de lo que cubren todas".
     * El sintoma no tenia nada que ver con la causa.
     *
     * Lo correcto es lo contrario: se escribe el tramo pedido ENTERO y lo que
     * esta pista no cubre sale como silencio, que es exactamente lo que estaba
     * grabando entonces: nada. Asi todas duran lo mismo sin recortarle a nadie
     * lo que si tiene, y la interfaz puede ofrecer la UNION. */
    (void)sf;
    (void)st;
    if (to <= from)
    {
        ssb_mutex_unlock(t->mtx);
        return ssb_err_empty;
    }
    /* El mutex se mantiene durante toda la escritura, y eso bloquea a la vez
       la captura y el dibujo: al guardar 140 s, el lienzo pasa de 17 ms a 2478
       ms por fotograma y aparecen huecos.
       Se intento soltarlo y aplazar solo el descarte del anillo
       (`ssb_ring_hold`). No vale: con la captura anadiendo a la vez, el fichero
       exportado salio a la mitad — 70,49 s de los 135,17 pedidos. El lector
       necesita mas garantias que "no me muevas el principio". Queda pendiente
       hacerlo bien, con una prueba que compare duraciones exportadas mientras
       se graba. */
    res = ssb_ring_save_wav(t->ring, t->channels, t->rate, from, to, path, &filled);
    t->filled_frames += filled;
    ssb_mutex_unlock(t->mtx);
    return res;
}
