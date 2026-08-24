/* Mide compresion real sobre audio capturado de verdad.
   modo rec  <segundos> <fichero.raw>  captura el loopback del sistema a int16 48k estereo
   modo test <fichero.raw>             aplica cada codec y reporta ratio, velocidad y round-trip */
#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------ captura */

static int record_raw(double secs, const char *path)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioClient *ac = NULL;
    IAudioCaptureClient *cap = NULL;
    WAVEFORMATEX *fmt = NULL;
    FILE *f = NULL;
    ULONGLONG t0;
    UINT64 total = 0;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en);
    if (FAILED(hr)) { printf("CoCreateInstance 0x%08X\n", (unsigned)hr); return 1; }
    hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (FAILED(hr)) { printf("GetDefaultAudioEndpoint 0x%08X\n", (unsigned)hr); return 1; }
    hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&ac);
    if (FAILED(hr)) { printf("Activate 0x%08X\n", (unsigned)hr); return 1; }
    ac->GetMixFormat(&fmt);
    hr = ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, NULL);
    if (FAILED(hr)) { printf("Initialize 0x%08X\n", (unsigned)hr); return 1; }
    ac->GetService(__uuidof(IAudioCaptureClient), (void **)&cap);

    f = fopen(path, "wb");
    if (f == NULL) { printf("no puedo escribir %s\n", path); return 1; }
    printf("capturando %.1f s de la salida del sistema (%u ch, %u Hz, %u bits) -> int16 estereo\n",
           secs, (unsigned)fmt->nChannels, (unsigned)fmt->nSamplesPerSec, (unsigned)fmt->wBitsPerSample);
    ac->Start();
    t0 = GetTickCount64();
    while (GetTickCount64() - t0 < (ULONGLONG)(secs * 1000))
    {
        UINT32 packet = 0;
        Sleep(5);
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0)
        {
            BYTE *data = NULL;
            UINT32 frames = 0, i;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, NULL, NULL))) break;
            for (i = 0; i < frames; ++i)
            {
                short out[2];
                int c;
                for (c = 0; c < 2; ++c)
                {
                    double v = 0.0;
                    UINT32 ch = (fmt->nChannels > 1) ? (UINT32)c : 0;
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) v = 0.0;
                    else if (fmt->wBitsPerSample == 32) v = ((const float *)data)[i * fmt->nChannels + ch];
                    else v = ((const short *)data)[i * fmt->nChannels + ch] / 32768.0;
                    if (v > 1.0) v = 1.0;
                    if (v < -1.0) v = -1.0;
                    out[c] = (short)(v * 32767.0);
                }
                fwrite(out, sizeof(short), 2, f);
            }
            total += frames;
            cap->ReleaseBuffer(frames);
            packet = 0;
        }
    }
    ac->Stop();
    fclose(f);
    printf("escritos %llu frames = %.2f s (%llu bytes)\n",
           (unsigned long long)total, (double)total / 48000.0,
           (unsigned long long)(total * 4));
    cap->Release(); ac->Release(); dev->Release(); en->Release();
    CoTaskMemFree(fmt);
    CoUninitialize();
    return 0;
}

/* -------------------------------------------------------------------- mu-law */

static unsigned char mulaw_encode(short pcm)
{
    static const int BIAS = 0x84;
    int sign = (pcm >> 8) & 0x80;
    int mag, exponent, mantissa;
    unsigned char out;
    if (sign != 0) pcm = (short)-pcm;
    if (pcm > 32635) pcm = 32635;
    mag = pcm + BIAS;
    exponent = 7;
    { int mask = 0x4000; while ((mag & mask) == 0 && exponent > 0) { exponent--; mask >>= 1; } }
    mantissa = (mag >> (exponent + 3)) & 0x0F;
    out = (unsigned char)~(sign | (exponent << 4) | mantissa);
    return out;
}

static short mulaw_decode(unsigned char u)
{
    static const int BIAS = 0x84;
    int sign, exponent, mantissa, mag;
    u = (unsigned char)~u;
    sign = u & 0x80;
    exponent = (u >> 4) & 0x07;
    mantissa = u & 0x0F;
    mag = ((mantissa << 3) + BIAS) << exponent;
    mag -= BIAS;
    return (short)(sign ? -mag : mag);
}

/* ---------------------------------------------------------------- IMA ADPCM */

static const int ima_index_tab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int ima_step_tab[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
    279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 875, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static int ima_encode_sample(int sample, int *pred, int *index)
{
    int step = ima_step_tab[*index];
    int diff = sample - *pred;
    int code = 0, diffq;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= step) { code |= 4; diff -= step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; }
    step >>= 1;
    if (diff >= step) { code |= 1; }
    /* reconstruir igual que el decodificador */
    step = ima_step_tab[*index];
    diffq = step >> 3;
    if (code & 4) diffq += step;
    if (code & 2) diffq += step >> 1;
    if (code & 1) diffq += step >> 2;
    if (code & 8) *pred -= diffq; else *pred += diffq;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *index += ima_index_tab[code];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;
    return code;
}

