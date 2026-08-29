/* Linea de comandos del motor. Sirve para ejercitarlo entero sin interfaz.
 *
 *   ssb list
 *   ssb rec --secs 20 --src output --src app:WhatsApp --src input [opciones]
 *   ssb selftest
 */
#include "ssb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TRACKS 8

static const char *i_kind_str(ssb_src_kind k)
{
    switch (k)
    {
    case ssb_src_output_device:
        return "salida ";
    case ssb_src_input_device:
        return "entrada";
    case ssb_src_process:
        return "app    ";
    default:
        return "?      ";
    }
}

static int i_cmd_list(void)
{
    ssb_source list[256];
    uint32_t n, i, idx_out = 0, idx_in = 0;

    n = ssb_enumerate(list, 256);
    if (n > 256)
        n = 256;
    printf("%u fuente(s) disponibles:\n\n", (unsigned)n);
    for (i = 0; i < n; ++i)
    {
        const ssb_source *s = &list[i];
        char spec[SSB_NAME_MAX + 16];
        if (s->kind == ssb_src_output_device)
            snprintf(spec, sizeof(spec), "output:%u", (unsigned)idx_out++);
        else if (s->kind == ssb_src_input_device)
            snprintf(spec, sizeof(spec), "input:%u", (unsigned)idx_in++);
        else
            snprintf(spec, sizeof(spec), "app:%u", (unsigned)s->pid);
        printf("  %s  %-22s %s\n", i_kind_str(s->kind), spec, s->name);
    }
    printf("\nTambien valen: output, input, app:<nombre>  (p.ej. app:WhatsApp)\n");
    return 0;
}

/* --------------------------------------------------------------- grabacion */

static int i_cmd_rec(int argc, char **argv)
{
    const char *specs[MAX_TRACKS];
    uint32_t nspecs = 0;
    const char *outdir = "ssb-buffer";
    double secs = 10.0;
    double save_last = 0.0;
    ssb_track_config cfg;
    ssb_track *tracks[MAX_TRACKS];
    ssb_source srcs[MAX_TRACKS];
    char names[MAX_TRACKS][SSB_NAME_MAX];
    uint32_t ntracks = 0;
    int i, k;
    ssb_time t0;
    int tick = 0;

    ssb_track_config_default(&cfg);

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--src") == 0 && i + 1 < argc && nspecs < MAX_TRACKS)
            specs[nspecs++] = argv[++i];
        else if (strcmp(argv[i], "--secs") == 0 && i + 1 < argc)
            secs = atof(argv[++i]);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outdir = argv[++i];
        else if (strcmp(argv[i], "--buffer") == 0 && i + 1 < argc)
            cfg.max_seconds = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-mb") == 0 && i + 1 < argc)
            cfg.max_bytes = (uint64_t)atoi(argv[++i]) * 1024ull * 1024ull;
        else if (strcmp(argv[i], "--segment-kb") == 0 && i + 1 < argc)
            cfg.segment_bytes = (uint32_t)atoi(argv[++i]) * 1024u;
        else if (strcmp(argv[i], "--no-compress") == 0)
            cfg.compress = 0;
        else if (strcmp(argv[i], "--save-last") == 0 && i + 1 < argc)
            save_last = atof(argv[++i]);
        else
        {
            printf("opcion desconocida: %s\n", argv[i]);
            return 2;
        }
    }
    if (nspecs == 0)
        specs[nspecs++] = "output";

    if (ssb_mkdir(outdir) != ssb_ok)
    {
        printf("no puedo crear el directorio '%s'\n", outdir);
        return 1;
    }

    printf("buffer: %u s por pista, techo %.0f MB, segmentos de %u KB, compresion %s\n\n",
           (unsigned)cfg.max_seconds, (double)cfg.max_bytes / 1048576.0,
           (unsigned)(cfg.segment_bytes / 1024u), cfg.compress ? "si" : "no");

    for (k = 0; k < (int)nspecs; ++k)
    {
        char dir[512];
        ssb_res res = ssb_source_parse(specs[k], &srcs[ntracks]);
        if (res != ssb_ok)
        {
            printf("  [!] '%s': %s — se omite\n", specs[k], ssb_res_str(res));
            continue;
        }
        snprintf(names[ntracks], SSB_NAME_MAX, "pista%d", k);
        snprintf(dir, sizeof(dir), "%s/%s", outdir, names[ntracks]);
        res = ssb_track_create(names[ntracks], dir, &srcs[ntracks], &cfg, &tracks[ntracks]);
        if (res != ssb_ok)
        {
            printf("  [!] '%s' (%s): %s — se omite\n", specs[k], srcs[ntracks].name, ssb_res_str(res));
            continue;
        }
        {
            ssb_track_stats st;
            ssb_track_stats_get(tracks[ntracks], &st);
            printf("  [%u] %s  %-8s %s  (%u ch, %u Hz)\n", (unsigned)ntracks,
                   i_kind_str(srcs[ntracks].kind), specs[k], srcs[ntracks].name,
                   (unsigned)st.channels, (unsigned)st.rate);
        }
        ntracks++;
    }
    if (ntracks == 0)
    {
        printf("\nninguna fuente pudo abrirse\n");
        return 1;
    }

    printf("\ngrabando %.1f s...\n\n", secs);
    t0 = ssb_now();
    for (;;)
    {
        double el = ssb_time_to_sec(ssb_now() - t0);
        if (el >= secs)
            break;
        if ((int)el > tick)
        {
            uint32_t j;
            tick = (int)el;
            printf("  t=%3ds", tick);
            for (j = 0; j < ntracks; ++j)
            {
                ssb_track_stats st;
                ssb_track_stats_get(tracks[j], &st);
                printf(" | [%u] %.1fs %.1fKB x%.1f pico %.3f",
                       (unsigned)j, (double)st.frames / (double)(st.rate ? st.rate : 1),
                       (double)st.disk_bytes / 1024.0, st.ratio, st.peak);
            }
            printf("\n");
        }
        /* El trabajo de verdad esta en los hilos de captura: aqui solo se espera. */
        ssb_sleep(50);
    }

    /* Ventana comun a todas las pistas: la interseccion de sus tramos. Guardar
       todas sobre la MISMA ventana es lo que hace que los WAV sean comparables
       entre si; guardar cada una sobre su propio tramo las desalinea. */
    {
        ssb_time win_from = 0, win_to = 0;
        int have = 0;
        for (k = 0; k < (int)ntracks; ++k)
        {
            ssb_time a = 0, b = 0;
            if (ssb_track_span(tracks[k], &a, &b) != ssb_ok)
                continue;
            if (!have)
            {
                win_from = a;
                win_to = b;
                have = 1;
            }
            else
            {
                if (a > win_from)
                    win_from = a;
                if (b < win_to)
                    win_to = b;
            }
        }
        if (save_last > 0.0 && have)
        {
            ssb_time want = (ssb_time)(save_last * (double)SSB_TICKS_PER_SEC);
            if (win_to > win_from + want)
                win_from = win_to - want;
        }

        printf("\nresumen:\n");
        if (have && win_to > win_from)
            printf("  ventana comun a todas las pistas: %.3f s\n", ssb_time_to_sec(win_to - win_from));

        for (k = 0; k < (int)ntracks; ++k)
        {
            ssb_track_stats st;
            ssb_time from = 0, to = 0;
            char path[512];
            ssb_res res;

            ssb_track_stats_get(tracks[k], &st);
            printf("\n  [%d] %s\n", k, srcs[k].name);
            printf("      %u ch a %u Hz, %.2f s capturados\n",
                   (unsigned)st.channels, (unsigned)st.rate,
                   (double)st.frames / (double)(st.rate ? st.rate : 1));
            printf("      disco %.1f KB ahora (escritos %.1f KB de %.1f KB crudos  ->  ratio %.2f:1)\n",
                   (double)st.disk_bytes / 1024.0, (double)st.written_bytes / 1024.0,
                   (double)st.raw_bytes / 1024.0, st.ratio);
            printf("      bloques %u vivos, %u descartados, %u en silencio\n",
                   (unsigned)st.blocks, (unsigned)st.dropped, (unsigned)st.silent_blocks);
            printf("      discontinuidades %u, reanclas %u, pico %.4f\n", (unsigned)st.discont, (unsigned)st.reanchors, st.peak);
            printf("      frecuencia efectiva %.1f Hz de %u nominales (%+.3f %%)\n",
                   st.eff_rate, (unsigned)st.rate,
                   st.rate ? 100.0 * (st.eff_rate - st.rate) / st.rate : 0.0);

            if (ssb_track_span(tracks[k], &from, &to) != ssb_ok)
            {
                printf("      el buffer esta vacio\n");
                continue;
            }
            printf("      buffer cubre %.2f s\n", ssb_time_to_sec(to - from));
            if (!have || win_to <= win_from)
                continue;
            snprintf(path, sizeof(path), "%s/%s.wav", outdir, names[k]);
            res = ssb_track_save_wav(tracks[k], win_from, win_to, path);
            if (res == ssb_ok)
                printf("      guardado %s (%.3f s de la ventana comun)\n",
                       path, ssb_time_to_sec(win_to - win_from));
            else
                printf("      no se pudo guardar: %s\n", ssb_res_str(res));
        }
    }

    for (k = 0; k < (int)ntracks; ++k)
        ssb_track_destroy(&tracks[k]);
    printf("\n");
    return 0;
}


