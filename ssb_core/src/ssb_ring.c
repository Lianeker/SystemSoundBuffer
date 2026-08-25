/* Buffer circular en disco.
 *
 * El audio va a segmentos de tamano fijo (`seg-XXXXXXXX.dat`) y el indice de
 * bloques vive en RAM. Cuando se pasa del presupuesto de bytes o de la duracion
 * configurada, se descarta el segmento mas viejo entero.
 *
 * Se eligio segmentos y no un unico fichero circular por una razon concreta:
 * los bloques comprimidos son de tamano variable, y un circular de registros
 * variables obliga a aritmetica de solape que es justo donde se cuelan los
 * fallos. Con segmentos, descartar es borrar un fichero.
 *
 * Este modulo NO se sincroniza solo: quien lo usa (ssb_track) es el dueno del
 * mutex.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ssb_time t;      /* instante del primer frame */
    uint32_t frames;
    uint32_t bytes;
    uint32_t seg;
    uint32_t off;
} i_block;

struct ssb_ring_t
{
    char dir[480];
    ssb_ring_config cfg;

    i_block *idx;
    uint32_t cap;
    uint32_t head;
    uint32_t count;

    uint64_t total_bytes;
    uint32_t dropped;

    uint32_t cur_seg;
    uint32_t cur_off;
    uint32_t cur_frames;         /* audio acumulado en el segmento en curso */
    uint32_t max_segment_frames; /* techo de duracion de un segmento */
    ssb_time keep;               /* tramo que se conserva antes de descartar */
    FILE *cur_f;

    /* cache de lectura, para no abrir y cerrar el mismo segmento por bloque */
    FILE *rd_f;
    uint32_t rd_seg;

    uint8_t *scratch;
    int32_t *pcm;
    uint32_t sample_bytes; /* en el modo crudo: 2 a 16 bits, 4 a 24 */
};

static void i_seg_path(const ssb_ring *r, uint32_t seg, char *out, size_t cap)
{
    snprintf(out, cap, "%s/seg-%08u.dat", r->dir, seg);
}

static ssb_time i_block_end(const ssb_ring *r, const i_block *b)
{
    return b->t + (ssb_time)b->frames * SSB_TICKS_PER_SEC / (ssb_time)r->cfg.rate;
}

static const i_block *i_at(const ssb_ring *r, uint32_t i)
{
    return &r->idx[(r->head + i) % r->cap];
}

ssb_res ssb_ring_create(const char *dir, const ssb_ring_config *cfg, ssb_ring **out)
{
    ssb_ring *r;
    uint32_t seconds;

    if (dir == NULL || cfg == NULL || out == NULL)
        return ssb_err_arg;
    if (cfg->rate == 0 || cfg->channels < 1 || cfg->channels > SSB_MAX_CHANNELS)
        return ssb_err_arg;
    if (ssb_mkdir(dir) != ssb_ok)
        return ssb_err_io;

    r = (ssb_ring *)calloc(1, sizeof(ssb_ring));
    if (r == NULL)
        return ssb_err_mem;

    snprintf(r->dir, sizeof(r->dir), "%s", dir);
    r->cfg = *cfg;
    if (r->cfg.segment_bytes == 0)
        r->cfg.segment_bytes = 4u * 1024u * 1024u;

    /* Se descarta por segmentos enteros, asi que la duracion de un segmento es
       el "salto" con el que retrocede el principio del buffer. Con 1/8 ese salto
       era del 12 % del buffer y se veia: el borde derecho avanzaba continuo y el
       izquierdo pegaba tirones. Con 1/16, y acotado por arriba y por abajo, el
       salto baja al 6 % y el buffer sigue conservando SIEMPRE al menos lo
       pedido (ver `keep`), que es lo que de verdad quita el tiron. */
    seconds = (cfg->max_seconds > 0) ? cfg->max_seconds : 3600u;
    r->max_segment_frames = (uint32_t)((uint64_t)seconds * cfg->rate / 16u);
    if (r->max_segment_frames < cfg->rate / 2u)
        r->max_segment_frames = cfg->rate / 2u; /* medio segundo */
    if (r->max_segment_frames < SSB_BLOCK_FRAMES)
        r->max_segment_frames = SSB_BLOCK_FRAMES;
    if (r->max_segment_frames > cfg->rate * 30u)
        r->max_segment_frames = cfg->rate * 30u;

    /* Se conserva lo pedido MAS un segmento. Asi, tras descartar, el buffer
       sigue teniendo lo pedido entero y la interfaz puede ensenar una ventana
       de esa duracion exacta sin quedarse nunca sin datos por la izquierda. */
    r->keep = (ssb_time)seconds * SSB_TICKS_PER_SEC +
              (ssb_time)r->max_segment_frames * SSB_TICKS_PER_SEC / (ssb_time)cfg->rate;

    /* El indice cubre la duracion pedida mas un segmento entero, que es lo que
       puede sobrar antes de que el descarte lo libere. */
    r->cap = (uint32_t)(((uint64_t)seconds * cfg->rate + 2u * r->max_segment_frames) / SSB_BLOCK_FRAMES) + 64u;
    r->idx = (i_block *)calloc(r->cap, sizeof(i_block));
    if (r->cfg.bits == 0)
        r->cfg.bits = 16;
    r->sample_bytes = (r->cfg.bits > 16) ? 4u : 2u;
    r->scratch = (uint8_t *)malloc(ssb_codec_max_size32(SSB_BLOCK_FRAMES, cfg->channels, r->sample_bytes));
    r->pcm = (int32_t *)malloc((size_t)SSB_BLOCK_FRAMES * cfg->channels * sizeof(int32_t));
    if (r->idx == NULL || r->scratch == NULL || r->pcm == NULL)
    {
        ssb_ring_destroy(&r);
        return ssb_err_mem;
    }
    r->rd_seg = 0xFFFFFFFFu;
    *out = r;
    return ssb_ok;
}

