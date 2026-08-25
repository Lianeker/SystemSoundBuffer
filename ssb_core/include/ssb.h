/* SystemSoundBuffer — motor de captura. API publica.
 *
 * Sin ninguna dependencia de GUI (decision D1 de docs/01). Todo lo que hay aqui
 * se puede ejercitar desde linea de comandos: ver tools/ssb_cli.c.
 */
#ifndef SSB_H
#define SSB_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* El tiempo en todo el motor son unidades de 100 ns sobre el reloj QPC, que es
   lo que entrega WASAPI en el 5.o parametro de GetBuffer. Es el unico reloj
   comun a todas las fuentes (hallazgo H3 de docs/01). */
typedef uint64_t ssb_time;
#define SSB_TICKS_PER_SEC 10000000ULL

#define SSB_BLOCK_FRAMES 4096 /* granularidad de compresion y de acceso: 85 ms a 48 kHz */
#define SSB_PEAK_FRAMES 256   /* granularidad del mapa de picos */
#define SSB_MAX_CHANNELS 2
#define SSB_NAME_MAX 128

typedef enum
{
    ssb_ok = 0,
    ssb_err_arg,
    ssb_err_io,
    ssb_err_mem,
    ssb_err_platform,
    ssb_err_notfound,
    ssb_err_empty,
    ssb_err_format /* formatos que no se pueden juntar (frecuencias distintas) */
} ssb_res;

const char *ssb_res_str(ssb_res r);

/* ------------------------------------------------------------------- fuentes */

typedef enum
{
    ssb_src_output_device = 0, /* loopback de un dispositivo de salida */
    ssb_src_process,           /* loopback de una aplicacion concreta */
    ssb_src_input_device       /* microfono o linea de entrada */
} ssb_src_kind;

typedef struct
{
    ssb_src_kind kind;
    char id[SSB_NAME_MAX];   /* id opaco del dispositivo, o "" para el predeterminado */
    char name[SSB_NAME_MAX]; /* nombre legible */
    uint32_t pid;            /* solo para ssb_src_process */
    /* Solo para ssb_src_process: por que salida esta sonando esa aplicacion, y
       si esta sonando ahora mismo. Sirve para responder a la pregunta que uno
       se hace de verdad —"¿por donde sale el audio de esta app?"— sin tener que
       adivinarlo probando. Una app puede estar renderizando a un dispositivo
       distinto del que estas capturando por loopback, y entonces grabas silencio
       sin ninguna pista de por que. */
    char endpoint[SSB_NAME_MAX];
    int active;              /* 1 si la sesion esta produciendo audio ahora */
} ssb_source;

/* Enumera dispositivos de salida, de entrada y aplicaciones que tienen sesion
   de audio. `out` recibe hasta `cap` entradas; devuelve cuantas hay en total. */
uint32_t ssb_enumerate(ssb_source *out, uint32_t cap);

/* Resuelve una especificacion de texto a una fuente concreta.
     "output" / "output:default" / "output:<indice>" / "output:<subcadena>"
     "app:<nombre>" / "app:<pid>"
     "input" / "input:default" / "input:<indice>" / "input:<subcadena>"  */
ssb_res ssb_source_parse(const char *spec, ssb_source *out);

/* 1 si la fuente es un dispositivo de SALIDA cuyo endpoint esta silenciado o a
   volumen cero. Importa: el loopback de dispositivo captura la mezcla del
   endpoint, asi que con el sistema mudo graba silencio y no hay forma de
   saberlo mirando la onda. El loopback por proceso no se ve afectado, porque
   pincha antes de la mezcla. Devuelve 0 para el resto de fuentes. */
int ssb_output_muted(const ssb_source *src);

/* ------------------------------------------------- vigilante de dispositivos

   Preguntar al sistema por los dispositivos cuesta COM, y COM se atasca cuando
   un dispositivo esta desapareciendo. Hacerlo desde el hilo de interfaz congela
   el programa: paso con unos auriculares Bluetooth al apagarse.

   Este vigilante hace ese trabajo en un hilo propio y publica una foto. Leerla
   es copiar memoria bajo un mutex, asi que la interfaz no puede bloquearse.
   Y como la foto se rehace sola, las entradas y salidas que aparecen o
   desaparecen se reflejan sin que nadie pulse nada. */