/* ------------------------------------------------------------------ deriva */

/* Graba unas fuentes y vuelca la serie de sincronizacion de cada una. Sirve para
   distinguir un desfase de arranque (escalon) de una deriva de reloj (pendiente). */
static int i_cmd_drift(int argc, char **argv)
{
    const char *specs[MAX_TRACKS];
    uint32_t nspecs = 0;
    const char *csv = NULL;
    double secs = 20.0;
    ssb_track_config cfg;
    ssb_track *tracks[MAX_TRACKS];
    ssb_source srcs[MAX_TRACKS];
    uint32_t ntracks = 0;
    int i, k;
    ssb_time t0;
    static ssb_drift_sample buf[SSB_DRIFT_SAMPLES];
    FILE *f = NULL;

    ssb_track_config_default(&cfg);
    cfg.max_seconds = 30;

    for (i = 0; i < argc; ++i)
    {
        if (strcmp(argv[i], "--src") == 0 && i + 1 < argc && nspecs < MAX_TRACKS)
            specs[nspecs++] = argv[++i];
        else if (strcmp(argv[i], "--secs") == 0 && i + 1 < argc)
            secs = atof(argv[++i]);
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
            csv = argv[++i];
        else
        {
            printf("opcion desconocida: %s\n", argv[i]);
            return 2;
        }
    }
    if (nspecs == 0)
        specs[nspecs++] = "output";

    if (ssb_mkdir("ssb-drift") != ssb_ok)
        return 1;

    for (k = 0; k < (int)nspecs; ++k)
    {
        char dir[512];
        if (ssb_source_parse(specs[k], &srcs[ntracks]) != ssb_ok)
        {
            printf("  [!] '%s' no se pudo resolver\n", specs[k]);
            continue;
        }
        snprintf(dir, sizeof(dir), "ssb-drift/d%d", k);
        if (ssb_track_create("d", dir, &srcs[ntracks], &cfg, &tracks[ntracks]) != ssb_ok)
        {
            printf("  [!] '%s' no se pudo abrir\n", specs[k]);
            continue;
        }
        printf("  [%u] %-22s %s\n", (unsigned)ntracks, specs[k], srcs[ntracks].name);
        ntracks++;
    }
    if (ntracks == 0)
        return 1;

    printf("\nmidiendo %.0f s...\n\n", secs);
    t0 = ssb_now();
    while (ssb_time_to_sec(ssb_now() - t0) < secs)
        ssb_sleep(100);

    if (csv != NULL)
    {
        f = fopen(csv, "wb");
        if (f != NULL)
            fprintf(f, "pista,muestra,t_s,frames,paquete,silencio,deriva_ms\n");
    }

    for (k = 0; k < (int)ntracks; ++k)
    {
        uint32_t n = ssb_track_drift(tracks[k], buf, SSB_DRIFT_SAMPLES);
        ssb_track_stats st;
        uint32_t j;
        double first = 0.0, last = 0.0, mx = 0.0, mn = 0.0;
        uint32_t jumps = 0;

        ssb_track_stats_get(tracks[k], &st);
        printf("  [%d] %s  (%u muestras, %u ch, %u Hz)\n", k, srcs[k].name,
               (unsigned)n, (unsigned)st.channels, (unsigned)st.rate);
        if (n == 0)
            continue;

        for (j = 0; j < n; ++j)
        {
            double d = buf[j].drift_ms;
            if (j == 0)
            {
                first = d;
                mx = d;
                mn = d;
            }
            if (d > mx)
                mx = d;
            if (d < mn)
                mn = d;
            if (j > 0 && (buf[j].drift_ms - buf[j - 1].drift_ms) > 5.0)
                jumps++;
            last = d;
            if (f != NULL)
                fprintf(f, "%d,%u,%.4f,%llu,%u,%d,%.4f\n", k, (unsigned)j,
                        ssb_time_to_sec(buf[j].reported - buf[0].reported),
                        (unsigned long long)buf[j].frames, (unsigned)buf[j].packet,
                        buf[j].silent, d);
        }
        /* Perfil rapido: la deriva en cada decil de la grabacion. */
        printf("       perfil:");
        for (j = 0; j < 10; ++j)
            printf(" %.1f", buf[(uint64_t)j * n / 10].drift_ms);
        printf("  (ms, por deciles)\n");
        printf("       primera %.2f  ultima %.2f  min %.2f  max %.2f  saltos>5ms %u\n",
               first, last, mn, mx, (unsigned)jumps);
        {
            double half = buf[n / 2].drift_ms;
            const char *forma;
            if (mx - mn < 2.0)
                forma = "plano: ni desfase ni deriva";
            else if (jumps <= 2 && last > 5.0 && half > last * 0.7)
                forma = "ESCALON: se desfasa de golpe y se queda ahi";
            else if (jumps <= 2 && half > 0.0 && half < last * 0.7)
                forma = "PENDIENTE: deriva de reloj, crece con el tiempo";
            else
                forma = "irregular: saltos repetidos";
            printf("       forma: %s\n", forma);
        }
    }
    if (f != NULL)
    {
        fclose(f);
        printf("\n  serie completa en %s\n", csv);
    }
    for (k = 0; k < (int)ntracks; ++k)
        ssb_track_destroy(&tracks[k]);
    printf("\n");
    return 0;
}