/* ------------------------------------------- sin perdida: predictor + Rice */

typedef struct _bw_t { unsigned char *buf; size_t cap; size_t bytes; unsigned long long acc; int nbits; } BW;

static void bw_init(BW *b, unsigned char *buf, size_t cap) { b->buf = buf; b->cap = cap; b->bytes = 0; b->acc = 0; b->nbits = 0; }
static void bw_put(BW *b, unsigned long long v, int n)
{
    while (n > 0)
    {
        int take = n < 32 ? n : 32;
        unsigned long long chunk = (v >> (n - take)) & ((take == 64) ? ~0ULL : ((1ULL << take) - 1));
        b->acc = (b->acc << take) | chunk;
        b->nbits += take;
        n -= take;
        while (b->nbits >= 8)
        {
            if (b->bytes < b->cap) b->buf[b->bytes] = (unsigned char)((b->acc >> (b->nbits - 8)) & 0xFF);
            b->bytes++;
            b->nbits -= 8;
        }
    }
}
static void bw_unary(BW *b, unsigned q) { while (q >= 32) { bw_put(b, 0, 32); q -= 32; } bw_put(b, 1, (int)q + 1); }
static void bw_flush(BW *b) { if (b->nbits > 0) bw_put(b, 0, 8 - b->nbits); }

typedef struct _br_t { const unsigned char *buf; size_t bytes; size_t pos; int bit; } BR;
static void br_init(BR *r, const unsigned char *buf, size_t bytes) { r->buf = buf; r->bytes = bytes; r->pos = 0; r->bit = 0; }
static int br_bit(BR *r)
{
    int v;
    if (r->pos >= r->bytes) return 0;
    v = (r->buf[r->pos] >> (7 - r->bit)) & 1;
    if (++r->bit == 8) { r->bit = 0; r->pos++; }
    return v;
}
static unsigned long long br_get(BR *r, int n) { unsigned long long v = 0; while (n-- > 0) v = (v << 1) | (unsigned long long)br_bit(r); return v; }
static unsigned br_unary(BR *r) { unsigned q = 0; while (br_bit(r) == 0 && r->pos < r->bytes) q++; return q; }

static unsigned zig(int v) { return (unsigned)((v << 1) ^ (v >> 31)); }
static int unzig(unsigned u) { return (int)((u >> 1) ^ (~(u & 1) + 1)); }

#define BLOCK 4096

/* Codifica un bloque de un canal. Devuelve bytes escritos. Escribe orden y k usados. */
static size_t rice_block(const int *x, int n, unsigned char *out, size_t cap, int *used_order, int *used_k)
{
    int order, best_order = 0, best_k = 0;
    double best_cost = 1e30;
    int res[BLOCK];
    BW bw;
    int i;

    for (order = 0; order <= 3; ++order)
    {
        double sum = 0.0;
        for (i = 0; i < n; ++i)
        {
            int p = 0;
            if (order == 1) p = (i >= 1) ? x[i - 1] : 0;
            else if (order == 2) p = (i >= 2) ? 2 * x[i - 1] - x[i - 2] : ((i == 1) ? x[0] : 0);
            else if (order == 3) p = (i >= 3) ? 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3] : ((i >= 1) ? x[i - 1] : 0);
            sum += fabs((double)(x[i] - p));
        }
        {
            double mean = sum / (double)(n > 0 ? n : 1);
            int kk = 0;
            while ((1 << (kk + 1)) < (int)(mean + 1.0) && kk < 30) kk++;
            {
                double cost = (double)n * (kk + 2.0) + sum / (double)(1 << kk);
                if (cost < best_cost) { best_cost = cost; best_order = order; best_k = kk; }
            }
        }
    }

    for (i = 0; i < n; ++i)
    {
        int p = 0;
        if (best_order == 1) p = (i >= 1) ? x[i - 1] : 0;
        else if (best_order == 2) p = (i >= 2) ? 2 * x[i - 1] - x[i - 2] : ((i == 1) ? x[0] : 0);
        else if (best_order == 3) p = (i >= 3) ? 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3] : ((i >= 1) ? x[i - 1] : 0);
        res[i] = x[i] - p;
    }

    bw_init(&bw, out, cap);
    bw_put(&bw, (unsigned long long)best_order, 2);
    bw_put(&bw, (unsigned long long)best_k, 5);
    for (i = 0; i < n; ++i)
    {
        unsigned u = zig(res[i]);
        bw_unary(&bw, u >> best_k);
        if (best_k > 0) bw_put(&bw, u & ((1u << best_k) - 1), best_k);
    }
    bw_flush(&bw);
    *used_order = best_order;
    *used_k = best_k;
    return bw.bytes;
}

