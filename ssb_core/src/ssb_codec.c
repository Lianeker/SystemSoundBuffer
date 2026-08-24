/* Compresion sin perdida por bloques independientes.
 *
 * Formato de un bloque:
 *   byte 0  modo: 1 = rice, 2 = verbatim (muestras crudas)
 *   modo 2  frames*channels muestras crudas: int16 a 16 bits, int32 a 24
 *   modo 1  un flujo de bits con un sub-flujo por canal decorrelacionado
 *           (mid/side si hay dos canales, el canal a secas si hay uno):
 *              1 bit   constante?
 *              si 1    26 bits zigzag con el valor constante  -> fin del sub-flujo
 *              si 0    2 bits orden del predictor (0..3)
 *                      5 bits k de Rice
 *                      n codigos Rice con los residuos en zigzag
 *
 * El escape "constante" es lo que hace que el silencio sea casi gratis: un
 * bloque de 4096 frames en silencio cabe en 6 bytes. El escape "verbatim"
 * garantiza que comprimir nunca agranda.
 */
#include "ssb.h"
#include "ssb_internal.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Ancho del campo del escape "constante". 26 bits cubren el zigzag del canal
   lateral a 24 bits (l-r llega a +-2^24, o sea 25 bits de zigzag). */
#define SSB_CONST_BITS 26

#define MODE_RICE 1
#define MODE_VERBATIM 2

/* ---------------------------------------------------------- escritor de bits */

typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t bytes; /* cuenta incluso cuando ya no cabe, para poder decidir */
    uint64_t acc;
    int nbits;
} bitw;

static void bw_init(bitw *b, uint8_t *buf, size_t cap)
{
    b->buf = buf;
    b->cap = cap;
    b->bytes = 0;
    b->acc = 0;
    b->nbits = 0;
}

static void bw_put(bitw *b, uint32_t v, int n)
{
    if (n <= 0)
        return;
    b->acc = (b->acc << n) | (uint64_t)(n >= 32 ? v : (v & ((1u << n) - 1u)));
    b->nbits += n;
    while (b->nbits >= 8)
    {
        if (b->bytes < b->cap)
            b->buf[b->bytes] = (uint8_t)((b->acc >> (b->nbits - 8)) & 0xFFu);
        b->bytes++;
        b->nbits -= 8;
    }
}

static void bw_unary(bitw *b, uint32_t q)
{
    while (q >= 24)
    {
        bw_put(b, 0, 24);
        q -= 24;
    }
    bw_put(b, 1, (int)q + 1);
}

static void bw_flush(bitw *b)
{
    if (b->nbits > 0)
        bw_put(b, 0, 8 - b->nbits);
}

/* ----------------------------------------------------------- lector de bits */

typedef struct
{
    const uint8_t *buf;
    size_t bytes;
    size_t pos;
    int bit;
    int overrun;
} bitr;

static void br_init(bitr *r, const uint8_t *buf, size_t bytes)
{
    r->buf = buf;
    r->bytes = bytes;
    r->pos = 0;
    r->bit = 0;
    r->overrun = 0;
}

static int br_bit(bitr *r)
{
    int v;
    if (r->pos >= r->bytes)
    {
        r->overrun = 1;
        return 0;
    }
    v = (r->buf[r->pos] >> (7 - r->bit)) & 1;
    if (++r->bit == 8)
    {
        r->bit = 0;
        r->pos++;
    }
    return v;
}

static uint32_t br_get(bitr *r, int n)
{
    uint32_t v = 0;
    while (n-- > 0)
        v = (v << 1) | (uint32_t)br_bit(r);
    return v;
}

static uint32_t br_unary(bitr *r)
{
    uint32_t q = 0;
    while (br_bit(r) == 0)
    {
        if (r->overrun)
            return q;
        q++;
        if (q > (1u << 26))
        {
            r->overrun = 1;
            return q;
        }
    }
    return q;
}

/* ----------------------------------------------------------------- zigzag */

static uint32_t zig(int32_t v)
{
    return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31);
}

static int32_t unzig(uint32_t u)
{
    return (int32_t)((u >> 1) ^ (~(u & 1u) + 1u));
}

/* -------------------------------------------------------------- predictores */

/* Predice x[i] con los anteriores. Encoder y decoder comparten esta funcion,
   que es la unica garantia de que el round-trip sea exacto. */
static int32_t predict(const int32_t *x, int i, int order)
{
    switch (order)
    {
    case 1:
        return (i >= 1) ? x[i - 1] : 0;
    case 2:
        return (i >= 2) ? 2 * x[i - 1] - x[i - 2] : ((i == 1) ? x[0] : 0);
    case 3:
        return (i >= 3) ? 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3] : ((i >= 1) ? x[i - 1] : 0);
    default:
        return 0;
    }
}