/* ---------------------------------------------------------------- selftest */


static uint32_t i_rnd_state = 12345u;
static uint32_t i_rnd(void)
{
    i_rnd_state = i_rnd_state * 1103515245u + 12345u;
    return (i_rnd_state >> 8);
}

static int i_fails = 0;

static void i_check(int cond, const char *what)
{
    printf("  %-58s %s\n", what, cond ? "ok" : "FALLA");
    if (!cond)
        i_fails++;
}

/* El mismo caso, a 24 bits. Va aparte porque lo que hay que demostrar es
   distinto: no solo que el ida y vuelta sea exacto, sino que subir la
   resolucion NO tira la compresion por tierra — que es lo que uno teme al
   permitir 24 bits en un buffer circular de horas. */
static int i_codec_case24(const char *what, const int32_t *pcm, uint32_t frames, uint32_t ch)
{
    static uint8_t enc[1 + SSB_BLOCK_FRAMES * SSB_MAX_CHANNELS * 4 + 64];
    static int32_t dec[SSB_BLOCK_FRAMES * SSB_MAX_CHANNELS];
    size_t n = ssb_codec_encode32(pcm, frames, ch, 1, 4, enc, sizeof(enc));
    ssb_res res;
    uint32_t i, total = frames * ch;
    int exact = 1;

    if (n == 0)
    {
        printf("  %-40s codificar FALLA\n", what);
        i_fails++;
        return 0;
    }
    res = ssb_codec_decode32(enc, n, frames, ch, 4, dec);
    if (res != ssb_ok)
    {
        printf("  %-40s decodificar FALLA (%s)\n", what, ssb_res_str(res));
        i_fails++;
        return 0;
    }
    for (i = 0; i < total; ++i)
    {
        if (dec[i] != pcm[i])
        {
            exact = 0;
            break;
        }
    }
    printf("  %-40s %8u -> %6u bytes  ratio %7.2f:1  %s\n", what,
           (unsigned)(total * 3), (unsigned)n, (double)(total * 3) / (double)n,
           exact ? "identico" : "DIFIERE");
    if (!exact)
        i_fails++;
    return exact;
}