void ssb_ring_destroy(ssb_ring **rp)
{
    ssb_ring *r;
    uint32_t seg;
    if (rp == NULL || *rp == NULL)
        return;
    r = *rp;
    if (r->cur_f != NULL)
        fclose(r->cur_f);
    if (r->rd_f != NULL)
        fclose(r->rd_f);
    /* Borrar los segmentos: si no, quedan ficheros huerfanos en el directorio
       que otra pista podria encontrarse si alguna vez reutilizara el nombre. */
    if (r->count > 0)
    {
        uint32_t first = i_at(r, 0)->seg;
        for (seg = first; seg <= r->cur_seg; ++seg)
        {
            char path[512];
            i_seg_path(r, seg, path, sizeof(path));
            remove(path);
        }
    }
    free(r->idx);
    free(r->scratch);
    free(r->pcm);
    free(r);
    *rp = NULL;
}

/* Cuanto quedaria si se descartase el segmento mas viejo. 0 si no hay nada
   que quede. */
static ssb_time i_span_after_evict(const ssb_ring *r)
{
    uint32_t seg, i;
    ssb_time to;

    if (r->count == 0)
        return 0;
    seg = i_at(r, 0)->seg;
    to = i_block_end(r, i_at(r, r->count - 1));
    for (i = 0; i < r->count; ++i)
    {
        const i_block *b = i_at(r, i);
        if (b->seg != seg)
            return (to > b->t) ? to - b->t : 0;
    }
    return 0;
}

/* Descarta el segmento mas viejo entero. No toca el segmento en curso.
 *
 * `force` distingue dos motivos muy distintos para descartar:
 *   - por bytes o por indice lleno, hay que liberar SI O SI;
 *   - por duracion, solo si lo que queda sigue cubriendo lo pedido.
 * Sin esa distincion, al reducir el buffer los segmentos viejos (creados con el
 * tamano anterior, mucho mayores) se descartan enteros y el buffer se queda muy
 * por debajo de lo pedido: pedias 10 s y te quedaban cuatro, con la grabacion
 * apinada a la izquierda de la vista. El invariante es que NUNCA se queda corto.
 */
static int i_evict_oldest_segment(ssb_ring *r, int force)
{
    uint32_t seg;
    char path[512];

    if (r->count == 0)
        return 0;
    seg = i_at(r, 0)->seg;
    if (seg == r->cur_seg)
        return 0;
    if (!force)
    {
        ssb_time after = i_span_after_evict(r);
        if (after < (ssb_time)r->cfg.max_seconds * SSB_TICKS_PER_SEC)
            return 0;
    }

    while (r->count > 0 && i_at(r, 0)->seg == seg)
    {
        r->total_bytes -= i_at(r, 0)->bytes;
        r->head = (r->head + 1) % r->cap;
        r->count--;
        r->dropped++;
    }
    if (r->rd_f != NULL && r->rd_seg == seg)
    {
        fclose(r->rd_f);
        r->rd_f = NULL;
        r->rd_seg = 0xFFFFFFFFu;
    }
    i_seg_path(r, seg, path, sizeof(path));
    remove(path);
    return 1;
}