/* Codifica un sub-flujo. */
static void stream_encode(bitw *bw, const int32_t *x, int n)
{
    int order, best_order = 0, best_k = 0, i, all_same = 1;
    double best_cost = 1e300;
    int32_t res[SSB_BLOCK_FRAMES];

    for (i = 1; i < n; ++i)
    {
        if (x[i] != x[0])
        {
            all_same = 0;
            break;
        }
    }
    /* El escape "constante" guardaba el valor en 20 bits, que sobra para int16
       pero se queda corto en cuanto se sube a 24: el canal lateral es l-r y
       llega al doble de la escala completa, o sea 25 bits de zigzag. Truncarlo
       daba un bloque que decodificaba MAL en silencio, y solo se vio porque el
       autotest compara muestra a muestra.
       Ahora caben 26 bits, y si aun asi no cupiera se usa el camino normal en
       vez de escribir un valor equivocado: mejor gastar bytes que mentir. */
    if (all_same && zig(x[0]) < (1u << SSB_CONST_BITS))
    {
        bw_put(bw, 1, 1);
        bw_put(bw, zig(x[0]), SSB_CONST_BITS);
        return;
    }
    bw_put(bw, 0, 1);

    for (order = 0; order <= 3; ++order)
    {
        double sum = 0.0;
        int k, kk;
        for (i = 0; i < n; ++i)
            sum += fabs((double)(x[i] - predict(x, i, order)));
        {
            double mean = sum / (double)n;
            kk = 0;
            while (kk < 30 && (double)(1u << (kk + 1)) < mean + 1.0)
                kk++;
            for (k = (kk > 1 ? kk - 1 : 0); k <= kk + 1 && k <= 30; ++k)
            {
                double cost = (double)n * (double)(k + 2) + sum / (double)(1u << k);
                if (cost < best_cost)
                {
                    best_cost = cost;
                    best_order = order;
                    best_k = k;
                }
            }
        }
    }

    for (i = 0; i < n; ++i)
        res[i] = x[i] - predict(x, i, best_order);

    bw_put(bw, (uint32_t)best_order, 2);
    bw_put(bw, (uint32_t)best_k, 5);
    for (i = 0; i < n; ++i)
    {
        uint32_t u = zig(res[i]);
        bw_unary(bw, u >> best_k);
        if (best_k > 0)
            bw_put(bw, u & ((1u << best_k) - 1u), best_k);
    }
}

static void stream_decode(bitr *br, int32_t *x, int n)
{
    int order, k, i;
    if (br_bit(br) == 1)
    {
        int32_t v = unzig(br_get(br, SSB_CONST_BITS));
        for (i = 0; i < n; ++i)
            x[i] = v;
        return;
    }
    order = (int)br_get(br, 2);
    k = (int)br_get(br, 5);
    for (i = 0; i < n; ++i)
    {
        uint32_t q = br_unary(br);
        uint32_t u = (k > 0) ? ((q << k) | br_get(br, k)) : q;
        x[i] = predict(x, i, order) + unzig(u);
    }
}

/* ------------------------------------------------------------------- publico */

size_t ssb_codec_max_size32(uint32_t frames, uint32_t channels, uint32_t sample_bytes)
{
    if (sample_bytes != 2 && sample_bytes != 4)
        sample_bytes = 2;
    return 1 + (size_t)frames * (size_t)channels * (size_t)sample_bytes;
}

size_t ssb_codec_max_size(uint32_t frames, uint32_t channels)
{
    return ssb_codec_max_size32(frames, channels, sizeof(int16_t));
}

/* Empaqueta el bloque crudo. Es el modo de escape —se usa cuando no se quiere
   comprimir y cuando el Rice no ha ganado nada—, asi que tiene que ser exacto
   en las dos profundidades. */
static void i_pack_verbatim(uint8_t *out, const int32_t *pcm, size_t n, uint32_t sample_bytes)
{
    size_t i;
    if (sample_bytes == 2)
    {
        int16_t *d = (int16_t *)out;
        for (i = 0; i < n; ++i)
            d[i] = (int16_t)pcm[i];
    }
    else
    {
        memcpy(out, pcm, n * sizeof(int32_t));
    }
}

static void i_unpack_verbatim(int32_t *pcm, const uint8_t *in, size_t n, uint32_t sample_bytes)
{
    size_t i;
    if (sample_bytes == 2)
    {
        const int16_t *s = (const int16_t *)in;
        for (i = 0; i < n; ++i)
            pcm[i] = s[i];
    }
    else
    {
        memcpy(pcm, in, n * sizeof(int32_t));
    }
}