static int i_codec_case(const char *what, const int16_t *pcm, uint32_t frames, uint32_t ch)
{
    static uint8_t enc[1 + SSB_BLOCK_FRAMES * SSB_MAX_CHANNELS * 2 + 64];
    static int16_t dec[SSB_BLOCK_FRAMES * SSB_MAX_CHANNELS];
    size_t n = ssb_codec_encode(pcm, frames, ch, enc, sizeof(enc));
    ssb_res res;
    uint32_t i, total = frames * ch;
    int exact = 1;

    if (n == 0)
    {
        printf("  %-40s codificar FALLA\n", what);
        i_fails++;
        return 0;
    }
    res = ssb_codec_decode(enc, n, frames, ch, dec);
    if (res != ssb_ok)
    {
        printf("  %-40s decodificar FALLA (%s)\n", what, ssb_res_str(res));
        i_fails++;
        return 0;
    }
    for (i = 0; i < total; ++i)
    {
        if (dec[i] != pcm[i])
        {
            exact = 0;
            break;
        }
    }
    printf("  %-40s %8u -> %6u bytes  ratio %7.2f:1  %s\n", what,
           (unsigned)(total * 2), (unsigned)n, (double)(total * 2) / (double)n,
           exact ? "identico" : "DIFIERE");
    if (!exact)
        i_fails++;
    if (n > ssb_codec_max_size(frames, ch))
    {
        printf("  %-40s AGRANDA por encima del maximo\n", what);
        i_fails++;
    }
    return exact;
}

static ssb_res i_count_blocks(void *ctx, ssb_time t, const int32_t *pcm, uint32_t frames)
{
    uint32_t *n = (uint32_t *)ctx;
    (void)t;
    (void)pcm;
    (void)frames;
    (*n)++;
    return ssb_ok;
}

typedef struct
{
    int ok;
    uint32_t seen;
} i_verify_ctx;

static ssb_res i_verify_block(void *ctx, ssb_time t, const int32_t *pcm, uint32_t frames)
{
    i_verify_ctx *v = (i_verify_ctx *)ctx;
    uint32_t i;
    (void)t;
    for (i = 0; i < frames; ++i)
    {
        if (pcm[i * 2] != (int16_t)(i * 3) || pcm[i * 2 + 1] != (int16_t)(i * 7))
        {
            v->ok = 0;
            return ssb_ok;
        }
    }
    v->seen++;
    return ssb_ok;
}

/* ---- WAV de juguete para probar el mezclador ----------------------------
   La cabecera se escribe a mano, 44 bytes canonicos, en vez de usar el escritor
   del motor: asi la prueba comprueba que el mezclador sabe leer un WAV, y no
   solo que sabe leer lo que nosotros escribimos. */
static int i_wav_put(const char *path, uint32_t ch, uint32_t rate, const int16_t *pcm, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t dbytes = frames * ch * 2u;
    uint8_t h[44];
    uint32_t v;
    if (f == NULL)
        return 0;
    memcpy(h, "RIFF", 4);
    v = 36u + dbytes;
    h[4] = (uint8_t)v; h[5] = (uint8_t)(v >> 8); h[6] = (uint8_t)(v >> 16); h[7] = (uint8_t)(v >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;
    h[20] = 1; h[21] = 0;
    h[22] = (uint8_t)ch; h[23] = 0;
    h[24] = (uint8_t)rate; h[25] = (uint8_t)(rate >> 8); h[26] = (uint8_t)(rate >> 16); h[27] = (uint8_t)(rate >> 24);
    v = rate * ch * 2u;
    h[28] = (uint8_t)v; h[29] = (uint8_t)(v >> 8); h[30] = (uint8_t)(v >> 16); h[31] = (uint8_t)(v >> 24);
    h[32] = (uint8_t)(ch * 2u); h[33] = 0;
    h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)dbytes; h[41] = (uint8_t)(dbytes >> 8); h[42] = (uint8_t)(dbytes >> 16); h[43] = (uint8_t)(dbytes >> 24);
    fwrite(h, 1, 44, f);
    fwrite(pcm, 2, (size_t)frames * ch, f);
    fclose(f);
    return 1;
}

static uint32_t i_wav_get(const char *path, int16_t *pcm, uint32_t cap, uint32_t *ch)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[44];
    uint32_t n;
    if (f == NULL)
        return 0;
    if (fread(h, 1, 44, f) != 44)
    {
        fclose(f);
        return 0;
    }
    *ch = (uint32_t)h[22] | ((uint32_t)h[23] << 8);
    n = (uint32_t)fread(pcm, 2, cap, f);
    fclose(f);
    return (*ch == 0) ? 0 : n / (*ch);
}