static void i_enforce_budget(ssb_ring *r)
{
    for (;;)
    {
        int force = 0;
        int over_time = 0;

        /* Por bytes o por indice lleno hay que liberar aunque duela. */
        if (r->cfg.max_bytes > 0 && r->total_bytes > r->cfg.max_bytes)
            force = 1;
        if (r->count >= r->cap)
            force = 1;

        if (r->cfg.max_seconds > 0 && r->count > 0)
        {
            ssb_time from = i_at(r, 0)->t;
            ssb_time to = i_block_end(r, i_at(r, r->count - 1));
            if (to > from && (to - from) > r->keep)
                over_time = 1;
        }
        if (!force && !over_time)
            return;
        if (!i_evict_oldest_segment(r, force))
            return; /* no se puede liberar mas sin quedarse corto */
    }
}

static ssb_res i_rotate(ssb_ring *r)
{
    char path[512];
    if (r->cur_f != NULL)
    {
        fclose(r->cur_f);
        r->cur_f = NULL;
        r->cur_seg++;
    }
    r->cur_off = 0;
    r->cur_frames = 0;
    i_seg_path(r, r->cur_seg, path, sizeof(path));
    r->cur_f = fopen(path, "wb");
    return (r->cur_f != NULL) ? ssb_ok : ssb_err_io;
}

ssb_res ssb_ring_append(ssb_ring *r, ssb_time t, const uint8_t *data,
                        uint32_t bytes, uint32_t frames)
{
    i_block *b;

    if (r == NULL || data == NULL || bytes == 0 || frames == 0)
        return ssb_err_arg;

    if (r->cur_f == NULL ||
        r->cur_off + bytes > r->cfg.segment_bytes ||
        r->cur_frames + frames > r->max_segment_frames)
    {
        ssb_res res = i_rotate(r);
        if (res != ssb_ok)
            return res;
    }

    /* Red de seguridad: el indice es circular y sobrescribirlo corromperia el
       buffer entero. Si el presupuesto no pudo liberar sitio, se suelta el
       bloque mas viejo antes que pisar nada. */
    if (r->count == r->cap)
    {
        r->total_bytes -= i_at(r, 0)->bytes;
        r->head = (r->head + 1) % r->cap;
        r->count--;
        r->dropped++;
    }
    if (fwrite(data, 1, bytes, r->cur_f) != bytes)
        return ssb_err_io;
    /* Sin fflush por bloque: el coste de I/O no debe caer en el hilo de captura
       mas de lo imprescindible. El segmento se cierra al rotar. */

    b = &r->idx[(r->head + r->count) % r->cap];
    b->t = t;
    b->frames = frames;
    b->bytes = bytes;
    b->seg = r->cur_seg;
    b->off = r->cur_off;
    r->cur_off += bytes;
    r->cur_frames += frames;
    r->total_bytes += bytes;
    r->count++;

    i_enforce_budget(r);
    return ssb_ok;
}

/* Cambia la duracion conservada en caliente. Reducir descarta por el principio
   en el siguiente `append`; ampliar simplemente deja crecer. El indice no se
   reasigna: se dimensiono con holgura y si no llega, la red de seguridad de
   `ssb_ring_append` suelta lo mas viejo antes que pisar nada. */
/* El indice tiene que caber la nueva duracion.
 *
 * Se dimensiona al crear el anillo y antes no se tocaba al cambiar la duracion.
 * Consecuencia: al AGRANDAR el buffer, el indice se llenaba con la capacidad
 * vieja y `i_enforce_budget` empezaba a descartar por "indice lleno" — que es un
 * descarte FORZADO y se salta la regla de no bajar de lo pedido. El buffer se
 * quedaba clavado donde le permitiera el indice viejo, muy por debajo de lo
 * pedido, y el borde izquierdo pegaba saltos de un segmento entero.
 *
 * Medido: pista creada con 120 s y luego 300 s pedidos -> se quedaba oscilando
 * entre 123 y 140 s, que es justo lo que caben 1646 bloques.
 *
 * Solo crece. Encoger no hace falta y obligaria a decidir que se tira. */