#define SSB_WATCH_MAX 128

ssb_res ssb_watch_start(void);
void ssb_watch_stop(void);

/* Copia la ultima foto. `version` (opcional) solo cambia cuando cambia la
   LISTA, para saber cuando hay que reconstruir un desplegable. */
uint32_t ssb_watch_sources(ssb_source *out, uint32_t cap, uint64_t *version);

/* Mudo / sigue existiendo, resueltos DESDE LA FOTO: no tocan COM. */
int ssb_watch_muted(const ssb_source *src);
int ssb_watch_alive(const ssb_source *src);

/* --------------------------------------------------------------------- codec */

/* Compresion sin perdida por bloques independientes (§5 de docs/01).
   Cada bloque se decodifica solo: es lo que permite recortar cualquier tramo
   sin descomprimir desde el principio. */
size_t ssb_codec_max_size(uint32_t frames, uint32_t channels);
size_t ssb_codec_encode(const int16_t *pcm, uint32_t frames, uint32_t channels,
                        uint8_t *out, size_t cap);
/* Igual, pero con `compress` a 0 guarda int16 crudo: sirve para medir contra
   que se compara y para la pista sin compresion. */
size_t ssb_codec_encode_ex(const int16_t *pcm, uint32_t frames, uint32_t channels,
                           int compress, uint8_t *out, size_t cap);
ssb_res ssb_codec_decode(const uint8_t *in, size_t bytes, uint32_t frames,
                         uint32_t channels, int16_t *pcm);

/* Las mismas, en 32 bits. El predictor y el codigo Rice ya trabajaban en int32
   por dentro, asi que subir la resolucion NO cuesta compresion: solo crecen los
   residuos. `sample_bytes` es lo que ocupa una muestra en el modo crudo (2 para
   16 bits, 4 para 24), y es lo unico que cambia entre profundidades.
   Las versiones de int16 de arriba son envoltorios de estas. */
size_t ssb_codec_max_size32(uint32_t frames, uint32_t channels, uint32_t sample_bytes);
size_t ssb_codec_encode32(const int32_t *pcm, uint32_t frames, uint32_t channels,
                          int compress, uint32_t sample_bytes, uint8_t *out, size_t cap);
ssb_res ssb_codec_decode32(const uint8_t *in, size_t bytes, uint32_t frames,
                           uint32_t channels, uint32_t sample_bytes, int32_t *pcm);

/* ----------------------------------------------------------------- ring */

/* Buffer circular en disco: segmentos de tamano fijo con un indice en RAM.
   Se descarta el segmento mas viejo cuando se pasa del presupuesto de bytes o
   de la duracion configurada, la que llegue primero. */
typedef struct ssb_ring_t ssb_ring;

typedef struct
{
    uint32_t channels;
    uint32_t rate;
    uint64_t max_bytes;     /* techo de disco. 0 = sin techo */
    uint32_t max_seconds;   /* duracion objetivo del buffer. 0 = sin limite */
    uint32_t segment_bytes; /* 0 = 4 MB */
    uint32_t bits;          /* 16 o 24. 0 se toma como 16. */
} ssb_ring_config;

ssb_res ssb_ring_create(const char *dir, const ssb_ring_config *cfg, ssb_ring **out);
void ssb_ring_destroy(ssb_ring **r);
ssb_res ssb_ring_append(ssb_ring *r, ssb_time t, const uint8_t *data,
                        uint32_t bytes, uint32_t frames);
uint32_t ssb_ring_blocks(const ssb_ring *r);
uint64_t ssb_ring_bytes(const ssb_ring *r);
uint32_t ssb_ring_dropped(const ssb_ring *r);
uint32_t ssb_ring_bits(const ssb_ring *r);
ssb_res ssb_ring_set_seconds(ssb_ring *r, uint32_t seconds);