static int i_cmd_selftest(void)
{
    static int16_t pcm[SSB_BLOCK_FRAMES * SSB_MAX_CHANNELS];
    uint32_t i;

    printf("\n== codec: round-trip exacto y ratios ==\n");

    memset(pcm, 0, sizeof(pcm));
    i_codec_case("silencio digital, estereo", pcm, SSB_BLOCK_FRAMES, 2);

    for (i = 0; i < SSB_BLOCK_FRAMES * 2; ++i)
        pcm[i] = 1234;
    i_codec_case("constante distinta de cero, estereo", pcm, SSB_BLOCK_FRAMES, 2);

    for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
    {
        pcm[i * 2] = (int16_t)(i & 0x7FFF);
        pcm[i * 2 + 1] = (int16_t)(-(int)(i & 0x7FFF));
    }
    i_codec_case("rampa, estereo", pcm, SSB_BLOCK_FRAMES, 2);

    for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
    {
        /* Seno de 1 kHz a 48 kHz, aproximado con enteros para no depender de libm. */
        static const int tab[16] = { 0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
                                     0, -12539, -23170, -30273, -32767, -30273, -23170, -12539 };
        int16_t v = (int16_t)(tab[i % 16] / 2);
        pcm[i * 2] = v;
        pcm[i * 2 + 1] = (int16_t)(v / 2);
    }
    i_codec_case("tono periodico, estereo", pcm, SSB_BLOCK_FRAMES, 2);

    for (i = 0; i < SSB_BLOCK_FRAMES * 2; ++i)
        pcm[i] = (int16_t)(i_rnd() & 0xFFFF);
    i_codec_case("ruido blanco (peor caso), estereo", pcm, SSB_BLOCK_FRAMES, 2);

    for (i = 0; i < SSB_BLOCK_FRAMES * 2; ++i)
        pcm[i] = (int16_t)((i & 1) ? 32767 : -32768);
    i_codec_case("extremos alternos, estereo", pcm, SSB_BLOCK_FRAMES, 2);

    for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
        pcm[i] = (int16_t)(i_rnd() & 0x0FFF);
    i_codec_case("ruido de baja amplitud, mono", pcm, SSB_BLOCK_FRAMES, 1);

    memset(pcm, 0, sizeof(pcm));
    i_codec_case("bloque corto de 7 frames, estereo", pcm, 7, 2);

    /* --- 24 bits: la resolucion alta tiene que ser igual de exacta ---
       Se usa un seno a escala completa de 24 bits, que es lo que de verdad se
       guardara. Lo que se vigila no es solo la exactitud: es que el ratio siga
       siendo util, porque de eso depende que un buffer de horas quepa. */
    {
        static int32_t p24[SSB_BLOCK_FRAMES * SSB_MAX_CHANNELS];
        static const int tab[16] = { 0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
                                     0, -12539, -23170, -30273, -32767, -30273, -23170, -12539 };
        for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
        {
            int32_t v = tab[i % 16] * 256;   /* mismo tono, escala de 24 bits */
            p24[i * 2] = v;
            p24[i * 2 + 1] = v / 2;
        }
        i_codec_case24("24 bits: tono periodico, estereo", p24, SSB_BLOCK_FRAMES, 2);

        for (i = 0; i < SSB_BLOCK_FRAMES * 2; ++i)
            p24[i] = (int32_t)(i_rnd() & 0xFFFFFF) - 0x800000;
        i_codec_case24("24 bits: ruido blanco (peor caso)", p24, SSB_BLOCK_FRAMES, 2);

        for (i = 0; i < SSB_BLOCK_FRAMES * 2; ++i)
            p24[i] = (i & 1) ? 8388607 : -8388608;
        i_codec_case24("24 bits: extremos alternos", p24, SSB_BLOCK_FRAMES, 2);

        for (i = 0; i < SSB_BLOCK_FRAMES * 2; ++i)
            p24[i] = 0;
        i_codec_case24("24 bits: silencio", p24, SSB_BLOCK_FRAMES, 2);
    }

    printf("\n== ring: escritura, presupuesto y lectura ==\n");
    {
        ssb_ring *r = NULL;
        ssb_ring_config rc;
        static uint8_t enc[1 + SSB_BLOCK_FRAMES * 2 * 2 + 64];
        ssb_res res;
        uint32_t blocks_written = 3000; /* de sobra para desbordar el techo y forzar descartes */
        size_t n;
        ssb_time from = 0, to = 0;

        memset(&rc, 0, sizeof(rc));
        rc.channels = 2;
        rc.rate = 48000;
        rc.max_bytes = 256u * 1024u; /* muy pequeno a proposito: fuerza descartes */
        rc.max_seconds = 0;
        rc.segment_bytes = 64u * 1024u;

        res = ssb_ring_create("ssb-selftest-ring", &rc, &r);
        i_check(res == ssb_ok, "crear el ring");
        if (res != ssb_ok)
            return 1;

        for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
        {
            pcm[i * 2] = (int16_t)(i * 3);
            pcm[i * 2 + 1] = (int16_t)(i * 7);
        }
        n = ssb_codec_encode(pcm, SSB_BLOCK_FRAMES, 2, enc, sizeof(enc));
        i_check(n > 0, "codificar el bloque de prueba");

        for (i = 0; i < blocks_written; ++i)
        {
            ssb_time t = (ssb_time)i * SSB_BLOCK_FRAMES * SSB_TICKS_PER_SEC / 48000;
            if (ssb_ring_append(r, t, enc, (uint32_t)n, SSB_BLOCK_FRAMES) != ssb_ok)
                break;
        }
        i_check(ssb_ring_blocks(r) < blocks_written, "el circular descarta lo viejo");
        printf("       (%u bloques escritos, %u vivos, %u descartados, %.0f KB en disco)\n",
               (unsigned)blocks_written, (unsigned)ssb_ring_blocks(r),
               (unsigned)ssb_ring_dropped(r), (double)ssb_ring_bytes(r) / 1024.0);
        i_check(ssb_ring_dropped(r) > 0, "cuenta los bloques descartados");
        i_check(ssb_ring_bytes(r) <= rc.max_bytes + rc.segment_bytes,
                "se mantiene dentro del presupuesto de bytes");
        /* Lo vivo no es lo escrito. Se confundieron una vez: la pista sumaba
           cada bloque codificado y ensenaba ese total como "disco", asi que la
           cifra subia sin parar aunque el circular estuviera descartando bien. */
        i_check(ssb_ring_bytes(r) < (uint64_t)blocks_written * (uint64_t)n,
                "los bytes vivos no son los bytes escritos");

        i_check(ssb_ring_span(r, &from, &to) == ssb_ok, "el tramo cubierto es consultable");
        i_check(to > from, "el tramo cubierto no esta vacio");
        i_check(from > 0, "el principio del buffer avanza al descartar");

        {
            uint32_t seen = 0;
            i_check(ssb_ring_read(r, from, to, i_count_blocks, &seen) == ssb_ok,
                    "leer todo el tramo");
            i_check(seen == ssb_ring_blocks(r), "se leen todos los bloques vivos");
        }
        {
            i_verify_ctx v;
            v.ok = 1;
            v.seen = 0;
            ssb_ring_read(r, from, to, i_verify_block, &v);
            i_check(v.ok == 1, "lo que sale del ring es identico a lo que entro");
        }
        {
            /* Un tramo de un solo bloque en mitad del buffer. */
            uint32_t seen = 0;
            ssb_time mid = from + (to - from) / 2;
            ssb_time end = mid + SSB_TICKS_PER_SEC / 100; /* 10 ms */
            ssb_ring_read(r, mid, end, i_count_blocks, &seen);
            i_check(seen >= 1 && seen <= 2, "acceso aleatorio a un tramo corto");
        }
        ssb_ring_destroy(&r);
    }

    /* Este caso es el que fallaba: buffer corto de duracion y segmentos grandes.
       Si el segmento no rota por tiempo, el limite de duracion es inaplicable y
       el indice circular acaba sobrescribiendose. */
    printf("\n== ring: el limite de duracion manda aunque el segmento sea enorme ==\n");
    {
        ssb_ring *r = NULL;
        ssb_ring_config rc;
        static uint8_t enc[1 + SSB_BLOCK_FRAMES * 2 * 2 + 64];
        size_t n;
        ssb_time from = 0, to = 0;
        double span;
        uint32_t written = 400; /* 34 s de audio en un buffer de 8 s */

        memset(&rc, 0, sizeof(rc));
        rc.channels = 2;
        rc.rate = 48000;
        rc.max_bytes = 0;                  /* sin techo de bytes: manda la duracion */
        rc.max_seconds = 8;
        rc.segment_bytes = 4u * 1024u * 1024u; /* mas grande que el buffer entero */

        i_check(ssb_ring_create("ssb-selftest-dur", &rc, &r) == ssb_ok, "crear el ring");
        for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
        {
            pcm[i * 2] = (int16_t)(i * 3);
            pcm[i * 2 + 1] = (int16_t)(i * 7);
        }
        n = ssb_codec_encode(pcm, SSB_BLOCK_FRAMES, 2, enc, sizeof(enc));
        for (i = 0; i < written; ++i)
        {
            ssb_time t = (ssb_time)i * SSB_BLOCK_FRAMES * SSB_TICKS_PER_SEC / 48000;
            if (ssb_ring_append(r, t, enc, (uint32_t)n, SSB_BLOCK_FRAMES) != ssb_ok)
                break;
        }
        i_check(ssb_ring_span(r, &from, &to) == ssb_ok, "el tramo es consultable");
        span = ssb_time_to_sec(to - from);
        printf("       (%u bloques escritos = %.1f s; el buffer cubre %.2f s)\n",
               (unsigned)written, (double)written * SSB_BLOCK_FRAMES / 48000.0, span);
        /* Contrato: se conserva SIEMPRE lo pedido, y como mucho un segmento de
           mas. Que no se quede corto es lo que permite a la interfaz ensenar una
           ventana de esa duracion exacta sin huecos por la izquierda. */
        i_check(span >= 8.0 * 0.99, "el buffer nunca se queda corto");
        i_check(span <= 8.0 * 1.15, "y no guarda mucho mas de lo pedido");
        i_check(ssb_ring_dropped(r) > 0, "hubo descartes");
        {
            i_verify_ctx v;
            v.ok = 1;
            v.seen = 0;
            ssb_ring_read(r, from, to, i_verify_block, &v);
            i_check(v.ok == 1, "lo que sobrevive sigue siendo identico");
        }
        ssb_ring_destroy(&r);
    }


    printf("\n== exportacion: el WAV cubre el tramo pedido aunque falten frames ==\n");
    {
        ssb_ring *r = NULL;
        ssb_ring_config rc;
        static uint8_t enc[1 + SSB_BLOCK_FRAMES * 2 * 2 + 64];
        size_t n;
        ssb_time from = 0, to = 0;
        uint64_t filled = 0;
        const ssb_time BLOCK_T = (ssb_time)SSB_BLOCK_FRAMES * SSB_TICKS_PER_SEC / 48000;
        const ssb_time HOLE = SSB_TICKS_PER_SEC / 2; /* medio segundo perdido */
        ssb_time t = 0;
        FILE *f;
        long size = 0;
        double span_s, wav_s;

        memset(&rc, 0, sizeof(rc));
        rc.channels = 2;
        rc.rate = 48000;
        rc.max_seconds = 60;
        rc.segment_bytes = 1024u * 1024u;
        i_check(ssb_ring_create("ssb-selftest-wav", &rc, &r) == ssb_ok, "crear el ring");

        for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
        {
            pcm[i * 2] = (int16_t)(i * 3);
            pcm[i * 2 + 1] = (int16_t)(i * 7);
        }
        n = ssb_codec_encode(pcm, SSB_BLOCK_FRAMES, 2, enc, sizeof(enc));

        /* 20 bloques seguidos, un hueco de medio segundo, y 20 mas. */
        for (i = 0; i < 40; ++i)
        {
            if (i == 20)
                t += HOLE;
            ssb_ring_append(r, t, enc, (uint32_t)n, SSB_BLOCK_FRAMES);
            t += BLOCK_T;
        }
        i_check(ssb_ring_span(r, &from, &to) == ssb_ok, "el tramo es consultable");
        i_check(ssb_ring_save_wav(r, 2, 48000, from, to, "ssb-selftest-wav/out.wav", &filled) == ssb_ok,
                "exportar el tramo entero");

        f = fopen("ssb-selftest-wav/out.wav", "rb");
        if (f != NULL)
        {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fclose(f);
        }
        span_s = ssb_time_to_sec(to - from);
        wav_s = (double)(size - 44) / 4.0 / 48000.0;
        printf("       (tramo %.4f s; WAV %.4f s; silencio insertado %.4f s)\n",
               span_s, wav_s, (double)filled / 48000.0);
        i_check(size > 44, "el WAV tiene datos");
        i_check(wav_s > span_s - 0.002 && wav_s < span_s + 0.002,
                "la duracion del WAV es la del tramo pedido");
        i_check(filled > 20000 && filled < 28000, "se relleno justo el hueco (0.5 s)");

        /* Un tramo interior arbitrario tiene que salir con su duracion exacta. */
        {
            ssb_time a = from + BLOCK_T * 5 + BLOCK_T / 3;
            ssb_time b = a + SSB_TICKS_PER_SEC;
            i_check(ssb_ring_save_wav(r, 2, 48000, a, b, "ssb-selftest-wav/cut.wav", &filled) == ssb_ok,
                    "exportar un tramo interior arbitrario");
            f = fopen("ssb-selftest-wav/cut.wav", "rb");
            if (f != NULL)
            {
                fseek(f, 0, SEEK_END);
                size = ftell(f);
                fclose(f);
            }
            wav_s = (double)(size - 44) / 4.0 / 48000.0;
            printf("       (recorte de 1.0000 s -> WAV de %.4f s)\n", wav_s);
            i_check(wav_s > 0.998 && wav_s < 1.002, "el recorte dura exactamente lo pedido");
        }
        ssb_ring_destroy(&r);
    }


    printf("\n== ring: cambiar la duracion en caliente ==\n");
    {
        ssb_ring *r = NULL;
        ssb_ring_config rc;
        static uint8_t enc[1 + SSB_BLOCK_FRAMES * 2 * 2 + 64];
        size_t n;
        ssb_time from = 0, to = 0, t = 0;
        const ssb_time BT = (ssb_time)SSB_BLOCK_FRAMES * SSB_TICKS_PER_SEC / 48000;
        double span;

        memset(&rc, 0, sizeof(rc));
        rc.channels = 2;
        rc.rate = 48000;
        rc.max_seconds = 20;
        rc.segment_bytes = 1024u * 1024u;
        i_check(ssb_ring_create("ssb-selftest-resize", &rc, &r) == ssb_ok, "crear el ring de 20 s");

        for (i = 0; i < SSB_BLOCK_FRAMES; ++i)
        {
            pcm[i * 2] = (int16_t)(i * 3);
            pcm[i * 2 + 1] = (int16_t)(i * 7);
        }
        n = ssb_codec_encode(pcm, SSB_BLOCK_FRAMES, 2, enc, sizeof(enc));
        for (i = 0; i < 500; ++i) /* 42 s: de sobra para llenar 20 s */
        {
            ssb_ring_append(r, t, enc, (uint32_t)n, SSB_BLOCK_FRAMES);
            t += BT;
        }
        ssb_ring_span(r, &from, &to);
        span = ssb_time_to_sec(to - from);
        printf("       (con 20 s pedidos, el buffer cubre %.2f s)\n", span);
        i_check(span >= 20.0 * 0.99 && span <= 20.0 * 1.15, "arranca con la duracion pedida");

        /* Encoger en caliente: lo que sobra se descarta por el principio. */
        i_check(ssb_ring_set_seconds(r, 5) == ssb_ok, "reducir a 5 s en caliente");
        ssb_ring_append(r, t, enc, (uint32_t)n, SSB_BLOCK_FRAMES);
        t += BT;
        ssb_ring_span(r, &from, &to);
        span = ssb_time_to_sec(to - from);
        printf("       (tras reducir a 5 s, cubre %.2f s)\n", span);
        i_check(span <= 20.0 * 0.60, "el buffer se encoge de verdad");
        i_check(span >= 5.0, "y NUNCA se queda por debajo de lo pedido");

        /* Ampliar: vuelve a crecer desde ahora. */
        i_check(ssb_ring_set_seconds(r, 15) == ssb_ok, "ampliar a 15 s en caliente");
        for (i = 0; i < 300; ++i)
        {
            ssb_ring_append(r, t, enc, (uint32_t)n, SSB_BLOCK_FRAMES);
            t += BT;
        }
        ssb_ring_span(r, &from, &to);
        span = ssb_time_to_sec(to - from);
        printf("       (tras ampliar a 15 s, cubre %.2f s)\n", span);
        i_check(span >= 15.0 * 0.99 && span <= 15.0 * 1.15, "el buffer vuelve a crecer");

        {
            i_verify_ctx v;
            v.ok = 1;
            v.seen = 0;
            ssb_ring_read(r, from, to, i_verify_block, &v);
            i_check(v.ok == 1, "lo que queda sigue siendo identico");
        }
        ssb_ring_destroy(&r);
    }

    printf("\n== mezcla ==\n");
    {
        /* El mezclador tuvo dos fallos reales y ninguna prueba que los pillara.
           Uno solo aparecia al mezclar una fuente MONO con una estereo —un
           microfono con una salida, que es lo normal— y el otro solo si una
           entrada se quedaba corta. Los dos se comprueban aqui, y sin depender
           de que haya un microfono conectado a la maquina. */
        enum
        {
            MIXN = 10000
        }; /* mas de dos bloques: es donde aparecen los desfases */
        static int16_t sa[MIXN * 2];
        static int16_t sb[MIXN];
        static int16_t sm[MIXN * 2 + 16];
        const char *plist[2];
        double gain = -1.0;
        uint32_t got = 0, mch = 0, bad = 0;

        for (i = 0; i < MIXN; ++i)
        {
            sa[i * 2] = (int16_t)(i % 1000);
            sa[i * 2 + 1] = (int16_t)(-(int)(i % 1000));
            sb[i] = (int16_t)(i % 700);
        }
        i_check(i_wav_put("ssb-selftest-mix-a.wav", 2, 48000, sa, MIXN) == 1, "escribir un WAV estereo");
        i_check(i_wav_put("ssb-selftest-mix-b.wav", 1, 48000, sb, MIXN) == 1, "escribir un WAV mono");

        plist[0] = "ssb-selftest-mix-a.wav";
        plist[1] = "ssb-selftest-mix-b.wav";
        i_check(ssb_mix_wavs(plist, 2, "ssb-selftest-mix-o.wav", &gain) == ssb_ok,
                "mono + estereo se pueden mezclar");
        got = i_wav_get("ssb-selftest-mix-o.wav", sm, MIXN * 2 + 16, &mch);
        i_check(mch == 2, "la mezcla sale con los canales del que mas tiene");
        i_check(got == MIXN, "la mezcla dura lo mismo que las entradas");
        i_check(gain == 1.0, "no hizo falta bajar la ganancia");

        /* La mono va a los DOS canales, y frame a frame: si el mezclador se
           desfasa en algun bloque, esto lo caza. */
        for (i = 0; i < got && i < MIXN; ++i)
        {
            if (sm[i * 2] != (int16_t)(sa[i * 2] + sb[i]))
                bad++;
            if (sm[i * 2 + 1] != (int16_t)(sa[i * 2 + 1] + sb[i]))
                bad++;
        }
        i_check(bad == 0, "cada muestra es la suma exacta, con la mono en los dos canales");

        /* Frecuencias distintas: negarse, y con un error que se pueda explicar.
           Juntarlas sin remuestrear pondria una de las dos a otra velocidad. */
        i_check(i_wav_put("ssb-selftest-mix-c.wav", 2, 44100, sa, MIXN) == 1, "escribir un WAV a 44100");
        plist[1] = "ssb-selftest-mix-c.wav";
        i_check(ssb_mix_wavs(plist, 2, "ssb-selftest-mix-o2.wav", &gain) == ssb_err_format,
                "frecuencias distintas se rechazan con ssb_err_format");

        remove("ssb-selftest-mix-a.wav");
        remove("ssb-selftest-mix-b.wav");
        remove("ssb-selftest-mix-c.wav");
        remove("ssb-selftest-mix-o.wav");
        remove("ssb-selftest-mix-o2.wav");
    }

    printf("\n== resultado ==\n");


    if (i_fails == 0)
        printf("  todas las comprobaciones pasan\n\n");
    else
        printf("  %d comprobacion(es) FALLAN\n\n", i_fails);
    return (i_fails == 0) ? 0 : 1;
}