static ssb_res i_grow_index(ssb_ring *r, uint32_t seconds)
{
    uint64_t need64;
    uint32_t need, i;
    i_block *ni;

    need64 = ((uint64_t)seconds * r->cfg.rate + 2ull * r->max_segment_frames) / SSB_BLOCK_FRAMES + 64ull;
    need = (need64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)need64;
    if (need <= r->cap)
        return ssb_ok;

    ni = (i_block *)calloc(need, sizeof(i_block));
    if (ni == NULL)
        return ssb_err_mem;

    /* Se copia en orden logico, asi que el nuevo indice empieza en 0. `i_at`
       usa head y cap, que todavia son los viejos: no se tocan hasta el final. */
    for (i = 0; i < r->count; ++i)
        ni[i] = *i_at(r, i);

    free(r->idx);
    r->idx = ni;
    r->cap = need;
    r->head = 0;
    return ssb_ok;
}

ssb_res ssb_ring_set_seconds(ssb_ring *r, uint32_t seconds)
{
    ssb_res res;
    if (r == NULL || seconds == 0)
        return ssb_err_arg;
    r->cfg.max_seconds = seconds;
    r->max_segment_frames = (uint32_t)((uint64_t)seconds * r->cfg.rate / 16u);
    if (r->max_segment_frames < r->cfg.rate / 2u)
        r->max_segment_frames = r->cfg.rate / 2u;
    if (r->max_segment_frames < SSB_BLOCK_FRAMES)
        r->max_segment_frames = SSB_BLOCK_FRAMES;
    if (r->max_segment_frames > r->cfg.rate * 30u)
        r->max_segment_frames = r->cfg.rate * 30u;
    r->keep = (ssb_time)seconds * SSB_TICKS_PER_SEC +
              (ssb_time)r->max_segment_frames * SSB_TICKS_PER_SEC / (ssb_time)r->cfg.rate;
    res = i_grow_index(r, seconds);
    if (res != ssb_ok)
        return res;
    i_enforce_budget(r);
    return ssb_ok;
}

uint32_t ssb_ring_blocks(const ssb_ring *r)
{
    return (r != NULL) ? r->count : 0;
}

uint64_t ssb_ring_bytes(const ssb_ring *r)
{
    return (r != NULL) ? r->total_bytes : 0;
}

uint32_t ssb_ring_bits(const ssb_ring *r)
{
    return (r != NULL) ? r->cfg.bits : 16u;
}

uint32_t ssb_ring_dropped(const ssb_ring *r)
{
    return (r != NULL) ? r->dropped : 0;
}

ssb_res ssb_ring_span(const ssb_ring *r, ssb_time *from, ssb_time *to)
{
    if (r == NULL)
        return ssb_err_arg;
    if (r->count == 0)
        return ssb_err_empty;
    if (from != NULL)
        *from = i_at(r, 0)->t;
    if (to != NULL)
        *to = i_block_end(r, i_at(r, r->count - 1));
    return ssb_ok;
}

ssb_res ssb_ring_read(ssb_ring *r, ssb_time from, ssb_time to, ssb_block_fn fn, void *ctx)
{
    uint32_t i;
    int found = 0;

    if (r == NULL || fn == NULL)
        return ssb_err_arg;
    if (r->count == 0)
        return ssb_err_empty;

    for (i = 0; i < r->count; ++i)
    {
        const i_block *b = i_at(r, i);
        ssb_time bend = i_block_end(r, b);
        ssb_res res;

        if (bend <= from)
            continue;
        if (b->t >= to)
            break;

        if (r->rd_f == NULL || r->rd_seg != b->seg)
        {
            char path[512];
            if (r->rd_f != NULL)
                fclose(r->rd_f);
            i_seg_path(r, b->seg, path, sizeof(path));
            r->rd_f = fopen(path, "rb");
            r->rd_seg = b->seg;
            if (r->rd_f == NULL)
            {
                r->rd_seg = 0xFFFFFFFFu;
                return ssb_err_io;
            }
        }
        /* El segmento en curso puede tener datos aun en el buffer de stdio. */
        if (b->seg == r->cur_seg && r->cur_f != NULL)
            fflush(r->cur_f);

        if (fseek(r->rd_f, (long)b->off, SEEK_SET) != 0)
            return ssb_err_io;
        if (fread(r->scratch, 1, b->bytes, r->rd_f) != b->bytes)
            return ssb_err_io;

        res = ssb_codec_decode32(r->scratch, b->bytes, b->frames, r->cfg.channels,
                                 r->sample_bytes, r->pcm);
        if (res != ssb_ok)
            return res;

        res = fn(ctx, b->t, r->pcm, b->frames);
        if (res != ssb_ok)
            return res;
        found = 1;
    }
    return found ? ssb_ok : ssb_err_empty;
}