/* Tramo cubierto por el buffer. ssb_err_empty si aun no hay nada. */
ssb_res ssb_ring_span(const ssb_ring *r, ssb_time *from, ssb_time *to);

/* Se llama una vez por bloque que solapa el tramo pedido, en orden. */
/* Las muestras llegan como int32 en el rango de la profundidad del anillo:
   +-32767 a 16 bits, +-8388607 a 24. Que sea siempre int32 evita tener dos
   caminos de lectura que puedan divergir. */
typedef ssb_res (*ssb_block_fn)(void *ctx, ssb_time t, const int32_t *pcm, uint32_t frames);
ssb_res ssb_ring_read(ssb_ring *r, ssb_time from, ssb_time to, ssb_block_fn fn, void *ctx);

/* Vuelca el tramo a un WAV colocando cada bloque en su instante real y
   rellenando con silencio los huecos que dejo la fuente. El WAV resultante dura
   exactamente `to - from`. `filled_frames` (opcional) devuelve cuanto silencio
   hubo que insertar: si no es cero, la fuente perdio frames. */
ssb_res ssb_ring_save_wav(ssb_ring *r, uint32_t channels, uint32_t rate,
                          ssb_time from, ssb_time to, const char *path,
                          uint64_t *filled_frames);

/* ------------------------------------------------------------------- mezcla */

/* Mezcla varios WAV ya exportados en uno solo.
   Va DESPUES de la exportacion normal, no en paralelo: cada pista se escribe
   con el camino de siempre y esto solo suma los resultados, asi que la mezcla
   no puede sonar distinta de lo que se exporta por separado.
   Las entradas NO tienen por que traer el mismo numero de canales: una fuente
   mono —un microfono, por ejemplo— se reparte por igual entre los canales de la
   salida, que es lo que hace cualquier mesa de mezclas. Lo que si tiene que
   coincidir es la FRECUENCIA: juntar 44100 con 48000 sin remuestrear daria una
   pista a otra velocidad. Si no coinciden devuelve `ssb_err_format`, y quien
   llame tiene que decirlo con esas palabras y no tragarselo.
   La profundidad de la salida es la MAYOR de las entradas. Si la suma se sale
   de escala, se aplica la ganancia justa para que quepa y se devuelve en
   `gain_used` (1.0 si no hizo falta): recortar distorsionaria y callarlo seria
   peor. */
ssb_res ssb_mix_wavs(const char **paths, uint32_t n, const char *out, double *gain_used);

/* ------------------------------------------------------------- reproduccion */

/* Reproduce un WAV de los que produce este motor, con pausa.
   Se le da el fichero YA exportado, no el anillo: asi lo que se oye es por
   construccion lo mismo que se guardaria, y no una segunda lectura que pudiera
   diferir. Un hilo propio; pausar no cierra el flujo. */
typedef struct ssb_play_t ssb_play;

ssb_res ssb_play_open(const char *path, ssb_play **out);
void ssb_play_pause(ssb_play *p, int paused);
/* Salta a ese segundo dentro del fichero. Lo aplica el hilo de reproduccion. */
void ssb_play_seek(ssb_play *p, double seconds);
int ssb_play_paused(const ssb_play *p);
int ssb_play_done(const ssb_play *p);      /* 1 cuando llego al final */
double ssb_play_position(const ssb_play *p);
double ssb_play_duration(const ssb_play *p);
void ssb_play_close(ssb_play **p);

/* --------------------------------------------------------------------- pista */

typedef struct ssb_track_t ssb_track;