static int i_cmd_encode(int argc, char **argv)
{
    ssb_format fmt = ssb_fmt_mp3;
    const char *dot;
    ssb_res res;

    if (argc < 2)
    {
        printf("uso: ssb encode <entrada.wav> <salida.mp3|salida.m4a> [kbps]\n");
        return 2;
    }
    dot = strrchr(argv[1], '.');
    if (dot != NULL && (strcmp(dot, ".m4a") == 0 || strcmp(dot, ".M4A") == 0))
        fmt = ssb_fmt_m4a;

    res = ssb_encode(argv[0], argv[1], fmt, (argc > 2) ? (uint32_t)atoi(argv[2]) : 0);
    if (res != ssb_ok)
    {
        printf("no se pudo codificar: %s\n", ssb_res_str(res));
        return 1;
    }
    {
        FILE *f = fopen(argv[1], "rb");
        long n = 0;
        if (f != NULL)
        {
            fseek(f, 0, SEEK_END);
            n = ftell(f);
            fclose(f);
        }
        printf("escrito %s (%ld bytes)\n", argv[1], n);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "help") == 0)
    {
        printf("SystemSoundBuffer — motor\n\n");
        printf("  ssb list\n");
        printf("  ssb rec [--src <spec>]... [--secs N] [--out dir] [--buffer SEGS]\n");
        printf("          [--max-mb N] [--segment-kb N] [--no-compress] [--save-last SEGS]\n");
        printf("  ssb encode <entrada.wav> <salida.mp3|.m4a> [kbps]\n");
        printf("  ssb selftest\n\n");
        printf("  spec: output | output:<n> | output:<nombre> | input | input:<n>\n");
        printf("        app:<nombre> | app:<pid>\n\n");
        return (argc < 2) ? 2 : 0;
    }
    if (strcmp(argv[1], "list") == 0)
        return i_cmd_list();
    if (strcmp(argv[1], "rec") == 0)
        return i_cmd_rec(argc - 2, argv + 2);
    if (strcmp(argv[1], "drift") == 0)
        return i_cmd_drift(argc - 2, argv + 2);
    if (strcmp(argv[1], "encode") == 0)
        return i_cmd_encode(argc - 2, argv + 2);
    if (strcmp(argv[1], "selftest") == 0)
        return i_cmd_selftest();
    printf("comando desconocido: %s\n", argv[1]);
    return 2;
}