size_t ssb_codec_encode32(const int32_t *pcm, uint32_t frames, uint32_t channels,
                          int compress, uint32_t sample_bytes, uint8_t *out, size_t cap)
{
    int32_t a[SSB_BLOCK_FRAMES];
    int32_t b[SSB_BLOCK_FRAMES];
    size_t verbatim;
    size_t n;
    bitw bw;
    uint32_t i;

    if (sample_bytes != 2 && sample_bytes != 4)
        return 0;
    if (pcm == NULL || out == NULL || frames == 0 || frames > SSB_BLOCK_FRAMES)
        return 0;
    if (channels < 1 || channels > SSB_MAX_CHANNELS)
        return 0;

    verbatim = ssb_codec_max_size32(frames, channels, sample_bytes);
    n = (size_t)frames * channels;
    if (cap < verbatim)
        return 0;

    if (!compress)
    {
        out[0] = MODE_VERBATIM;
        i_pack_verbatim(out + 1, pcm, n, sample_bytes);
        return verbatim;
    }

    if (channels == 2)
    {
        for (i = 0; i < frames; ++i)
        {
            int32_t l = pcm[i * 2 + 0];
            int32_t r = pcm[i * 2 + 1];
            b[i] = l - r;             /* side */
            a[i] = r + (b[i] >> 1);   /* mid, invertible */
        }
    }
    else
    {
        for (i = 0; i < frames; ++i)
            a[i] = pcm[i];
    }

    bw_init(&bw, out + 1, cap - 1);
    stream_encode(&bw, a, (int)frames);
    if (channels == 2)
        stream_encode(&bw, b, (int)frames);
    bw_flush(&bw);

    if (bw.bytes + 1 < verbatim)
    {
        out[0] = MODE_RICE;
        return bw.bytes + 1;
    }
    out[0] = MODE_VERBATIM;
    i_pack_verbatim(out + 1, pcm, n, sample_bytes);
    return verbatim;
}

ssb_res ssb_codec_decode32(const uint8_t *in, size_t bytes, uint32_t frames,
                           uint32_t channels, uint32_t sample_bytes, int32_t *pcm)
{
    int32_t a[SSB_BLOCK_FRAMES];
    int32_t b[SSB_BLOCK_FRAMES];
    bitr br;
    uint32_t i;

    if (sample_bytes != 2 && sample_bytes != 4)
        return ssb_err_arg;
    if (in == NULL || pcm == NULL || bytes < 1 || frames == 0 || frames > SSB_BLOCK_FRAMES)
        return ssb_err_arg;
    if (channels < 1 || channels > SSB_MAX_CHANNELS)
        return ssb_err_arg;

    if (in[0] == MODE_VERBATIM)
    {
        size_t n = (size_t)frames * channels;
        size_t need = n * (size_t)sample_bytes;
        if (bytes < need + 1)
            return ssb_err_arg;
        i_unpack_verbatim(pcm, in + 1, n, sample_bytes);
        return ssb_ok;
    }
    if (in[0] != MODE_RICE)
        return ssb_err_arg;

    br_init(&br, in + 1, bytes - 1);
    stream_decode(&br, a, (int)frames);
    if (channels == 2)
    {
        stream_decode(&br, b, (int)frames);
        for (i = 0; i < frames; ++i)
        {
            int32_t r = a[i] - (b[i] >> 1);
            int32_t l = r + b[i];
            pcm[i * 2 + 0] = l;
            pcm[i * 2 + 1] = r;
        }
    }
    else
    {
        for (i = 0; i < frames; ++i)
            pcm[i] = a[i];
    }
    return br.overrun ? ssb_err_io : ssb_ok;
}

/* ------------------------------------------------- envoltorios de 16 bits

   La API de 16 bits se queda: la usa el autotest y no hay razon para tocarla.
   Ahora son envoltorios de las de 32, asi que solo hay UNA implementacion del
   codec y no dos que puedan divergir. */

size_t ssb_codec_encode(const int16_t *pcm, uint32_t frames, uint32_t channels,
                        uint8_t *out, size_t cap)
{
    return ssb_codec_encode_ex(pcm, frames, channels, 1, out, cap);
}

size_t ssb_codec_encode_ex(const int16_t *pcm, uint32_t frames, uint32_t channels,
                           int compress, uint8_t *out, size_t cap)
{
    int32_t *tmp;
    size_t n;
    size_t i;
    size_t res;
    if (pcm == NULL || frames == 0 || frames > SSB_BLOCK_FRAMES)
        return 0;
    if (channels < 1 || channels > SSB_MAX_CHANNELS)
        return 0;
    n = (size_t)frames * channels;
    /* Del monton, no estatico: estas funciones son publicas y nadie promete
       llamarlas desde un solo hilo. */
    tmp = (int32_t *)malloc(n * sizeof(int32_t));
    if (tmp == NULL)
        return 0;
    for (i = 0; i < n; ++i)
        tmp[i] = pcm[i];
    res = ssb_codec_encode32(tmp, frames, channels, compress, sizeof(int16_t), out, cap);
    free(tmp);
    return res;
}

ssb_res ssb_codec_decode(const uint8_t *in, size_t bytes, uint32_t frames,
                         uint32_t channels, int16_t *pcm)
{
    int32_t *tmp;
    size_t n;
    size_t i;
    ssb_res res;
    if (pcm == NULL || frames == 0 || frames > SSB_BLOCK_FRAMES)
        return ssb_err_arg;
    if (channels < 1 || channels > SSB_MAX_CHANNELS)
        return ssb_err_arg;
    n = (size_t)frames * channels;
    tmp = (int32_t *)malloc(n * sizeof(int32_t));
    if (tmp == NULL)
        return ssb_err_arg;
    res = ssb_codec_decode32(in, bytes, frames, channels, sizeof(int16_t), tmp);
    if (res == ssb_ok)
    {
        for (i = 0; i < n; ++i)
            pcm[i] = (int16_t)tmp[i];
    }
    free(tmp);
    return res;
}
