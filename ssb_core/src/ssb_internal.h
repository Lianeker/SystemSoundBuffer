/* Piezas internas del motor: sincronizacion, hilos y el contrato de captura.
   Nada de esto sale en la API publica. */
#ifndef SSB_INTERNAL_H
#define SSB_INTERNAL_H

#include "ssb.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* ------------------------------------------------------ mutex e hilos */

typedef struct ssb_mutex_t ssb_mutex;
typedef struct ssb_thread_t ssb_thread;

ssb_mutex *ssb_mutex_create(void);
void ssb_mutex_destroy(ssb_mutex **m);
void ssb_mutex_lock(ssb_mutex *m);
void ssb_mutex_unlock(ssb_mutex *m);

typedef void (*ssb_thread_fn)(void *ctx);
ssb_thread *ssb_thread_start(ssb_thread_fn fn, void *ctx);
void ssb_thread_join(ssb_thread **t);
void ssb_sleep_ms(uint32_t ms);

/* -------------------------------------------------- contrato de captura

   La implementacion por plataforma (win/ssb_capture_win.cpp, linux/...) llama a
   este callback desde SU PROPIO hilo, con audio ya normalizado a float
   intercalado. `t` es el instante del primer frame del paquete. */

typedef void (*ssb_audio_fn)(void *ctx, const float *pcm, uint32_t frames,
                             ssb_time t, int silent, int discontinuity);

/* Parte portable de `ssb_source_parse`.
 *
 * Separar "clase:selector" y elegir de lo que devuelve `ssb_enumerate()` es la
 * misma cuenta en todas partes; lo unico propio de cada sistema es el nombre
 * legible del dispositivo por omision, que sale de una llamada suya. Estaba
 * escrito dos veces, una por backend, con el riesgo de que "output:2" acabara
 * significando cosas distintas segun el sistema.
 *
 * `defname` puede ser NULL: entonces el dispositivo por omision se queda con un
 * nombre generico. */
typedef void (*ssb_defname_fn)(ssb_src_kind kind, char *out, uint32_t size);
ssb_res _ssb_source_select(const char *spec, ssb_defname_fn defname, ssb_source *out);

typedef struct ssb_capture_t ssb_capture;

/* Abre la fuente y arranca su hilo. Rellena channels/rate con lo que de verdad
   entrega el sistema. */
ssb_res ssb_capture_open(const ssb_source *src, ssb_audio_fn fn, void *ctx,
                         uint32_t *channels, uint32_t *rate, ssb_capture **out);
void ssb_capture_close(ssb_capture **c);

/* ----------------------------------------------------------------- WAV */

typedef struct ssb_wav_t ssb_wav;
ssb_res ssb_wav_open(const char *path, uint32_t channels, uint32_t rate, ssb_wav **out);
/* `bits` es 16 o 24. Las muestras entran siempre como int32 en el rango de esa
   profundidad; empaquetar a 3 bytes es cosa del escritor, no del llamante. */
ssb_res ssb_wav_open_ex(const char *path, uint32_t channels, uint32_t rate,
                        uint32_t bits, ssb_wav **out);
uint32_t ssb_wav_bits(const ssb_wav *w);
ssb_res ssb_wav_write(ssb_wav *w, const int16_t *pcm, uint32_t frames);
ssb_res ssb_wav_write32(ssb_wav *w, const int32_t *pcm, uint32_t frames);
uint64_t ssb_wav_frames(const ssb_wav *w);
ssb_res ssb_wav_close(ssb_wav **w);

#if defined(__cplusplus)
}
#endif

#endif /* SSB_INTERNAL_H */