typedef struct
{
    uint32_t max_seconds;   /* duracion del buffer circular */
    uint64_t max_bytes;     /* techo de disco por pista */
    uint32_t segment_bytes; /* 0 = 4 MB */
    int compress;           /* 1 = sin perdida por bloques, 0 = crudo */
    /* Resolucion con la que se GUARDA. WASAPI entrega float32 siempre; esto
       decide cuanto de esa precision se conserva.
         16 = como hasta ahora. Ocupa la mitad y sobra para casi todo.
         24 = todo lo que un float32 de audio puede representar. Es el maximo
              util: el mantisa del float son 24 bits.
       Medido: el codigo Rice ya trabajaba en int32, asi que 24 bits NO desactiva
       la compresion, solo agranda los residuos. 0 se toma como 16. */
    uint32_t bits;
} ssb_track_config;

void ssb_track_config_default(ssb_track_config *cfg);

/* Crea la pista, abre la fuente y arranca su hilo de captura. */
ssb_res ssb_track_create(const char *name, const char *dir, const ssb_source *src,
                         const ssb_track_config *cfg, ssb_track **out);
void ssb_track_destroy(ssb_track **t);

typedef struct
{
    uint64_t frames;      /* frames capturados */
    uint64_t raw_bytes;   /* lo que ocuparian sin comprimir */
    uint64_t disk_bytes;  /* lo que ocupan de verdad */
    uint32_t blocks;      /* bloques vivos en el buffer */
    uint32_t dropped;     /* bloques descartados por el circular */
    uint32_t discont;     /* discontinuidades reportadas por el sistema */
    uint32_t reanchors;   /* veces que se rehizo la linea de tiempo porque
                             faltaba audio de verdad. Cero es lo normal: si
                             sube, la fuente esta perdiendo audio y en la
                             exportacion apareceran huecos de silencio. */
    uint64_t filled;      /* frames de silencio insertados al exportar, en
                             total, para tapar esos huecos */
    uint32_t silent_blocks;
    double ratio;         /* raw/disk */
    double drift_ms;      /* reloj del dispositivo contra QPC */
    double eff_rate;      /* frames por segundo de reloj REALES. Si se aleja del
                             nominal, la fuente esta perdiendo frames. */
    double peak;          /* pico observado desde el arranque */
    double level;         /* nivel AHORA, con caida de ~150 ms. Es lo unico que
                             dice si la fuente esta entregando audio en este
                             momento; `peak` se queda arriba para siempre en
                             cuanto suena algo una vez. */
    uint32_t channels;
    uint32_t rate;
} ssb_track_stats;

void ssb_track_stats_get(ssb_track *t, ssb_track_stats *s);
ssb_res ssb_track_span(ssb_track *t, ssb_time *from, ssb_time *to);

/* Serie de sincronizacion: una muestra por paquete que entrega el sistema.
   Sirve para distinguir un desfase de arranque (escalon) de una deriva de reloj
   (pendiente): con un solo valor final no se puede. */
typedef struct
{
    ssb_time reported; /* instante que reporta el sistema para el paquete */
    ssb_time expected; /* el que sale de contar frames desde el ancla */
    uint64_t frames;   /* frames acumulados ANTES de este paquete */
    uint32_t packet;   /* frames en este paquete */
    double drift_ms;   /* reported - expected */
    int silent;
} ssb_drift_sample;

#define SSB_DRIFT_SAMPLES 8192

/* Copia las ultimas muestras, de la mas vieja a la mas nueva. */
uint32_t ssb_track_drift(ssb_track *t, ssb_drift_sample *out, uint32_t cap);

/* Mapa de picos: min/max por cada SSB_PEAK_FRAMES frames. Es de lo que dibuja
   la onda la interfaz, sin descomprimir nada.
   Cada pico lleva SU instante, tomado del reloj del sistema igual que los
   bloques. Deducirlo contando picos seria repetir el error de docs/03: si la
   fuente pierde frames, el mapa se desplaza respecto del audio. */
typedef struct
{
    ssb_time t;
    int16_t min;
    int16_t max;
} ssb_peak;

/* Copia hasta `cap` picos del tramo pedido. Devuelve cuantos copio. */
uint32_t ssb_track_peaks(ssb_track *t, ssb_time from, ssb_time to, ssb_peak *out, uint32_t cap);