static void rice_unblock(const unsigned char *in, size_t bytes, int n, int *x)
{
    BR br;
    int order, k, i;
    br_init(&br, in, bytes);
    order = (int)br_get(&br, 2);
    k = (int)br_get(&br, 5);
    for (i = 0; i < n; ++i)
    {
        unsigned q = br_unary(&br);
        unsigned u = (k > 0) ? ((q << k) | (unsigned)br_get(&br, k)) : q;
        int r = unzig(u);
        int p = 0;
        if (order == 1) p = (i >= 1) ? x[i - 1] : 0;
        else if (order == 2) p = (i >= 2) ? 2 * x[i - 1] - x[i - 2] : ((i == 1) ? x[0] : 0);
        else if (order == 3) p = (i >= 3) ? 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3] : ((i >= 1) ? x[i - 1] : 0);
        x[i] = p + r;
    }
}

/* ------------------------------------------------------------------- pruebas */

typedef struct _stat_t { const char *name; double bytes_out; double ms; int lossless; long long maxerr; } Stat;

static double now_ms(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

/* Comprime sin perdida un tramo de int16 estereo con mid/side + Rice por bloques. */
static double lossless_pass(const short *pcm, size_t frames, int verify, long long *maxerr, int *blocks, double *ms)
{
    size_t total = 0, off;
    unsigned char *tmp = (unsigned char *)malloc(BLOCK * 8 + 64);
    int mid[BLOCK], side[BLOCK], dm[BLOCK], ds[BLOCK];
    double t0 = now_ms();
    *maxerr = 0;
    *blocks = 0;
    for (off = 0; off < frames; off += BLOCK)
    {
        int n = (int)((frames - off > BLOCK) ? BLOCK : (frames - off));
        int i, o1, k1, o2, k2;
        size_t b1, b2;
        for (i = 0; i < n; ++i)
        {
            int l = pcm[(off + i) * 2 + 0];
            int r = pcm[(off + i) * 2 + 1];
            side[i] = l - r;
            mid[i] = r + (side[i] >> 1);
        }
        b1 = rice_block(mid, n, tmp, BLOCK * 8 + 64, &o1, &k1);
        b2 = rice_block(side, n, tmp, BLOCK * 8 + 64, &o2, &k2);
        total += b1 + b2 + 2; /* +2 bytes de cabecera de bloque (longitud) */
        (*blocks)++;
        if (verify)
        {
            /* round-trip real de un bloque: recodificar y decodificar mid y side */
            unsigned char *buf = (unsigned char *)malloc(BLOCK * 8 + 64);
            size_t nb = rice_block(mid, n, buf, BLOCK * 8 + 64, &o1, &k1);
            rice_unblock(buf, nb, n, dm);
            nb = rice_block(side, n, buf, BLOCK * 8 + 64, &o2, &k2);
            rice_unblock(buf, nb, n, ds);
            for (i = 0; i < n; ++i)
            {
                int s = ds[i], m = dm[i];
                int r = m - (s >> 1);
                int l = r + s;
                long long e1 = (long long)labs((long)(l - pcm[(off + i) * 2 + 0]));
                long long e2 = (long long)labs((long)(r - pcm[(off + i) * 2 + 1]));
                if (e1 > *maxerr) *maxerr = e1;
                if (e2 > *maxerr) *maxerr = e2;
            }
            free(buf);
        }
    }
    free(tmp);
    *ms = now_ms() - t0;
    return (double)total;
}

static void report(const char *tag, double in_bytes, double out_bytes, double ms, const char *nota)
{
    double ratio = in_bytes / (out_bytes > 0 ? out_bytes : 1);
    double mbmin = out_bytes / (in_bytes / 192000.0) / 60.0 / 1048576.0 * 60.0;
    /* in_bytes/192000 = segundos (int16 48k estereo). MB por minuto de audio: */
    double secs = in_bytes / 192000.0;
    mbmin = (out_bytes / 1048576.0) / (secs / 60.0);
    printf("  %-34s ratio %6.2f:1   %8.3f MB/min   %s%s\n", tag, ratio, mbmin,
           ms > 0 ? "" : "", nota ? nota : "");
}

static int test_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    short *pcm;
    size_t frames, i;
    double secs;
    long long maxerr;
    int blocks;
    double ms, out;

    if (f == NULL) { printf("no puedo leer %s\n", path); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    pcm = (short *)malloc((size_t)size);
    if (fread(pcm, 1, (size_t)size, f) != (size_t)size) { printf("lectura corta\n"); return 1; }
    fclose(f);
    frames = (size_t)size / 4;
    secs = (double)frames / 48000.0;
    printf("material: %.2f s, int16 48k estereo, %ld bytes (%.2f MB/min sin comprimir)\n\n",
           secs, size, (double)size / 1048576.0 / (secs / 60.0));

    /* cuanta parte del material es digitalmente silenciosa */
    {
        size_t zeros = 0;
        for (i = 0; i < frames; ++i)
            if (pcm[i * 2] == 0 && pcm[i * 2 + 1] == 0) zeros++;
        printf("  frames en silencio digital exacto: %.1f%%\n\n", 100.0 * (double)zeros / (double)frames);
    }

    printf("SIN PERDIDA (mid/side + predictor fijo + Rice, bloques de %d)\n", BLOCK);
    out = lossless_pass(pcm, frames, 1, &maxerr, &blocks, &ms);
    report("sin perdida, todo el material", (double)size, out, ms, "");
    printf("  %-34s %lld  (0 = sin perdida de verdad)\n", "error maximo del round-trip:", maxerr);
    printf("  %-34s %.1f MB/s de audio comprimido\n", "velocidad:", ((double)size / 1048576.0) / (ms / 1000.0));
    printf("  %-34s %d bloques, acceso aleatorio a cualquiera\n\n", "granularidad:", blocks);

    /* mitades: la primera con audio, la segunda con lo que hubiera */
    {
        size_t half = frames / 2;
        long long me;
        int bl;
        double m2, o1, o2;
        o1 = lossless_pass(pcm, half, 0, &me, &bl, &m2);
        o2 = lossless_pass(pcm + half * 2, frames - half, 0, &me, &bl, &m2);
        printf("  primera mitad : ratio %.2f:1   (%.3f MB/min)\n",
               (double)(half * 4) / o1, (o1 / 1048576.0) / ((double)half / 48000.0 / 60.0));
        printf("  segunda mitad : ratio %.2f:1   (%.3f MB/min)\n\n",
               (double)((frames - half) * 4) / o2, (o2 / 1048576.0) / ((double)(frames - half) / 48000.0 / 60.0));
    }

    printf("CON PERDIDA (ratio fijo, cobran lo mismo por el silencio)\n");
    {
        double t0 = now_ms();
        long long err = 0;
        for (i = 0; i < frames * 2; ++i)
        {
            short d = mulaw_decode(mulaw_encode(pcm[i]));
            long long e = (long long)labs((long)(d - pcm[i]));
            if (e > err) err = e;
        }
        ms = now_ms() - t0;
        report("mu-law 8 bits, 48k estereo", (double)size, (double)(frames * 2), ms, "");
        printf("  %-34s %lld cuentas de 32768\n", "error maximo:", err);
    }
    {
        int pred = 0, index = 0;
        long long err = 0;
        double t0 = now_ms();
        int p2 = 0, i2 = 0;
        for (i = 0; i < frames; ++i)
        {
            int a = pcm[i * 2], b = pcm[i * 2 + 1];
            int before_a = pred, before_b = p2;
            (void)before_a; (void)before_b;
            ima_encode_sample(a, &pred, &index);
            ima_encode_sample(b, &p2, &i2);
            { long long e = (long long)labs((long)(pred - a)); if (e > err) err = e; }
        }
        ms = now_ms() - t0;
        report("IMA ADPCM 4 bits, 48k estereo", (double)size, (double)(frames), ms, "");
        printf("  %-34s %lld cuentas de 32768\n", "error maximo:", err);
    }
    printf("\nCOMBINADO PARA VOZ (submuestreo a 16k mono + ADPCM)\n");
    {
        /* 48k estereo -> 16k mono: promedio de canales y de cada 3 muestras.
           Filtro rudimentario, solo para estimar el tamaño. */
        size_t n16 = frames / 3;
        printf("  %-34s ratio %6.2f:1   %8.3f MB/min\n", "16k mono + ADPCM 4 bits",
               (double)size / (double)(n16 / 2),
               ((double)(n16 / 2) / 1048576.0) / (secs / 60.0));
        printf("  %-34s una hora por pista\n",
               "");
        printf("  %-34s %.1f MB\n", "  ->", ((double)(n16 / 2) / 1048576.0) / (secs / 3600.0));
    }
    free(pcm);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && strcmp(argv[1], "rec") == 0)
        return record_raw(atof(argv[2]), argv[3]);
    if (argc >= 3 && strcmp(argv[1], "test") == 0)
        return test_file(argv[2]);
    printf("uso: squeeze rec <segundos> <fichero.raw>\n     squeeze test <fichero.raw>\n");
    return 1;
}