/* Exportacion a formatos comprimidos, con el codificador que trae el sistema.
   No hay dependencias externas: en Windows lo pone Media Foundation. Se
   transcodifica desde un WAV ya escrito, para no duplicar el camino de
   exportacion (ventana comun, huecos, recorte) que ya esta verificado. */
typedef enum
{
    ssb_fmt_wav = 0,
    ssb_fmt_mp3,
    ssb_fmt_m4a
} ssb_format;

const char *ssb_format_ext(ssb_format fmt);

/* `target_kbps` orienta la eleccion del caudal; 0 = el mas alto disponible.
   Devuelve ssb_err_notfound si el sistema no sabe codificar a ese formato, y
   ssb_err_platform si el resultado salio con otra frecuencia que la fuente
   (Media Foundation lo hace en silencio por algunos caminos). */
ssb_res ssb_encode(const char *wav_path, const char *out_path,
                   ssb_format fmt, uint32_t target_kbps);

/* Vuelca el tramo a un WAV. `to` == 0 significa "hasta el final del buffer". */
ssb_res ssb_track_save_wav(ssb_track *t, ssb_time from, ssb_time to, const char *path);

/* Detiene y reanuda la ENTRADA de la pista. El cliente de captura sigue abierto
   (reabrirlo cuesta mas de un segundo, hallazgo H2), pero los frames que llegan
   se descartan, asi que el buffer queda exactamente como estaba. Es lo que
   permite mirar y guardar sin que lo que buscas se salga del circular por atras.
   Al reanudar queda un hueco real, y como tal se representa. */
void ssb_track_pause(ssb_track *t, int paused);
int ssb_track_paused(const ssb_track *t);

/* Cambia la duracion del buffer de una pista que ya esta grabando. Al reducir se
   descarta lo que sobra por el principio; al ampliar, el buffer crece desde
   ahora. Es una operacion en caliente: no se pierde la captura. */
ssb_res ssb_track_set_buffer(ssb_track *t, uint32_t max_seconds);

/* Duracion configurada del buffer. La interfaz la CONSULTA; nunca al reves: el
   buffer no depende de lo que se dibuje. */
uint32_t ssb_track_buffer_seconds(const ssb_track *t);
/* Resolucion con la que esta pista esta guardando: 16 o 24. */
uint32_t ssb_track_bits(const ssb_track *t);

/* Congela y descongela el avance del cursor de la vista. La captura NO se
   detiene (decision D7 de docs/01); para eso esta ssb_track_pause. */
void ssb_track_freeze(ssb_track *t, int frozen);
int ssb_track_frozen(const ssb_track *t);
ssb_time ssb_track_frozen_at(const ssb_track *t);

/* ------------------------------------------------------------------ utiles */

ssb_time ssb_now(void);
double ssb_time_to_sec(ssb_time t);

/* Convierte un instante de la linea de tiempo a hora local de pared. La
   referencia se toma la primera vez que se llama a cualquiera de las dos. */
void ssb_wall_clock(ssb_time t, int *year, int *month, int *day,
                    int *hour, int *minute, int *second);
ssb_res ssb_mkdir(const char *path);

/* Convierte una ruta a absoluta respecto del directorio de trabajo ACTUAL.
   Hay que llamarla pronto: basta con que el usuario abra un dialogo de fichero
   para que Windows cambie el directorio de trabajo del proceso, y a partir de
   ahi cualquier ruta relativa apunta a otro sitio. */
ssb_res ssb_abs_path(const char *path, char *out, uint32_t size);
ssb_res ssb_set_cwd(const char *path);

/* Ruta del fichero de ajustes del usuario, creando el directorio que lo
   contiene: %APPDATA%\<app>\<file> en Windows, ~/.config/<app>/<file> en el
   resto. Va aparte del directorio de trabajo a proposito: los ajustes deben
   sobrevivir a que la aplicacion se lance desde otro sitio. */
ssb_res ssb_config_path(const char *app, const char *file, char *out, uint32_t size);
void ssb_sleep(uint32_t ms);

#if defined(__cplusplus)
}
#endif

#endif /* SSB_H */
